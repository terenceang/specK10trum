#include "SpectrumBase.h"
#include "Snapshot.h"
#include <cstdio>
#include <esp_log.h>

static const char* TAG = "SpectrumBase";

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
