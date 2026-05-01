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

    // Player mode: keep mixed speaker+tape behavior.
    // Normal mode + monitor ON: make monitor exclusive (EAR-only) only while
    // the ROM loader routine is active, not for the whole tape playback.
    bool audible_tape_monitor = false;
    bool speaker_path_enabled = true;
    if (m_tape.getMode() == TapeMode::PLAYER) {
        audible_tape_monitor = true;
        speaker_path_enabled = true;
    } else if (m_tape.getMode() == TapeMode::NORMAL && m_tapeMonitorEnabled) {
        audible_tape_monitor = true;
        bool in_rom_loader = isTapeRomActive() && (m_cpu.pc >= 0x0556 && m_cpu.pc < 0x0800);
        speaker_path_enabled = !in_rom_loader;
    }

    m_beeper.setTapeMonitorEnabled(audible_tape_monitor);
    m_beeper.setSpeakerPathEnabled(speaker_path_enabled);

    // Advance the tape. Always update m_lastTapeEar for port FE sampling.
    uint32_t start_ula_clocks = m_ulaClocks - tstates;
    m_tape.advance(tstates, [&](uint32_t offset, bool ear) {
        if (audible_tape_monitor) {
            m_beeper.recordTapeEvent(start_ula_clocks + offset, ear ? 1 : 0);
        }
        m_lastTapeEar = ear;
    });
}
