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

// Attenuation applied while tape is being advanced. Beeper::m_volume scales
// BOTH the speaker base waveform AND the tape EAR overlay (see speakerAmp()
// and tapeAmp() in Beeper.cpp), so this knob lowers the whole mix during
// loading -- not just the speaker path. If a future change wants to attenuate
// only one of those sources, Beeper would need separate gain controls; for
// now keep this as a single tuning constant to preserve current behavior.
static constexpr float TAPE_LOADING_MIX_GAIN = 0.3f;

void SpectrumBase::flushTape() {
    if (m_pendingTapeTstates <= 0) return;

    uint32_t tstates = (uint32_t)m_pendingTapeTstates;
    m_pendingTapeTstates = 0;

    // Attenuate the combined speaker+tape mix while loading is active.
    float prev_volume = m_beeper.getVolume();
    m_beeper.setVolume(TAPE_LOADING_MIX_GAIN);

    // Advance the tape and record all EAR toggles for the beeper to hear them
    uint32_t start_ula_clocks = m_ulaClocks - tstates;
    m_tape.advance(tstates, [&](uint32_t offset, bool ear) {
        m_beeper.recordTapeEvent(start_ula_clocks + offset, ear ? 1 : 0);
        m_lastTapeEar = ear;
    });

    // Restore previous beeper volume
    m_beeper.setVolume(prev_volume);
}
