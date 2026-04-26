#pragma once
#include "SpectrumBase.h"

class Spectrum48K : public SpectrumBase {
public:
    Spectrum48K();
    virtual ~Spectrum48K();
    
    // IMemoryBus implementation
    void writePort(uint16_t port, uint8_t value) override;
    uint8_t readPort(uint16_t port) override;
    void reset() override;
    
    // 48K specific methods
    uint8_t* getRAM() { return m_ram; }
    uint8_t* getROM() { return m_rom; }
    // Apply snapshot data provided by the base loader
    bool applySnapshotData(const uint8_t* data, size_t len) override;

    const MemoryRegion* getMemoryMap(size_t& count) const;
    
private:
    uint8_t* m_ram;      // 48KB
    
    static constexpr size_t ROM_SIZE = 16384;
    static constexpr size_t RAM_SIZE = 49152;
};
