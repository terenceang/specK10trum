#include "SpectrumBase.h"
#include "Snapshot.h"
#include "Spectrum128K.h"
#include "input/Input.h"
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
    , m_lastSpeakerBit(0)
    , m_lastTapeEar(false)
{
    for (int i = 0; i < 4; i++) {
        m_memReadMap[i] = nullptr;
        m_memWriteMap[i] = nullptr;
    }
    input_resetKeyboardRows();

    // Initialize CPU
    z80_init(&m_cpu);
    m_cpu.ctx = this;
    m_cpu.mem_read = z80_mem_read;
    m_cpu.mem_write = z80_mem_write;
    m_cpu.io_read = z80_io_read;
    m_cpu.io_write = z80_io_write;
    // Beeper constructed automatically
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

    // Speaker (bit 4) handling: delegate to Beeper
    m_lastSpeakerBit = (value >> 4) & 0x01;
    m_beeper.recordEvent(m_ulaClocks, m_lastSpeakerBit | (m_tape.getEar() ? 1 : 0));
}

uint8_t SpectrumBase::readPortFE(uint16_t port) {
    uint8_t val = 0xBF; // Bits 0-4 are columns (active low), Bit 6 (EAR) is 0 by default, others 1
    
    // Standard Spectrum keyboard: Address bits A8-A15 select rows.
    // If a bit is 0, that row is selected. If multiple bits are 0, results are ANDed.
    uint8_t row_addr = (port >> 8);
    for (int i = 0; i < 8; i++) {
        if (!(row_addr & (1 << i))) {
            val &= input_getKeyboardRow(i);
        }
    }
    
    // EAR input is bit 6. 
    bool current_ear = m_tape.getEar();
    static bool last_logged_ear = false;
    if (m_tape.isPlaying() && current_ear != last_logged_ear) {
        static int toggle_count = 0;
        toggle_count++;
        if (toggle_count % 1000 == 0) {
            ESP_LOGI(TAG, "EAR transition detected by CPU: %d (toggle %d)", current_ear, toggle_count);
        }
        last_logged_ear = current_ear;
    }

    if (current_ear) {
        val |= 0x40;
    } else {
        val &= ~0x40;
    }
    
    // Bits 5 and 7 are usually 1 or floating (we'll keep them 1)
    val |= 0xA0; 
    
    return val;
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
    return Snapshot::load(this, filepath);
}

void SpectrumBase::setKeyboardRow(uint8_t row, uint8_t columns) {
    input_setKeyboardRow(row, columns);
}

void SpectrumBase::reset() {
    m_borderColor = 0;
    m_lastSpeakerBit = 0;
    m_lastTapeEar = false;
    m_initialBorderColor = 0;
    m_borderEventCount = 0;
    m_renderInitialBorderColor = 0;
    m_renderBorderEventCount = 0;
    input_resetKeyboardRows();
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
    // Reset beeper
    m_beeper.reset();
}

