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
    , m_enabled(false)
    , m_mode(TapeMode::NORMAL)
    , m_playing(false)
    , m_paused(false)
    , m_load_typed(false)
    , m_ear(false)
    , m_tstate_counter(0)
    , m_num_blocks(0)
    , m_current_block_idx(0)
    , m_pstate(PlayState::IDLE)
    , m_state_pulses_left(0)
    , m_current_pulse_len(0)
    , m_data_byte_idx(0)
    , m_data_bit_idx(0)
    , m_is_tzx(false)
{}

Tape::~Tape() { unload(); }

bool Tape::load(const char* filepath) {
    unload();

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open tape: %s", filepath);
        return false;
    }
    fseek(f, 0, SEEK_END);
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
    m_enabled = true;

    if (m_size > 7 && memcmp(m_data, "ZXTape!", 7) == 0) {
        m_is_tzx = true;
    } else {
        m_is_tzx = false;
    }

    buildBlockList();
    resetPlaybackState();
    
    ESP_LOGI(TAG, "Tape loaded: %s (%zu bytes, %d blocks, TZX=%s)", 
             filepath, m_size, m_num_blocks, m_is_tzx ? "yes" : "no");
    return true;
}

void Tape::unload() {
    stop();
    if (m_data) {
        free(m_data);
        m_data = nullptr;
    }
    m_size = 0;
    m_enabled = false;
    m_num_blocks = 0;
}

void Tape::play() {
    if (m_enabled && m_num_blocks > 0) {
        m_playing = true;
        m_paused = false;
        if (m_pstate == PlayState::IDLE) {
            nextState();
        }
        ESP_LOGI(TAG, "Tape PLAY");
    }
}

void Tape::stop() {
    m_playing = false;
    m_paused = false;
    resetPlaybackState();
    ESP_LOGI(TAG, "Tape STOP");
}

void Tape::rewind() {
    resetPlaybackState();
    ESP_LOGI(TAG, "Tape REWIND");
}

void Tape::fastForward() {
    m_current_block_idx = m_num_blocks - 1;
    m_pstate = PlayState::IDLE;
    ESP_LOGI(TAG, "Tape FFWD to last block");
}

void Tape::pause() {
    m_paused = !m_paused;
    ESP_LOGI(TAG, "Tape %s", m_paused ? "PAUSED" : "RESUMED");
}

void Tape::eject() {
    unload();
    ESP_LOGI(TAG, "Tape EJECTED");
}

void Tape::resetPlaybackState() {
    m_current_block_idx = 0;
    m_pstate = PlayState::IDLE;
    m_state_pulses_left = 0;
    m_tstate_counter = 0;
    m_data_byte_idx = 0;
    m_data_bit_idx = 0;
    m_ear = false;
}

