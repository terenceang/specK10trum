#include "PSG.h"
#include <string.h>

// Logarithmic volume table for AY-3-8910
// Values from 0 to 65535, but we'll use a subset or scale them.
// This table is based on the YM2149/AY-3-8910 characteristic curve.
const uint16_t PSG::s_volumes[16] = {
    0, 218, 311, 442, 628, 891, 1264, 1793,
    2545, 3612, 5126, 7274, 10323, 14650, 20791, 29505
};

PSG::PSG() {
    reset();
}

void PSG::reset() {
    memset(m_registers, 0, sizeof(m_registers));
    // Set some defaults
    m_registers[7] = 0x3F; // All channels off
    
    memset(m_toneCount, 0, sizeof(m_toneCount));
    memset(m_toneOut, 0, sizeof(m_toneOut));
    m_noiseCount = 0;
    m_noiseShiftReg = 1;
    m_noiseOut = 0;
    m_envCount = 0;
    m_envPos = 15;
    m_envHolding = false;
    m_phase = 0;
    m_renderedSamples = 0;
}

void PSG::writeRegister(uint8_t reg, uint8_t value) {
    if (reg >= 16) return;
    m_registers[reg] = value;
    if (reg == 13) {
        // Reset envelope
        m_envHolding = false;
        m_envCount = 0;
        // The position depends on the "attack" bit of the shape
        m_envPos = (value & 4) ? 0 : 15;
    }
}

uint8_t PSG::readRegister(uint8_t reg) const {
    return (reg < 16) ? m_registers[reg] : 0xFF;
}

void PSG::renderTo(int16_t* buffer, int target_sample, double clock_hz, double sample_rate) {
    if (target_sample <= m_renderedSamples) return;

    // PSG internal clock is clock_hz / 16
    double psg_clock = clock_hz / 16.0;
    double ticks_per_sample = psg_clock / sample_rate;

    for (int s = m_renderedSamples; s < target_sample; ++s) {
        m_phase += ticks_per_sample;
        uint32_t ticks = (uint32_t)m_phase;
        m_phase -= ticks;

        while (ticks--) {
            // Update Tones
            for (int i = 0; i < 3; ++i) {
                uint16_t period = ((m_registers[i * 2 + 1] & 0x0F) << 8) | m_registers[i * 2];
                if (period == 0) period = 1;
                m_toneCount[i]++;
                if (m_toneCount[i] >= period) {
                    m_toneCount[i] = 0;
                    m_toneOut[i] ^= 1;
                }
            }

            // Update Noise
            uint8_t noise_period = m_registers[6] & 0x1F;
            if (noise_period == 0) noise_period = 1;
            m_noiseCount++;
            if (m_noiseCount >= (uint32_t)(noise_period * 2)) {
                m_noiseCount = 0;
                // 17-bit LFSR
                if (((m_noiseShiftReg & 1) ^ ((m_noiseShiftReg >> 3) & 1))) {
                    m_noiseShiftReg |= 0x20000;
                }
                m_noiseShiftReg >>= 1;
                m_noiseOut = (m_noiseShiftReg & 1);
            }

            // Update Envelope
            uint16_t env_period = (m_registers[12] << 8) | m_registers[11];
            if (env_period == 0) env_period = 1;
            m_envCount++;
            // Envelope frequency is clock_hz / 256. 
            // Since our ticks are clock_hz / 16, we need 16 ticks for one envelope step.
            if (m_envCount >= (uint32_t)(env_period * 16)) {
                m_envCount = 0;
                updateEnvelope();
            }
        }

        // Mix channels
        int32_t mixed = 0;
        uint8_t mixer = m_registers[7];
        
        for (int i = 0; i < 3; ++i) {
            // Mixer bits: 0-2 tone off, 3-5 noise off (active low)
            bool tone_off = (mixer & (1 << i)) != 0;
            bool noise_off = (mixer & (1 << (i + 3))) != 0;
            
            bool out = true;
            if (!tone_off) out &= m_toneOut[i];
            if (!noise_off) out &= m_noiseOut;
            
            if (out) {
                uint8_t amp = m_registers[8 + i] & 0x0F;
                bool use_env = (m_registers[8 + i] & 0x10) != 0;
                if (use_env) amp = (uint8_t)m_envPos;
                mixed += s_volumes[amp];
            }
        }
        
        // Output is mono for now, centered around 0.
        // Max sum is ~90000, so divide by 3 to stay in range of int16.
        buffer[s] = (int16_t)(mixed / 3);
    }
    m_renderedSamples = target_sample;
}

void PSG::updateEnvelope() {
    if (m_envHolding) return;

    uint8_t shape = m_registers[13];
    bool attack = (shape & 4) != 0;
    bool alternate = (shape & 2) != 0;
    bool hold = (shape & 1) != 0;
    bool continue_bit = (shape & 8) != 0;

    if (attack) {
        m_envPos++;
        if (m_envPos > 15) {
            if (!continue_bit) {
                m_envPos = 0;
                m_envHolding = true;
            } else {
                if (alternate) {
                    // Alternate: reverse direction
                    // This is complex because we need to store current direction
                    // Simplified: just hold or wrap for now
                    m_envPos = 15;
                    m_envHolding = true; 
                } else if (hold) {
                    m_envPos = 15;
                    m_envHolding = true;
                } else {
                    m_envPos = 0; // Wrap
                }
            }
        }
    } else {
        m_envPos--;
        if (m_envPos < 0) {
            if (!continue_bit) {
                m_envPos = 0;
                m_envHolding = true;
            } else {
                if (alternate) {
                    m_envPos = 0;
                    m_envHolding = true;
                } else if (hold) {
                    m_envPos = 0;
                    m_envHolding = true;
                } else {
                    m_envPos = 15; // Wrap
                }
            }
        }
    }
}
