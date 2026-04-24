#include "Tape.h"
#include "SpectrumBase.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* TAG = "Tape";

// Arbitrary sanity cap (1 MiB). Real tapes are typically ~50 KiB.
static constexpr size_t TAPE_MAX_BYTES = 1024 * 1024;

Tape::Tape()
    : m_data(nullptr)
    , m_size(0)
    , m_pos(0)
    , m_enabled(false)
{}

Tape::~Tape() { unload(); }

bool Tape::load(const char* filepath) {
    unload();

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open tape: %s", filepath);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || (size_t)sz > TAPE_MAX_BYTES) {
        ESP_LOGE(TAG, "Invalid tape size: %ld", sz);
        fclose(f);
        return false;
    }

    size_t caps = MALLOC_CAP_DEFAULT;
#ifdef CONFIG_SPIRAM
    if (esp_psram_is_initialized()) caps = MALLOC_CAP_SPIRAM;
#endif
    m_data = (uint8_t*)heap_caps_malloc((size_t)sz, caps);
    if (!m_data) {
        m_data = (uint8_t*)malloc((size_t)sz);
    }
    if (!m_data) {
        ESP_LOGE(TAG, "Failed to allocate tape buffer (%ld bytes)", sz);
        fclose(f);
        return false;
    }

    size_t rd = fread(m_data, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        ESP_LOGE(TAG, "Short read on tape: %zu / %ld", rd, sz);
        free(m_data);
        m_data = nullptr;
        return false;
    }

    m_size = (size_t)sz;
    m_pos = 0;
    m_enabled = true;

    // Quick sanity scan of block lengths; non-fatal if something's off.
    size_t p = 0, blocks = 0;
    while (p + 2 <= m_size) {
        uint16_t blen = (uint16_t)m_data[p] | ((uint16_t)m_data[p + 1] << 8);
        if (blen < 2 || p + 2 + blen > m_size) {
            ESP_LOGW(TAG, "Malformed block at offset %zu (len=%u)", p, blen);
            break;
        }
        p += 2 + blen;
        blocks++;
    }
    ESP_LOGI(TAG, "Tape loaded: %s (%zu bytes, %zu blocks)", filepath, m_size, blocks);
    return true;
}

void Tape::unload() {
    if (m_data) {
        free(m_data);
        m_data = nullptr;
    }
    m_size = 0;
    m_pos = 0;
    m_enabled = false;
}

// Small helper: pop a 16-bit value from the Spectrum stack via the memory bus.
static inline uint16_t popPC(SpectrumBase* s, Z80* cpu) {
    uint16_t lo = s->read(cpu->sp);
    uint16_t hi = s->read((uint16_t)(cpu->sp + 1));
    cpu->sp = (uint16_t)(cpu->sp + 2);
    return (uint16_t)(lo | (hi << 8));
}

static inline void trapReturn(SpectrumBase* s, Z80* cpu, bool carry) {
    if (carry) cpu->f |= Z80_CF;
    else       cpu->f &= (uint8_t)~Z80_CF;
    cpu->pc = popPC(s, cpu);
}

int Tape::serviceLoadTrap(SpectrumBase* spectrum) {
    Z80* cpu = spectrum->getCPU();

    // End-of-tape or no data: return CF=0.
    if (!m_data || m_pos >= m_size) {
        trapReturn(spectrum, cpu, false);
        return 11;
    }
    if (m_pos + 2 > m_size) {
        m_pos = m_size;
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    uint16_t blockLen = (uint16_t)m_data[m_pos] | ((uint16_t)m_data[m_pos + 1] << 8);
    if (blockLen < 2 || m_pos + 2 + blockLen > m_size) {
        ESP_LOGW(TAG, "Truncated block at pos %zu (len=%u)", m_pos, blockLen);
        m_pos = m_size;
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    const uint8_t* block = m_data + m_pos + 2;
    const uint8_t  tapeFlag = block[0];
    const uint16_t dataBytes = (uint16_t)(blockLen - 2); // data bytes excluding flag+xor

    const uint8_t  expectedFlag = cpu->a;
    const uint16_t DE = (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e);
    const uint16_t IX = cpu->ix;
    const bool     isLoad = (cpu->f & Z80_CF) != 0;

    // Consume this block regardless of outcome so the next call sees the next
    // block. Matches real-tape behaviour: a mismatched header is skipped.
    m_pos += 2 + blockLen;

    if (tapeFlag != expectedFlag) {
        ESP_LOGD(TAG, "Flag mismatch: wanted 0x%02X, got 0x%02X (skipping block)",
                 expectedFlag, tapeFlag);
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    if (DE > dataBytes) {
        ESP_LOGW(TAG, "Block too short: asked %u, have %u", DE, dataBytes);
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    uint8_t parity = tapeFlag;
    for (uint16_t i = 0; i < DE; ++i) {
        uint8_t b = block[1 + i];
        parity ^= b;
        if (isLoad) spectrum->write((uint16_t)(IX + i), b);
    }
    // XOR the tape's parity byte; valid block => parity == 0.
    const uint8_t parityByte = block[1 + dataBytes];
    parity ^= parityByte;
    const bool ok = (parity == 0);

    cpu->ix = (uint16_t)(IX + DE);
    cpu->d = 0;
    cpu->e = 0;
    cpu->a = parityByte;     // ROM leaves last tape byte in A.

    trapReturn(spectrum, cpu, ok);

    ESP_LOGI(TAG, "%s block flag=0x%02X len=%u -> %s (pos %zu/%zu)",
             isLoad ? "LOAD" : "VERIFY",
             tapeFlag, DE, ok ? "OK" : "PARITY-ERR",
             m_pos, m_size);

    return 11;
}

bool Tape::autoload(Tape& tape) {
    static const char* candidates[] = {
        "/spiffs/tape.tap",
        "/spiffs/autoload.tap",
    };
    struct stat st;
    for (const char* path : candidates) {
        if (stat(path, &st) == 0) {
            return tape.load(path);
        }
    }
    return false;
}