void Tape::notifyLoad() {
    m_load_typed = true;
    if (m_mode == TapeMode::NORMAL) {
        play();
    }
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

void Tape::buildBlockList() {
    m_num_blocks = 0;
    if (!m_data) return;

    if (m_is_tzx) {
        // Skip "ZXTape!\x1A" (8 bytes) + version (2 bytes)
        size_t p = 10;
        while (p < m_size && m_num_blocks < MAX_TAPE_BLOCKS) {
            uint8_t type = m_data[p++];
            TapeBlockInternal& b = m_blocks[m_num_blocks];
            b.type = type;
            
            switch (type) {
                case 0x10: { // Standard Speed Data
                    b.pause_tstates = (uint32_t)((m_data[p] | (m_data[p + 1] << 8))) * 3500;
                    uint16_t len = m_data[p + 2] | (m_data[p + 3] << 8);
                    b.data = &m_data[p + 4];
                    b.length = len;
                    b.pilot_len = 2168;
                    b.pilot_count = (b.data[0] < 128) ? 8063 : 3223;
                    b.sync1_len = 667;
                    b.sync2_len = 735;
                    b.zero_len = 855;
                    b.one_len = 1710;
                    b.used_bits = 8;
                    p += 4 + len;
                    m_num_blocks++;
                    break;
                }
                case 0x11: { // Turbo Speed Data
                    b.pilot_len = m_data[p] | (m_data[p + 1] << 8);
                    b.sync1_len = m_data[p + 2] | (m_data[p + 3] << 8);
                    b.sync2_len = m_data[p + 4] | (m_data[p + 5] << 8);
                    b.zero_len = m_data[p + 6] | (m_data[p + 7] << 8);
                    b.one_len = m_data[p + 8] | (m_data[p + 9] << 8);
                    b.pilot_count = m_data[p + 10] | (m_data[p + 11] << 8);
                    b.used_bits = m_data[p + 12];
                    b.pause_tstates = (uint32_t)(m_data[p + 13] | (m_data[p + 14] << 8)) * 3500;
                    uint32_t len = m_data[p + 15] | (m_data[p + 16] << 8) | (m_data[p + 17] << 16);
                    b.data = &m_data[p + 18];
                    b.length = len;
                    p += 18 + len;
                    m_num_blocks++;
                    break;
                }
                case 0x14: { // Pure Data
                    b.zero_len = m_data[p] | (m_data[p + 1] << 8);
                    b.one_len = m_data[p + 2] | (m_data[p + 3] << 8);
                    b.used_bits = m_data[p + 4];
                    b.pause_tstates = (uint32_t)(m_data[p + 5] | (m_data[p + 6] << 8)) * 3500;
                    uint32_t len = m_data[p + 7] | (m_data[p + 8] << 8) | (m_data[p + 9] << 16);
                    b.data = &m_data[p + 10];
                    b.length = len;
                    b.pilot_count = 0; // No pilot
                    p += 10 + len;
                    m_num_blocks++;
                    break;
                }
                case 0x20: { // Pause
                    b.pause_tstates = (uint32_t)(m_data[p] | (m_data[p + 1] << 8)) * 3500;
                    b.data = nullptr;
                    b.length = 0;
                    p += 2;
                    m_num_blocks++;
                    break;
                }
                default:
                    ESP_LOGW(TAG, "Unsupported TZX block 0x%02X at %zu", type, p - 1);
                    return;
            }
        }
    } else {
        size_t p = 0;
        while (p + 2 <= m_size && m_num_blocks < MAX_TAPE_BLOCKS) {
            uint16_t len = m_data[p] | (m_data[p + 1] << 8);
            if (p + 2 + len > m_size) break;
            TapeBlockInternal& b = m_blocks[m_num_blocks];
            b.type = 0x10;
            b.data = &m_data[p + 2];
            b.length = len;
            b.pause_tstates = 1000 * 3500;
            b.pilot_len = 2168;
            b.pilot_count = (b.data[0] < 128) ? 8063 : 3223;
            b.sync1_len = 667;
            b.sync2_len = 735;
            b.zero_len = 855;
            b.one_len = 1710;
            b.used_bits = 8;
            p += 2 + len;
            m_num_blocks++;
        }
    }
}

void Tape::advance(uint32_t tstates) {
    if (!m_playing || m_paused) return;

    m_tstate_counter += tstates;
    while (m_playing && m_tstate_counter >= m_current_pulse_len) {
        m_tstate_counter -= m_current_pulse_len;
        m_ear = !m_ear;
        
        if (m_state_pulses_left > 0) {
            m_state_pulses_left--;
        }
        
        if (m_state_pulses_left == 0) {
            nextState();
        }
    }
}

void Tape::nextState() {
    if (m_current_block_idx >= m_num_blocks) {
        stop();
        return;
    }

    const TapeBlockInternal& b = m_blocks[m_current_block_idx];

    switch (m_pstate) {
        case PlayState::IDLE:
            if (b.pilot_count > 0) {
                m_pstate = PlayState::PILOT;
                m_state_pulses_left = b.pilot_count;
                m_current_pulse_len = b.pilot_len;
            } else {
                m_pstate = PlayState::DATA;
                m_data_byte_idx = 0;
                m_data_bit_idx = 0;
                // For Pure Data, there's no sync pulses
                uint8_t bit = (b.data[0] & 0x80) ? 1 : 0;
                m_current_pulse_len = bit ? b.one_len : b.zero_len;
                m_state_pulses_left = 2;
            }
            break;

        case PlayState::PILOT:
            m_pstate = PlayState::SYNC1;
            m_state_pulses_left = 1;
            m_current_pulse_len = b.sync1_len;
            break;

        case PlayState::SYNC1:
            m_pstate = PlayState::SYNC2;
            m_state_pulses_left = 1;
            m_current_pulse_len = b.sync2_len;
            break;

        case PlayState::SYNC2:
            m_pstate = PlayState::DATA;
            m_data_byte_idx = 0;
            m_data_bit_idx = 0;
            {
                uint8_t bit = (b.data[0] & 0x80) ? 1 : 0;
                m_current_pulse_len = bit ? b.one_len : b.zero_len;
                m_state_pulses_left = 2;
            }
            break;

        case PlayState::DATA:
            m_state_pulses_left = 2; // Always 2 pulses per bit
            m_data_bit_idx++;
            
            bool lastByte = (m_data_byte_idx == b.length - 1);
            uint8_t bitsInThisByte = lastByte ? b.used_bits : 8;

            if (m_data_bit_idx >= bitsInThisByte) {
                m_data_bit_idx = 0;
                m_data_byte_idx++;
            }

            if (m_data_byte_idx < b.length) {
                uint8_t bit = (b.data[m_data_byte_idx] & (0x80 >> m_data_bit_idx)) ? 1 : 0;
                m_current_pulse_len = bit ? b.one_len : b.zero_len;
            } else {
                m_pstate = PlayState::PAUSE;
                m_state_pulses_left = 1;
                m_current_pulse_len = b.pause_tstates;
                m_ear = false; // SILENCE
            }
            break;

        case PlayState::PAUSE:
            m_current_block_idx++;
            m_pstate = PlayState::IDLE;
            m_current_pulse_len = 1; // Transition immediately
            m_state_pulses_left = 0;
            if (m_current_block_idx < m_num_blocks) {
                nextState();
            } else {
                stop();
            }
            break;
    }
}

int Tape::serviceLoadTrap(SpectrumBase* spectrum) {
    Z80* cpu = spectrum->getCPU();

    if (m_num_blocks == 0 || m_current_block_idx >= m_num_blocks) {
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    const TapeBlockInternal& block = m_blocks[m_current_block_idx];
    
    // We only support instant-loading of standard data blocks (0x10) for now.
    // TAP blocks are parsed as type 0x10.
    if (block.type != 0x10) {
        ESP_LOGW(TAG, "Instant load: block %d is not type 0x10 (type=0x%02X), skipping", 
                 m_current_block_idx, block.type);
        m_current_block_idx++;
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    const uint8_t* data = block.data;
    const uint8_t tapeFlag = data[0];
    const uint16_t dataBytes = (uint16_t)(block.length - 2);

    const uint8_t  expectedFlag = cpu->a;
    const uint16_t DE = (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e);
    const uint16_t IX = cpu->ix;
    const bool     isLoad = (cpu->f & Z80_CF) != 0;

    // Consume this block.
    m_current_block_idx++;

    if (tapeFlag != expectedFlag) {
        ESP_LOGD(TAG, "Flag mismatch: wanted 0x%02X, got 0x%02X", expectedFlag, tapeFlag);
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
        uint8_t b = data[1 + i];
        parity ^= b;
        if (isLoad) spectrum->write((uint16_t)(IX + i), b);
    }
    const uint8_t parityByte = data[1 + dataBytes];
    parity ^= parityByte;
    const bool ok = (parity == 0);

    cpu->ix = (uint16_t)(IX + DE);
    cpu->d = 0;
    cpu->e = 0;
    cpu->a = parityByte;

    trapReturn(spectrum, cpu, ok);

    ESP_LOGI(TAG, "INSTANT %s flag=0x%02X len=%u -> %s",
             isLoad ? "LOAD" : "VERIFY",
             tapeFlag, DE, ok ? "OK" : "PARITY-ERR");

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
