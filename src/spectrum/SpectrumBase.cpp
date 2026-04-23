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

void SpectrumBase::restoreCPUFromSnapshot(const Z80SnapshotHeader& header, const uint8_t* filebuf, size_t got) {
    if (!header.haveHeader) return;
    const uint8_t* hdr = header.hdr;
    
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
    m_borderColor = (flags12 >> 1) & 0x07;
    m_initialBorderColor = m_borderColor;
    m_renderInitialBorderColor = m_borderColor;
    m_borderEventCount = 0;
    m_renderBorderEventCount = 0;
    ESP_LOGI(TAG, "Restored border color: %u", (unsigned)m_borderColor);

    // Apply to Z80 CPU state
    m_cpu.a = hdr[0]; m_cpu.f = hdr[1];
    m_cpu.c = hdr[2]; m_cpu.b = hdr[3];
    m_cpu.l = hdr[4]; m_cpu.h = hdr[5];
    m_cpu.e = hdr[13]; m_cpu.d = hdr[14];

    m_cpu.c_ = hdr[15]; m_cpu.b_ = hdr[16];
    m_cpu.e_ = hdr[17]; m_cpu.d_ = hdr[18];
    m_cpu.l_ = hdr[19]; m_cpu.h_ = hdr[20];
    m_cpu.a_ = hdr[21]; m_cpu.f_ = hdr[22];

    m_cpu.iy = (uint16_t)hdr[23] | ((uint16_t)hdr[24] << 8);
    m_cpu.ix = (uint16_t)hdr[25] | ((uint16_t)hdr[26] << 8);
    m_cpu.sp = sp; m_cpu.pc = pc;
    m_cpu.i = hdr[10]; m_cpu.r = R_full;

    m_cpu.iff1 = (hdr[27] != 0) ? 1 : 0;
    m_cpu.iff2 = (hdr[28] != 0) ? 1 : 0;
    m_cpu.im = flags29 & 0x03;

    // If PC points to HALT (0x76) mark CPU halted so emulation resumes correctly
    if (read((uint16_t)pc) == 0x76) {
        m_cpu.halted = 1;
        ESP_LOGI(TAG, "Restored PC points to HALT; setting cpu.halted=1");
    }

    ESP_LOGI(TAG, "CPU Restored: PC=%04X SP=%04X IM=%u IFF1=%u", pc, sp, m_cpu.im, m_cpu.iff1);
}

bool SpectrumBase::decompressZ80RLE(const uint8_t* src, size_t srclen, uint8_t* dest, size_t destlen, size_t offset) {
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

void SpectrumBase::loadCompressedMemPage(const uint8_t* src, size_t srclen, uint8_t* memPage, size_t memlen) {
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

bool SpectrumBase::loadSnapshot(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    size_t got = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* filebuf = (uint8_t*)malloc(got);
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
    if (applySnapshotData(filebuf, got)) {
        success = true;
    } else {
        uint8_t* tmpout = (uint8_t*)malloc(49152);
        if (tmpout) {
            if (hdr_pc != 0) { // Version 1
                bool is_compressed = (header.hdr[12] & 0x20) != 0;
                if (!is_compressed && got >= 30 + 49152) {
                    success = applySnapshotData(filebuf + 30, 49152);
                } else {
                    if (decompressZ80RLE(filebuf, got, tmpout, 49152, 30)) {
                        success = applySnapshotData(tmpout, 49152);
                    }
                }
            } else if (header.extra_len > 0) { // Version 2/3
                size_t pos = 30 + 2 + header.extra_len;
                Spectrum128K* s128 = is128k() ? static_cast<Spectrum128K*>(this) : nullptr;
                uint8_t* tmp48 = s128 ? nullptr : (uint8_t*)calloc(1, 49152);
                uint16_t pageStart48[12] = {0,0,0,0,0x8000,0xC000,0,0,0x4000,0,0};

                if (s128 && header.extra_len >= 4) writePort(0x7FFD, filebuf[32 + 3]);

                while (pos + 3 <= got) {
                    uint16_t compLen = (uint16_t)filebuf[pos] | ((uint16_t)filebuf[pos + 1] << 8);
                    uint8_t pageId = filebuf[pos + 2];
                    pos += 3;
                    uint8_t* dest = nullptr;
                    if (s128 && pageId >= 3 && pageId < 11) dest = s128->getBank(pageId - 3);
                    else if (tmp48 && pageId < 12 && pageStart48[pageId]) dest = tmp48 + (pageStart48[pageId] - 0x4000);

                    if (dest) {
                        if (compLen == 0xFFFF) memcpy(dest, filebuf + pos, 0x4000);
                        else loadCompressedMemPage(filebuf + pos, compLen, dest, 0x4000);
                        success = true;
                    }
                    pos += (compLen == 0xFFFF) ? 0x4000 : compLen;
                }
                if (tmp48) { if (success) applySnapshotData(tmp48, 49152); free(tmp48); }
            }
            free(tmpout);
        }
    }

    if (success) restoreCPUFromSnapshot(header, filebuf, got);
    free(filebuf);
    return success;
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
    ESP_LOGI(TAG, "Memory Dump (0x%04X - 0x%04X)", start, end);
    char line[128];
    int pos = 0;
    for (uint32_t addr = start; addr <= end; addr++) {
        if ((addr & 0x0F) == 0 && pos > 0) {
            ESP_LOGI(TAG, "%s", line);
            pos = 0;
        }
        if (pos == 0) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%04X: ", (uint16_t)addr);
        }
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", read((uint16_t)addr));
    }
    if (pos > 0) {
        ESP_LOGI(TAG, "%s", line);
    }
}

void SpectrumBase::dumpMemoryMap() const {
    size_t count = 0;
    const MemoryRegion* map = getMemoryMap(count);
    if (!map || count == 0) {
        ESP_LOGI(TAG, "No memory map available.");
        return;
    }

    ESP_LOGI(TAG, "Memory Map:");
    for (size_t i = 0; i < count; ++i) {
        const MemoryRegion& region = map[i];
        ESP_LOGI(TAG, "%04X-%04X : %-30s %s",
            region.start,
            region.end,
            region.name,
            region.contended ? "(contended)" : "");
    }
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
