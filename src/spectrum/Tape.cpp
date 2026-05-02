#include "Tape.h"
#include "SpectrumBase.h"

// #include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <esp_timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Arbitrary sanity cap (1 MiB). Real tapes are typically ~50 KiB.
static constexpr size_t TAPE_MAX_BYTES = 1024 * 1024;

Tape::Tape()
    : m_data(nullptr)
    , m_size(0)
    , m_enabled(false)
    , m_lastInstaloadDiag{"idle"}
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

void Tape::setInstaloadDiag(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(m_lastInstaloadDiag, sizeof(m_lastInstaloadDiag), fmt, args);
    va_end(args);
}

// Skip non-audio/control TZX blocks so mixed-content tapes can still parse
// and reach playable data blocks.
static bool skip_tzx_block(uint8_t type, const uint8_t* data, size_t size, size_t& p) {
    auto need = [&](size_t n) -> bool { return p + n <= size; };
    switch (type) {
        case 0x12: // Pure tone
            if (!need(4)) return false;
            p += 4;
            return true;
        case 0x13: { // Pulse sequence
            if (!need(1)) return false;
            uint8_t count = data[p++];
            if (!need((size_t)count * 2)) return false;
            p += (size_t)count * 2;
            return true;
        }
        case 0x15: { // Direct recording
            if (!need(8)) return false;
            uint32_t len = (uint32_t)data[p + 5] | ((uint32_t)data[p + 6] << 8) | ((uint32_t)data[p + 7] << 16);
            if (!need(8 + len)) return false;
            p += 8 + len;
            return true;
        }
        case 0x18: // CSW recording
        case 0x19: // Generalized data
        case 0x2B: { // Set signal level
            if (!need(4)) return false;
            uint32_t len = (uint32_t)data[p] | ((uint32_t)data[p + 1] << 8) | ((uint32_t)data[p + 2] << 16) | ((uint32_t)data[p + 3] << 24);
            if (!need(4 + len)) return false;
            p += 4 + len;
            return true;
        }
        case 0x21: { // Group start
            if (!need(1)) return false;
            uint8_t len = data[p++];
            if (!need(len)) return false;
            p += len;
            return true;
        }
        case 0x22: // Group end
        case 0x25: // Loop end
        case 0x27: // Return from sequence
            return true;
        case 0x23: // Jump
        case 0x24: // Loop start
            if (!need(2)) return false;
            p += 2;
            return true;
        case 0x26: { // Call sequence
            if (!need(2)) return false;
            uint16_t n = (uint16_t)data[p] | ((uint16_t)data[p + 1] << 8);
            if (!need(2 + (size_t)n * 2)) return false;
            p += 2 + (size_t)n * 2;
            return true;
        }
        case 0x28: { // Select block
            if (!need(2)) return false;
            uint16_t len = (uint16_t)data[p] | ((uint16_t)data[p + 1] << 8);
            if (!need(2 + len)) return false;
            p += 2 + len;
            return true;
        }
        case 0x2A: // Stop tape if in 48K mode
            if (!need(4)) return false;
            p += 4;
            return true;
        case 0x30: { // Text description
            if (!need(1)) return false;
            uint8_t len = data[p++];
            if (!need(len)) return false;
            p += len;
            return true;
        }
        case 0x31: { // Message block
            if (!need(2)) return false;
            uint8_t len = data[p + 1];
            if (!need(2 + len)) return false;
            p += 2 + len;
            return true;
        }
        case 0x32: { // Archive info
            if (!need(2)) return false;
            uint16_t len = (uint16_t)data[p] | ((uint16_t)data[p + 1] << 8);
            if (!need(2 + len)) return false;
            p += 2 + len;
            return true;
        }
        case 0x33: { // Hardware type
            if (!need(1)) return false;
            uint8_t n = data[p++];
            if (!need((size_t)n * 3)) return false;
            p += (size_t)n * 3;
            return true;
        }
        case 0x35: { // Custom info block
            if (!need(20)) return false;
            uint32_t len = (uint32_t)data[p + 16] | ((uint32_t)data[p + 17] << 8) | ((uint32_t)data[p + 18] << 16) | ((uint32_t)data[p + 19] << 24);
            if (!need(20 + len)) return false;
            p += 20 + len;
            return true;
        }
        case 0x5A: // Glue block
            if (!need(9)) return false;
            p += 9;
            return true;
        default:
            return false;
    }
}

Tape::~Tape() { unload(); }

void Tape::setMode(TapeMode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
}

