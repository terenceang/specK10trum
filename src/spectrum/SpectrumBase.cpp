#include "SpectrumBase.h"
#include "Spectrum128K.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <string.h>
#include <cstdio>

static const char* TAG = "SpectrumBase";

// Centralized palette
#include "SpectrumPalette.h"

// Build-time options for attribute LUT caching.
#ifndef SPECTRUM_ATTR_LUT_ENABLED
#define SPECTRUM_ATTR_LUT_ENABLED 1
#endif

#ifndef SPECTRUM_ATTR_LUT_MAX
// Maximum number of attribute entries to cache (max 128). Set to 0 to disable caching.
#define SPECTRUM_ATTR_LUT_MAX 128
#endif

#if SPECTRUM_ATTR_LUT_MAX > 128
#undef SPECTRUM_ATTR_LUT_MAX
#define SPECTRUM_ATTR_LUT_MAX 128
#endif



SpectrumBase::SpectrumBase()
    : m_rom(nullptr)
    , m_romSize(0)
    , m_videoPagePtr(nullptr)
    , m_lastRenderedBorderColor(0xFF)
    , m_borderEventCount(0)
    , m_initialBorderColor(0)
    , m_renderBorderEventCount(0)
    , m_renderInitialBorderColor(0)
    , m_ulaClocks(0)
    , m_ulaScanline(0)
    , m_ulaCycle(0)
    , m_borderColor(0)
{
    for (int i = 0; i < 4; i++) {
        m_memReadMap[i] = nullptr;
        m_memWriteMap[i] = nullptr;
    }
    memset(m_keyboardRows, 0xFF, 8);

    // Initialize CPU
    z80_init(&m_cpu);
    m_cpu.ctx = this;
    m_cpu.mem_read = z80_mem_read;
    m_cpu.mem_write = z80_mem_write;
    m_cpu.io_read = z80_io_read;
    m_cpu.io_write = z80_io_write;
}

SpectrumBase::~SpectrumBase() {
    // Subclasses should free their own RAM, but we free ROM if allocated
    if (m_rom) free(m_rom);
}

// Static callbacks for Z80
uint8_t SpectrumBase::z80_mem_read(void* ctx, uint16_t addr) {
    return static_cast<SpectrumBase*>(ctx)->read(addr);
}

void SpectrumBase::z80_mem_write(void* ctx, uint16_t addr, uint8_t val) {
    static_cast<SpectrumBase*>(ctx)->write(addr, val);
}

uint8_t SpectrumBase::z80_io_read(void* ctx, uint16_t port) {
    return static_cast<SpectrumBase*>(ctx)->readPort(port);
}

void SpectrumBase::z80_io_write(void* ctx, uint16_t port, uint8_t val) {
    static_cast<SpectrumBase*>(ctx)->writePort(port, val);
}

uint8_t* SpectrumBase::allocateMemory(size_t size, const char* name) {
    size_t allocCaps = MALLOC_CAP_DEFAULT;
#ifdef CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        allocCaps = MALLOC_CAP_SPIRAM;
    }
#endif

    uint8_t* ptr = (uint8_t*)heap_caps_malloc(size, allocCaps);
#ifdef CONFIG_SPIRAM
    if (!ptr && allocCaps == MALLOC_CAP_SPIRAM) {
        ESP_LOGW(TAG, "PSRAM allocation for %s failed, falling back to DRAM", name);
        ptr = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
    }
#endif

    if (!ptr) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for %s!", size, name);
    }
    return ptr;
}

void SpectrumBase::updateMap(int block, uint8_t* ptr, bool writable) {
    if (block >= 0 && block < 4) {
        m_memReadMap[block] = ptr;
        m_memWriteMap[block] = writable ? ptr : nullptr;
        // Update Z80 cached page pointers for fast access
        m_cpu.page_read[block] = ptr;
        m_cpu.page_write[block] = writable ? ptr : nullptr;
    }
}

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;
static constexpr int FIRST_ACTIVE_LINE = 64;
static constexpr int ACTIVE_LINES = 192;
static constexpr int ACTIVE_CYCLES_PER_LINE = 128;
static constexpr uint16_t SCREEN_BASE = 0x4000;
static constexpr uint16_t ATTR_BASE = 0x5800;

static uint16_t screenMemoryAddress(int line, int xByte) {
    return SCREEN_BASE
        | ((line & 0x07) << 11)
        | ((line & 0x38) << 5)
        | ((line & 0xC0) << 2)
        | xByte;
}

