#include "SpectrumBase.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <string.h>
#include <cstdio>

static const char* TAG = "SpectrumBase";

// Palette and color helper used by the centralized renderer
static const uint16_t s_paletteNormal_local[8] = {
    0x0000, // black
    0x001B, // blue
    0xF800, // red
    0xF81F, // magenta
    0x07E0, // green
    0x07FF, // cyan
    0xFFE0, // yellow
    0xFFFF, // white
};

static const uint16_t s_paletteBright_local[8] = {
    0x0000, // black
    0x001F, // bright blue
    0xF800, // bright red
    0xF81F, // bright magenta
    0x07E0, // bright green
    0x07FF, // bright cyan
    0xFFE0, // bright yellow
    0xFFFF, // bright white
};

static inline uint16_t spectrum_palette_local(uint8_t index, bool bright) {
    return bright ? s_paletteBright_local[index & 0x07] : s_paletteNormal_local[index & 0x07];
}

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

static inline uint16_t color565_local(uint8_t r, uint8_t g, uint8_t b) {
    (void)r; (void)g; (void)b; // unused; palettes are precomputed
    return 0;
}

SpectrumBase::SpectrumBase()
    : m_rom(nullptr)
    , m_romSize(0)
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

void SpectrumBase::setKeyboardRow(uint8_t row, uint8_t columns) {
    if (row < 8) {
        m_keyboardRows[row] = columns;
    }
}

void SpectrumBase::reset() {
    m_borderColor = 0;
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
}

