#pragma once
#include <stdint.h>
#include <stddef.h>

class SpectrumBase;

enum class TapeMode {
    INSTANT = 0,
    NORMAL = 1,
    PLAYER = 2
};

class Tape {
public:
    Tape();
    ~Tape();

    // Load a .tap or .tzx/.tsx file from SPIFFS
    bool load(const char* filepath);
    void unload();

    // Player transport controls
    void play();
    void stop();
    void rewind();
    void fastForward();
    void pause();
    void eject();

    // Modes and state
    void setMode(TapeMode mode) { m_mode = mode; }
    TapeMode getMode() const { return m_mode; }
    bool isEnabled() const { return m_enabled; }
    bool isLoaded() const { return m_data != nullptr; }
    bool isPlaying() const { return m_playing && !m_paused; }
    bool isPaused() const { return m_paused; }

    size_t totalSize() const { return m_size; }
    int currentBlockIndex() const { return m_current_block_idx; }
    int totalBlocks() const { return m_num_blocks; }

    // Generates/Advances the EAR signal
    // Called per T-state or batch of T-states
    void advance(uint32_t tstates);
    bool getEar() const { return m_ear; }

    // Service a LD-BYTES trap (only used in INSTANT mode or when appropriate)
    int serviceLoadTrap(SpectrumBase* spectrum);

    // Direct memory injection for standard tapes (Header + Data)
    void instantLoad(SpectrumBase* spectrum);

    // Address of the 48K ROM LD-BYTES routine.
    static constexpr uint16_t LD_BYTES_ENTRY = 0x0556;

private:
    void buildBlockList();
    void resetPlaybackState();
    void nextState();

    uint8_t* m_data;
    size_t   m_size;
    bool     m_enabled;

    TapeMode m_mode;
    bool m_playing;
    bool m_paused;

    // Output state
    bool m_ear;
    uint32_t m_tstate_counter;

    // Block parsing
    struct TapeBlockInternal {
        uint8_t type;          // 0x10: Standard, 0x11: Turbo, 0x14: Pure Data, 0x20: Pause, etc.
        const uint8_t* data;
        size_t length;         // Data length in bytes
        uint32_t pause_tstates; // Pause after block in T-states
        
        // Timing params (T-states)
        uint32_t pilot_len;
        uint32_t pilot_count;
        uint32_t sync1_len;
        uint32_t sync2_len;
        uint32_t zero_len;
        uint32_t one_len;
        uint8_t  used_bits;    // Bits used in the last byte
    };

    static constexpr int MAX_TAPE_BLOCKS = 256;
    TapeBlockInternal m_blocks[MAX_TAPE_BLOCKS];
    int m_num_blocks;
    int m_current_block_idx;

    // Pulse generator state machine
    enum class PlayState {
        IDLE,
        PILOT,
        SYNC1,
        SYNC2,
        DATA,
        PAUSE
    };
    PlayState m_pstate;
    uint32_t m_state_pulses_left;
    uint32_t m_current_pulse_len;
    size_t   m_data_byte_idx;
    uint8_t  m_data_bit_idx;
    bool     m_is_tzx;
};
