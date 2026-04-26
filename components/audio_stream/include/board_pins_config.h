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

// Combined structure to match ESP-IDF 5.x i2s_std_gpio_config_t layout
// AND satisfy ADF's i2s_stream_idf5.c PDM member access
typedef struct {
    union {
        gpio_num_t mclk;            
        gpio_num_t mck_io_num;
    };
    union {
        gpio_num_t bclk;            
        gpio_num_t bck_io_num;
    };
    union {
        gpio_num_t ws;              
        gpio_num_t ws_io_num;
    };
    union {
        gpio_num_t dout;            
        gpio_num_t data_out_num;
    };
    union {
        gpio_num_t din;             
        gpio_num_t data_in_num;
    };
    struct {
        uint32_t mclk_inv: 1;   
        uint32_t bclk_inv: 1;   
        uint32_t ws_inv: 1;     
    } invert_flags;             
} board_i2s_pin_t;

inline esp_err_t get_i2s_pins(int port, board_i2s_pin_t *pins) {
    if (pins) {
        pins->mclk = GPIO_NUM_3;
        pins->bclk = GPIO_NUM_0;
        pins->ws = GPIO_NUM_38;
        pins->dout = GPIO_NUM_45;
        pins->din = (gpio_num_t)-1; // I2S_GPIO_UNUSED
        pins->invert_flags.mclk_inv = 0;
        pins->invert_flags.bclk_inv = 0;
        pins->invert_flags.ws_inv = 0;
        return ESP_OK;
    }
    return ESP_FAIL;
}

#endif
