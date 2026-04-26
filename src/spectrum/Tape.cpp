#define KEYBOARD_DEBUG 0
#include "Tape.h"
#include "SpectrumBase.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "Tape";

// Arbitrary sanity cap (1 MiB). Real tapes are typically ~50 KiB.
static constexpr size_t TAPE_MAX_BYTES = 1024 * 1024;

Tape::Tape()
    : m_data(nullptr)
    , m_size(0)
    , m_enabled(false)
    , m_mode(TapeMode::INSTANT)
    , m_playing(false)
    , m_paused(false)
    , m_ear(false)
    , m_tstate_counter(0)
    , m_num_blocks(0)
    , m_current_block_idx(0)
    , m_pstate(PlayState::IDLE)
    , m_state_pulses_left(0)
    , m_current_pulse_len(1)
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
    }
}

void Tape::stop() {
    m_playing = false;
    m_paused = false;
    resetPlaybackState();
}

void Tape::rewind() {
    resetPlaybackState();
}

void Tape::fastForward() {
    if (m_current_block_idx < m_num_blocks - 1) {
        m_current_block_idx++;
        m_pstate = PlayState::IDLE;
        m_state_pulses_left = 0;
        m_tstate_counter = 0;
    }
}

void Tape::pause() {
    m_paused = !m_paused;
}

void Tape::eject() {
    unload();
}

