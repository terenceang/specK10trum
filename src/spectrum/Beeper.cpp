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
    
    int render_samples = (num_samples > 2048) ? 2048 : num_samples;
    size_t evt = 0;
    uint8_t level = m_initialLevel;
    float next_sample_correction = 0.0f;

    // Butterworth LPF coefficients (fs=44.1kHz, fc=8kHz)
    const float b0 = 0.1804f, b1 = 0.3608f, b2 = 0.1804f;
    const float a1 = -0.4932f, a2 = 0.2149f;

    for (int i = 0; i < render_samples; ++i) {
        uint32_t end_t = (uint32_t)(((uint64_t)(i + 1) * FRAME_T_STATES) / render_samples);
        
        float x = level ? (float)AMPLITUDE : -(float)AMPLITUDE;
        float current_sample_correction = next_sample_correction;
        next_sample_correction = 0.0f;

        // Process all events falling within this sample
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates < end_t) {
            uint32_t t = m_renderEvents[evt].tstates;
            float old_v = level ? (float)AMPLITUDE : -(float)AMPLITUDE;
            float new_v = m_renderEvents[evt].level ? (float)AMPLITUDE : -(float)AMPLITUDE;
            float h = new_v - old_v;

            // Fractional position p in [0, 1] relative to sample start
            uint32_t start_t = (uint32_t)(((uint64_t)i * FRAME_T_STATES) / render_samples);
            float p = (float)(t - start_t) * (float)render_samples / (float)FRAME_T_STATES;
            if (p > 1.0f) p = 1.0f;

            // 2-tap PolyBLEP residual
            current_sample_correction += h * (p - (p * p * 0.5f) - 0.5f);
            next_sample_correction += h * (p * p * 0.5f);

            level = m_renderEvents[evt].level;
            evt++;
        }
        
        x += current_sample_correction;

        // --- Chain: Butterworth LPF -> DC Blocker ---
        
        // Butterworth IIR
        float lp_out = b0 * x + b1 * m_lp_x1 + b2 * m_lp_x2 - a1 * m_lp_y1 - a2 * m_lp_y2;
        m_lp_x2 = m_lp_x1; m_lp_x1 = x;
        m_lp_y2 = m_lp_y1; m_lp_y1 = lp_out;

        // DC Blocker (High-pass ~20Hz)
        float y = lp_out - m_lastX + 0.995f * m_lastY;
        m_lastX = lp_out;
        m_lastY = y;

        // Clamp and output
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        audio_buf[i] = (int16_t)y;
    }
}
