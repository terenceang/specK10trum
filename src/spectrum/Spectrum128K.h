#pragma once
#include "SpectrumBase.h"
#include "PSG.h"

class Spectrum128K : public SpectrumBase {
public:
    Spectrum128K();
    virtual ~Spectrum128K();
    
    // IMemoryBus implementation
    void writePort(uint16_t port, uint8_t value) override;
    uint8_t readPort(uint16_t port) override;
    void reset() override;
    
    // 128K specific methods
    uint8_t getCurrentBank(uint16_t addr);
    uint8_t* getBank(uint8_t bank) { return (bank < 8) ? m_ramBanks[bank] : nullptr; }
    const MemoryRegion* getMemoryMap(size_t& count) const;
    bool is128k() const override { return true; }
    bool applySnapshotData(const uint8_t* data, size_t len) override;
    
    // AY-3-8912 sound chip access
    uint8_t readAY(uint8_t reg);
    void writeAY(uint8_t reg, uint8_t value);
    void renderPSGAudio(int16_t* buffer, int num_samples) override;

    // Only the 48K BASIC ROM (bit 4 of 0x7FFD) contains LD-BYTES at 0x0556.
    bool isTapeRomActive() const override { return (m_port7FFD & 0x10) != 0; }
    
private:
    uint8_t* m_ramBanks[8];   // 8 banks of 16KB each = 128KB total
    
    uint8_t m_port7FFD;       // paging register
    bool m_pagingLocked;
    
    // AY-3-8912 sound chip emulation
    PSG m_psg;
    uint8_t m_aySelectedReg;
    
    void updatePaging();
    
    static constexpr size_t ROM_SIZE = 32768; // 2 banks of 16KB
    static constexpr size_t BANK_SIZE = 16384;
};