bool Tape::load(const char* filepath) {
    unload();

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || (size_t)sz > TAPE_MAX_BYTES) {
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
        fclose(f);
        return false;
    }

    size_t rd = fread(m_data, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
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
    setInstaloadDiag("loaded blocks=%d", m_num_blocks);
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
    setInstaloadDiag("no_tape");
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
        if (m_size < 10) return;
        size_t p = 10;
        while (p < m_size && m_num_blocks < MAX_TAPE_BLOCKS) {
            if (p + 1 > m_size) break;
            uint8_t type = m_data[p++];

            switch (type) {
                case 0x10: {
                    if (p + 4 > m_size) return;
                    uint32_t pause = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t len = m_data[p + 2] | (m_data[p + 3] << 8);
                    if (p + 4 + len > m_size) return;
                    if (len == 0) { p += 4; break; }
                    const uint8_t* data = &m_data[p + 4];
                    if (len < 2) { p += 4 + len; break; }
                    uint8_t flag = data[0];
                    addBlock(type, data, len, pause, T_PILOT,
                             (flag < 128) ? C_PILOT_HDR : C_PILOT_DAT,
                             T_SYNC1, T_SYNC2, T_ZERO, T_ONE, 8);
                    p += 4 + len;
                    break;
                }
                case 0x11: {
                    if (p + 18 > m_size) return;
                    uint16_t pilot = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t s1 = m_data[p + 2] | (m_data[p + 3] << 8);
                    uint16_t s2 = m_data[p + 4] | (m_data[p + 5] << 8);
                    uint16_t zero = m_data[p + 6] | (m_data[p + 7] << 8);
                    uint16_t one = m_data[p + 8] | (m_data[p + 9] << 8);
                    uint16_t pcount = m_data[p + 10] | (m_data[p + 11] << 8);
                    uint8_t bits = m_data[p + 12];
                    uint32_t pause = m_data[p + 13] | (m_data[p + 14] << 8);
                    uint32_t len = m_data[p + 15] | (m_data[p + 16] << 8) | (m_data[p + 17] << 16);

                    if (p + 18 + len > m_size) return;
                    if (bits == 0 || bits > 8) { p += 18 + len; break; }
                    if (zero == 0 || one == 0) { p += 18 + len; break; }

                    addBlock(type, &m_data[p + 18], len, pause, pilot, pcount,
                             s1, s2, zero, one, bits);
                    p += 18 + len;
                    break;
                }
                case 0x14: {
                    if (p + 10 > m_size) return;
                    uint16_t zero = m_data[p] | (m_data[p + 1] << 8);
                    uint16_t one = m_data[p + 2] | (m_data[p + 3] << 8);
                    uint8_t bits = m_data[p + 4];
                    uint32_t pause = m_data[p + 5] | (m_data[p + 6] << 8);
                    uint32_t len = m_data[p + 7] | (m_data[p + 8] << 8) | (m_data[p + 9] << 16);

                    if (p + 10 + len > m_size) return;
                    if (bits == 0 || bits > 8) { p += 10 + len; break; }
                    if (zero == 0 || one == 0) { p += 10 + len; break; }

                    addBlock(type, &m_data[p + 10], len, pause, 0, 0, 0, 0,
                             zero, one, bits);
                    p += 10 + len;
                    break;
                }
                case 0x20: {
                    if (p + 2 > m_size) return;
                    uint32_t pause = m_data[p] | (m_data[p + 1] << 8);
                    addBlock(type, nullptr, 0, pause);
                    p += 2;
                    break;
                }
                default:
                    if (!skip_tzx_block(type, m_data, m_size, p)) {
                        ESP_LOGW("Tape", "Unsupported or malformed TZX block 0x%02X at offset %u", type, (unsigned)(p - 1));
                        return;
                    }
                    break;
            }
        }
    } else {
        size_t p = 0;
        while (p + 2 <= m_size && m_num_blocks < MAX_TAPE_BLOCKS) {
            uint16_t len = m_data[p] | (m_data[p + 1] << 8);
            if (len == 0) break;
            if (p + 2 + len > m_size) break;
            const uint8_t* data = &m_data[p + 2];
            if (len < 2) { p += 2 + len; continue; }
            uint8_t flag = data[0];
            addBlock(0x10, data, len, 1000, T_PILOT,
                     (flag < 128) ? C_PILOT_HDR : C_PILOT_DAT,
                     T_SYNC1, T_SYNC2, T_ZERO, T_ONE, 8);
            p += 2 + len;
        }
    }
}

