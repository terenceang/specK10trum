#pragma once
#include "IMemoryBus.h"

extern "C" {
#include "../z80/z80.h"
}

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include "Beeper.h"
#include "Tape.h"
#include "esp_log.h"

class SpectrumBase : public IMemoryBus {
public:
    SpectrumBase();
    virtual ~SpectrumBase();

    // ULA Constants
    static constexpr int T_STATES_PER_LINE = 224;
    static constexpr int FRAME_LINES = 312;
    static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;
    static constexpr int FIRST_ACTIVE_LINE = 64;
    static constexpr int ACTIVE_LINES = 192;
    static constexpr int ACTIVE_CYCLES_PER_LINE = 128;
    static constexpr int SAMPLES_PER_FRAME = 882; // 44100 / 50
    static constexpr uint16_t SCREEN_BASE = 0x4000;
    static constexpr uint16_t ATTR_BASE = 0x5800;

    // Memory access (optimized)
    inline uint8_t read(uint16_t addr) override {
        return m_memReadMap[addr >> 14][addr & 0x3FFF];
    }

    inline void write(uint16_t addr, uint8_t value) override {
        uint8_t* ptr = m_memWriteMap[addr >> 14];
        if (ptr) {
            ptr[addr & 0x3FFF] = value;
        }
    }

    // Common methods
    bool loadROM(const char* filepath) override;
    void setKeyboardRow(uint8_t row, uint8_t columns);
    uint8_t getBorderColor() const { return m_borderColor; }

    struct MemoryRegion {
        uint16_t start;
        uint16_t end;
        const char* name;
        bool contended;
    };

    virtual const MemoryRegion* getMemoryMap(size_t& count) const = 0;
    void dumpMemoryMap() const;

    // Abstract methods to be implemented by subclasses
    virtual void writePort(uint16_t port, uint8_t value) override = 0;
    virtual uint8_t readPort(uint16_t port) override = 0;
    
    // Reset the system
    virtual void reset() override;
    virtual void dumpMemory(uint16_t start, uint16_t end) override;

    // Snapshot loading
    virtual bool loadSnapshot(const char* filepath);
    virtual bool applySnapshotData(const uint8_t* data, size_t len) { (void)data; (void)len; return false; }
    
    // Query helper
    virtual bool is128k() const { return false; }
    
    // CPU execution
    int step();
    
    Z80* getCPU() { return &m_cpu; }
    Tape& tape() { return m_tape; }

    // Unified tape start policy helpers (enforces compatibility and ROM state)
    bool startTapePlayback();
    bool startTapeInstaload();

    void flushTape();
    void logTapeTrap(const char* msg);

    // True when the 48K BASIC ROM is paged.
    virtual bool isTapeRomActive() const { return true; }

    // Direct memory access for renderer
    inline uint8_t* getPagePtr(int block) { return m_memReadMap[block & 3]; }
    
    // Rendering
    void renderToRGB565(uint16_t* buffer, int bufWidth, int bufHeight);
    void renderBeeperAudio(int16_t* buffer, int num_samples) { m_beeper.getFrameBuffer(buffer, num_samples); }
    virtual void renderPSGAudio(int16_t* buffer, int num_samples) { 
        memset(buffer, 0, num_samples * sizeof(int16_t)); 
    }

protected:
    friend class Snapshot;
    void advanceULA(int tstates);
    uint8_t getFloatingBusValue();

    // Shared Port 0xFE handlers
    void writePortFE(uint8_t value);
    uint8_t readPortFE(uint16_t port);

    // Rendering sub-functions
    void renderBorder(uint16_t* buffer, int bufWidth, int bufHeight, int offset_x, int offset_y, int source_width, int source_height);
    void renderActiveArea(uint16_t* buffer, int bufWidth, int bufHeight, int offset_x, int offset_y, int source_width, int source_height);

    Z80 m_cpu;

    // Memory mapping cache
    uint8_t* m_memReadMap[4];
    uint8_t* m_memWriteMap[4];

    uint8_t* m_rom;
    size_t m_romSize;
    
    uint8_t* m_videoPagePtr;
    uint8_t m_lastRenderedBorderColor;

    struct BorderEvent {
        uint32_t tstates;
        uint8_t color;
    };
    static constexpr size_t MAX_BORDER_EVENTS = 1024;
    BorderEvent m_borderEvents[MAX_BORDER_EVENTS];
    size_t m_borderEventCount;
    uint8_t m_initialBorderColor;

    BorderEvent m_renderBorderEvents[MAX_BORDER_EVENTS];
    size_t m_renderBorderEventCount;
    uint8_t m_renderInitialBorderColor;
    portMUX_TYPE m_borderMux;

    uint32_t m_ulaClocks;
    uint16_t m_ulaScanline;
    uint16_t m_ulaCycle;

    uint8_t m_borderColor;
    uint8_t m_lastSpeakerBit;
    bool m_lastTapeEar;

    Beeper m_beeper;
    Tape m_tape;
    int32_t m_pendingTapeTstates;
    bool m_postTrapWatch = false;
    uint32_t m_postTrapSteps = 0;
    bool m_wasInLDBytes = false;

    uint8_t* allocateMemory(size_t size, const char* name);
    void updateMap(int block, uint8_t* ptr, bool writable);

    static uint8_t z80_mem_read(void* ctx, uint16_t addr);
    static void z80_mem_write(void* ctx, uint16_t addr, uint8_t val);
    static uint8_t z80_io_read(void* ctx, uint16_t port);
    static void z80_io_write(void* ctx, uint16_t port, uint8_t val);
};
