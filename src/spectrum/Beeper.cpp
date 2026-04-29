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
    // DC blocker state (only lastX, lastY used now)
    m_lastX = 0;
    m_lastY = 0;
    // Legacy LP filter state (kept for compatibility, not used)
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

        // PolyBLEP first half-step (at current sample)
        // Correction: h * (p - p^2/2 - 0.5)
        float p_sq = p * p;
        float correction_i = h * (p - p_sq * 0.5f - 0.5f);

        // Apply to current sample (add, since sample may already be partially rendered)
        int16_t current = m_frameBuffer[i];
        int32_t corrected = (int32_t)current + (int32_t)correction_i;
        if (corrected > 32767) corrected = 32767;
        else if (corrected < -32768) corrected = -32768;
        m_frameBuffer[i] = (int16_t)corrected;

        // Store correction for next sample (h * p^2/2)
        if (i + 1 < SAMPLES_PER_FRAME) {
            m_nextSampleCorrection = h * p_sq * 0.5f;
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
    // Simplified approach: reduce filtering overhead
    // DC blocker with faster response (1-pole HPF ~100Hz)
    const float HPF_COEFF = 0.98f;  // Faster DC removal than 0.995f
    int16_t speaker_value = m_speakerLevel ? AMPLITUDE : -AMPLITUDE;

    for (int i = start; i < end; ++i) {
        // Input: square wave + residual correction
        float x = (float)speaker_value + m_nextSampleCorrection;
        m_nextSampleCorrection = 0.0f;

        // Fast 1-pole high-pass for DC blocking
        // y[n] = HPF_COEFF * y[n-1] + (1 - HPF_COEFF) * (x[n] - x[n-1])
        float dc_free = HPF_COEFF * m_lastY + 0.02f * (x - m_lastX);
        m_lastX = x;
        m_lastY = dc_free;

        // Clamp to prevent overflow
        if (dc_free > 32767.0f) dc_free = 32767.0f;
        else if (dc_free < -32768.0f) dc_free = -32768.0f;

        m_frameBuffer[i] = (int16_t)dc_free;
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
