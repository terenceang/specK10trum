#include "test_audio.h"
#include "../src/spectrum/Beeper.h"
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>

// Lightweight, deterministic regression coverage for the Beeper DSP path.
// The Beeper has no hardware dependencies, so we drive it with synthetic
// events and inspect the rendered frame buffer directly.

static const char* TAG = "AudioTest";

// Mirror of constants in Beeper.cpp -- kept local to avoid widening the
// header surface for tests only.
static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;

// Tolerance for steady-state values away from any transition window.
// PolyBLEP corrections are localized to one or two samples around the edge.
static constexpr int STEADY_TOLERANCE = 64;

static bool nearlyEqual(int actual, int expected, int tol, const char* label) {
    int diff = actual - expected;
    if (diff < 0) diff = -diff;
    if (diff > tol) {
        ESP_LOGE(TAG, "  %s: expected ~%d, got %d (diff=%d, tol=%d)",
                 label, expected, actual, diff, tol);
        return false;
    }
    return true;
}

static int sampleIndexForTstate(uint32_t tstates) {
    return (int)(((uint64_t)tstates * Beeper::SAMPLES_PER_FRAME) / FRAME_T_STATES);
}

bool AudioTest::testSilenceFrame() {
    Beeper b;
    b.reset();
    b.copyForFrame();

    int16_t buf[Beeper::SAMPLES_PER_FRAME];
    b.getFrameBuffer(buf, Beeper::SAMPLES_PER_FRAME);

    // Speaker is bipolar; with level=0 the steady-state baseline is -AMPLITUDE.
    // Tape is unipolar at 0. So a silent frame should sit at -AMPLITUDE.
    bool ok = true;
    ok &= nearlyEqual(buf[0], -Beeper::AMPLITUDE, STEADY_TOLERANCE, "silence[0]");
    ok &= nearlyEqual(buf[Beeper::SAMPLES_PER_FRAME / 2],
                      -Beeper::AMPLITUDE, STEADY_TOLERANCE, "silence[mid]");
    ok &= nearlyEqual(buf[Beeper::SAMPLES_PER_FRAME - 1],
                      -Beeper::AMPLITUDE, STEADY_TOLERANCE, "silence[last]");
    ESP_LOGI(TAG, "  %s SilenceFrame", ok ? "PASS" : "FAIL");
    return ok;
}

bool AudioTest::testSpeakerOnlyTransition() {
    Beeper b;
    b.reset();

    const uint32_t mid_t = FRAME_T_STATES / 2;
    b.recordSpeakerEvent(mid_t, 1);  // 0 -> 1 transition mid-frame
    b.copyForFrame();

    int16_t buf[Beeper::SAMPLES_PER_FRAME];
    b.getFrameBuffer(buf, Beeper::SAMPLES_PER_FRAME);

    int mid = sampleIndexForTstate(mid_t);
    bool ok = true;
    // Well before the edge: baseline at -AMPLITUDE.
    ok &= nearlyEqual(buf[0], -Beeper::AMPLITUDE, STEADY_TOLERANCE, "spk_pre");
    // Well after the edge: baseline at +AMPLITUDE.
    ok &= nearlyEqual(buf[mid + 4], Beeper::AMPLITUDE, STEADY_TOLERANCE, "spk_post");
    ok &= nearlyEqual(buf[Beeper::SAMPLES_PER_FRAME - 1],
                      Beeper::AMPLITUDE, STEADY_TOLERANCE, "spk_last");
    ESP_LOGI(TAG, "  %s SpeakerOnlyTransition", ok ? "PASS" : "FAIL");
    return ok;
}

bool AudioTest::testTapeOnlyTransition() {
    Beeper b;
    b.reset();

    const uint32_t mid_t = FRAME_T_STATES / 2;
    b.recordTapeEvent(mid_t, 1);  // tape EAR 0 -> 1
    b.copyForFrame();

    int16_t buf[Beeper::SAMPLES_PER_FRAME];
    b.getFrameBuffer(buf, Beeper::SAMPLES_PER_FRAME);

    int mid = sampleIndexForTstate(mid_t);
    const int tape_amp = (int)((float)Beeper::AMPLITUDE * Beeper::TAPE_GAIN);

    bool ok = true;
    // Speaker stays at level=0, baseline = -AMPLITUDE; tape adds 0 then +tape_amp.
    ok &= nearlyEqual(buf[0], -Beeper::AMPLITUDE, STEADY_TOLERANCE, "tape_pre");
    ok &= nearlyEqual(buf[mid + 4],
                      -Beeper::AMPLITUDE + tape_amp, STEADY_TOLERANCE, "tape_post");
    ESP_LOGI(TAG, "  %s TapeOnlyTransition", ok ? "PASS" : "FAIL");
    return ok;
}

bool AudioTest::testSimultaneousTransition() {
    Beeper b;
    b.reset();

    const uint32_t mid_t = FRAME_T_STATES / 2;
    b.recordSpeakerEvent(mid_t, 1);
    b.recordTapeEvent(mid_t, 1);
    b.copyForFrame();

    int16_t buf[Beeper::SAMPLES_PER_FRAME];
    b.getFrameBuffer(buf, Beeper::SAMPLES_PER_FRAME);

    int mid = sampleIndexForTstate(mid_t);
    const int tape_amp = (int)((float)Beeper::AMPLITUDE * Beeper::TAPE_GAIN);

    bool ok = true;
    ok &= nearlyEqual(buf[0], -Beeper::AMPLITUDE, STEADY_TOLERANCE, "sim_pre");
    ok &= nearlyEqual(buf[mid + 4],
                      Beeper::AMPLITUDE + tape_amp, STEADY_TOLERANCE, "sim_post");
    ESP_LOGI(TAG, "  %s SimultaneousTransition", ok ? "PASS" : "FAIL");
    return ok;
}

bool AudioTest::runAllTests() {
    printf("\n╔═════════════════════════════════════════════════╗\n");
    printf("║ AUDIO/BEEPER TESTS                              ║\n");
    printf("╚═════════════════════════════════════════════════╝\n");
    fflush(stdout);

    bool ok = true;
    ok &= testSilenceFrame();
    ok &= testSpeakerOnlyTransition();
    ok &= testTapeOnlyTransition();
    ok &= testSimultaneousTransition();

    if (ok) ESP_LOGI(TAG, "All Beeper tests PASSED");
    else    ESP_LOGE(TAG, "Some Beeper tests FAILED");
    return ok;
}
