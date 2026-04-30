#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>

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
    void setMode(TapeMode mode);
    TapeMode getMode() const { return m_mode; }
    bool isEnabled() const { return m_enabled; }
    bool isLoaded() const { return m_data != nullptr; }
    bool isPlaying() const { return m_playing && !m_paused; }
    bool isPaused() const { return m_paused; }

    size_t totalSize() const { return m_size; }
    int currentBlockIndex() const { return m_current_block_idx; }
    int totalBlocks() const { return m_num_blocks; }

    // Generates/Advances the EAR signal
    // Called per T-state or batch of T-states.
    // Callback is invoked for each EAR transition (m_ear toggle).
    // offset_tstates is the relative T-state within the advanced batch where toggle occurred.
    void advance(uint32_t tstates, std::function<void(uint32_t offset_tstates, bool ear)> onToggle = nullptr);
    bool getEar() const { return m_ear; }

    // Service a LD-BYTES trap (only used in INSTANT mode or when appropriate)
    int serviceLoadTrap(SpectrumBase* spectrum);

    // Direct memory injection for standard tapes (Header + Data)
    void instaload(SpectrumBase* spectrum);

    // Detect if tape appears to be 128K-only program
    bool looks128K() const;

    // Check if tape is compatible with the current machine model
    bool canPlay(SpectrumBase* spectrum) const;

    // Address of the 48K ROM LD-BYTES routine.
    static constexpr uint16_t LD_BYTES_ENTRY = 0x0556;

    // Standard Spectrum Tape timings (T-states)
    static constexpr uint16_t T_PILOT    = 2168;
    static constexpr uint16_t T_SYNC1    = 667;
    static constexpr uint16_t T_SYNC2    = 735;
    static constexpr uint16_t T_ZERO     = 855;   // Single pulse for bit 0
    static constexpr uint16_t T_ONE      = 1710;  // Single pulse for bit 1
    static constexpr uint16_t C_PILOT_HDR = 8063;
    static constexpr uint16_t C_PILOT_DAT = 3223;

private:
    void buildBlockList();
    void resetPlaybackState();
    void nextState();

    // Pulse generator helpers
    void startDataState();
    bool advanceBit();

    // Data extraction helpers
    bool isDataBlock(int idx) const;
    bool getBlockContent(int idx, const uint8_t** data, uint32_t* length, uint8_t* flag = nullptr) const;

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
        const uint8_t* data;
        uint32_t length;        // Data length in bytes
        uint32_t pause_tstates; // Pause after block in T-states
        uint16_t pilot_len;
        uint16_t pilot_count;
        uint16_t sync1_len;
        uint16_t sync2_len;
        uint16_t zero_len;
        uint16_t one_len;
        uint8_t  type;          // 0x10: Standard, 0x11: Turbo, 0x14: Pure Data, 0x20: Pause, etc.
        uint8_t  used_bits;     // Bits used in the last byte
    };

    static constexpr int MAX_TAPE_BLOCKS = 1024;
    TapeBlockInternal m_blocks[MAX_TAPE_BLOCKS];
    int m_num_blocks;
    int m_current_block_idx;

    // Helper for adding blocks
    void addBlock(uint8_t type, const uint8_t* data, uint32_t length, uint32_t pause_ms,
                  uint16_t pilot_len = 0, uint16_t pilot_count = 0, 
                  uint16_t sync1_len = 0, uint16_t sync2_len = 0,
                  uint16_t zero_len = 0, uint16_t one_len = 0, uint8_t used_bits = 8);
    
    int seekToNextDataBlock();

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
