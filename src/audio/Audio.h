#pragma once
#include <stdint.h>
#include "spectrum/SpectrumBase.h"

// Initialize audio subsystem (I2S)
bool audio_init();
// Render and play one frame of audio from the given Spectrum instance
void audio_play_frame(SpectrumBase* spectrum);
// Play a simple square tone (blocking). Frequency in Hz, duration in ms.
void audio_play_tone(int freq_hz, int duration_ms);
