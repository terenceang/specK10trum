#include "input/Input.h"
#include "expander/Expander.h"
#include "audio/Audio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include <string.h>

// Keyboard rows (active-low; 0 = pressed)
static uint8_t s_keyboardRows[8];

void input_setKeyboardRow(uint8_t row, uint8_t columns) {
    if (row < 8) s_keyboardRows[row] = columns;
}

uint8_t input_getKeyboardRow(uint8_t row) {
    return s_keyboardRows[row & 0x07];
}

void input_resetKeyboardRows() {
    memset(s_keyboardRows, 0xFF, sizeof(s_keyboardRows));
}

static const char* TAG = "Input";

static void input_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Input task started");

    uint8_t p0, p1;
    bool lastA = false, lastB = false;
    uint32_t bothPressedStart = 0;
    bool muted = false;

    while (1) {
        if (expander_read_port0(&p0) == ESP_OK && expander_read_port1(&p1) == ESP_OK) {
            // P14 is Port 1 bit 4 (0x10); P02 is Port 0 bit 2 (0x04)
            bool pressedA = !(p1 & 0x10);
            bool pressedB = !(p0 & 0x04);

            if (pressedA && pressedB) {
                if (bothPressedStart == 0) bothPressedStart = xTaskGetTickCount();
                else if (pdTICKS_TO_MS(xTaskGetTickCount() - bothPressedStart) > 500) {
                    // Toggle mute on long press of both
                    muted = !muted;
                    audio_set_mute(muted);
                    // Wait for release
                    while (!(expander_read_port0(&p0) == ESP_OK && expander_read_port1(&p1) == ESP_OK && 
                           (p1 & 0x10) && (p0 & 0x04))) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    bothPressedStart = 0;
                    lastA = lastB = false;
                    continue;
                }
            } else {
                bothPressedStart = 0;
                if (pressedA && !lastA) {
                    int vol = audio_get_volume();
                    audio_set_volume(vol + 10);
                }
                if (pressedB && !lastB) {
                    int vol = audio_get_volume();
                    audio_set_volume(vol - 10);
                }
            }
            lastA = pressedA;
            lastB = pressedB;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void input_init() {
    xTaskCreate(input_task, "input", 4096, NULL, 4, NULL);
}