static uint16_t attributeMemoryAddress(int line, int xByte) {
    return ATTR_BASE + ((line >> 3) * 32) + xByte;
}

uint8_t SpectrumBase::getFloatingBusValue() {
    int line = m_ulaScanline;
    int cycle = m_ulaCycle;

    if (line < FIRST_ACTIVE_LINE || line >= FIRST_ACTIVE_LINE + ACTIVE_LINES) {
        return 0xFF;
    }

    if (cycle >= ACTIVE_CYCLES_PER_LINE) {
        return 0xFF;
    }

    int offset = cycle & 0x07;
    if (offset >= 4) {
        return 0xFF;
    }

    int cell = cycle >> 3;
    int xByte = (cell << 1) | ((offset >> 1) & 0x01);
    bool requestAttribute = (offset & 0x01) != 0;
    int lineInDisplay = line - FIRST_ACTIVE_LINE;

    uint16_t addr = requestAttribute
        ? attributeMemoryAddress(lineInDisplay, xByte)
        : screenMemoryAddress(lineInDisplay, xByte);

    uint8_t* page = m_memReadMap[addr >> 14];
    return page ? page[addr & 0x3FFF] : 0xFF;
}

// Shared handlers for even ULA ports (0xFE and equivalents)
void SpectrumBase::writePortFE(uint8_t value) {
    uint8_t newColor = value & 0x07;
    if (newColor != m_borderColor) {
        if (m_borderEventCount < MAX_BORDER_EVENTS) {
            m_borderEvents[m_borderEventCount++] = { m_ulaClocks, newColor };
        }
        m_borderColor = newColor;
    }
}

uint8_t SpectrumBase::readPortFE(uint16_t port) {
    uint8_t row = (port >> 8) & 0x1F;
    return m_keyboardRows[row & 0x07];
}

bool SpectrumBase::loadROM(const char* filepath) {
    if (!m_rom) {
        ESP_LOGE(TAG, "ROM buffer not allocated");
        return false;
    }

    FILE* file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open ROM: %s", filepath);
        return false;
    }

    size_t bytesRead = fread(m_rom, 1, m_romSize, file);
    fclose(file);

    if (bytesRead != m_romSize) {
        ESP_LOGE(TAG, "ROM size mismatch: read %zu, expected %zu", bytesRead, m_romSize);
        return false;
    }

    ESP_LOGI(TAG, "ROM loaded: %zu bytes", bytesRead);
    return true;
}

