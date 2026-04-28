#include "SpectrumBase.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "SpectrumBase";

void SpectrumBase::logTapeTrap(const char* msg) {
    static int64_t s_last_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - s_last_us < 250000) return;
    s_last_us = now;
    ESP_LOGI(TAG, "%s (mode=%d, playing=%d)",
             msg, (int)m_tape.getMode(), (int)m_tape.isPlaying());
}

void SpectrumBase::flushTape() {
    if (m_pendingTapeTstates <= 0) return;

    uint32_t tstates = (uint32_t)m_pendingTapeTstates;
    m_pendingTapeTstates = 0;

    // Advance the tape and record all EAR toggles for the beeper to hear them
    uint32_t start_ula_clocks = m_ulaClocks - tstates;
    m_tape.advance(tstates, [&](uint32_t offset, bool ear) {
        m_beeper.recordEvent(start_ula_clocks + offset, m_lastSpeakerBit | (ear ? 1 : 0));
        m_lastTapeEar = ear;
    });
}