void SpectrumBase::advanceULA(int tstates) {
    m_ulaClocks += (uint32_t)tstates;
    if (m_ulaClocks >= FRAME_T_STATES) {
        m_ulaClocks -= FRAME_T_STATES;
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

    const uint16_t border = spectrum_palette_local(getBorderColor() & 0x07, false);

    // Clear top border
    for (int i = 0; i < offset_y * bufWidth; ++i) buffer[i] = border;
    // Clear bottom border
    for (int i = (offset_y + source_height) * bufWidth; i < bufWidth * bufHeight; ++i) buffer[i] = border;

    // Left/Right borders
    for (int y = 0; y < source_height; ++y) {
        uint16_t* lineStart = &buffer[(offset_y + y) * bufWidth];
        for (int x = 0; x < offset_x; ++x) lineStart[x] = border;
        for (int x = offset_x + source_width; x < bufWidth; ++x) lineStart[x] = border;
    }

    uint8_t* ramBank1 = getPagePtr(1); // 0x4000-0x7FFF
    if (!ramBank1) return;

    const int source_width_bytes = 32;

    // Attribute LUT cache: up to 128 possible attribute combinations
#if SPECTRUM_ATTR_LUT_ENABLED
    static uint16_t* attr_lut[128] = { 0 };
    static uint32_t attr_last_used[128] = { 0 };
    static int cached_count = 0;
    static uint32_t use_counter = 1;
    auto ensure_attr_lut = [&](int attr_index) -> uint16_t* {
#if !SPECTRUM_ATTR_LUT_ENABLED
        (void)attr_index;
        return nullptr;
#else
        if (attr_index < 0 || attr_index >= 128) return nullptr;
        // Respect configured cache size limit
        if (SPECTRUM_ATTR_LUT_MAX == 0 || attr_index >= SPECTRUM_ATTR_LUT_MAX) return nullptr;

        uint16_t* table = attr_lut[attr_index];
        if (table) {
            attr_last_used[attr_index] = use_counter++;
            return table;
        }

        // If caching disabled by configured max, refuse
        if (SPECTRUM_ATTR_LUT_MAX == 0) return nullptr;

        // If cache is full, evict least-recently-used entry
        if (cached_count >= SPECTRUM_ATTR_LUT_MAX) {
            // find LRU
            uint32_t oldest = UINT32_MAX;
            int oldest_idx = -1;
            for (int i = 0; i < 128; ++i) {
                if (attr_lut[i] && attr_last_used[i] < oldest) {
                    oldest = attr_last_used[i];
                    oldest_idx = i;
                }
            }
            if (oldest_idx >= 0) {
                heap_caps_free(attr_lut[oldest_idx]);
                attr_lut[oldest_idx] = nullptr;
                attr_last_used[oldest_idx] = 0;
                cached_count--;
            }
        }

        // Allocate 256 entries (pixels byte) each expanded to 8 uint16_t pixels
        size_t entries = 256 * 8;
        size_t bytes = entries * sizeof(uint16_t);

        // Prefer PSRAM when available
        uint16_t* alloc = nullptr;
#ifdef CONFIG_SPIRAM
        alloc = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!alloc) {
            alloc = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        }
#else
        alloc = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#endif
        if (!alloc) return nullptr;

        // Decode attr_index into ink/paper/bright
        bool bright = (attr_index & 0x40) != 0;
        uint8_t ink_idx = attr_index & 0x07;
        uint8_t paper_idx = (attr_index >> 3) & 0x07;
        uint16_t ink = spectrum_palette_local(ink_idx, bright);
        uint16_t paper = spectrum_palette_local(paper_idx, bright);

        for (int pix = 0; pix < 256; ++pix) {
            uint16_t* out = &alloc[pix * 8];
            out[0] = (pix & 0x80) ? ink : paper;
            out[1] = (pix & 0x40) ? ink : paper;
            out[2] = (pix & 0x20) ? ink : paper;
            out[3] = (pix & 0x10) ? ink : paper;
            out[4] = (pix & 0x08) ? ink : paper;
            out[5] = (pix & 0x04) ? ink : paper;
            out[6] = (pix & 0x02) ? ink : paper;
            out[7] = (pix & 0x01) ? ink : paper;
        }

        attr_lut[attr_index] = alloc;
        attr_last_used[attr_index] = use_counter++;
        cached_count++;
        return alloc;
#endif
    };

#else
    // LUT caching disabled: ensure_attr_lut always returns nullptr
    auto ensure_attr_lut = [&](int attr_index) -> uint16_t* {
        (void)attr_index;
        return nullptr;
    };
#endif

    for (int y = 0; y < source_height; ++y) {
        uint16_t* linePtr = &buffer[(offset_y + y) * bufWidth + offset_x];
        uint16_t y_off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16_t attr_off = 0x1800 + ((y >> 3) * 32);

        for (int xByte = 0; xByte < source_width_bytes; ++xByte) {
            uint8_t pixels = ramBank1[y_off | xByte];
            uint8_t attr = ramBank1[attr_off | xByte];
            int idx = attr & 0x7F; // 0..127
            uint16_t* lut = ensure_attr_lut(idx);
            if (lut) {
                // copy 8 pixels from table — use two 64-bit writes when aligned for speed
                uint16_t* src = &lut[pixels * 8];
                uintptr_t dst_addr = (uintptr_t)linePtr;
                uintptr_t src_addr = (uintptr_t)src;
                if ((dst_addr % 8) == 0 && (src_addr % 8) == 0) {
                    uint64_t* dst64 = (uint64_t*)linePtr;
                    uint64_t* src64 = (uint64_t*)src;
                    dst64[0] = src64[0];
                    dst64[1] = src64[1];
                } else {
                    memcpy(linePtr, src, 8 * sizeof(uint16_t));
                }
            } else {
                // fallback: compute manually
                bool bright = (attr & 0x40) != 0;
                uint16_t ink = spectrum_palette_local(attr & 0x07, bright);
                uint16_t paper = spectrum_palette_local((attr >> 3) & 0x07, bright);
                linePtr[0] = (pixels & 0x80) ? ink : paper;
                linePtr[1] = (pixels & 0x40) ? ink : paper;
                linePtr[2] = (pixels & 0x20) ? ink : paper;
                linePtr[3] = (pixels & 0x10) ? ink : paper;
                linePtr[4] = (pixels & 0x08) ? ink : paper;
                linePtr[5] = (pixels & 0x04) ? ink : paper;
                linePtr[6] = (pixels & 0x02) ? ink : paper;
                linePtr[7] = (pixels & 0x01) ? ink : paper;
            }
            linePtr += 8;
        }
    }
}
