#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XL9535_I2C_ADDR 0x20

// Pin masks for Port 0
#define XL9535_P00_BACKLIGHT 0x01
#define XL9535_P01_LCD_CS    0x02
#define XL9535_P02_LCD_DC    0x04
#define XL9535_P03_LCD_RST   0x08
// ... others as needed

// Pin masks for Port 1
#define XL9535_P17_USER_LED  0x80

bool expander_init();
esp_err_t expander_write_port0(uint8_t value);
esp_err_t expander_write_port1(uint8_t value);
esp_err_t expander_read_port0(uint8_t *value);
esp_err_t expander_read_port1(uint8_t *value);

// Convenience functions
void expander_set_backlight(bool on);
void expander_set_led(bool on);
void expander_blink_led(int count, int ms);

#ifdef __cplusplus
}
#endif
