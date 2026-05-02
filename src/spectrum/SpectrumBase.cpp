#include "SpectrumBase.h"
#include "Spectrum128K.h"
#include "input/Input.h"
#include <esp_log.h>
#include <string.h>
#include <cstdio>

static const char* TAG = "SpectrumBase";

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
    , m_borderMux(portMUX_INITIALIZER_UNLOCKED)
    , m_ulaClocks(0)
    , m_ulaScanline(0)
    , m_ulaCycle(0)
    , m_borderColor(0)
    , m_lastSpeakerBit(0)
    , m_lastTapeEar(false)
    , m_tapeMonitorEnabled(false)
    , m_pendingTapeTstates(0)
    , m_wasInLDBytes(false)
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

void SpectrumBase::reset() {
    ESP_LOGI("SpectrumBase", "Base Reset: PC was 0x%04X, clocks %llu", m_cpu.pc, m_cpu.clocks);
    m_borderColor = 0;
    m_lastSpeakerBit = 0;
    m_lastTapeEar = false;
    m_pendingTapeTstates = 0;
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
    m_cpu.halted = 0; // Explicitly clear halted state

    // Restore cached page pointers from our memory maps
    for (int i = 0; i < 4; i++) {
        m_cpu.page_read[i] = m_memReadMap[i];
        m_cpu.page_write[i] = m_memWriteMap[i];
    }
    // Reset beeper
    m_beeper.reset();
    // Stop tape player
    m_tape.stop();
    m_wasInLDBytes = false;
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

int SpectrumBase::step() {
    int tstates = 0;
    const TapeModeProfile& mode = tapeModeProfile(m_tape.getMode());

    // In instant mode, bypass tape playback entirely and inject the loaded
    // program into memory as soon as ROM tape loading begins.
    if (mode.loadsInstantly &&
        m_tape.isLoaded() &&
        isTapeRomActive() &&
        m_cpu.pc == Tape::LD_BYTES_ENTRY) {
        m_tape.stop();
        if (m_tape.instaload(this)) {
            tstates = 11;
        } else {
            tstates = z80_step(&m_cpu);
        }
    } else {
        tstates = z80_step(&m_cpu);
    }

    m_pendingTapeTstates += tstates;
    advanceULA(tstates);

    if (mode.autoStartsPlayback && m_tape.isLoaded() && !m_tape.isPlaying()) {
        bool inLDBytes = (m_cpu.pc >= 0x0556 && m_cpu.pc < 0x0800);
        if (!m_wasInLDBytes && inLDBytes) {
            m_tape.play();
        }
        m_wasInLDBytes = inLDBytes;
    } else {
        m_wasInLDBytes = false;
    }

    return tstates;
}
