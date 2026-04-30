#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize input subsystem and spawn the input task.
void input_init();

// Keyboard matrix API (8 rows, active-low columns)
void input_setKeyboardRow(uint8_t row, uint8_t columns);
void input_update_key(uint8_t row, uint8_t bit, bool pressed);
uint8_t input_getKeyboardRow(uint8_t row);
void input_resetKeyboardRows();

// Kempston joystick (port 0x1F, active-HIGH: bit0=right,1=left,2=down,3=up,4=fire)
void input_setJoystickBit(uint8_t bit, bool pressed);
uint8_t input_getJoystick(void);