bool SpectrumBase::loadSnapshot(const char* filepath) {
    // Read entire file into memory
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open snapshot: %s", filepath);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ESP_LOGE(TAG, "Failed to seek snapshot file");
        return false;
    }
    long size = ftell(f);
    if (size <= 0) size = 0;
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        ESP_LOGE(TAG, "Failed to rewind snapshot file");
        return false;
    }

    uint8_t* filebuf = (uint8_t*)malloc(size > 0 ? (size_t)size : 1);
    if (!filebuf) {
        fclose(f);
        ESP_LOGE(TAG, "Out of memory reading snapshot");
        return false;
    }
    size_t got = fread(filebuf, 1, (size_t)size, f);
    fclose(f);

    // Parse .z80 30-byte header if present to capture CPU/register state
    bool haveHeader = false;
    uint8_t hdr[30] = {0};
    uint16_t hdr_pc = 0;
    if (got >= 30) {
        memcpy(hdr, filebuf, 30);
        haveHeader = true;
        hdr_pc = (uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8);
    }

    // If an extended header is present (v2/v3 .z80), bytes 30..31 give its length.
    // In .z80 format, v2/v3 headers are only present if the PC in the main header is 0.
    uint16_t extra_len = 0;
    uint16_t ext_pc = 0;
    if (haveHeader && hdr_pc == 0 && got >= 34) {
        extra_len = (uint16_t)filebuf[30] | ((uint16_t)filebuf[31] << 8);
        ESP_LOGI(TAG, "Snapshot extra-header length: %u", extra_len);
        if (extra_len >= 2 && got >= 34) {
            ext_pc = (uint16_t)filebuf[32] | ((uint16_t)filebuf[33] << 8);
            ESP_LOGI(TAG, "Extended header PC: %04X", ext_pc);
        }
        // Dump up to first 16 bytes of the extra header for inspection
        int dump = extra_len;
        if (dump > 16) dump = 16;
        if (dump > 0 && got >= 32 + dump) {
            char bufdump[64];
            int pos = 0;
            for (int i = 0; i < dump; ++i) {
                pos += snprintf(bufdump + pos, sizeof(bufdump) - pos, "%02X ", filebuf[32 + i]);
            }
            ESP_LOGI(TAG, "Extra header bytes (first %d): %s", dump, bufdump);
        }
    }

    // Determine and log the .z80 version
    if (haveHeader) {
        if (hdr_pc != 0) {
            ESP_LOGI(TAG, "Detected .z80 Version 1 snapshot");
        } else if (extra_len == 23 || extra_len == 30) {
            ESP_LOGI(TAG, "Detected .z80 Version 2 snapshot (extra_len=%u)", extra_len);
        } else if (extra_len == 54 || extra_len == 55) {
            ESP_LOGI(TAG, "Detected .z80 Version 3 snapshot (extra_len=%u)", extra_len);
        } else {
            ESP_LOGI(TAG, "Detected .z80 Version 2/3 snapshot (extra_len=%u)", extra_len);
        }
    }

    // Helper to restore CPU state from header bytes (called after memory applied)
    auto restore_cpu_from_header = [&]() {
        if (!haveHeader) return;
        // Registers in header (LSB first for 16-bit pairs)
        uint8_t A = hdr[0];
        uint8_t F = hdr[1];
        uint8_t C = hdr[2];
        uint8_t B = hdr[3];
        uint8_t L = hdr[4];
        uint8_t H = hdr[5];
        uint16_t pc = (uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8);
        uint16_t sp = (uint16_t)hdr[8] | ((uint16_t)hdr[9] << 8);
        uint8_t I = hdr[10];
        uint8_t R = hdr[11];
        uint8_t flags12 = hdr[12];

        uint8_t E = hdr[13];
        uint8_t D = hdr[14];
        uint8_t C2 = hdr[15];
        uint8_t B2 = hdr[16];
        uint8_t E2 = hdr[17];
        uint8_t D2 = hdr[18];
        uint8_t L2 = hdr[19];
        uint8_t H2 = hdr[20];
        uint8_t A2 = hdr[21];
        uint8_t F2 = hdr[22];
        uint16_t IY = (uint16_t)hdr[23] | ((uint16_t)hdr[24] << 8);
        uint16_t IX = (uint16_t)hdr[25] | ((uint16_t)hdr[26] << 8);
        uint8_t iff1_byte = hdr[27];
        uint8_t iff2_byte = hdr[28];
        uint8_t flags29 = hdr[29];

        // For version 2/3 files prefer extended header PC when present
        if (hdr_pc == 0 && extra_len >= 2 && ext_pc != 0) {
            pc = ext_pc;
        } else if (pc == 0 && got >= 34) {
            uint16_t addlen = (uint16_t)filebuf[30] | ((uint16_t)filebuf[31] << 8);
            if (addlen >= 2 && got >= 34) {
                pc = (uint16_t)filebuf[32] | ((uint16_t)filebuf[33] << 8);
            }
        }

        // Combine R-register high bit from flags12 bit0
        uint8_t R_full = (R & 0x7F) | ((flags12 & 0x01) ? 0x80 : 0x00);
        
        // Restore border color from flags12 bits 1-3
        m_borderColor = (flags12 >> 1) & 0x07;
        m_initialBorderColor = m_borderColor;
        m_renderInitialBorderColor = m_borderColor;
        m_borderEventCount = 0;
        m_renderBorderEventCount = 0;
        ESP_LOGI(TAG, "Restored border color: %u", (unsigned)m_borderColor);

        // Log parsed header details for debugging
        ESP_LOGI(TAG, "Snapshot header: A=%02X F=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X", A, F, B, C, D, E, H, L);
        ESP_LOGI(TAG, "Shadow regs: A'=%02X F'=%02X B'=%02X C'=%02X D'=%02X E'=%02X H'=%02X L'=%02X", A2, F2, B2, C2, D2, E2, H2, L2);
        ESP_LOGI(TAG, "IX=%04X IY=%04X SP=%04X PC=%04X I=%02X R=%02X (full=%02X)", IX, IY, sp, pc, I, R, R_full);
        ESP_LOGI(TAG, "IFF1=%u IFF2=%u IM=%u flags12=%02X flags29=%02X", (unsigned)(iff1_byte?1:0), (unsigned)(iff2_byte?1:0), (unsigned)(flags29 & 0x03), flags12, flags29);

        // Apply to Z80 CPU state
        m_cpu.a = A; m_cpu.f = F;
        m_cpu.b = B; m_cpu.c = C;
        m_cpu.d = D; m_cpu.e = E;
        m_cpu.h = H; m_cpu.l = L;

        m_cpu.a_ = A2; m_cpu.f_ = F2;
        m_cpu.b_ = B2; m_cpu.c_ = C2;
        m_cpu.d_ = D2; m_cpu.e_ = E2;
        m_cpu.h_ = H2; m_cpu.l_ = L2;

        m_cpu.ix = IX; m_cpu.iy = IY;
        m_cpu.sp = sp; m_cpu.pc = pc;
        m_cpu.i = I; m_cpu.r = R_full;

        m_cpu.iff1 = (iff1_byte != 0) ? 1 : 0;
        m_cpu.iff2 = iff2_byte ? 1 : 0;
        m_cpu.im = flags29 & 0x03;

        // If PC points to HALT (0x76) mark CPU halted so emulation resumes correctly
        uint8_t instr = read((uint16_t)pc);
        if (instr == 0x76) {
            m_cpu.halted = 1;
            ESP_LOGI(TAG, "Restored PC points to HALT; setting cpu.halted=1");
        }

        // Additional runtime state logging
        ESP_LOGI(TAG, "CPU runtime: halted=%u ei_delay=%u clocks=%llu", (unsigned)m_cpu.halted, (unsigned)m_cpu.ei_delay, (unsigned long long)m_cpu.clocks);
        ESP_LOGI(TAG, "CPU control: IFF1=%u IFF2=%u IM=%u", (unsigned)m_cpu.iff1, (unsigned)m_cpu.iff2, (unsigned)m_cpu.im);

        // Dump a few bytes at PC to inspect next instruction(s)
        uint16_t dump_pc = m_cpu.pc;
        uint8_t opbuf[8];
        for (int i = 0; i < 8; ++i) opbuf[i] = read((uint16_t)(dump_pc + i));
        ESP_LOGI(TAG, "PC=%04X next bytes: %02X %02X %02X %02X %02X %02X %02X %02X", dump_pc, opbuf[0], opbuf[1], opbuf[2], opbuf[3], opbuf[4], opbuf[5], opbuf[6], opbuf[7]);
    };

    // If the file contains a raw RAM image, try passing it directly
    if (applySnapshotData(filebuf, got)) {
        restore_cpu_from_header();
        free(filebuf);
        ESP_LOGI(TAG, "Snapshot applied as raw image (%zu bytes)", got);
        return true;
    }

    // Try simple .z80 RLE decompression starting at common offsets (30-byte header or 0)
    bool decompressed_ok = false;
    uint8_t* tmpout = (uint8_t*)malloc(49152); // max 48K raw target for fallback
    if (!tmpout) {
        free(filebuf);
        ESP_LOGE(TAG, "Out of memory for snapshot decompression");
        return false;
    }

    auto try_decompress = [&](int offset) -> bool {
        size_t inpos = offset >= 0 ? (size_t)offset : 0;
        size_t inlen = got;
        size_t outpos = 0;

        while (inpos < inlen && outpos < 49152) {
            // Standard .z80 RLE: 0xED 0xED <count> <value>
            if (inpos + 3 < inlen && filebuf[inpos] == 0xED && filebuf[inpos+1] == 0xED) {
                uint8_t count = filebuf[inpos+2];
                uint8_t value = filebuf[inpos+3];
                inpos += 4;
                if (count == 0) {
                    // Historically some formats may encode ED ED 00 as literal sequence;
                    // treat as two ED bytes followed by literal 00 and continue.
                    if (outpos < 49152) tmpout[outpos++] = 0xED;
                    if (outpos < 49152) tmpout[outpos++] = 0xED;
                    if (outpos < 49152) tmpout[outpos++] = value; // value is 0
                } else {
                    for (int i = 0; i < count && outpos < 49152; ++i) tmpout[outpos++] = value;
                }
            } else {
                tmpout[outpos++] = filebuf[inpos++];
            }
        }

        if (outpos > 0) {
            if (applySnapshotData(tmpout, outpos)) return true;
        }
        return false;
    };

    // --- Version 1 Handling ---
    if (hdr_pc != 0) {
        bool is_v1_compressed = (hdr[12] & 0x20) != 0;
        if (!is_v1_compressed && got >= 30 + 49152) {
            ESP_LOGI(TAG, "Snapshot is v1 uncompressed, applying directly from offset 30");
            if (applySnapshotData(filebuf + 30, 49152)) {
                decompressed_ok = true;
            }
        } else {
            decompressed_ok = try_decompress(30);
        }

        if (decompressed_ok) {
            restore_cpu_from_header();
            free(tmpout);
            free(filebuf);
            ESP_LOGI(TAG, "Snapshot applied from %s (v1)", filepath);
            return true;
        }
    }

    // Attempt full v2/v3 block parsing (multi-block format)
    if (hdr_pc == 0 && extra_len > 0) {
        size_t start = 30 + 2 + extra_len;
        if (start < got) {
            ESP_LOGI(TAG, "Attempting v2/v3 block parsing at offset %zu", start);

            // Helper to decompress a compressed page from an in-memory buffer
            auto loadCompressedMemPageFromBuffer = [&](const uint8_t* src, size_t srclen, uint8_t* memPage, size_t memlen) {
                size_t dataOff = 0;
                int ed_cnt = 0;
                uint8_t repcnt = 0;
                uint8_t repval = 0;
                size_t memidx = 0;

                while (dataOff < srclen && memidx < memlen) {
                    uint8_t databyte = src[dataOff++];
                    if (ed_cnt == 0) {
                        if (databyte != 0xED) memPage[memidx++] = databyte;
                        else ed_cnt = 1;
                    } else if (ed_cnt == 1) {
                        if (databyte != 0xED) {
                            memPage[memidx++] = 0xED;
                            memPage[memidx++] = databyte;
                            ed_cnt = 0;
                        } else ed_cnt = 2;
                    } else if (ed_cnt == 2) {
                        repcnt = databyte;
                        ed_cnt = 3;
                    } else if (ed_cnt == 3) {
                        repval = databyte;
                        for (uint8_t i = 0; i < repcnt && memidx < memlen; ++i) memPage[memidx++] = repval;
                        ed_cnt = 0;
                    }
                }
            };

            Spectrum128K* s128 = nullptr;
            if (is128k()) s128 = static_cast<Spectrum128K*>(this);

            // For 48K snapshots we'll collect data into a temporary 48K buffer and call applySnapshotData
            uint8_t* tmp48 = nullptr;
            uint16_t pageStart48[12] = {0,0,0,0,0x8000,0xC000,0,0,0x4000,0,0};
            if (!s128) {
                tmp48 = (uint8_t*)malloc(49152);
                if (tmp48) memset(tmp48, 0, 49152);
            }

            // If this is a 128K snapshot, restore paging register from extra header (header[35])
            if (s128 && extra_len >= 4) {
                uint8_t b35 = filebuf[32 + 3];
                ESP_LOGI(TAG, "Found paging byte in extra header: %02X", b35);
                // writePort will handle locking and mapping
                writePort(0x7FFD, b35);
            }

            size_t pos = start;
            bool any_written = false;
            while (pos + 3 <= got) {
                uint16_t compDataLen = (uint16_t)filebuf[pos] | ((uint16_t)filebuf[pos + 1] << 8);
                uint8_t hdr2 = filebuf[pos + 2];
                pos += 3;

                if (compDataLen == 0xFFFF) {
                    // uncompressed 16KB
                    if (pos + 0x4000 > got) break;
                    if (s128) {
                        if (hdr2 >= 3 && hdr2 < 11) {
                            uint8_t* bankPtr = s128->getBank(hdr2 - 3);
                            if (bankPtr) {
                                memcpy(bankPtr, filebuf + pos, 0x4000);
                                any_written = true;
                                ESP_LOGI(TAG, "Applied uncompressed block id=%u -> bank %d", hdr2, hdr2 - 3);
                            }
                        }
                    } else if (tmp48) {
                        if (hdr2 < 12) {
                            uint16_t memoff = pageStart48[hdr2];
                            if (memoff != 0) {
                                memcpy(tmp48 + (memoff - 0x4000), filebuf + pos, 0x4000);
                                any_written = true;
                                ESP_LOGI(TAG, "Buffered uncompressed 48K block id=%u -> offset %04X", hdr2, memoff);
                            }
                        }
                    }
                    pos += 0x4000;
                } else {
                    if (pos + compDataLen > got) break;
                    if (s128) {
                        if (hdr2 >= 3 && hdr2 < 11) {
                            uint8_t* bankPtr = s128->getBank(hdr2 - 3);
                            if (bankPtr) {
                                loadCompressedMemPageFromBuffer(filebuf + pos, compDataLen, bankPtr, 0x4000);
                                any_written = true;
                                ESP_LOGI(TAG, "Applied compressed block id=%u len=%u -> bank %d", hdr2, compDataLen, hdr2 - 3);
                            }
                        }
                    } else if (tmp48) {
                        if (hdr2 < 12) {
                            uint16_t memoff = pageStart48[hdr2];
                            if (memoff != 0) {
                                loadCompressedMemPageFromBuffer(filebuf + pos, compDataLen, tmp48 + (memoff - 0x4000), 0x4000);
                                any_written = true;
                                ESP_LOGI(TAG, "Buffered compressed 48K block id=%u len=%u -> offset %04X", hdr2, compDataLen, memoff);
                            }
                        }
                    }
                    pos += compDataLen;
                }
            }

            if (any_written) {
                // Try to heuristically restore 7FFD/paging from extra header bytes (common location)
                if (s128 && extra_len >= 4) {
                    // Commonly the paging byte can appear near the start of the extra header; try offsets 2 and 4
                    uint8_t cand1 = filebuf[32 + 2];
                    uint8_t cand2 = filebuf[32 + 4 < 32 + extra_len ? 32 + 4 : 32 + 2];
                    uint8_t cand = 0xFF;
                    if (cand1 <= 0x3F) cand = cand1;
                    else if (cand2 <= 0x3F) cand = cand2;
                    if (cand != 0xFF) {
                        ESP_LOGI(TAG, "Heuristic paging restore: writing 0x7FFD=%02X", cand);
                        writePort(0x7FFD, cand);
                    }
                }

                // If we buffered a 48K image, apply it now
                if (!s128 && tmp48) {
                    applySnapshotData(tmp48, 49152);
                    free(tmp48);
                }

                restore_cpu_from_header();
                free(tmpout);
                free(filebuf);
                ESP_LOGI(TAG, "Snapshot applied via v2/v3 block parser (%s)", filepath);
                return true;
            }
            if (tmp48) free(tmp48);
        }
    }

    // --- Last Resort Fallbacks ---
    if (!decompressed_ok && got > 30) decompressed_ok = try_decompress(30);
    if (!decompressed_ok) decompressed_ok = try_decompress(0);

    if (decompressed_ok) {
        // restore CPU state now that memory has been applied
        restore_cpu_from_header();
        free(tmpout);
        free(filebuf);
        ESP_LOGI(TAG, "Snapshot decompressed and applied (fallback) from %s", filepath);
        return true;
    }

    free(tmpout);
    free(filebuf);

    ESP_LOGE(TAG, "Snapshot could not be applied (file: %s, size: %zu)", filepath, got);
    return false;
}

