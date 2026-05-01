
#include "display/Display.h"
#include "display/splash_image.h"
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <esp_timer.h>
#include "instrumentation/Instrumentation.h"
#include "expander/Expander.h"
#include "audio/Audio.h"

#define SHOW_FPS_DEBUG 1

static const char* TAG = "Display";

static const int LCD_PANEL_WIDTH = 320;
static const int LCD_PANEL_HEIGHT = 240;
static int s_lcdDisplayWidth = LCD_PANEL_WIDTH;
static int s_lcdDisplayHeight = LCD_PANEL_HEIGHT;
static const gpio_num_t LCD_PIN_SCLK = GPIO_NUM_12;
static const gpio_num_t LCD_PIN_MOSI = GPIO_NUM_21;
static const gpio_num_t LCD_PIN_CS = GPIO_NUM_14;
static const gpio_num_t LCD_PIN_DC = GPIO_NUM_13;
static const gpio_num_t LCD_PIN_ENABLE = GPIO_NUM_40;

static const uint8_t MADCTL_LANDSCAPE = 0x28;
static const uint8_t MADCTL_LANDSCAPE_FLIP = 0xE8;
static uint8_t s_lcdOrientation = MADCTL_LANDSCAPE;

static spi_device_handle_t s_spi = NULL;
static uint16_t* s_frameBuffers[2] = { NULL, NULL };
static int s_drawBuffer = 0;
static TaskHandle_t s_videoTaskHandle = NULL;
static TaskHandle_t s_emulatorTaskHandle = NULL;
static SpectrumBase* s_pendingSpectrum = NULL;
static volatile bool s_pause_video = false;
static SemaphoreHandle_t s_video_pause_semaphore = NULL;

static char s_overlayText[64] = {0};
static uint16_t s_overlayColor = 0xFFFF;
static SemaphoreHandle_t s_overlay_mutex = NULL;
static int s_overlay_clear_frames = 0;


// Ping-pong strip buffers allocated in internal RAM (IRAM)
static const int NUM_STRIPS = 5;
static uint16_t* s_stripBuffers[2] = { NULL, NULL };

// Pre-allocated SPI transactions to avoid heap churn
static const int MAX_TRANSACTIONS = 60;
static spi_transaction_t* s_trans_pool = NULL;
static int s_trans_count = 0;
static int s_pool_offset = 0;  // Alternates 0-29 and 30-59 for pool alternation

static void lcd_push_frame_async_pingpong(const uint16_t* buffer);

