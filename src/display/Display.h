#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "spectrum/SpectrumBase.h"

#ifdef __cplusplus
extern "C" {
#endif

bool display_init();
uint16_t* display_getBackBuffer();
void display_renderSpectrum(SpectrumBase* spectrum);
void display_present();
void display_clear();
void display_trigger_frame(SpectrumBase* spectrum);
void showSplashScreen();
void display_setOverlayText(const char* text, uint16_t color);
void display_boot_log_add(const char* message);
void display_boot_log_add_step(int step, int total, const char* description);
void display_boot_log_hide();
void display_boot_update();  // Force immediate frame render during boot
void display_pause_for_reset();
void display_resume_after_reset();
static inline void display_clearOverlay() { display_setOverlayText(NULL, 0); }

#ifdef __cplusplus
}
#endif
