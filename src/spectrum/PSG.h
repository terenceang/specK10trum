#pragma once
#include <stdint.h>

class PSG {
public:
    PSG();
    void reset();
    
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg) const;
    
    // Render PSG audio into the buffer.
    // num_samples: number of samples to generate.
    // clock_hz: The clock frequency of the PSG (usually ~1.77 MHz for Spectrum).
    // sample_rate: The output audio sample rate (e.g. 44100).
    void render(int16_t* buffer, int num_samples, double clock_hz, double sample_rate);

private:
    uint8_t m_registers[16];
    
    // Internal state
    uint32_t m_toneCount[3];
    uint8_t  m_toneOut[3];
    
    uint32_t m_noiseCount;
    uint32_t m_noiseShiftReg;
    uint8_t  m_noiseOut;
    
    uint32_t m_envCount;
    int32_t  m_envPos;
    bool     m_envHolding;
    
    double m_phase; // For fractional clock steps per sample
    
    static const uint16_t s_volumes[16];
    
    void updateEnvelope();
};
