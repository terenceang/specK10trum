#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "spectrum/SpectrumBase.h"

#ifdef __cplusplus
extern "C" {
#endif

void menu_init(SpectrumBase* spectrum);
void menu_open();
void menu_close();
bool menu_is_open();

// Returns true if input was consumed by the menu
bool menu_handle_input(bool pressedA, bool pressedB);

void menu_render(uint16_t* buffer, int width, int height);

#ifdef __cplusplus
}
#endif