static void drawChar(uint16_t* buffer, int x, int y, char c, uint16_t color) {
    static const uint8_t glyphs[][8] = {
        {0x3c, 0x66, 0x6e, 0x7e, 0x76, 0x66, 0x3c, 0x00}, // 0
        {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00}, // 1
        {0x3c, 0x66, 0x06, 0x0c, 0x18, 0x30, 0x7e, 0x00}, // 2
        {0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0x00}, // 3
        {0x06, 0x0e, 0x16, 0x26, 0x7e, 0x06, 0x06, 0x00}, // 4
        {0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0x00}, // 5
        {0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0x00}, // 6
        {0x7e, 0x06, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x00}, // 7
        {0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0x00}, // 8
        {0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0x00}, // 9
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x00}, // .
        {0x00, 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}, // :
        {0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00}, // A
        {0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00}, // B
        {0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00}, // C
        {0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00}, // D
        {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x7e, 0x00}, // E
        {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x00}, // F
        {0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3c, 0x00}, // G
        {0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00}, // H
        {0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00}, // I
        {0x1e, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3c, 0x00}, // J
        {0x66, 0x6c, 0x78, 0x70, 0x78, 0x6c, 0x66, 0x00}, // K
        {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00}, // L
        {0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0x00}, // M
        {0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0x00}, // N
        {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00}, // O
        {0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x00}, // P
        {0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x0e, 0x00}, // Q
        {0x7c, 0x66, 0x66, 0x7c, 0x78, 0x6c, 0x66, 0x00}, // R
        {0x3c, 0x66, 0x30, 0x1c, 0x06, 0x66, 0x3c, 0x00}, // S
        {0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
        {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00}, // U
        {0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x00}, // V
        {0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00}, // W
        {0x66, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x66, 0x00}, // X
        {0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x18, 0x00}, // Y
        {0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0x00}, // Z
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space (at index 38)
        {0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00}, // - (at index 39)
        {0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00}, // / (at index 40)
        {0x00, 0x01, 0x02, 0x44, 0x28, 0x10, 0x00, 0x00}, // ✓ (approx checkmark) (at index 41)
        {0x00, 0x3c, 0x42, 0x5a, 0x5a, 0x42, 0x3c, 0x00}  // ? (fallback) (at index 42)
    };

    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == '.') idx = 10;
    else if (c == ':') idx = 11;
    else if (c >= 'A' && c <= 'Z') idx = 12 + (c - 'A');
    else if (c >= 'a' && c <= 'z') idx = 12 + (c - 'a');
    else if (c == ' ') idx = 38;
    else if (c == '-') idx = 39;
    else if (c == '/') idx = 40;
    else if ((unsigned char)c == 0xE2 || (unsigned char)c == 0x80) idx = 41; // Crude check for UTF-8 start of ✓
    else if (c == '?') idx = 42;
    else idx = 42; // Fallback to ? for any unknown char

    if (idx == -1) return;

    const uint8_t* glyph = glyphs[idx];
    for (int i = 0; i < 8; i++) {
        uint8_t row = glyph[i];
        for (int j = 0; j < 8; j++) {
            if (row & (0x80 >> j)) {
                int px = x + j;
                int py = y + i;
                if (px >= 0 && px < s_lcdDisplayWidth && py >= 0 && py < s_lcdDisplayHeight) {
                    buffer[py * s_lcdDisplayWidth + px] = color;
                }
            }
        }
    }
}

// SPI Pre-transfer callback to handle DC pin
static void IRAM_ATTR lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
    int dc = (int)(intptr_t)t->user;
    gpio_set_level(LCD_PIN_DC, dc);
}

static void video_task(void* pvParameters) {
    ESP_LOGI(TAG, "Video task started on core %d", xPortGetCoreID());
    static int perf_frame_count = 0;
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            if (s_pause_video) {
                xSemaphoreGive(s_video_pause_semaphore);
                continue;
            }
            if (s_pendingSpectrum) {
                int64_t t_video_start = esp_timer_get_time();
                instr_video_start();

                // Only do SPI push (rendering is now on Core 0 for load balancing)
                int64_t t_present_start = esp_timer_get_time();
                display_present();
                int64_t t_present_end = esp_timer_get_time();

                instr_video_end();
                int64_t t_video_end = esp_timer_get_time();

                // Log video timing every 100 frames
                if (++perf_frame_count >= 100) {
                    int64_t t_present = t_present_end - t_present_start;
                    int64_t t_video = t_video_end - t_video_start;
                    ESP_LOGI(TAG, "VIDEO: present=%.2fms total=%.2fms",
                             t_present / 1000.0, t_video / 1000.0);
                    perf_frame_count = 0;
                }

                // Signal emulator task that frame is complete
                if (s_emulatorTaskHandle) {
                    xTaskNotifyGive(s_emulatorTaskHandle);
                }
            }
        }
    }
}

void display_trigger_frame(SpectrumBase* spectrum) {
    s_pendingSpectrum = spectrum;
    if (s_videoTaskHandle) {
        xTaskNotifyGive(s_videoTaskHandle);
    }
}

void display_set_emulator_task(TaskHandle_t handle) {
    s_emulatorTaskHandle = handle;
}

void display_wait_frame() {
    if (s_emulatorTaskHandle) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static const uint16_t s_palette[16] = {
    // Normal
    0x0000, 0x001B, 0xF800, 0xF81F, 0x07E0, 0x07FF, 0xFFE0, 0xFFFF,
    // Bright
    0x0000, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x07FF, 0xFFE0, 0xFFFF,
};

static void lcd_send_command(uint8_t command) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &command;
    t.user = (void*)0; // DC=0
    spi_device_transmit(s_spi, &t);
}