void SpectrumBase::setKeyboardRow(uint8_t row, uint8_t columns) {
    if (row < 8) {
        m_keyboardRows[row] = columns;
    }
}

void SpectrumBase::reset() {
    m_borderColor = 0;
    m_initialBorderColor = 0;
    m_borderEventCount = 0;
    m_renderInitialBorderColor = 0;
    m_renderBorderEventCount = 0;
    memset(m_keyboardRows, 0xFF, 8);
    m_ulaClocks = 0;
    m_ulaScanline = 0;
    m_ulaCycle = 0;
    z80_init(&m_cpu);
    m_cpu.ctx = this;
    m_cpu.mem_read = z80_mem_read;
    m_cpu.mem_write = z80_mem_write;
    m_cpu.io_read = z80_io_read;
    m_cpu.io_write = z80_io_write;
    
    // Restore cached page pointers from our memory maps
    for (int i = 0; i < 4; i++) {
        m_cpu.page_read[i] = m_memReadMap[i];
        m_cpu.page_write[i] = m_memWriteMap[i];
    }
}

void SpectrumBase::advanceULA(int tstates) {
    m_ulaClocks += (uint32_t)tstates;
    if (m_ulaClocks >= FRAME_T_STATES) {
        m_ulaClocks -= FRAME_T_STATES;
        
        // Copy current frame events to render buffer
        memcpy(m_renderBorderEvents, m_borderEvents, m_borderEventCount * sizeof(BorderEvent));
        m_renderBorderEventCount = m_borderEventCount;
        m_renderInitialBorderColor = m_initialBorderColor;

        // Reset border events for the new frame
        m_initialBorderColor = m_borderColor;
        m_borderEventCount = 0;

        // Trigger the 50Hz maskable interrupt
        // Standard data byte for Spectrum interrupts is 0xFF (RST 38h opcode)
        z80_interrupt(&m_cpu, 0xFF);
    }

    m_ulaCycle += (uint16_t)tstates;
    while (m_ulaCycle >= T_STATES_PER_LINE) {
        m_ulaCycle -= T_STATES_PER_LINE;
        m_ulaScanline++;
        if (m_ulaScanline >= FRAME_LINES) {
            m_ulaScanline = 0;
        }
    }
}

