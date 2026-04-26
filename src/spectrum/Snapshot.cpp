#include "Snapshot.h"
#include "SpectrumBase.h"
#include "Spectrum128K.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "Snapshot";

bool Snapshot::load(SpectrumBase* spectrum, const char* filepath) {
    if (!spectrum) return false;

    FILE* f = fopen(filepath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    size_t got = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* filebuf = (uint8_t*)spectrum->allocateMemory(got, "SnapshotFile");
    if (!filebuf) { fclose(f); return false; }
    fread(filebuf, 1, got, f);
    fclose(f);

    Z80SnapshotHeader header = {};
    memset(&header, 0, sizeof(header));
    uint16_t hdr_pc = 0;
    if (got >= 30) {
        memcpy(header.hdr, filebuf, 30);
        header.haveHeader = true;
        hdr_pc = (uint16_t)header.hdr[6] | ((uint16_t)header.hdr[7] << 8);
    }

    if (header.haveHeader && hdr_pc == 0 && got >= 34) {
        header.extra_len = (uint16_t)filebuf[30] | ((uint16_t)filebuf[31] << 8);
        if (header.extra_len >= 2) {
            header.ext_pc = (uint16_t)filebuf[32] | ((uint16_t)filebuf[33] << 8);
        }
    }

    bool success = false;
    if (spectrum->applySnapshotData(filebuf, got)) {
        success = true;
    } else {
        uint8_t* tmpout = (uint8_t*)spectrum->allocateMemory(49152, "SnapshotDecompress");
        if (tmpout) {
            if (hdr_pc != 0) { // Version 1
                bool is_compressed = (header.hdr[12] & 0x20) != 0;
                if (!is_compressed && got >= 30 + 49152) {
                    success = spectrum->applySnapshotData(filebuf + 30, 49152);
                } else {
                    if (decompressZ80RLE(filebuf, got, tmpout, 49152, 30)) {
                        success = spectrum->applySnapshotData(tmpout, 49152);
                    }
                }
            } else if (header.extra_len > 0) { // Version 2/3
                size_t pos = 30 + 2 + header.extra_len;
                Spectrum128K* s128 = spectrum->is128k() ? static_cast<Spectrum128K*>(spectrum) : nullptr;
                uint8_t* tmp48 = s128 ? nullptr : (uint8_t*)spectrum->allocateMemory(49152, "Snapshot48KTemp");
                uint16_t pageStart48[12] = {0,0,0,0,0x8000,0xC000,0,0,0x4000,0,0};

                if (s128 && header.extra_len >= 4) {
                    spectrum->writePort(0x7FFD, filebuf[32 + 3]);

                    // AY sound chip registers
                    if (header.extra_len >= 23) {
                        for (int i = 0; i < 16; i++) {
                            s128->writePort(0xFFFD, i); // Select register
                            s128->writePort(0xBFFD, filebuf[32 + 7 + i]); // Write data
                        }
                        // Restore the user's selected register
                        s128->writePort(0xFFFD, filebuf[32 + 6]);
                    }
                }

                while (pos + 3 <= got) {
                    uint16_t compLen = (uint16_t)filebuf[pos] | ((uint16_t)filebuf[pos + 1] << 8);
                    uint8_t pageId = filebuf[pos + 2];
                    pos += 3;
                    uint8_t* dest = nullptr;
                    if (s128 && filebuf[34] >= 3 && pageId >= 3 && pageId < 11) {
                        // 128K snapshot loading into 128K machine
                        dest = s128->getBank(pageId - 3);
                    } else if (s128 && pageId < 12 && pageStart48[pageId]) {
                        // 48K snapshot loading into 128K machine directly
                        if (pageStart48[pageId] == 0x4000) dest = s128->getBank(5);
                        else if (pageStart48[pageId] == 0x8000) dest = s128->getBank(2);
                        else if (pageStart48[pageId] == 0xC000) dest = s128->getBank(0);
                    } else if (tmp48 && pageId < 12 && pageStart48[pageId]) {
                        // 48K snapshot (or loading 128K into 48K machine via fallback temp buffer)
                        dest = tmp48 + (pageStart48[pageId] - 0x4000);
                    }

                    if (dest) {
                        if (compLen == 0xFFFF) {
                            if (pos + 0x4000 <= got) memcpy(dest, filebuf + pos, 0x4000);
                        } else {
                            loadCompressedMemPage(filebuf + pos, compLen, dest, 0x4000);
                        }
                        success = true;
                    }
                    pos += (compLen == 0xFFFF) ? 0x4000 : compLen;
                }
                if (s128 && filebuf[34] < 3) {
                    // It was a 48K snapshot loaded directly into 128K machine, ensure paging is set to 48K state
                    s128->writePort(0x7FFD, 0x30);
                }

                if (tmp48) { if (success) spectrum->applySnapshotData(tmp48, 49152); free(tmp48); }
            }
            free(tmpout);
        }
    }

    if (success) restoreCPUFromSnapshot(spectrum, header, filebuf, got);
    free(filebuf);
    return success;
}

void Snapshot::restoreCPUFromSnapshot(SpectrumBase* spectrum, const Z80SnapshotHeader& header, const uint8_t* filebuf, size_t got) {
    if (!header.haveHeader || !spectrum) return;
    const uint8_t* hdr = header.hdr;
    Z80* cpu = spectrum->getCPU();
    
    // Registers in header (LSB first for 16-bit pairs)
    uint16_t pc = (uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8);
    uint16_t sp = (uint16_t)hdr[8] | ((uint16_t)hdr[9] << 8);
    uint8_t flags12 = hdr[12];
    uint8_t flags29 = hdr[29];

    // For version 2/3 files prefer extended header PC when present
    if (pc == 0 && header.extra_len >= 2) {
        pc = header.ext_pc;
    } else if (pc == 0 && got >= 34) {
        pc = (uint16_t)filebuf[32] | ((uint16_t)filebuf[33] << 8);
    }

    // Combine R-register high bit from flags12 bit0
    uint8_t R_full = (hdr[11] & 0x7F) | ((flags12 & 0x01) ? 0x80 : 0x00);
    
    // Restore border color from flags12 bits 1-3
    spectrum->m_borderColor = (flags12 >> 1) & 0x07;
    spectrum->m_initialBorderColor = spectrum->m_borderColor;
    spectrum->m_renderInitialBorderColor = spectrum->m_borderColor;
    spectrum->m_borderEventCount = 0;
    spectrum->m_renderBorderEventCount = 0;
    ESP_LOGI(TAG, "Restored border color: %u", (unsigned)spectrum->m_borderColor);

    // Apply to Z80 CPU state
    cpu->a = hdr[0]; cpu->f = hdr[1];
    cpu->c = hdr[2]; cpu->b = hdr[3];
    cpu->l = hdr[4]; cpu->h = hdr[5];
    cpu->e = hdr[13]; cpu->d = hdr[14];

    cpu->c_ = hdr[15]; cpu->b_ = hdr[16];
    cpu->e_ = hdr[17]; cpu->d_ = hdr[18];
    cpu->l_ = hdr[19]; cpu->h_ = hdr[20];
    cpu->a_ = hdr[21]; cpu->f_ = hdr[22];

    cpu->iy = (uint16_t)hdr[23] | ((uint16_t)hdr[24] << 8);
    cpu->ix = (uint16_t)hdr[25] | ((uint16_t)hdr[26] << 8);
    cpu->sp = sp; cpu->pc = pc;
    cpu->i = hdr[10]; cpu->r = R_full;

    cpu->iff1 = (hdr[27] != 0) ? 1 : 0;
    cpu->iff2 = (hdr[28] != 0) ? 1 : 0;
    cpu->im = flags29 & 0x03;

    // If PC points to HALT (0x76) mark CPU halted so emulation resumes correctly
    if (spectrum->read((uint16_t)pc) == 0x76) {
        cpu->halted = 1;
        ESP_LOGI(TAG, "Restored PC points to HALT; setting cpu.halted=1");
    }

    ESP_LOGI(TAG, "CPU Restored: PC=%04X SP=%04X IM=%u IFF1=%u", pc, sp, cpu->im, cpu->iff1);
}

bool Snapshot::decompressZ80RLE(const uint8_t* src, size_t srclen, uint8_t* dest, size_t destlen, size_t offset) {
    size_t inpos = offset;
    size_t outpos = 0;

    while (inpos < srclen && outpos < destlen) {
        if (inpos + 3 < srclen && src[inpos] == 0xED && src[inpos+1] == 0xED) {
            uint8_t count = src[inpos+2];
            uint8_t value = src[inpos+3];
            inpos += 4;
            if (count == 0) {
                if (outpos < destlen) dest[outpos++] = 0xED;
                if (outpos < destlen) dest[outpos++] = 0xED;
                if (outpos < destlen) dest[outpos++] = value;
            } else {
                for (int i = 0; i < count && outpos < destlen; ++i) dest[outpos++] = value;
            }
        } else {
            dest[outpos++] = src[inpos++];
        }
    }
    return outpos > 0;
}

void Snapshot::loadCompressedMemPage(const uint8_t* src, size_t srclen, uint8_t* memPage, size_t memlen) {
    size_t dataOff = 0;
    int ed_cnt = 0;
    size_t memidx = 0;

    while (dataOff < srclen && memidx < memlen) {
        uint8_t databyte = src[dataOff++];
        if (ed_cnt == 0) {
            if (databyte != 0xED) memPage[memidx++] = databyte;
            else ed_cnt = 1;
        } else if (ed_cnt == 1) {
            if (databyte != 0xED) {
                if (memidx < memlen) memPage[memidx++] = 0xED;
                if (memidx < memlen) memPage[memidx++] = databyte;
                ed_cnt = 0;
            } else ed_cnt = 2;
        } else if (ed_cnt == 2) {
            uint8_t repcnt = databyte;
            if (dataOff < srclen) {
                uint8_t repval = src[dataOff++];
                for (uint8_t i = 0; i < repcnt && memidx < memlen; ++i) memPage[memidx++] = repval;
            }
            ed_cnt = 0;
        }
    }
    // Handle trailing ED
    if (ed_cnt == 1 && memidx < memlen) {
        memPage[memidx++] = 0xED;
    }
}
