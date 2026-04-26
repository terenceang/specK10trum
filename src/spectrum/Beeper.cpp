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
    
    // Use a static buffer to avoid malloc in the hot path. 
    // SAMPLES_PER_FRAME is 882, so 2048 is more than enough.
    static int16_t raw[2048];
    int render_samples = (num_samples > 2048) ? 2048 : num_samples;

    size_t evt = 0;
    uint8_t level = m_initialLevel;
    for (int i = 0; i < render_samples; ++i) {
        uint32_t sample_tstate = (uint32_t)((uint64_t)i * FRAME_T_STATES / (uint32_t)render_samples);
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates <= sample_tstate) {
            level = m_renderEvents[evt].level;
            evt++;
        }
        raw[i] = level ? AMPLITUDE : -AMPLITUDE;
    }

    // Apply a small symmetric FIR low-pass to reduce aliasing while preserving character.
    // Using a simple 9-tap symmetric kernel: [1,2,3,4,5,4,3,2,1] / 25
    const int taps[9] = {1,2,3,4,5,4,3,2,1};
    const int taps_sum = 25;

    for (int i = 0; i < render_samples; ++i) {
        int64_t acc = 0;
        for (int t = 0; t < 9; ++t) {
            int idx = i + t - 4; // center the kernel
            int16_t v = 0;
            if (idx < 0) v = raw[0];
            else if (idx >= render_samples) v = raw[render_samples - 1];
            else v = raw[idx];
            acc += (int64_t)v * taps[t];
        }
        
        // Final sample with DC blocker
        float x = (float)acc / taps_sum;
        float y = x - m_lastX + 0.999f * m_lastY;
        m_lastX = x;
        m_lastY = y;

        if (y > 32767.0f) y = 32767.0f;
        if (y < -32768.0f) y = -32768.0f;
        audio_buf[i] = (int16_t)y;
    }
}