void SpectrumBase::dumpMemory(uint16_t start, uint16_t end) {
    printf("\n=== Memory Dump (0x%04X - 0x%04X) ===\n", start, end);
    for (uint32_t addr = start; addr <= end; addr++) {
        if ((addr & 0x0F) == 0) {
            printf("\n%04X: ", (uint16_t)addr);
        }
        printf("%02X ", read((uint16_t)addr));
    }
    printf("\n=== End of Dump ===\n");
}

void SpectrumBase::dumpMemoryMap() const {
    size_t count = 0;
    const MemoryRegion* map = getMemoryMap(count);
    if (!map || count == 0) {
        printf("No memory map available.\n");
        return;
    }

    printf("\n=== Memory Map ===\n");
    for (size_t i = 0; i < count; ++i) {
        const MemoryRegion& region = map[i];
        printf("%04X-%04X : %-30s %s\n",
            region.start,
            region.end,
            region.name,
            region.contended ? "(contended)" : "");
    }
    printf("=== End of Memory Map ===\n");
}

void SpectrumBase::renderToRGB565(uint16_t* buffer, int bufWidth, int bufHeight) {
    if (!buffer) return;

    const int source_width = 256;
    const int source_height = 192;
    const int offset_x = (bufWidth - source_width) / 2;
    const int offset_y = (bufHeight - source_height) / 2;

    // Attribute LUT cache: up to 128 possible attribute combinations
    static uint16_t* attr_lut[128] = { 0 };
    static uint32_t attr_last_used[128] = { 0 };
    static int cached_count = 0;
    static uint32_t use_counter = 1;

    auto get_lut = [&](int attr_index) -> uint16_t* {
        if (attr_index < 0 || attr_index >= 128) return nullptr;
        uint16_t* table = attr_lut[attr_index];
        if (table) {
            attr_last_used[attr_index] = use_counter++;
            return table;
        }

        if (cached_count >= 128) {
            uint32_t oldest = UINT32_MAX;
            int oldest_idx = -1;
            for (int i = 0; i < 128; ++i) {
                if (attr_lut[i] && attr_last_used[i] < oldest) {
                    oldest = attr_last_used[i]; oldest_idx = i;
                }
            }
            if (oldest_idx >= 0) {
                heap_caps_free(attr_lut[oldest_idx]);
                attr_lut[oldest_idx] = nullptr;
                cached_count--;
            }
        }

        size_t bytes = 256 * 8 * sizeof(uint16_t);
        uint16_t* alloc = (uint16_t*)allocateMemory(bytes, "Attr LUT");
        if (!alloc) return nullptr;

        bool bright = (attr_index & 0x40) != 0;
        uint16_t ink = spectrum_palette(attr_index & 0x07, bright);
        uint16_t paper = spectrum_palette((attr_index >> 3) & 0x07, bright);

        uint32_t ink32 = (ink << 16) | ink;
        uint32_t paper32 = (paper << 16) | paper;
        uint32_t diff32 = ink32 ^ paper32;

        for (int pix = 0; pix < 256; ++pix) {
            uint32_t* out32 = (uint32_t*)&alloc[pix * 8];
            // 32-bit SWAR: Process 2 pixels at a time (4 iterations for 8 pixels)
            for (int i = 0; i < 4; i++) {
                uint8_t two_bits = (pix >> (6 - i * 2)) & 0x03;
                uint32_t mask = (two_bits & 0x02) ? 0xFFFF0000 : 0;
                mask |= (two_bits & 0x01) ? 0x0000FFFF : 0;
                out32[i] = paper32 ^ (mask & diff32);
            }
        }

        attr_lut[attr_index] = alloc;
        attr_last_used[attr_index] = use_counter++;
        cached_count++;
        return alloc;
    };

    auto get_border_color_for_tstate = [&](uint32_t tstate) -> uint16_t {
        uint8_t color = m_renderInitialBorderColor;
        for (size_t i = 0; i < m_renderBorderEventCount; ++i) {
            if (m_renderBorderEvents[i].tstates <= tstate) {
                color = m_renderBorderEvents[i].color;
            } else {
                break;
            }
        }
        return spectrum_palette(color & 0x07, false);
    };

    // Render border areas
    for (int y = 0; y < bufHeight; ++y) {
        // Correctly map the centered active display (192 lines) to Spectrum FIRST_ACTIVE_LINE (64)
        int spectrum_y = FIRST_ACTIVE_LINE + (y - offset_y);
        
        // Clamp to valid range (0-311)
        if (spectrum_y < 0) spectrum_y = 0;
        if (spectrum_y >= FRAME_LINES) spectrum_y = FRAME_LINES - 1;

        uint32_t tstate_base = spectrum_y * T_STATES_PER_LINE;
        uint32_t* lineStart32 = (uint32_t*)&buffer[y * bufWidth];
        bool is_active_y = (y >= offset_y && y < offset_y + source_height);
        
        if (!is_active_y) {
            // Full line is border. Sample at the middle of the line (T=112)
            uint16_t border = get_border_color_for_tstate(tstate_base + 112);
            uint32_t border32 = (border << 16) | border;
            for (int i = 0; i < bufWidth / 2; ++i) lineStart32[i] = border32;
        } else {
            // Left border: drawn at T=200...223 of PREVIOUS line
            // For simplicity, we use the end of the previous line.
            uint32_t tstate_left = (spectrum_y > 0) ? (spectrum_y - 1) * T_STATES_PER_LINE + 210 : 0;
            uint16_t border_left = get_border_color_for_tstate(tstate_left);
            uint32_t border_left32 = (border_left << 16) | border_left;
            
            // Right border: drawn at T=128...151 of CURRENT line.
            uint32_t tstate_right = tstate_base + 140;
            uint16_t border_right = get_border_color_for_tstate(tstate_right);
            uint32_t border_right32 = (border_right << 16) | border_right;
            
            // Render left border
            for (int i = 0; i < offset_x / 2; ++i) lineStart32[i] = border_left32;
            // Render right border
            for (int i = (offset_x + source_width) / 2; i < bufWidth / 2; ++i) lineStart32[i] = border_right32;
        }
    }

    uint8_t* ramBank1 = m_videoPagePtr ? m_videoPagePtr : getPagePtr(1);
    if (!ramBank1) return;

    uint16_t* current_luts[128];
    for (int i = 0; i < 128; ++i) current_luts[i] = get_lut(i);

    for (int y = 0; y < source_height; ++y) {
        uint16_t* linePtr = &buffer[(offset_y + y) * bufWidth + offset_x];
        uint16_t y_off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16_t attr_off = 0x1800 + ((y >> 3) * 32);

        for (int xByte = 0; xByte < 32; ++xByte) {
            uint8_t pixels = ramBank1[y_off | xByte];
            uint8_t attr = ramBank1[attr_off | xByte];
            uint16_t* lut = current_luts[attr & 0x7F];
            if (lut) {
                uint64_t* src64 = (uint64_t*)&lut[pixels * 8];
                uint64_t* dst64 = (uint64_t*)linePtr;
                dst64[0] = src64[0]; dst64[1] = src64[1];
            } else {
                bool bright = (attr & 0x40) != 0;
                uint16_t ink = spectrum_palette(attr & 0x07, bright);
                uint16_t paper = spectrum_palette((attr >> 3) & 0x07, bright);
                
                uint32_t ink32 = (ink << 16) | ink;
                uint32_t paper32 = (paper << 16) | paper;
                uint32_t diff32 = ink32 ^ paper32;
                uint32_t* linePtr32 = (uint32_t*)linePtr;

                // 32-bit SWAR: Process 2 pixels at a time
                for (int i = 0; i < 4; i++) {
                    uint8_t two_bits = (pixels >> (6 - i * 2)) & 0x03;
                    uint32_t mask = (two_bits & 0x02) ? 0xFFFF0000 : 0;
                    mask |= (two_bits & 0x01) ? 0x0000FFFF : 0;
                    linePtr32[i] = paper32 ^ (mask & diff32);
                }
            }
            linePtr += 8;
        }
    }
    // Update last rendered border color
    m_lastRenderedBorderColor = m_borderColor;
}
