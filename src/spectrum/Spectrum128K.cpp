#include "Spectrum128K.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "Spectrum128K";

Spectrum128K::Spectrum128K()
    : m_port7FFD(0)
    , m_pagingLocked(false)
    , m_aySelectedReg(0)
{
    m_romSize = ROM_SIZE;
    m_rom = allocateMemory(ROM_SIZE, "ROM");
    
    for (int i = 0; i < 8; i++) {
        m_ramBanks[i] = allocateMemory(BANK_SIZE, "RAM Bank");
        if (m_ramBanks[i]) {
            memset(m_ramBanks[i], 0, BANK_SIZE);
        }
    }
    
    if (m_rom) memset(m_rom, 0xFF, ROM_SIZE);
    memset(m_ayRegisters, 0, sizeof(m_ayRegisters));
    
    updatePaging();
    
    ESP_LOGI(TAG, "Spectrum 128K initialized");
}

Spectrum128K::~Spectrum128K() {
    for (int i = 0; i < 8; i++) {
        if (m_ramBanks[i]) free(m_ramBanks[i]);
    }
    ESP_LOGI(TAG, "Spectrum 128K destroyed");
}

void Spectrum128K::reset() {
    SpectrumBase::reset();
    m_port7FFD = 0;
    m_pagingLocked = false;
    m_aySelectedReg = 0;
    memset(m_ayRegisters, 0, sizeof(m_ayRegisters));
    
    for (int i = 0; i < 8; i++) {
        if (m_ramBanks[i]) memset(m_ramBanks[i], 0, BANK_SIZE);
    }
    
    updatePaging();
    ESP_LOGI(TAG, "Spectrum 128K reset");
}

void Spectrum128K::updatePaging() {
    // 0x0000-0x3FFF: ROM (selected by bit 4 of 0x7FFD)
    uint8_t romBank = (m_port7FFD & 0x10) ? 1 : 0;
    updateMap(0, m_rom + (romBank * 0x4000), false);
    
    // 0x4000-0x7FFF: RAM Bank 5 (Fixed)
    updateMap(1, m_ramBanks[5], true);
    
    // Video page pointer (selected by bit 3 of 0x7FFD: 0=Bank 5, 1=Bank 7)
    m_videoPagePtr = (m_port7FFD & 0x08) ? m_ramBanks[7] : m_ramBanks[5];
    
    // 0x8000-0xBFFF: RAM Bank 2 (Fixed in standard 128K)
    updateMap(2, m_ramBanks[2], true);
    
    // 0xC000-0xFFFF: RAM Bank n (selected by bits 0-2 of 0x7FFD)
    uint8_t ramBank = m_port7FFD & 0x07;
    updateMap(3, m_ramBanks[ramBank], true);
}

void Spectrum128K::writePort(uint16_t port, uint8_t value) {
    if ((port & 0x8002) == 0x0000) { // Port 0x7FFD
        if (!m_pagingLocked) {
            m_port7FFD = value & 0x3F;
            m_pagingLocked = (m_port7FFD & 0x20) != 0;
            updatePaging();
        }
    } 
    else if ((port & 0x0001) == 0) { // Port 0xFE
        writePortFE(value);
        return;
    }
    else if ((port & 0xC002) == 0x8000) { // Port 0xBFFD
        m_aySelectedReg = value & 0x0F;
    }
    else if ((port & 0xC002) == 0xC000) { // Port 0xFFFD
        writeAY(m_aySelectedReg, value);
    }
}

static const SpectrumBase::MemoryRegion s_memoryMap128K[] = {
    {0x0000, 0x3FFF, "ROM", false},
    {0x4000, 0x7FFF, "RAM bank 5", true},
    {0x8000, 0xBFFF, "RAM bank 0/2", true},
    {0xC000, 0xFFFF, "Paged RAM bank", false},
};

const SpectrumBase::MemoryRegion* Spectrum128K::getMemoryMap(size_t& count) const {
    count = sizeof(s_memoryMap128K) / sizeof(s_memoryMap128K[0]);
    return s_memoryMap128K;
}

uint8_t Spectrum128K::readPort(uint16_t port) {
    if ((port & 0x0001) == 0) { // Port 0xFE
        return readPortFE(port);
    }
    else if ((port & 0xC002) == 0xC000) { // Port 0xFFFD
        return readAY(m_aySelectedReg);
    }
    return getFloatingBusValue();
}

uint8_t Spectrum128K::getCurrentBank(uint16_t addr) {
    if (addr < 0x4000) return 0xFF; // ROM
    if (addr < 0x8000) return 5;
    if (addr < 0xC000) return 2;
    return m_port7FFD & 0x07;
}

bool Spectrum128K::applySnapshotData(const uint8_t* data, size_t len) {
    if (len == 49152) {
        // Apply 48K snapshot data into 128K standard memory mapping.
        // 48K memory maps to:
        // 0x4000 - 0x7FFF -> Bank 5 (Offset 0)
        // 0x8000 - 0xBFFF -> Bank 2 (Offset 16384)
        // 0xC000 - 0xFFFF -> Bank 0 (Offset 32768)
        memcpy(m_ramBanks[5], data, BANK_SIZE);
        memcpy(m_ramBanks[2], data + BANK_SIZE, BANK_SIZE);
        memcpy(m_ramBanks[0], data + 2 * BANK_SIZE, BANK_SIZE);

        // Setup paging for 48K state (ROM 1, RAM Bank 0) and lock it
        m_port7FFD = 0x30;
        m_pagingLocked = true;
        updatePaging();

        ESP_LOGI(TAG, "48K snapshot applied to 128K system (%zu bytes)", len);
        return true;
    }

    ESP_LOGD(TAG, "applySnapshotData: unsupported snapshot length %zu for 128K", len);
    return false;
}

uint8_t Spectrum128K::readAY(uint8_t reg) {
    return (reg < 16) ? m_ayRegisters[reg] : 0xFF;
}

void Spectrum128K::writeAY(uint8_t reg, uint8_t value) {
    if (reg < 16) {
        m_ayRegisters[reg] = value;
    }
}