void Tape::resetPlaybackState() {
    m_current_block_idx = 0;
    m_pstate = PlayState::IDLE;
    m_state_pulses_left = 0;
    m_tstate_counter = 0;
    m_current_pulse_len = 1; // Ensure advance() doesn't return immediately
    m_data_byte_idx = 0;
    m_data_bit_idx = 0;
    m_ear = false;
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

void Tape::addBlock(uint8_t type, const uint8_t* data, uint32_t length, uint32_t pause_ms,
                    uint16_t pilot_len, uint16_t pilot_count, 
                    uint16_t sync1_len, uint16_t sync2_len,
                    uint16_t zero_len, uint16_t one_len, uint8_t used_bits) {
    if (m_num_blocks >= MAX_TAPE_BLOCKS) return;
    TapeBlockInternal& b = m_blocks[m_num_blocks++];
    b.type = type;
    b.data = data;
    b.length = length;
    b.pause_tstates = pause_ms * 3500;
    b.pilot_len = pilot_len;
    b.pilot_count = pilot_count;
    b.sync1_len = sync1_len;
    b.sync2_len = sync2_len;
    b.zero_len = zero_len;
    b.one_len = one_len;
    b.used_bits = used_bits;
}

void Tape::buildBlockList() {
    m_num_blocks = 0;
    if (!m_data) return;

    if (m_is_tzx) {
        size_t p = 10; // Skip header
        while (p < m_size && m_num_blocks < MAX_TAPE_BLOCKS) {
            uint8_t type = m_data[p++];
            switch (type) {
                case 0x10: { // Standard Speed
                    uint32_t pause = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t len = m_data[p + 2] | (m_data[p + 3] << 8);
                    const uint8_t* data = &m_data[p + 4];
                    addBlock(type, data, len, pause, T_PILOT, (data[0] < 128) ? C_PILOT_HDR : C_PILOT_DAT, T_SYNC1, T_SYNC2, T_ZERO, T_ONE, 8);
                    p += 4 + len;
                    break;
                }
                case 0x11: { // Turbo Speed
                    uint16_t pilot = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t s1 = m_data[p + 2] | (m_data[p + 3] << 8);
                    uint16_t s2 = m_data[p + 4] | (m_data[p + 5] << 8);
                    uint16_t zero = m_data[p + 6] | (m_data[p + 7] << 8);
                    uint16_t one = m_data[p + 8] | (m_data[p + 9] << 8);
                    uint16_t pcount = m_data[p + 10] | (m_data[p + 11] << 8);
                    uint8_t bits = m_data[p + 12];
                    uint32_t pause = m_data[p + 13] | (m_data[p + 14] << 8);
                    uint32_t len = m_data[p + 15] | (m_data[p + 16] << 8) | (m_data[p + 17] << 16);
                    addBlock(type, &m_data[p + 18], len, pause, pilot, pcount, s1, s2, zero, one, bits);
                    p += 18 + len;
                    break;
                }
                case 0x14: { // Pure Data
                    uint16_t zero = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t one = m_data[p + 2] | (m_data[p + 3] << 8);
                    uint8_t bits = m_data[p + 4];
                    uint32_t pause = m_data[p + 5] | (m_data[p + 6] << 8);
                    uint32_t len = m_data[p + 7] | (m_data[p + 8] << 8) | (m_data[p + 9] << 16);
                    addBlock(type, &m_data[p + 10], len, pause, 0, 0, 0, 0, zero, one, bits);
                    p += 10 + len;
                    break;
                }
                case 0x20: { // Pause
                    uint32_t pause = m_data[p] | (m_data[p + 1] << 8);
                    addBlock(type, nullptr, 0, pause);
                    p += 2;
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
            const uint8_t* data = &m_data[p + 2];
            addBlock(0x10, data, len, 1000, T_PILOT, (data[0] < 128) ? C_PILOT_HDR : C_PILOT_DAT, T_SYNC1, T_SYNC2, T_ZERO, T_ONE, 8);
            p += 2 + len;
        }
    }
}

void Tape::advance(uint32_t tstates) {
    if (!m_playing || m_paused || !m_data) return;
    if (m_current_pulse_len == 0) return;

    m_tstate_counter += tstates;
    while (m_playing && m_tstate_counter >= m_current_pulse_len) {
        m_tstate_counter -= m_current_pulse_len;
        m_ear = !m_ear;

        if (m_state_pulses_left > 0) m_state_pulses_left--;
        if (m_state_pulses_left == 0) {
            nextState();
            if (m_current_pulse_len == 0) {
                if (m_playing) stop();
                return;
            }
        }
    }
}

void Tape::startDataState() {
    m_pstate = PlayState::DATA;
    m_data_byte_idx = 0;
    m_data_bit_idx = 0;
    if (!advanceBit()) {
        m_pstate = PlayState::PAUSE;
        m_state_pulses_left = 1;
        m_current_pulse_len = m_blocks[m_current_block_idx].pause_tstates;
    }
}

bool Tape::advanceBit() {
    const TapeBlockInternal& b = m_blocks[m_current_block_idx];
    if (m_data_byte_idx >= b.length) return false;

    uint8_t bits = (m_data_byte_idx == b.length - 1) ? b.used_bits : 8;
    if (m_data_bit_idx >= bits) {
        m_data_bit_idx = 0;
        m_data_byte_idx++;
        if (m_data_byte_idx >= b.length) return false;
    }

    uint8_t bit = (b.data[m_data_byte_idx] & (0x80 >> m_data_bit_idx)) ? 1 : 0;
    m_current_pulse_len = bit ? b.one_len : b.zero_len;
    m_state_pulses_left = 2;
    return true;
}

void Tape::nextState() {
    if (m_current_block_idx >= m_num_blocks) { stop(); return; }
    const TapeBlockInternal& b = m_blocks[m_current_block_idx];

    switch (m_pstate) {
        case PlayState::IDLE:
            if (b.pilot_count > 0) {
                m_pstate = PlayState::PILOT;
                m_state_pulses_left = b.pilot_count;
                m_current_pulse_len = b.pilot_len;
            } else startDataState();
            break;
        case PlayState::PILOT:
            m_pstate = PlayState::SYNC1; m_state_pulses_left = 1; m_current_pulse_len = b.sync1_len;
            break;
        case PlayState::SYNC1:
            m_pstate = PlayState::SYNC2; m_state_pulses_left = 1; m_current_pulse_len = b.sync2_len;
            break;
        case PlayState::SYNC2:
            startDataState();
            break;
        case PlayState::DATA:
            m_data_bit_idx++;
            if (!advanceBit()) {
                m_pstate = PlayState::PAUSE; m_state_pulses_left = 1; m_current_pulse_len = b.pause_tstates; m_ear = false;
            }
            break;
        case PlayState::PAUSE:
            m_current_block_idx++; m_pstate = PlayState::IDLE; m_current_pulse_len = 1; m_state_pulses_left = 0;
            if (m_current_block_idx < m_num_blocks) nextState(); else stop();
            break;
    }
}

bool Tape::isDataBlock(int idx) const {
    if (idx < 0 || idx >= m_num_blocks) return false;
    const TapeBlockInternal& b = m_blocks[idx];
    return b.data != nullptr && b.length > 0 && (b.type == 0x10 || b.type == 0x11 || b.type == 0x14);
}

bool Tape::getBlockContent(int idx, const uint8_t** data, uint32_t* length, uint8_t* flag) const {
    if (!isDataBlock(idx)) return false;
    const TapeBlockInternal& b = m_blocks[idx];
    if (b.type == 0x14) {
        if (data) *data = b.data; if (length) *length = b.length; if (flag) *flag = 0xFF;
    } else {
        if (data) *data = b.data + 1; if (length) *length = (b.length >= 2) ? b.length - 2 : 0;
        if (flag) *flag = b.data[0];
    }
    return true;
}

int Tape::seekToNextDataBlock() {
    while (m_current_block_idx < m_num_blocks) {
        if (isDataBlock(m_current_block_idx)) return m_current_block_idx;
        m_current_block_idx++;
    }
    return -1;
}

int Tape::serviceLoadTrap(SpectrumBase* spectrum) {
    Z80* cpu = spectrum->getCPU();
    if (seekToNextDataBlock() < 0) { trapReturn(spectrum, cpu, false); return 11; }

    const uint8_t* content; uint32_t contentLen; uint8_t tapeFlag;
    getBlockContent(m_current_block_idx, &content, &contentLen, &tapeFlag);
    const uint8_t* raw = m_blocks[m_current_block_idx].data;
    const uint32_t rawLen = m_blocks[m_current_block_idx].length;
    m_current_block_idx++;

    const uint8_t  expectedFlag = cpu->a;
    const uint16_t DE = (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e);
    const uint16_t IX = cpu->ix;
    const bool     isLoad = (cpu->f & Z80_CF) != 0;

    if (tapeFlag != expectedFlag || DE > contentLen) { trapReturn(spectrum, cpu, false); return 11; }

    uint8_t parity = tapeFlag;
    for (uint16_t i = 0; i < DE; ++i) {
        uint8_t b = content[i]; parity ^= b;
        if (isLoad) spectrum->write((uint16_t)(IX + i), b);
    }
    
    bool ok = (DE != contentLen) || ((parity ^ raw[rawLen - 1]) == 0);
    cpu->ix = (uint16_t)(IX + DE); cpu->d = 0; cpu->e = 0; cpu->a = raw[rawLen - 1];
    trapReturn(spectrum, cpu, ok);
    return 11;
}

void Tape::instaload(SpectrumBase* spectrum) {
    if (!m_enabled || m_num_blocks == 0) return;
    Z80* cpu = spectrum->getCPU();
    uint16_t lastCodeStart = 0, totalProgLen = 0;
    bool hasCode = false, hasBasic = false;

    for (int i = 0; i < (m_num_blocks - 1); i++) {
        const uint8_t* hData; uint32_t hLen; uint8_t hFlag;
        if (getBlockContent(i, &hData, &hLen, &hFlag) && hLen == 17 && hFlag == 0x00) {
            uint8_t type = hData[0];
            uint16_t len = hData[11] | (hData[12] << 8);
            uint16_t start = hData[13] | (hData[14] << 8);
            
            const uint8_t* dData; uint32_t dLen;
            if (getBlockContent(i + 1, &dData, &dLen)) {
                if (dLen > len) dLen = len;
                uint16_t addr = (type == 0) ? 23755 : start;
                for (uint16_t j = 0; j < dLen; j++) spectrum->write((uint16_t)(addr + j), dData[j]);
                if (type == 0) { hasBasic = true; totalProgLen = (uint16_t)dLen; }
                else if (type == 3 && start != 16384) { lastCodeStart = start; hasCode = true; }
                i++;
            }
        }
    }

    if (hasBasic) {
        uint16_t vars = (uint16_t)(23755 + totalProgLen), eLine = (uint16_t)(vars + 1);
        spectrum->write(23635, 23755 & 0xFF); spectrum->write(23636, 23755 >> 8);
        spectrum->write(23627, vars & 0xFF);  spectrum->write(23628, vars >> 8);
        spectrum->write(23641, eLine & 0xFF); spectrum->write(23642, eLine >> 8);
        spectrum->write(23645, eLine & 0xFF); spectrum->write(23646, eLine >> 8);
        spectrum->write(23647, eLine & 0xFF); spectrum->write(23648, eLine >> 8);
    }
    if (hasCode) {
        cpu->sp = 0xFFFE; cpu->iff1 = cpu->iff2 = 1; cpu->im = 1; cpu->halted = 0; cpu->pc = lastCodeStart;
    } else if (hasBasic) cpu->pc = 23755;
}
