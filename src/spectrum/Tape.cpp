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
    if (!m_playing || m_paused || !m_data) return;

    if (m_current_pulse_len == 0) return;

    m_tstate_counter += tstates;

    while (m_playing && m_tstate_counter >= m_current_pulse_len) {
        m_tstate_counter -= m_current_pulse_len;
        m_ear = !m_ear;

        if (m_state_pulses_left > 0) {
            m_state_pulses_left--;
        }

        if (m_state_pulses_left == 0) {
            nextState();
            if (m_current_pulse_len == 0) {
                if (m_playing) stop();
                return;
            }
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
        {
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

    // Skip any non-data blocks (like pauses or metadata) until we find a data-carrying block
    while (m_current_block_idx < m_num_blocks) {
        const TapeBlockInternal& b = m_blocks[m_current_block_idx];
        if (b.data != nullptr && b.length > 0 && (b.type == 0x10 || b.type == 0x11 || b.type == 0x14)) {
            break;
        }
        m_current_block_idx++;
    }

    if (m_num_blocks == 0 || m_current_block_idx >= m_num_blocks) {
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    const TapeBlockInternal& block = m_blocks[m_current_block_idx];
    const uint8_t* data = block.data;
    const uint16_t blockLen = (uint16_t)block.length;

    const uint8_t  expectedFlag = cpu->a;
    const uint16_t DE = (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e);
    const uint16_t IX = cpu->ix;
    const bool     isLoad = (cpu->f & Z80_CF) != 0;

    // Standard Spectrum data blocks have: [Flag] [Data...] [Checksum]
    // blockLen includes Flag and Checksum.
    uint16_t dataAvailable = (blockLen >= 2) ? (blockLen - 2) : 0;
    uint8_t tapeFlag = (blockLen > 0) ? data[0] : 0xFF;

    // If it's a Pure Data block (0x14), it doesn't have a flag or checksum byte in the TZX spec,
    // but the Spectrum LD-BYTES routine ALWAYS expects a flag byte at the start of the "pulse" stream.
    // However, most TZX 'Pure Data' blocks used for standard loaders ARE prefixed with a flag.
    // We'll trust the first byte of the block is the flag if it's not a standard block too.

    // Consume this block.
    m_current_block_idx++;

    if (tapeFlag != expectedFlag) {
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    // Note: Some loaders might ask for FEWER bytes than the block contains (e.g. reading header).
    // This is fine. If they ask for MORE, we fail.
    if (DE > dataAvailable) {
        trapReturn(spectrum, cpu, false);
        return 11;
    }

    uint8_t parity = tapeFlag;
    for (uint16_t i = 0; i < DE; ++i) {
        uint8_t b = data[1 + i];
        parity ^= b;
        if (isLoad) spectrum->write((uint16_t)(IX + i), b);
    }
    
    // The LD-BYTES routine expects the parity of ALL bytes including the final checksum byte 
    // to be zero. The checksum byte is the last byte of the block.
    const uint8_t checksumByte = data[blockLen - 1];
    
    // If we read exactly the data length, the next byte to 'XOR' for parity check is the checksum byte.
    // If the loader read a partial block, we can't easily verify parity against the block's checksum 
    // without reading the rest of the block's data. For now, if partial, we'll just succeed if flag matched.
    bool ok = true;
    if (DE == dataAvailable) {
        parity ^= checksumByte;
        ok = (parity == 0);
    }

    cpu->ix = (uint16_t)(IX + DE);
    cpu->d = 0;
    cpu->e = 0;
    cpu->a = checksumByte; // LD-BYTES returns last byte read (usually checksum) in A

    trapReturn(spectrum, cpu, ok);
    return 11;
}

void Tape::instaload(SpectrumBase* spectrum) {
    if (!m_enabled || m_num_blocks == 0) return;

    Z80* cpu = spectrum->getCPU();
    uint16_t lastCodeStart = 0;
    uint16_t totalProgLen = 0;
    bool hasCode = false;
    bool hasBasic = false;

    for (int i = 0; i < (m_num_blocks - 1); i++) {  // Note: check up to second-to-last block
        const TapeBlockInternal& b = m_blocks[i];
        
        // Check for standard header blocks (length 19, flag byte 0x00)
        if (b.length == 19 && b.data && b.data[0] == 0x00) {
            uint8_t type = b.data[1];
            uint8_t flag = b.data[2];  // The flag byte for the data block
            uint16_t len = b.data[12] | (b.data[13] << 8);
            uint16_t start = b.data[14] | (b.data[15] << 8);
            
            // The next block should be the data block
            if (i + 1 < m_num_blocks) {
                const TapeBlockInternal& db = m_blocks[i + 1];
                
                // Check if this is a valid data block
                bool isValidDataBlock = false;
                size_t dataOffset = 0;
                size_t dataLength = 0;
                
                if (db.data && db.length >= 2) {
                    if (db.type == 0x10 || db.type == 0x11) {
                        // Standard/Turbo data block: first byte is flag
                        if (db.data[0] == flag || db.data[0] == 0xFF) {  // Accept both exact flag and 0xFF
                            isValidDataBlock = true;
                            dataOffset = 1;  // Skip flag byte
                            dataLength = db.length - 2;  // Subtract flag and checksum
                        }
                    } else if (db.type == 0x14) {
                        // Pure Data block: no flag byte
                        isValidDataBlock = true;
                        dataOffset = 0;
                        dataLength = db.length;
                    }
                }
                
                if (isValidDataBlock) {
                    if (dataLength > len) {
                        dataLength = len;  // Limit to header-specified length
                    }

                    uint16_t loadAddr = start;
                    if (type == 0) {
                        loadAddr = 23755; // BASIC programs load to PROG area
                    }

                    for (uint16_t j = 0; j < dataLength; j++) {
                        spectrum->write((uint16_t)(loadAddr + j), db.data[dataOffset + j]);
                    }

                    if (type == 0) {
                        hasBasic = true;
                        totalProgLen = dataLength;
                    } else if (type == 3) { // CODE
                        // Avoid jumping to the loading screen (usually at 16384)
                        if (start != 16384) {
                            lastCodeStart = start;
                            hasCode = true;
                        }
                    }
                    i++; // Skip the processed data block
                }
            }
        }
    }

    if (hasBasic) {
        // Update BASIC system variables
        uint16_t vars = (uint16_t)(23755 + totalProgLen);
        uint16_t eLine = (uint16_t)(vars + 1);

        spectrum->write(23635, 23755 & 0xFF);
        spectrum->write(23636, 23755 >> 8);
        spectrum->write(23627, vars & 0xFF);
        spectrum->write(23628, vars >> 8);
        spectrum->write(23641, eLine & 0xFF);
        spectrum->write(23642, eLine >> 8);
        spectrum->write(23645, eLine & 0xFF);
        spectrum->write(23646, eLine >> 8);
        spectrum->write(23647, eLine & 0xFF);
        spectrum->write(23648, eLine >> 8);
    }

    if (hasCode) {
        // Land in a clean Spectrum-like state. The webserver pre-runs the
        // ROM init so IM 1 + IFF1 + sysvars are already correct, but the
        // BASIC main loop has its own stack frames that the user code would
        // otherwise pop into on any stray RET. A top-of-RAM stack matches
        // what most LOAD ""CODE then RANDOMIZE USR x loaders set up.
        cpu->sp = 0xFFFF;
        cpu->iff1 = 1;
        cpu->iff2 = 1;
        cpu->im = 1;
        cpu->halted = 0;
        cpu->pc = lastCodeStart;
    }
}
