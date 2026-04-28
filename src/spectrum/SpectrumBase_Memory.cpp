#include "SpectrumBase.h"
#include <esp_heap_caps.h>
#include <esp_psram.h>

static const char* TAG = "SpectrumBase";

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
