#include "Beeper.h"
#include <string.h>

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;

Beeper::Beeper()
    : m_renderedSamples(0)
    , m_nextSampleCorrection(0.0f)
    , m_speakerLevel(0)
    , m_externalEar(false)
    , m_lastX(0)
    , m_lastY(0)
    , m_lp_x1(0), m_lp_x2(0)
    , m_lp_y1(0), m_lp_y2(0)
{
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_renderBuffer, 0, sizeof(m_renderBuffer));
}

void Beeper::reset() {
    m_renderedSamples = 0;
    m_nextSampleCorrection = 0.0f;
    m_speakerLevel = 0;
    m_externalEar = false;
    m_lastX = 0;
    m_lastY = 0;
    m_lp_x1 = 0; m_lp_x2 = 0;
    m_lp_y1 = 0; m_lp_y2 = 0;
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_renderBuffer, 0, sizeof(m_renderBuffer));
}

void Beeper::recordEvent(uint32_t tstates, uint8_t level) {
    if (level == m_speakerLevel) return;
    
    // Render up to this event
    renderTo(tstates);
    
    // Apply PolyBLEP transition at current T-state
    int i = (int)(((uint64_t)tstates * SAMPLES_PER_FRAME) / FRAME_T_STATES);
    if (i < SAMPLES_PER_FRAME) {
        float old_v = m_speakerLevel ? (float)AMPLITUDE : -(float)AMPLITUDE;
        float new_v = level ? (float)AMPLITUDE : -(float)AMPLITUDE;
        float h = new_v - old_v;

        // Fractional position p in [0, 1] relative to sample start
        uint32_t start_t = (uint32_t)(((uint64_t)i * FRAME_T_STATES) / SAMPLES_PER_FRAME);
        float p = (float)(tstates - start_t) * (float)SAMPLES_PER_FRAME / (float)FRAME_T_STATES;
        if (p > 1.0f) p = 1.0f;

        // Apply corrections to current and next sample
        // If we are currently at i, we add directly to current_sample_correction logic
        // But since we render sample by sample, we'll store it.
        
        // PolyBLEP residual
        m_frameBuffer[i] += (int16_t)(h * (p - (p * p * 0.5f) - 0.5f));
        if (i + 1 < SAMPLES_PER_FRAME) {
            m_nextSampleCorrection += h * (p * p * 0.5f);
        }
    }

    m_speakerLevel = level;
}

void Beeper::renderTo(uint32_t tstates) {
    int targetSample = (int)(((uint64_t)tstates * SAMPLES_PER_FRAME) / FRAME_T_STATES);
    if (targetSample > SAMPLES_PER_FRAME) targetSample = SAMPLES_PER_FRAME;
    
    if (targetSample > m_renderedSamples) {
        renderSamples(m_renderedSamples, targetSample);
        m_renderedSamples = targetSample;
    }
}

void Beeper::renderSamples(int start, int end) {
    // Butterworth LPF coefficients (fs=44.1kHz, fc=8kHz)
    const float b0 = 0.1804f, b1 = 0.3608f, b2 = 0.1804f;
    const float a1 = -0.4932f, a2 = 0.2149f;

    for (int i = start; i < end; ++i) {
        float x = m_speakerLevel ? (float)AMPLITUDE : -(float)AMPLITUDE;
        
        // Add residual from previous event
        x += m_nextSampleCorrection;
        m_nextSampleCorrection = 0.0f;

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
        
        // Note: we add to the buffer because recordEvent might have already added a partial correction
        m_frameBuffer[i] += (int16_t)y;
    }
}

void Beeper::copyForFrame() {
    // Finish rendering the frame
    renderTo(FRAME_T_STATES);
    
    // Swap/Copy to render buffer
    memcpy(m_renderBuffer, m_frameBuffer, sizeof(m_renderBuffer));
    
    // Clear frame buffer for next frame
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    m_renderedSamples = 0;
}

void Beeper::getFrameBuffer(int16_t* audio_buf, int num_samples) {
    if (!audio_buf) return;
    int toCopy = (num_samples < SAMPLES_PER_FRAME) ? num_samples : SAMPLES_PER_FRAME;
    memcpy(audio_buf, m_renderBuffer, toCopy * sizeof(int16_t));
    if (num_samples > SAMPLES_PER_FRAME) {
        memset(audio_buf + SAMPLES_PER_FRAME, 0, (num_samples - SAMPLES_PER_FRAME) * sizeof(int16_t));
    }
}
