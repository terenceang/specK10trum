#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "spectrum/SpectrumBase.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_MADCTL_PORTRAIT 0x48
#define DISPLAY_MADCTL_LANDSCAPE 0x28
#define DISPLAY_MADCTL_PORTRAIT_FLIP 0x88
#define DISPLAY_MADCTL_LANDSCAPE_FLIP 0xE8

bool display_init();
uint16_t* display_getBackBuffer();
void display_setOrientation(uint8_t madctl);
void display_renderSpectrum(SpectrumBase* spectrum);
void display_present();
void display_trigger_frame(SpectrumBase* spectrum);
void display_test_pattern();

#ifdef __cplusplus
}
#endif
