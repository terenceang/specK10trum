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
void display_clear();
void display_trigger_frame(SpectrumBase* spectrum);
void display_test_pattern();
// Show a BMP splash screen from SPIFFS
bool display_showSplash(const char* filename);
// Boot-time visual test: blink twice and show a 1s color bar
void display_boot_test();

#ifdef __cplusplus
}
#endif