void Tape::advance(uint32_t tstates, std::function<void(uint32_t offset_tstates, bool ear)> onToggle) {
    if (!m_playing || m_paused || !m_data) return;

    // Sanity check to prevent infinite loop or stall
    if (m_current_pulse_len < 1) m_current_pulse_len = 1;

    uint32_t batch_consumed = 0;
    
    while (m_playing && tstates > 0) {
        uint32_t remaining_in_pulse = m_current_pulse_len - m_tstate_counter;
        if (tstates >= remaining_in_pulse) {
            // Pulse finishes during this batch
            tstates -= remaining_in_pulse;
            batch_consumed += remaining_in_pulse;
            m_tstate_counter = 0; // Pulse is done
            
            m_ear = !m_ear;
            if (onToggle) {
                onToggle(batch_consumed, m_ear);
            }
            
            if (m_state_pulses_left > 0) m_state_pulses_left--;
            if (m_state_pulses_left == 0) {
                nextState();
                if (m_current_pulse_len < 1) {
                    if (m_playing) stop();
                    return;
                }
            }
        } else {
            // Pulse doesn't finish during this batch
            m_tstate_counter += tstates;
            tstates = 0;
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
                m_pstate = PlayState::PAUSE; m_state_pulses_left = 1; m_current_pulse_len = b.pause_tstates;
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
        if (data) *data = b.data;
        if (length) *length = b.length;
        if (flag) *flag = 0xFF;
    } else {
        if (b.length < 2) return false;
        if (data) *data = b.data + 1;
        if (length) *length = b.length - 2;
        if (flag) *flag = b.data[0];

        if (b.type == 0x10 || b.type == 0x11) {
            uint8_t checksum = 0;
            for (uint32_t i = 0; i < b.length - 1; i++) {
                checksum ^= b.data[i];
            }
            if (checksum != b.data[b.length - 1]) {
                ESP_LOGD("Tape", "Block %d: checksum mismatch (calc=0x%02X, expected=0x%02X)",
                         idx, checksum, b.data[b.length - 1]);
                return false;
            }
        }
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

bool Tape::looks128K() const {
    if (m_num_blocks < 3) return false;

    // A program looks like 128K if it:
    // 1. Has multiple blocks loading to the SAME address >= 0x8000 (bank switching used for data/code)
    // 2. Or total code size exceeds 48K machine's available RAM (~42K after BASIC)
    
    uint16_t prev_addr = 0;
    int duplicate_addr_count = 0;
    uint32_t total_code_size = 0;

    for (int i = 0; i < m_num_blocks; i++) {
        if (!isDataBlock(i)) continue;

        const TapeBlockInternal& b = m_blocks[i];
        if (b.type == 0x10 || b.type == 0x11) {
            if (b.length == 19 && b.data[0] == 0x00) { // Header block
                uint8_t type = b.data[1];
                uint16_t start = b.data[14] | (b.data[15] << 8);
                if (type == 3 && start != 0) { // CODE block
                    if (start == prev_addr && start >= 0x8000) {
                        duplicate_addr_count++;
                    }
                    prev_addr = start;
                }
            } else if (b.data[0] >= 0x80) { // Data block (likely associated with previous header)
                total_code_size += b.length;
            }
        }
    }

    if (duplicate_addr_count >= 1) return true;
    if (total_code_size > 42000) return true;

    return false;
}

bool Tape::canPlay(SpectrumBase* spectrum) const {
    if (!m_enabled || !m_data) return true; // Nothing loaded is "compatible"
    if (looks128K() && !spectrum->is128k()) return false;
    return true;
}

bool Tape::instaload(SpectrumBase* spectrum) {
    if (!m_enabled || m_num_blocks == 0) {
        setInstaloadDiag("failed no_tape");
        return false;
    }

    // Policy enforcement (canPlay/ROM check) is now handled in SpectrumBase::startTapeInstaload()
    // and SpectrumBase::startTapePlayback(). 
    // This low-level function focuses strictly on the memory injection.

    ESP_LOGI("Tape", "instaload: %d blocks in tape", m_num_blocks);

    Z80* cpu = spectrum->getCPU();
    uint16_t lastCodeStart = 0, totalProgLen = 0;
    bool hasCode = false, hasBasic = false;
    int basicBlocks = 0, codeBlocks = 0, skippedBlocks = 0;
    uint32_t bytesLoaded = 0;

    int i = 0;
    while (i < m_num_blocks - 1) {
        const uint8_t* hData; uint32_t hLen; uint8_t hFlag;
        if (!getBlockContent(i, &hData, &hLen, &hFlag)) {
            skippedBlocks++;
            ++i;
            continue;
        }
        if (hLen != 17 || hFlag != 0x00) {
            skippedBlocks++;
            ++i;
            continue;
        }

        uint8_t type = hData[0];
        uint16_t len = hData[11] | (hData[12] << 8);
        uint16_t start = hData[13] | (hData[14] << 8);
        ESP_LOGD("Tape", "instaload block %d: type=%d len=%d start=0x%04X", i, type, len, start);

        // Validate header length is reasonable
        if (len == 0 || len > 0x8000) {
            skippedBlocks++;
            ++i;
            continue;
        }

        const uint8_t* dData; uint32_t dLen;
        if (!getBlockContent(i + 1, &dData, &dLen)) {
            ESP_LOGD("Tape", "  -> data block %d not found or invalid", i+1);
            skippedBlocks++;
            ++i;
            continue;
        }

        // Clamp and validate data length
        if (dLen > len) dLen = len;
        if (dLen > 0x8000) dLen = 0x8000;

        uint16_t addr;
        if (type == 0) {
            // BASIC program - always loads at 23755
            addr = 23755;
            hasBasic = true;
            basicBlocks++;
            totalProgLen = (uint16_t)dLen;
            ESP_LOGD("Tape", "  -> Loading BASIC, %d bytes to 0x%04X", dLen, addr);
        } else if (type == 3) {
            // CODE block - validate address range
            // Reject display memory (0x4000-0x57FF) which would corrupt screen
            // Allow everything else including workspace/BASIC areas for compatibility
            if (start >= 0x4000 && start <= 0x57FF) {
                ESP_LOGD("Tape", "  -> Skipping CODE block (load addr 0x%04X in display)", start);
                skippedBlocks++;
                ++i;
                continue;
            }
            // Reject if write would overflow 64K address space
            if (start > (0x10000 - (uint16_t)dLen)) {
                ESP_LOGD("Tape", "  -> Skipping CODE block (would overflow at 0x%04X)", start);
                skippedBlocks++;
                ++i;
                continue;
            }
            addr = start;
            lastCodeStart = start;
            hasCode = true;
            codeBlocks++;
            ESP_LOGD("Tape", "  -> Loading CODE, %d bytes to 0x%04X", dLen, addr);
        } else {
            // Reject unknown program types
            ESP_LOGD("Tape", "  -> Skipping unknown type %d", type);
            skippedBlocks++;
            ++i;
            continue;
        }

        // Write program data to memory
        for (uint16_t j = 0; j < dLen; j++) {
            spectrum->write((uint16_t)(addr + j), dData[j]);
        }
        bytesLoaded += dLen;
        i += 2; // Skip both header and data block
    }

    // Setup system variables for BASIC if loaded
    if (hasBasic) {
        uint16_t vars = (uint16_t)(23755 + totalProgLen), eLine = (uint16_t)(vars + 1);
        spectrum->write(23635, 23755 & 0xFF); spectrum->write(23636, 23755 >> 8);
        spectrum->write(23627, vars & 0xFF);  spectrum->write(23628, vars >> 8);
        spectrum->write(23641, eLine & 0xFF); spectrum->write(23642, eLine >> 8);
        spectrum->write(23645, eLine & 0xFF); spectrum->write(23646, eLine >> 8);
        spectrum->write(23647, eLine & 0xFF); spectrum->write(23648, eLine >> 8);
    }
    // Setup CPU state for loaded program.
    // Prefer BASIC entry when present; many tapes include CODE assets (e.g. screens)
    // that are data, not executable entry points.
    if (hasBasic) {
        setInstaloadDiag("ok basic len=%u code=%d skip=%d bytes=%lu", totalProgLen, codeBlocks, skippedBlocks, (unsigned long)bytesLoaded);
        ESP_LOGI("Tape", "instaload summary: %s", m_lastInstaloadDiag);
        ESP_LOGI("Tape", "instaload: executing BASIC at 23755");
        cpu->sp = 0xFFFE; cpu->iff1 = cpu->iff2 = 1; cpu->im = 1; cpu->halted = 0; cpu->pc = 23755;
        return true;
    } else if (hasCode) {
        setInstaloadDiag("ok code start=%04X code=%d skip=%d bytes=%lu", lastCodeStart, codeBlocks, skippedBlocks, (unsigned long)bytesLoaded);
        ESP_LOGI("Tape", "instaload summary: %s", m_lastInstaloadDiag);
        ESP_LOGI("Tape", "instaload: executing CODE at 0x%04X", lastCodeStart);
        cpu->sp = 0xFFFE; cpu->iff1 = cpu->iff2 = 1; cpu->im = 1; cpu->halted = 0; cpu->pc = lastCodeStart;
        return true;
    } else {
        setInstaloadDiag("failed basic=%d code=%d skip=%d bytes=%lu", basicBlocks, codeBlocks, skippedBlocks, (unsigned long)bytesLoaded);
        ESP_LOGW("Tape", "instaload summary: %s", m_lastInstaloadDiag);
        ESP_LOGW("Tape", "instaload: no valid program blocks found!");
        // No valid blocks loaded - leave CPU at LD-BYTES for manual loading or show error
        // Don't change CPU state to avoid jumping to garbage address
        return false;
    }
}
