#ifndef _BOARD_PINS_CONFIG_H_
#define _BOARD_PINS_CONFIG_H_

#include "driver/gpio.h"
#include "esp_err.h"

// Define legacy FreeRTOS types if not defined
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#ifndef xSemaphoreHandle
#define xSemaphoreHandle SemaphoreHandle_t
#endif

#ifndef xTimerHandle
#define xTimerHandle TimerHandle_t
#endif

#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

typedef struct {
    int bck_io_num;
    int ws_io_num;
    int data_out_num;
    int data_in_num;
    int mck_io_num;
} board_i2s_pin_t;

inline esp_err_t get_i2s_pins(int port, board_i2s_pin_t *pins) {
    if (pins) {
        pins->bck_io_num = 0;
        pins->ws_io_num = 38;
        pins->data_out_num = 45;
        pins->data_in_num = -1;
        pins->mck_io_num = 3;
        return ESP_OK;
    }
    return ESP_FAIL;
}

#endif
