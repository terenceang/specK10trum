#include "Beeper.h"
#include <string.h>

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;

Beeper::Beeper()
    : m_renderedSamples(0)
    , m_speakerNextCorrection(0.0f)
    , m_tapeNextCorrection(0.0f)
    , m_speakerLevel(0)
    , m_tapeLevel(0)
    , m_externalEar(false)
{
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_renderBuffer, 0, sizeof(m_renderBuffer));
}

void Beeper::reset() {
    m_renderedSamples = 0;
    m_speakerNextCorrection = 0.0f;
    m_tapeNextCorrection = 0.0f;
    m_speakerLevel = 0;
    m_tapeLevel = 0;
    m_externalEar = false;
    m_tapeMonitorEnabled = true;
    m_speakerPathEnabled = true;
    memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    memset(m_renderBuffer, 0, sizeof(m_renderBuffer));
}

void Beeper::recordSpeakerEvent(uint32_t tstates, uint8_t level) {
    if (level == m_speakerLevel) return;

    // Render up to this event using previous levels
    renderTo(tstates);

    // Apply PolyBLEP transition for speaker bit.
    // Speaker is modeled as a bipolar waveform (-AMPLITUDE to +AMPLITUDE).
    // Amplitude comes from speakerAmp() so this matches renderSamples().
    int i = (int)(((uint64_t)tstates * SAMPLES_PER_FRAME) / FRAME_T_STATES);
    if (i < SAMPLES_PER_FRAME) {
        const float vol_amp = speakerAmp();
        float old_v = m_speakerLevel ? vol_amp : -vol_amp;
        float new_v = level ? vol_amp : -vol_amp;
        float h = new_v - old_v;

        uint32_t start_t = (uint32_t)(((uint64_t)i * FRAME_T_STATES) / SAMPLES_PER_FRAME);
        float p = (float)(tstates - start_t) * (float)SAMPLES_PER_FRAME / (float)FRAME_T_STATES;
        if (p > 1.0f) p = 1.0f;

        float p_sq = p * p;
        float correction_i = h * (p - p_sq * 0.5f - 0.5f);

        int16_t current = m_frameBuffer[i];
        int32_t corrected = (int32_t)current + (int32_t)correction_i;
        if (corrected > 32767) corrected = 32767;
        else if (corrected < -32768) corrected = -32768;
        m_frameBuffer[i] = (int16_t)corrected;

        if (i + 1 < SAMPLES_PER_FRAME) {
            m_speakerNextCorrection += h * p_sq * 0.5f;
        }
    }

    m_speakerLevel = level;
}

void Beeper::recordTapeEvent(uint32_t tstates, uint8_t level) {
    if (level == m_tapeLevel) return;

    // Render up to this event using previous levels
    renderTo(tstates);

    // Apply PolyBLEP transition for tape EAR bit.
    // Tape is modeled as a smaller unipolar source (0 to tape_amp).
    // Amplitude comes from tapeAmp() so this matches renderSamples().
    int i = (int)(((uint64_t)tstates * SAMPLES_PER_FRAME) / FRAME_T_STATES);
    if (i < SAMPLES_PER_FRAME) {
        const float tape_amp = tapeAmp();
        float old_v = m_tapeLevel ? tape_amp : 0.0f;
        float new_v = level ? tape_amp : 0.0f;
        float h = new_v - old_v;

        uint32_t start_t = (uint32_t)(((uint64_t)i * FRAME_T_STATES) / SAMPLES_PER_FRAME);
        float p = (float)(tstates - start_t) * (float)SAMPLES_PER_FRAME / (float)FRAME_T_STATES;
        if (p > 1.0f) p = 1.0f;

        float p_sq = p * p;
        float correction_i = h * (p - p_sq * 0.5f - 0.5f);

        int16_t current = m_frameBuffer[i];
        int32_t corrected = (int32_t)current + (int32_t)correction_i;
        if (corrected > 32767) corrected = 32767;
        else if (corrected < -32768) corrected = -32768;
        m_frameBuffer[i] = (int16_t)corrected;

        if (i + 1 < SAMPLES_PER_FRAME) {
            m_tapeNextCorrection += h * p_sq * 0.5f;
        }
    }

    m_tapeLevel = level;
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
    const float speaker_vol = m_speakerPathEnabled ? speakerAmp() : 0.0f;
    const float tape_vol    = m_tapeMonitorEnabled ? tapeAmp() : 0.0f;

    for (int i = start; i < end; ++i) {
        // Speaker is bipolar (-AMP to +AMP)
        float s_base = m_speakerLevel ? speaker_vol : -speaker_vol;
        // Tape is unipolar (0 to tape_vol)
        float t_base = m_tapeLevel ? tape_vol : 0.0f;
        
        float y = s_base + t_base;

        // Apply independent correction tails
        y += m_speakerNextCorrection;
        y += m_tapeNextCorrection;
        m_speakerNextCorrection = 0.0f;
        m_tapeNextCorrection = 0.0f;

        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;

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
