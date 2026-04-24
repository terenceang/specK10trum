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
    , m_externalEar(true)
    , m_lpFilterState(0.0f)
    , m_dcFilterX(0.0f)
    , m_dcFilterY(0.0f)
{
}

void Beeper::reset() {
    m_eventCount = 0;
    m_renderEventCount = 0;
    m_initialLevel = 0;
    m_speakerLevel = 0;
    m_externalEar = true;
    m_lpFilterState = 0.0f;
    m_dcFilterX = 0.0f;
    m_dcFilterY = 0.0f;
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

    // Fast path: no events this frame -> fill with constant level.
    const int16_t high = 12000;
    const int16_t low  = -12000;
    if (m_renderEventCount == 0) {
        int16_t v = m_initialLevel ? high : low;
        for (int i = 0; i < num_samples; ++i) audio_buf[i] = v;
        return;
    }

    // Linear Q16 step instead of a 64-bit multiply/divide per sample.
    const uint32_t step_q16 = (uint32_t)(((uint64_t)FRAME_T_STATES << 16) / (uint32_t)num_samples);
    uint32_t t_q16 = 0;
    size_t evt = 0;
    uint8_t level = m_initialLevel;

    for (int i = 0; i < num_samples; ++i) {
        uint32_t t_start = (uint32_t)((uint64_t)i * FRAME_T_STATES / (uint32_t)num_samples);
        uint32_t t_end = (uint32_t)((uint64_t)(i + 1) * FRAME_T_STATES / (uint32_t)num_samples);
        uint32_t t_delta = t_end - t_start;

        if (t_delta == 0) t_delta = 1; // Prevent division by zero

        uint32_t high_tstates = 0;
        uint32_t current_t = t_start;

        // Process all events that occur within this sample window
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates < t_end) {
            // How long was the beeper in the current state since our last observation point?
            uint32_t dt = m_renderEvents[evt].tstates - current_t;
            if (level) {
                high_tstates += dt;
            }
            current_t = m_renderEvents[evt].tstates;
            level = m_renderEvents[evt].level;
            evt++;
        }

        // Add the remainder of the interval in the final state
        if (level) {
            high_tstates += (t_end - current_t);
        }

        // The average level over this sample period (0.0 to 1.0)
        float avg_level = (float)high_tstates / (float)t_delta;

        // Map [0.0, 1.0] to [-12000, 12000]
        float target = (avg_level * 24000.0f) - 12000.0f;

        // Apply a first-order IIR low-pass filter to soften the square wave
        // alpha = 0.5 gives a good roll-off without muffling too much
        float alpha = 0.5f;
        m_lpFilterState = alpha * target + (1.0f - alpha) * m_lpFilterState;

        // Apply DC blocking filter
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        float r = 0.995f;
        m_dcFilterY = m_lpFilterState - m_dcFilterX + r * m_dcFilterY;
        m_dcFilterX = m_lpFilterState;

        // Clamp just in case
        float final_val = m_dcFilterY;
        if (final_val > 32767.0f) final_val = 32767.0f;
        if (final_val < -32768.0f) final_val = -32768.0f;

        audio_buf[i] = (int16_t)final_val;
        uint32_t sample_tstate = t_q16 >> 16;
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates <= sample_tstate) {
            level = m_renderEvents[evt].level;
            evt++;
        }
        audio_buf[i] = level ? high : low;
        t_q16 += step_q16;
    }
}