static void lcd_send_data(const uint8_t* data, int len) {
    if (len <= 0) return;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void*)1; // DC=1
    spi_device_transmit(s_spi, &t);
}

static void ili9341_init() {
    // Reset sequence for GPIO 40
    gpio_set_level(LCD_PIN_ENABLE, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_send_command(0x01); // Software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_send_command(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t init_data1[] = { 0x00, 0xC1, 0x30 };
    lcd_send_command(0xCF); lcd_send_data(init_data1, sizeof(init_data1));
    const uint8_t init_data2[] = { 0x64, 0x03, 0x12, 0x81 };
    lcd_send_command(0xED); lcd_send_data(init_data2, sizeof(init_data2));
    const uint8_t init_data3[] = { 0x85, 0x00, 0x78 };
    lcd_send_command(0xE8); lcd_send_data(init_data3, sizeof(init_data3));
    const uint8_t init_data4[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    lcd_send_command(0xCB); lcd_send_data(init_data4, sizeof(init_data4));
    const uint8_t init_data5[] = { 0x20 };
    lcd_send_command(0xF7); lcd_send_data(init_data5, sizeof(init_data5));
    const uint8_t init_data6[] = { 0x00, 0x00 };
    lcd_send_command(0xEA); lcd_send_data(init_data6, sizeof(init_data6));
    const uint8_t init_data7[] = { 0x23 };
    lcd_send_command(0xC0); lcd_send_data(init_data7, sizeof(init_data7));
    const uint8_t init_data8[] = { 0x10 };
    lcd_send_command(0xC1); lcd_send_data(init_data8, sizeof(init_data8));
    const uint8_t init_data9[] = { 0x3E, 0x28 };
    lcd_send_command(0xC5); lcd_send_data(init_data9, sizeof(init_data9));
    const uint8_t init_data10[] = { 0x86 };
    lcd_send_command(0xC7); lcd_send_data(init_data10, sizeof(init_data10));
    const uint8_t init_data11[] = { s_lcdOrientation };
    lcd_send_command(0x36); lcd_send_data(init_data11, sizeof(init_data11));
    const uint8_t init_data12[] = { 0x55 };
    lcd_send_command(0x3A); lcd_send_data(init_data12, sizeof(init_data12));
    const uint8_t init_data13[] = { 0x00, 18 };
    lcd_send_command(0xB1); lcd_send_data(init_data13, sizeof(init_data13));
    const uint8_t init_data14[] = { 0x08, 0x82, 0x27 };
    lcd_send_command(0xB6); lcd_send_data(init_data14, sizeof(init_data14));
    const uint8_t init_data15[] = { 0x00 };
    lcd_send_command(0xF2); lcd_send_data(init_data15, sizeof(init_data15));
    const uint8_t init_data16[] = { 0x01 };
    lcd_send_command(0x26); lcd_send_data(init_data16, sizeof(init_data16));

    const uint8_t init_data17[] = {
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00
    };
    lcd_send_command(0xE0); lcd_send_data(init_data17, sizeof(init_data17));
    const uint8_t init_data18[] = {
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F
    };
    lcd_send_command(0xE1); lcd_send_data(init_data18, sizeof(init_data18));

    lcd_send_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_send_command(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void update_display_dimensions() {
    if (s_lcdOrientation == MADCTL_LANDSCAPE || s_lcdOrientation == MADCTL_LANDSCAPE_FLIP) {
        s_lcdDisplayWidth = LCD_PANEL_WIDTH; s_lcdDisplayHeight = LCD_PANEL_HEIGHT;
    } else {
        s_lcdDisplayWidth = LCD_PANEL_HEIGHT; s_lcdDisplayHeight = LCD_PANEL_WIDTH;
    }
}

void display_clear() {
    size_t bufferSize = s_lcdDisplayWidth * s_lcdDisplayHeight * sizeof(uint16_t);
    for (int i = 0; i < 2; i++) {
        if (s_frameBuffers[i]) {
            memset(s_frameBuffers[i], 0, bufferSize);
        }
    }
    display_present();
    display_present(); // Clear both buffers
}

bool display_init() {
    update_display_dimensions();
    size_t bufferSize = s_lcdDisplayWidth * s_lcdDisplayHeight * sizeof(uint16_t);
    for (int i = 0; i < 2; i++) {
        // Explicitly request PSRAM first to save internal RAM for Bluetooth
        s_frameBuffers[i] = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frameBuffers[i]) {
            ESP_LOGW(TAG, "Framebuffer %d allocation in PSRAM failed, falling back to internal RAM", i);
            s_frameBuffers[i] = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        }
        if (!s_frameBuffers[i]) return false;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LCD_PIN_DC, 1);

    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num = LCD_PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = LCD_PIN_SCLK;
    buscfg.max_transfer_sz = 32768;
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.clock_speed_hz = 80000000;
    devcfg.mode = 0;
    devcfg.spics_io_num = LCD_PIN_CS;
    devcfg.queue_size = 40; // Increased to hold an entire frame's worth of transactions
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    devcfg.pre_cb = lcd_spi_pre_transfer_callback;
    spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);

    ili9341_init();

    int linesPerStrip = s_lcdDisplayHeight / NUM_STRIPS;
    size_t stripBytes = s_lcdDisplayWidth * linesPerStrip * sizeof(uint16_t);
    s_stripBuffers[0] = (uint16_t*)heap_caps_malloc(stripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stripBuffers[1] = (uint16_t*)heap_caps_malloc(stripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    s_trans_pool = (spi_transaction_t*)heap_caps_malloc(sizeof(spi_transaction_t) * MAX_TRANSACTIONS, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, &s_videoTaskHandle, 1);
    // Create mutex for overlay updates
    s_overlay_mutex = xSemaphoreCreateMutex();
    // Create semaphore for pause synchronization
    s_video_pause_semaphore = xSemaphoreCreateBinary();

    display_clear();

    // Enable backlight as part of display init
    expander_set_backlight(true);
    return true;
}

uint16_t* display_getBackBuffer() { return s_frameBuffers[s_drawBuffer]; }

void display_renderSpectrum(SpectrumBase* spectrum) {
    if (!spectrum) return;
    spectrum->renderToRGB565(display_getBackBuffer(), s_lcdDisplayWidth, s_lcdDisplayHeight);
}

void display_present() {
    int presentingIndex = s_drawBuffer;
    s_drawBuffer = 1 - s_drawBuffer;

    uint16_t* buf = s_frameBuffers[presentingIndex];

    // Snapshot overlay state under the mutex. The emulator task can call
    // display_setOverlayText() concurrently and strncpy into s_overlayText
    // is not atomic — reading without the lock can yield a torn string and
    // a mismatched color.
    char overlay_text[sizeof(s_overlayText)];
    uint16_t overlay_color;
    bool do_clear;
    if (s_overlay_mutex) xSemaphoreTake(s_overlay_mutex, portMAX_DELAY);
    memcpy(overlay_text, s_overlayText, sizeof(overlay_text));
    overlay_color = s_overlayColor;
    do_clear = (s_overlay_clear_frames > 0);
    if (do_clear) s_overlay_clear_frames--;
    if (s_overlay_mutex) xSemaphoreGive(s_overlay_mutex);

    int overlay_y = s_lcdDisplayHeight - 10;
    if (do_clear) {
        for (int y = overlay_y; y < s_lcdDisplayHeight; y++) {
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                buf[y * s_lcdDisplayWidth + x] = 0x0000;
            }
        }
    }
    if (overlay_text[0]) {
        int current_y = overlay_y;
        const char* p = overlay_text;
        while (*p) {
            const char* line_end = p;
            while (*line_end && *line_end != '\n') line_end++;

            int line_len = line_end - p;
            int line_width = line_len * 8;
            int center_x = (s_lcdDisplayWidth - line_width) / 2;

            for (int i = 0; i < line_len; i++) {
                drawChar(buf, center_x + i * 8, current_y, p[i], overlay_color);
            }

            if (*line_end == '\n') {
                current_y -= 16;
                p = line_end + 1;
            } else {
                break;
            }
        }
    }

    lcd_push_frame_async_pingpong(buf);

    static uint32_t frame_count = 0;
    static int64_t last_time_us = 0;
    frame_count++;
    int64_t now = esp_timer_get_time();
    if (last_time_us == 0) last_time_us = now;
    if (now - last_time_us >= 5000000) { // Log every 5 seconds
        int64_t cpu_us = 0, video_us = 0; uint32_t cpu_frames = 0, video_frames = 0;
        instr_snapshot_and_reset(&cpu_us, &cpu_frames, &video_us, &video_frames);
        #if SHOW_FPS_DEBUG
        ESP_LOGI(TAG, "FPS: %.2f | CPU: %.3fms | Video: %.3fms", (double)frame_count * 1000000.0 / (double)(now - last_time_us), 
                 cpu_frames ? (double)cpu_us / cpu_frames / 1000.0 : 0, video_frames ? (double)video_us / video_frames / 1000.0 : 0);
        #endif
        frame_count = 0; last_time_us = now;
    }
}

// Drain N completed transactions from the SPI queue with a total budget.
// Returns true if all N were collected. Returns false on error or timeout,
// but never zeroes s_trans_count — the SPI driver still owns those transactions.
static bool drain_spi_transactions(int count, TickType_t total_timeout) {
    spi_transaction_t* rtrans;
    int start_count = count;
    int64_t deadline_ms = esp_timer_get_time() / 1000 + pdTICKS_TO_MS(total_timeout);

    for (int i = 0; i < count; i++) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        int64_t remaining_ms = deadline_ms - now_ms;
        if (remaining_ms <= 0) {
            ESP_LOGW(TAG, "SPI drain timeout: got %d/%d (budget)", start_count - count, start_count);
            return false;
        }

        // Use 10ms per transaction (SPI completes 3ms/strip @ 80MHz; 10ms accounts for contention)
        TickType_t timeout_ticks = pdMS_TO_TICKS(remaining_ms > 10 ? 10 : (remaining_ms > 0 ? remaining_ms : 1));
        esp_err_t r = spi_device_get_trans_result(s_spi, &rtrans, timeout_ticks);
        if (r == ESP_OK) {
            s_trans_count--;
        } else if (r == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "SPI drain timeout: got %d/%d (txn)", start_count - count, start_count);
            return false;
        } else {
            ESP_LOGE(TAG, "SPI drain error (%d/%d): %s",
                     start_count - count, start_count, esp_err_to_name(r));
            return false;
        }
    }
    return true;
}

static void lcd_push_frame_async_pingpong(const uint16_t* buffer) {
    if (!s_spi || !buffer || !s_stripBuffers[0] || !s_stripBuffers[1]) return;

    // Pool alternation: use halves 0-29 and 30-59 to allow Frame N+1 to queue
    // while Frame N's DMA is still running (different pool space = no collision).
    // Drain the previous pool half's transactions before switching to this half.
    if (s_trans_count > 0) {
        int64_t t_drain_start = esp_timer_get_time();

        // Fast attempt: try 1ms timeout (SPI usually finishes in ~15ms)
        if (!drain_spi_transactions(s_trans_count, pdMS_TO_TICKS(1))) {
            // If fast drain failed, use full 40ms budget (one frame's worth of SPI time)
            if (!drain_spi_transactions(s_trans_count, pdMS_TO_TICKS(40))) {
                ESP_LOGW(TAG, "SPI backlog (%d pending); skipping frame", s_trans_count);
                return;
            }
        }

        int64_t t_drain_end = esp_timer_get_time();
        static int drain_log_count = 0;
        if (++drain_log_count >= 100) {
            int64_t drain_time = t_drain_end - t_drain_start;
            if (drain_time > 1000) {  // Log if > 1ms
                ESP_LOGI(TAG, "SPI_DRAIN: %d txns from pool+%d took %.2fms",
                         s_trans_count, (s_pool_offset ? 30 : 0), drain_time / 1000.0);
            }
            drain_log_count = 0;
        }
    }

    int linesPerStrip = s_lcdDisplayHeight / NUM_STRIPS;
    int pool_idx = s_pool_offset;  // Use current pool half offset (0 or 30)

    for (int i = 0; i < NUM_STRIPS; i++) {
        int startY = i * linesPerStrip;
        int height = (i == NUM_STRIPS - 1) ? (s_lcdDisplayHeight - startY) : linesPerStrip;
        int bufIdx = i & 1;

        // Ensure the strip buffer we're about to reuse is free (its 6 txns
        // have completed). With NUM_STRIPS=5 and 2 strip buffers, strips 2-4
        // alias strips 0-2; strip i must wait for strip (i-2)'s 6 txns.
        // Use 80ms budget (one full frame transfer = 15ms, plus 5× margin for setup/overhead).
        if (i >= 2) {
            if (!drain_spi_transactions(6, pdMS_TO_TICKS(80))) {
                ESP_LOGW(TAG, "SPI strip %d backlog; abort", i);
                return;
            }
        }

        uint16_t* dst = s_stripBuffers[bufIdx];
        memcpy(dst, &buffer[startY * s_lcdDisplayWidth], s_lcdDisplayWidth * height * 2);

        spi_transaction_t* t = &s_trans_pool[pool_idx];
        memset(t, 0, sizeof(spi_transaction_t) * 6);

        t[0].length = 8; t[0].flags = SPI_TRANS_USE_TXDATA; t[0].tx_data[0] = 0x2A; t[0].user = (void*)0;
        t[1].length = 32; t[1].flags = SPI_TRANS_USE_TXDATA; t[1].user = (void*)1;
        t[1].tx_data[0] = 0; t[1].tx_data[1] = 0; t[1].tx_data[2] = (s_lcdDisplayWidth - 1) >> 8; t[1].tx_data[3] = (s_lcdDisplayWidth - 1) & 0xFF;
        t[2].length = 8; t[2].flags = SPI_TRANS_USE_TXDATA; t[2].tx_data[0] = 0x2B; t[2].user = (void*)0;
        t[3].length = 32; t[3].flags = SPI_TRANS_USE_TXDATA; t[3].user = (void*)1;
        t[3].tx_data[0] = startY >> 8; t[3].tx_data[1] = startY & 0xFF;
        t[3].tx_data[2] = (startY + height - 1) >> 8; t[3].tx_data[3] = (startY + height - 1) & 0xFF;
        t[4].length = 8; t[4].flags = SPI_TRANS_USE_TXDATA; t[4].tx_data[0] = 0x2C; t[4].user = (void*)0;
        t[5].length = s_lcdDisplayWidth * height * 16; t[5].tx_buffer = dst; t[5].user = (void*)1;

        // Bounded enqueue so a wedged driver can't hang us. The queue has
        // depth 40 and we hold at most NUM_STRIPS*6=30 in flight, so a full
        // queue here is a real fault, not normal back-pressure.
        bool aborted = false;
        for (int j = 0; j < 6; j++) {
            esp_err_t r = spi_device_queue_trans(s_spi, &t[j], pdMS_TO_TICKS(100));
            if (r != ESP_OK) {
                ESP_LOGE(TAG, "spi_device_queue_trans strip %d/%d: %s", i, j, esp_err_to_name(r));
                aborted = true;
                break;
            }
            s_trans_count++;
        }
        if (aborted) {
            // Drain whatever we've enqueued so the next frame starts clean.
            // If even this drain fails, leave s_trans_count alone — the next
            // frame's drain will handle it.
            drain_spi_transactions(s_trans_count, pdMS_TO_TICKS(40));
            return;
        }
        pool_idx += 6;
    }

    // Alternate pool half for next frame (0-29 and 30-59)
    s_pool_offset = (s_pool_offset == 0) ? 30 : 0;
}


void showSplashScreen() {
    uint16_t* buffer = display_getBackBuffer();
    
    // Copy embedded splash image to back buffer
    // We only support images that match or are smaller than the screen
    int drawW = splash_width > s_lcdDisplayWidth ? s_lcdDisplayWidth : splash_width;
    int drawH = splash_height > s_lcdDisplayHeight ? s_lcdDisplayHeight : splash_height;
    
    // Clear buffer first (in case splash is smaller than screen)
    memset(buffer, 0, s_lcdDisplayWidth * s_lcdDisplayHeight * 2);
    
    for (int y = 0; y < drawH; y++) {
        memcpy(&buffer[y * s_lcdDisplayWidth], &splash_image_data[y * splash_width], drawW * 2);
    }
    
    // Ensure both buffers have the splash for the boot overlay.
    // This prevents the screen from going black when display_boot_update() pushes 
    // the other buffer during the boot sequence.
    int currentBack = s_drawBuffer;
    int currentFront = 1 - s_drawBuffer;
    memcpy(s_frameBuffers[currentFront], s_frameBuffers[currentBack], s_lcdDisplayWidth * s_lcdDisplayHeight * 2);

    display_present();
    vTaskDelay(pdMS_TO_TICKS(50));
}

void display_setOverlayText(const char* text, uint16_t color) {
    if (s_overlay_mutex) xSemaphoreTake(s_overlay_mutex, portMAX_DELAY);

    uint16_t new_swapped_color = __builtin_bswap16(color);

    // Determine whether the requested overlay state actually changes anything.
    bool will_change = false;
    if (text) {
        if (s_overlayText[0] == '\0' || strncmp(s_overlayText, text, sizeof(s_overlayText)) != 0 || s_overlayColor != new_swapped_color) {
            will_change = true;
        }
    } else {
        // Clearing overlay -> change only if it isn't already empty
        if (s_overlayText[0] != '\0') will_change = true;
    }

    if (!will_change) {
        // No-op when identical state requested
        if (s_overlay_mutex) xSemaphoreGive(s_overlay_mutex);
        return;
    }

    if (text) {
        strncpy(s_overlayText, text, sizeof(s_overlayText) - 1);
        s_overlayText[sizeof(s_overlayText) - 1] = '\0';
        s_overlay_clear_frames = 1;
    } else {
        s_overlayText[0] = '\0';
        s_overlay_clear_frames = 2;
        int overlay_y = s_lcdDisplayHeight - 10;
        for (int b = 0; b < 2; b++) {
            if (s_frameBuffers[b]) {
                for (int y = overlay_y; y < s_lcdDisplayHeight; y++) {
                    for (int x = 0; x < s_lcdDisplayWidth; x++) {
                        s_frameBuffers[b][y * s_lcdDisplayWidth + x] = 0x0000;
                    }
                }
            }
        }
    }
    s_overlayColor = new_swapped_color;
    ESP_LOGI(TAG, "LCD Update: '%s' (color=0x%04X)", s_overlayText, color);
    if (s_overlay_mutex) xSemaphoreGive(s_overlay_mutex);
}

void display_boot_log_add(const char* message) {
    if (!message) return;
    display_setOverlayText(message, 0xFFFF);
}

void display_boot_log_add_step(int step, int total, const char* description) {
    static char buffer[128];
    if (description) {
        snprintf(buffer, sizeof(buffer), "Step %d/%d\n%s", step, total, description);
    } else {
        snprintf(buffer, sizeof(buffer), "Step %d/%d", step, total);
    }
    display_setOverlayText(buffer, 0xFFFF);
}

void display_boot_log_hide() {
    display_clearOverlay();
}

void display_boot_update() {
    // During boot, update overlay on BOTH buffers to ensure splash + overlay are always correct
    // regardless of which buffer is currently being displayed
    if (!s_frameBuffers[0] || !s_frameBuffers[1]) {
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }

    // Overlay is the bottom 50 pixels (y: 190-239)
    // Splash/logo is protected in rows 0-189
    const int OVERLAY_HEIGHT = 50;
    const int overlay_y_start = s_lcdDisplayHeight - OVERLAY_HEIGHT;  // 190
    const int overlay_y_end = s_lcdDisplayHeight;                     // 240

    // Safety check: ensure we never clear above row 189
    if (overlay_y_start < 190) {
        ESP_LOGE(TAG, "ERROR: overlay_y_start=%d is too high, would corrupt splash/logo!", overlay_y_start);
        return;
    }

    // Update both frame buffers to ensure consistent display
    for (int buf_idx = 0; buf_idx < 2; buf_idx++) {
        uint16_t* buf = s_frameBuffers[buf_idx];

        // ONLY clear the bottom 25 pixels (overlay area), never touch splash image
        for (int y = overlay_y_start; y < overlay_y_end; y++) {
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                buf[y * s_lcdDisplayWidth + x] = 0x0000;
            }
        }

        // Draw separator line at the top of the message area (y=215)
        uint16_t line_color = __builtin_bswap16(0x07E0);  // Green line for visual separation
        for (int x = 0; x < s_lcdDisplayWidth; x++) {
            buf[overlay_y_start * s_lcdDisplayWidth + x] = line_color;
        }

        // Draw new overlay text on both buffers (only in bottom 25 pixel area)
        if (s_overlayText[0]) {
            // Count lines
            int num_lines = 1;
            for (const char* t = s_overlayText; *t; t++) {
                if (*t == '\n') num_lines++;
            }
            // Calculate starting y to vertically center lines in overlay
            int total_text_height = num_lines * 16;
            int start_y = overlay_y_start + (OVERLAY_HEIGHT - total_text_height) / 2;
            int current_y = start_y;
            const char* p = s_overlayText;
            while (*p) {
                const char* line_end = p;
                while (*line_end && *line_end != '\n') line_end++;
                int line_len = line_end - p;
                int line_width = line_len * 8;
                int center_x = (s_lcdDisplayWidth - line_width) / 2;
                if (current_y >= overlay_y_start && current_y < overlay_y_end) {
                    for (int i = 0; i < line_len; i++) {
                        drawChar(buf, center_x + i * 8, current_y, p[i], s_overlayColor);
                    }
                }
                current_y += 16;
                if (*line_end == '\n') {
                    p = line_end + 1;
                } else {
                    break;
                }
            }
        }
    }

    // Reset clear counter since we just cleared both buffers
    s_overlay_clear_frames = 0;

    // Push current back buffer to LCD (which now has the correct overlay)
    lcd_push_frame_async_pingpong(s_frameBuffers[s_drawBuffer]);

    vTaskDelay(pdMS_TO_TICKS(20));
}

void display_pause_for_reset() {
    if (!s_videoTaskHandle || !s_video_pause_semaphore) return;

    s_pause_video = true;
    xTaskNotifyGive(s_videoTaskHandle);
    // Wait for the video task to ack. One frame = 20ms, plus some margin for PSRAM/Wi-Fi contention.
    if (xSemaphoreTake(s_video_pause_semaphore, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Video task did not ack pause");
    }
    // Drain any transactions still owned by the SPI driver. NEVER zero
    // s_trans_count without collecting them — the driver still has the
    // transaction structs and DMA may still be reading the strip buffers.
    if (s_trans_count > 0) {
        if (!drain_spi_transactions(s_trans_count, pdMS_TO_TICKS(100))) {
            ESP_LOGE(TAG, "SPI drain timeout during pause (%d pending)", s_trans_count);
        }
    }
    ESP_LOGD(TAG, "Display paused for reset");
}

void display_resume_after_reset() {
    if (!s_videoTaskHandle) return;

    s_pause_video = false;
    display_clear();
    ESP_LOGD(TAG, "Display resumed after reset");
}
