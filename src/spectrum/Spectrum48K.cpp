#include "Spectrum48K.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "Spectrum48K";

Spectrum48K::Spectrum48K()
    : m_ram(nullptr)
{
    m_romSize = ROM_SIZE;
    m_rom = allocateMemory(ROM_SIZE, "ROM");
    m_ram = allocateMemory(RAM_SIZE, "RAM");
    
    if (!m_rom || !m_ram) {
        return;
    }
    
    memset(m_ram, 0, RAM_SIZE);
    memset(m_rom, 0xFF, ROM_SIZE);
    
    // Setup initial memory map
    updateMap(0, m_rom, false);          // 0x0000 - 0x3FFF: ROM
    updateMap(1, m_ram, true);           // 0x4000 - 0x7FFF: RAM bank 0
    updateMap(2, m_ram + 0x4000, true);  // 0x8000 - 0xBFFF: RAM bank 1
    updateMap(3, m_ram + 0x8000, true);  // 0xC000 - 0xFFFF: RAM bank 2
    // Video page pointer (used by renderer)
    m_videoPagePtr = m_ram;
    
    ESP_LOGI(TAG, "Spectrum 48K initialized (ROM: %p, RAM: %p)", m_rom, m_ram);
}

Spectrum48K::~Spectrum48K() {
    if (m_ram) free(m_ram);
    ESP_LOGI(TAG, "Spectrum 48K destroyed");
}

void Spectrum48K::reset() {
    SpectrumBase::reset();
    memset(m_ram, 0, RAM_SIZE);
    m_videoPagePtr = m_ram;
    ESP_LOGI(TAG, "Spectrum 48K reset");
}

void Spectrum48K::writePort(uint16_t port, uint8_t value) {
    if ((port & 0x0001) == 0) {
        writePortFE(value);
        return;
    }
}

uint8_t Spectrum48K::readPort(uint16_t port) {
    if ((port & 0x0001) == 0) {
        return readPortFE(port);
    }
    return getFloatingBusValue();
}

static const SpectrumBase::MemoryRegion s_memoryMap48K[] = {
    {0x0000, 0x3FFF, "ROM", false},
    {0x4000, 0x57FF, "Screen bitmap", true},
    {0x5800, 0x5AFF, "Screen attributes", true},
    {0x5B00, 0x5BFF, "Printer buffer", true},
    {0x5C00, 0x5CBF, "System variables", true},
    {0x5CC0, 0x7FFF, "BASIC program area", true},
    {0x8000, 0xFFFF, "General RAM", false},
};

const SpectrumBase::MemoryRegion* Spectrum48K::getMemoryMap(size_t& count) const {
    count = sizeof(s_memoryMap48K) / sizeof(s_memoryMap48K[0]);
    return s_memoryMap48K;
}

bool Spectrum48K::applySnapshotData(const uint8_t* data, size_t len) {
    if (!m_ram) {
        ESP_LOGE(TAG, "No RAM allocated, cannot apply snapshot");
        return false;
    }

    if (len == RAM_SIZE) {
        memcpy(m_ram, data, RAM_SIZE);
        updateMap(1, m_ram, true);
        updateMap(2, m_ram + 0x4000, true);
        updateMap(3, m_ram + 0x8000, true);
        m_videoPagePtr = m_ram;
        ESP_LOGI(TAG, "Snapshot applied as raw RAM image (%zu bytes)", len);
        return true;
    }

    // Only accept exact 48K data here; otherwise fail and let base try other strategies
    ESP_LOGD(TAG, "applySnapshotData: unsupported snapshot length %zu for 48K", len);
    return false;
}
