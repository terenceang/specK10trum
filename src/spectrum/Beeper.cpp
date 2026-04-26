#include "Beeper.h"
#include <string.h>

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;

Beeper::Beeper()
    : m_eventCount(0)
    , m_renderEventCount(0)
    , m_initialLevel(0)
    , m_speakerLevel(0)
    , m_externalEar(false)
    , m_lastX(0)
    , m_lastY(0)
    , m_lp_x1(0), m_lp_x2(0)
    , m_lp_y1(0), m_lp_y2(0)
{
}

void Beeper::reset() {
    m_eventCount = 0;
    m_renderEventCount = 0;
    m_initialLevel = 0;
    m_speakerLevel = 0;
    m_externalEar = false;
    m_lastX = 0;
    m_lastY = 0;
    m_lp_x1 = 0; m_lp_x2 = 0;
    m_lp_y1 = 0; m_lp_y2 = 0;
}

void Beeper::recordEvent(uint32_t tstates, uint8_t level) {
    if (level == m_speakerLevel) return;
    if (m_eventCount < MAX_BEEPER_EVENTS) {
        m_events[m_eventCount++] = { tstates, level };
    }
    m_speakerLevel = level;
}

void Beeper::copyForFrame() {
    if (m_eventCount > 0) {
        size_t copyCount = m_eventCount;
        if (copyCount > MAX_BEEPER_EVENTS) copyCount = MAX_BEEPER_EVENTS;
        memcpy(m_renderEvents, m_events, copyCount * sizeof(BeeperEvent));
        m_renderEventCount = copyCount;
    } else {
        m_renderEventCount = 0;
    }
    m_initialLevel = m_speakerLevel;
    m_eventCount = 0;
}

void Beeper::renderFrame(int16_t* audio_buf, int num_samples) {
    if (!audio_buf || num_samples <= 0) return;
    
    // Use a float buffer for intermediate processing to maintain precision for BLEP
    static float raw_f[2048];
    int render_samples = (num_samples > 2048) ? 2048 : num_samples;

    // 1. Generate the raw square wave by point-sampling
    size_t evt = 0;
    uint8_t level = m_initialLevel;
    for (int i = 0; i < render_samples; ++i) {
        uint32_t sample_tstate = (uint32_t)((uint64_t)i * FRAME_T_STATES / (uint32_t)render_samples);
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates <= sample_tstate) {
            level = m_renderEvents[evt].level;
            evt++;
        }
        raw_f[i] = level ? (float)AMPLITUDE : -(float)AMPLITUDE;
    }

    // 2. Apply PolyBLEP residuals at each transition point
    // This removes the high-frequency aliasing (clicking) caused by edges 
    // falling between samples.
    level = m_initialLevel;
    evt = 0;
    for (; evt < m_renderEventCount; evt++) {
        uint32_t t = m_renderEvents[evt].tstates;
        // Calculate fractional sample position
        float sample_pos = (float)((uint64_t)t * render_samples) / FRAME_T_STATES;
        int i = (int)sample_pos;
        float p = sample_pos - (float)i; // Fractional part [0, 1]

        if (i < render_samples) {
            float old_val = level ? (float)AMPLITUDE : -(float)AMPLITUDE;
            float new_val = m_renderEvents[evt].level ? (float)AMPLITUDE : -(float)AMPLITUDE;
            float h = new_val - old_val;

            // 2-tap PolyBLEP residual
            // Current sample correction: h * (p - p^2/2 - 0.5)
            // Next sample correction:    h * (p^2/2)
            raw_f[i] += h * (p - (p * p * 0.5f) - 0.5f);
            if (i + 1 < render_samples) {
                raw_f[i+1] += h * (p * p * 0.5f);
            }
        }
        level = m_renderEvents[evt].level;
    }

    // 3. Final processing: Low-pass filter and DC blocker
    // We use a 2nd-order Butterworth IIR filter for "analog warmth" (cutoff ~8kHz).
    // Coefficients for fs=44.1kHz, fc=8kHz:
    const float b0 = 0.1804f, b1 = 0.3608f, b2 = 0.1804f;
    const float a1 = -0.4932f, a2 = 0.2149f;

    for (int i = 0; i < render_samples; ++i) {
        float x = raw_f[i];
        
        // Butterworth LPF
        float lp_out = b0 * x + b1 * m_lp_x1 + b2 * m_lp_x2 - a1 * m_lp_y1 - a2 * m_lp_y2;
        m_lp_x2 = m_lp_x1; m_lp_x1 = x;
        m_lp_y2 = m_lp_y1; m_lp_y1 = lp_out;

        // DC blocker (High-pass filter at ~20Hz)
        float y = lp_out - m_lastX + 0.995f * m_lastY;
        m_lastX = lp_out;
        m_lastY = y;

        // Clamp and convert to 16-bit PCM
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        audio_buf[i] = (int16_t)y;
    }
}