void SpectrumBase::advanceULA(int tstates) {
    // Check if Tape EAR changed to record beeper events for loading sounds
    bool curTapeEar = m_tape.getEar();
    if (curTapeEar != m_lastTapeEar) {
        m_beeper.recordEvent(m_ulaClocks, m_lastSpeakerBit | (curTapeEar ? 1 : 0));
        m_lastTapeEar = curTapeEar;
    }

    m_ulaClocks += (uint32_t)tstates;
    if (m_ulaClocks >= FRAME_T_STATES) {
        m_ulaClocks -= FRAME_T_STATES;
        
        // Copy current frame events to render buffer
        memcpy(m_renderBorderEvents, m_borderEvents, m_borderEventCount * sizeof(BorderEvent));
        m_renderBorderEventCount = m_borderEventCount;
        m_renderInitialBorderColor = m_initialBorderColor;

        // Copy beeper events into render buffer
        m_beeper.copyForFrame();

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

    // Attribute LUT cache: 128 possible attribute combinations (attr & 0x7F).
    // The cache is sized for the full domain so it never evicts; lookups are
    // a single pointer load. Entries are filled lazily so we don't pay for
    // 512 KB of allocations during the first frame.
    static uint16_t* attr_lut[128] = { 0 };

    auto get_lut = [&](int attr_index) -> uint16_t* {
        if ((unsigned)attr_index >= 128) return nullptr;
        uint16_t* table = attr_lut[attr_index];
        if (table) return table;

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
                uint32_t mask = (two_bits & 0x02) ? 0x0000FFFF : 0; // bit 7/5/3/1 -> low bits (first pixel)
                mask |= (two_bits & 0x01) ? 0xFFFF0000 : 0;        // bit 6/4/2/0 -> high bits (second pixel)
                out32[i] = paper32 ^ (mask & diff32);
            }
        }

        attr_lut[attr_index] = alloc;
        return alloc;
    };

    // Streaming event walker. Events are timestamped with the T-state at
    // which the CPU wrote port 0xFE; they appear in monotonically increasing
    // order. We walk the buffer top-to-bottom, left-to-right, and the T-state
    // we care about at each pixel-pair is also monotonically increasing — so
    // a single pointer through the event list is enough (O(events + pixels)
    // instead of O(scanlines * events)). Per pixel-pair = 1 T-state of border,
    // matching real ULA timing (ULA paints 2 pixels per T-state).
    //
    // Pre-cache the palette colour for each border index so the inner loop
    // avoids a function call and a byte-swap per pixel pair.
    uint32_t border_pair[8];
    for (int i = 0; i < 8; ++i) {
        uint16_t c = spectrum_palette(i, false);
        border_pair[i] = ((uint32_t)c << 16) | c;
    }
    size_t evt_idx = 0;
    uint8_t cur_color = m_renderInitialBorderColor & 0x07;
    uint32_t cur_pair = border_pair[cur_color];

    const int total_pairs = bufWidth / 2;
    const int left_pairs = offset_x / 2;
    const int active_end_pair = (offset_x + source_width) / 2;

    auto consume_until = [&](uint32_t tstate_excl) {
        while (evt_idx < m_renderBorderEventCount &&
               m_renderBorderEvents[evt_idx].tstates < tstate_excl) {
            cur_color = m_renderBorderEvents[evt_idx].color & 0x07;
            evt_idx++;
        }
        cur_pair = border_pair[cur_color];
    };

    // Render border areas
    for (int y = 0; y < bufHeight; ++y) {
        int spectrum_y = FIRST_ACTIVE_LINE + (y - offset_y);
        if (spectrum_y < 0) spectrum_y = 0;
        if (spectrum_y >= FRAME_LINES) spectrum_y = FRAME_LINES - 1;

        uint32_t tstate_base = (uint32_t)spectrum_y * T_STATES_PER_LINE;
        uint32_t* lineStart32 = (uint32_t*)&buffer[y * bufWidth];
        bool is_active_y = (y >= offset_y && y < offset_y + source_height);

        // Catch the walker up to the start of this scanline (covers the
        // gap between this and the previous scanline's visible window —
        // HBLANK + horizontal sync).
        consume_until(tstate_base);

        if (!is_active_y) {
            // Whole row is border. Walk pair-by-pair so any colour change
            // mid-line produces a vertical stripe at the right column.
            for (int xp = 0; xp < total_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < m_renderBorderEventCount &&
                    m_renderBorderEvents[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }
        } else {
            // Left border
            for (int xp = 0; xp < left_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < m_renderBorderEventCount &&
                    m_renderBorderEvents[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }

            // Skip past any events that occurred while the ULA was painting
            // the active 256-pixel area. They don't draw a border stripe but
            // do update cur_color for the right border.
            consume_until(tstate_base + (uint32_t)active_end_pair);

            // Right border
            for (int xp = active_end_pair; xp < total_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < m_renderBorderEventCount &&
                    m_renderBorderEvents[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }
        }
    }

    uint8_t* ramBank1 = m_videoPagePtr ? m_videoPagePtr : getPagePtr(1);
    if (!ramBank1) return;

    for (int y = 0; y < source_height; ++y) {
        uint16_t* linePtr = &buffer[(offset_y + y) * bufWidth + offset_x];
        uint16_t y_off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16_t attr_off = 0x1800 + ((y >> 3) * 32);

        for (int xByte = 0; xByte < 32; ++xByte) {
            uint8_t pixels = ramBank1[y_off | xByte];
            uint8_t attr = ramBank1[attr_off | xByte];
            uint16_t* lut = get_lut(attr & 0x7F);
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
                    uint32_t mask = (two_bits & 0x02) ? 0x0000FFFF : 0;
                    mask |= (two_bits & 0x01) ? 0xFFFF0000 : 0;
                    linePtr32[i] = paper32 ^ (mask & diff32);
                }
            }
            linePtr += 8;
        }
    }
    // Update last rendered border color
    m_lastRenderedBorderColor = m_borderColor;
}

// Beeper rendering moved to Beeper class
