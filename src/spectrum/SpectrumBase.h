#pragma once
#include "IMemoryBus.h"

extern "C" {
#include "../z80/z80.h"
}

#include <stdint.h>
#include <stddef.h>

class SpectrumBase : public IMemoryBus {
public:
    SpectrumBase();
    virtual ~SpectrumBase();

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

    // Snapshot loading (shared file handling)
    // Subclasses should implement `applySnapshotData` to receive the raw or decompressed
    // RAM image bytes. `loadSnapshot` performs file reading and simple .z80 RLE
    // decompression and then calls `applySnapshotData`.
    virtual bool loadSnapshot(const char* filepath);

    // Apply a RAM image (raw or decompressed). Subclasses must implement this to
    // place the bytes into their memory layout (banks, pointers, etc.).
    virtual bool applySnapshotData(const uint8_t* data, size_t len) { (void)data; (void)len; return false; }
    // Query helper for subclass type identification (avoid RTTI/dynamic_cast)
    virtual bool is128k() const { return false; }
    // CPU execution
    int step() { int t = z80_step(&m_cpu); advanceULA(t); return t; }
    Z80* getCPU() { return &m_cpu; }

    // Direct memory access for renderer
    inline uint8_t* getPagePtr(int block) { return m_memReadMap[block & 3]; }
    // Render the current screen into an RGB565 framebuffer
    void renderToRGB565(uint16_t* buffer, int bufWidth, int bufHeight);
protected:
    void advanceULA(int tstates);
    uint8_t getFloatingBusValue();

    // Shared Port 0xFE handlers
    void writePortFE(uint8_t value);
    uint8_t readPortFE(uint16_t port);

    Z80 m_cpu;

    // Memory mapping cache for speed
    uint8_t* m_memReadMap[4];
    uint8_t* m_memWriteMap[4];

    uint8_t* m_rom;           // ROM buffer (subclasses decide size)
    size_t m_romSize;
    
    // Renderer optimization
    uint8_t* m_videoPagePtr;  // Pointer to active video RAM (usually Bank 5)
    uint8_t m_lastRenderedBorderColor; // Tracking for border clear optimization

    struct BorderEvent {
        uint32_t tstates;
        uint8_t color;
    };
    static constexpr size_t MAX_BORDER_EVENTS = 256;
    BorderEvent m_borderEvents[MAX_BORDER_EVENTS];
    size_t m_borderEventCount;
    uint8_t m_initialBorderColor;

    // Double buffering for renderer
    BorderEvent m_renderBorderEvents[MAX_BORDER_EVENTS];
    size_t m_renderBorderEventCount;
    uint8_t m_renderInitialBorderColor;

    uint32_t m_ulaClocks;
    uint16_t m_ulaScanline;
    uint16_t m_ulaCycle;

    uint8_t m_borderColor;
    uint8_t m_keyboardRows[8];

    // Helper for allocation
    uint8_t* allocateMemory(size_t size, const char* name);
    
    // Update the memory map (to be called by subclasses when paging)
    void updateMap(int block, uint8_t* ptr, bool writable);

private:
    // Static callbacks for the Z80 core
    static uint8_t z80_mem_read(void* ctx, uint16_t addr);
    static void z80_mem_write(void* ctx, uint16_t addr, uint8_t val);
    static uint8_t z80_io_read(void* ctx, uint16_t port);
    static void z80_io_write(void* ctx, uint16_t port, uint8_t val);
};
