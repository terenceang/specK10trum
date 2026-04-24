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
{
}

void Beeper::reset() {
    m_eventCount = 0;
    m_renderEventCount = 0;
    m_initialLevel = 0;
    m_speakerLevel = 0;
    m_externalEar = true;
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
        uint32_t sample_tstate = t_q16 >> 16;
        while (evt < m_renderEventCount && m_renderEvents[evt].tstates <= sample_tstate) {
            level = m_renderEvents[evt].level;
            evt++;
        }
        audio_buf[i] = level ? high : low;
        t_q16 += step_q16;
    }
}
