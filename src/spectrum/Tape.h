#pragma once
#include <stdint.h>
#include <stddef.h>

class SpectrumBase;

/**
 * Virtual cassette player for ZX Spectrum TAP files.
 *
 * Strategy: trap the ROM LD-BYTES routine at 0x0556 and inject the next
 * TAP block directly into RAM. Gives instant-load behaviour for any game
 * that uses the standard ROM loader. Turbo loaders that bypass the ROM
 * (e.g. Speedlock) are not supported by this path.
 *
 * TAP format (little-endian throughout):
 *   [u16 len][u8 flag][u8 data..(len-2)][u8 xor]
 *   flag = 0x00 for headers, 0xFF for data blocks.
 *   xor  = byte-wise XOR of flag + data (inclusive); parity == 0 on success.
 */
class Tape {
public:
    Tape();
    ~Tape();

    // Load a .tap file from SPIFFS (or any path). Re-loads if already loaded.
    bool load(const char* filepath);
    void unload();

    void rewind() { m_pos = 0; }

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    bool isLoaded() const { return m_data != nullptr; }

    size_t position() const { return m_pos; }
    size_t totalSize() const { return m_size; }

    // Service a LD-BYTES trap. Reads the Z80 register state (A, F, DE, IX,
    // SP), injects the next TAP block into memory via spectrum->write, sets
    // IX += DE, DE = 0, CF = (parity ok), and pops PC (RET). Returns the
    // number of T-states to charge for this "instruction".
    int serviceLoadTrap(SpectrumBase* spectrum);

    // Convenience: look for a known autoload filename on SPIFFS.
    // Returns true iff a tape was loaded.
    static bool autoload(Tape& tape);

    // Address of the 48K ROM LD-BYTES routine.
    static constexpr uint16_t LD_BYTES_ENTRY = 0x0556;

private:
    uint8_t* m_data;
    size_t   m_size;
    size_t   m_pos;
    bool     m_enabled;
};
