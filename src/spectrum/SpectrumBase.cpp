#include "SpectrumBase.h"
#include "Snapshot.h"
#include "Spectrum128K.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <esp_attr.h>
#include <string.h>
#include <cstdio>

static const char* TAG = "SpectrumBase";

// Centralized palette
#include "SpectrumPalette.h"

// Pixel-byte -> 4x uint32_t select-mask table. Each entry m[0..3] matches the
// SWAR layout used by the renderer: m[i] = (bit (7-2i) ? 0xFFFF0000 : 0)
//                                        | (bit (6-2i) ? 0x0000FFFF : 0)
// Placed in DRAM so the hot scanline loop never touches PSRAM.
static DRAM_ATTR uint32_t s_pixel_mask_lut[256][4];
static bool s_pixel_mask_initialized = false;

static void init_pixel_mask_lut() {
    if (s_pixel_mask_initialized) return;
    for (int b = 0; b < 256; ++b) {
        for (int i = 0; i < 4; ++i) {
            uint32_t m = 0;
            if (b & (1 << (7 - 2 * i))) m |= 0xFFFF0000u;
            if (b & (1 << (6 - 2 * i))) m |= 0x0000FFFFu;
            s_pixel_mask_lut[b][i] = m;
        }
    }
    s_pixel_mask_initialized = true;
}

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
    uint8_t newSpeaker = (value >> 4) & 0x01;
    m_beeper.recordEvent(m_ulaClocks, newSpeaker);
}

uint8_t SpectrumBase::readPortFE(uint16_t port) {
    uint8_t row = (port >> 8) & 0x1F;
    uint8_t val = m_keyboardRows[row & 0x07];
    // EAR input is bit 6; approximate board coupling: EAR = externalEar OR speaker
    if (m_beeper.getExternalEar() || m_beeper.currentSpeakerLevel()) {
        val |= 0x40;
    } else {
        val &= ~0x40;
    }
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

bool SpectrumBase::loadAutoexec() {
    return Snapshot::loadAutoexec(this);
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
    // Reset beeper
    m_beeper.reset();
}

void SpectrumBase::advanceULA(int tstates) {
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

    init_pixel_mask_lut();

    const int source_width = 256;
    const int source_height = 192;
    const int offset_x = (bufWidth - source_width) / 2;
    const int offset_y = (bufHeight - source_height) / 2;

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
        int spectrum_y = FIRST_ACTIVE_LINE + (y - offset_y);
        if (spectrum_y < 0) spectrum_y = 0;
        if (spectrum_y >= FRAME_LINES) spectrum_y = FRAME_LINES - 1;

        uint32_t tstate_base = spectrum_y * T_STATES_PER_LINE;
        uint32_t* lineStart32 = (uint32_t*)&buffer[y * bufWidth];
        bool is_active_y = (y >= offset_y && y < offset_y + source_height);

        if (!is_active_y) {
            uint16_t border = get_border_color_for_tstate(tstate_base + 112);
            uint32_t border32 = ((uint32_t)border << 16) | border;
            for (int i = 0; i < bufWidth / 2; ++i) lineStart32[i] = border32;
        } else {
            uint32_t tstate_left = (spectrum_y > 0) ? (spectrum_y - 1) * T_STATES_PER_LINE + 210 : 0;
            uint16_t border_left = get_border_color_for_tstate(tstate_left);
            uint32_t border_left32 = ((uint32_t)border_left << 16) | border_left;

            uint32_t tstate_right = tstate_base + 140;
            uint16_t border_right = get_border_color_for_tstate(tstate_right);
            uint32_t border_right32 = ((uint32_t)border_right << 16) | border_right;

            for (int i = 0; i < offset_x / 2; ++i) lineStart32[i] = border_left32;
            for (int i = (offset_x + source_width) / 2; i < bufWidth / 2; ++i) lineStart32[i] = border_right32;
        }
    }

    uint8_t* ramBank1 = m_videoPagePtr ? m_videoPagePtr : getPagePtr(1);
    if (!ramBank1) return;

    // Per-attribute state cache: avoids recomputing paper/ink/diff when the
    // attribute repeats across a scanline (very common).
    int last_attr = -1;
    uint32_t paper32 = 0, diff32 = 0;

    for (int y = 0; y < source_height; ++y) {
        uint32_t* linePtr32 = (uint32_t*)&buffer[(offset_y + y) * bufWidth + offset_x];
        uint16_t y_off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16_t attr_off = 0x1800 + ((y >> 3) * 32);
        const uint8_t* pxRow = &ramBank1[y_off];
        const uint8_t* attrRow = &ramBank1[attr_off];

        for (int xByte = 0; xByte < 32; ++xByte) {
            uint8_t pixels = pxRow[xByte];
            uint8_t raw_attr = attrRow[xByte];

            if (raw_attr != last_attr) {
                last_attr = raw_attr;
                bool bright = (raw_attr & 0x40) != 0;
                uint16_t ink = spectrum_palette(raw_attr & 0x07, bright);
                uint16_t paper = spectrum_palette((raw_attr >> 3) & 0x07, bright);
                paper32 = ((uint32_t)paper << 16) | paper;
                uint32_t ink32 = ((uint32_t)ink << 16) | ink;
                diff32 = ink32 ^ paper32;
            }

            const uint32_t* m = s_pixel_mask_lut[pixels];
            linePtr32[0] = paper32 ^ (m[0] & diff32);
            linePtr32[1] = paper32 ^ (m[1] & diff32);
            linePtr32[2] = paper32 ^ (m[2] & diff32);
            linePtr32[3] = paper32 ^ (m[3] & diff32);
            linePtr32 += 4;
        }
    }
    m_lastRenderedBorderColor = m_borderColor;
}

// Beeper rendering moved to Beeper class
