
#include "display/Display.h"
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

#define SHOW_FPS_DEBUG 0

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
static const int MAX_TRANSACTIONS = 40; 
static spi_transaction_t* s_trans_pool = NULL;
static int s_trans_count = 0;

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
        {0x00, 0x3c, 0x42, 0x5a, 0x5a, 0x42, 0x3c, 0x00}, // ? (fallback) (at index 42)
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

static void drawText(uint16_t* buffer, int x, int y, const char* text, uint16_t color) {
    while (*text) {
        drawChar(buffer, x, y, *text, color);
        x += 8;
        text++;
    }
}

// SPI Pre-transfer callback to handle DC pin
static void IRAM_ATTR lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
    int dc = (int)(intptr_t)t->user;
    gpio_set_level(LCD_PIN_DC, dc);
}

static void video_task(void* pvParameters) {
    ESP_LOGI(TAG, "Video task started on core %d", xPortGetCoreID());
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            if (s_pause_video) {
                xSemaphoreGive(s_video_pause_semaphore);
                continue;
            }
            if (s_pendingSpectrum) {
                instr_video_start();
                display_renderSpectrum(s_pendingSpectrum);
                display_present();
                instr_video_end();
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

    xTaskCreatePinnedToCore(video_task, "video", 3072, NULL, 6, &s_videoTaskHandle, 1);
    // Create mutex for overlay updates
    s_overlay_mutex = xSemaphoreCreateMutex();
    // Create semaphore for pause synchronization
    s_video_pause_semaphore = xSemaphoreCreateBinary();

    display_clear();

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
    
    // Draw overlay text if present
    int overlay_y = s_lcdDisplayHeight - 10;
    // If we need to clear leftover overlay from the other framebuffer, wipe the strip.
    // Only log and perform the more expensive clear when the overlay state is dirty
    // or we explicitly need to clear frames; otherwise draw overlay quietly.
    if (s_overlay_clear_frames > 0) {
        for (int y = overlay_y; y < s_lcdDisplayHeight; y++) {
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                buf[y * s_lcdDisplayWidth + x] = 0x0000;
            }
        }
        s_overlay_clear_frames--;
    }
    if (s_overlayText[0]) {
        drawText(buf, 2, overlay_y, s_overlayText, s_overlayColor);
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

static void lcd_push_frame_async_pingpong(const uint16_t* buffer) {
    if (!s_spi || !buffer || !s_stripBuffers[0] || !s_stripBuffers[1]) return;

    // Drain any remaining transactions from the previous frame to avoid overwriting spi_transaction_t structures
    spi_transaction_t* rtrans;
    while (s_trans_count > 0) {
        esp_err_t result = spi_device_get_trans_result(s_spi, &rtrans, pdMS_TO_TICKS(100));
        if (result == ESP_OK) {
            s_trans_count--;
        } else if (result == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "SPI timeout draining previous frame, resetting transaction count");
            s_trans_count = 0;
            break;
        } else {
            ESP_LOGE(TAG, "SPI error draining previous frame: %s", esp_err_to_name(result));
            s_trans_count = 0;
            break;
        }
    }

    int linesPerStrip = s_lcdDisplayHeight / NUM_STRIPS;
    int pool_idx = 0;
    s_trans_count = 0;

    for (int i = 0; i < NUM_STRIPS; i++) {
        int startY = i * linesPerStrip;
        int height = (i == NUM_STRIPS - 1) ? (s_lcdDisplayHeight - startY) : linesPerStrip;
        int bufIdx = i & 1;

        // Drain any transactions that are already finished for this device (to avoid queue overflow)
        while (spi_device_get_trans_result(s_spi, &rtrans, 0) == ESP_OK) {
            s_trans_count--;
        }

        // Wait specifically for the strip buffer we want to reuse (ping-pong)
        if (i >= 2) {
            for (int j = 0; j < 6; j++) {
                esp_err_t result = spi_device_get_trans_result(s_spi, &rtrans, pdMS_TO_TICKS(100));
                if (result == ESP_OK) {
                    s_trans_count--;
                } else if (result == ESP_ERR_TIMEOUT) {
                    ESP_LOGW(TAG, "SPI timeout waiting for strip buffer %d transaction %d", i, j);
                    s_trans_count = 0;
                    break;
                } else {
                    ESP_LOGE(TAG, "SPI error waiting for strip: %s", esp_err_to_name(result));
                    s_trans_count = 0;
                    break;
                }
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

        for (int j = 0; j < 6; j++) spi_device_queue_trans(s_spi, &t[j], portMAX_DELAY);
        s_trans_count += 6;
        pool_idx += 6;
    }
}

bool display_showSplash(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open splash file: %s", filename);
        return false;
    }

    // Basic BMP Header check
    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54 || header[0] != 'B' || header[1] != 'M') {
        ESP_LOGE(TAG, "Invalid BMP file: %s", filename);
        fclose(f);
        return false;
    }

    int32_t width = *(int32_t*)&header[18];
    int32_t height = *(int32_t*)&header[22];
    uint16_t bpp = *(uint16_t*)&header[28];
    uint32_t dataOffset = *(uint32_t*)&header[10];

    if (bpp != 24) {
        ESP_LOGE(TAG, "Only 24-bit BMP supported (found %d bpp)", bpp);
        fclose(f);
        return false;
    }

    ESP_LOGI(TAG, "Loading splash: %s (%dx%d, %d bpp)", filename, (int)width, (int)height, (int)bpp);

    bool flip = true;
    if (height < 0) {
        height = -height;
        flip = false;
    }

    // We only support full screen or smaller
    int drawW = width > s_lcdDisplayWidth ? s_lcdDisplayWidth : width;
    int drawH = height > s_lcdDisplayHeight ? s_lcdDisplayHeight : height;

    uint16_t* buffer = display_getBackBuffer();
    memset(buffer, 0, s_lcdDisplayWidth * s_lcdDisplayHeight * 2);

    fseek(f, dataOffset, SEEK_SET);

    // Read BGR24 and convert to RGB565 line by line
    // BMP lines are padded to 4-byte boundaries
    int rowSize = (width * 3 + 3) & ~3;
    uint8_t* rowBuf = (uint8_t*)heap_caps_malloc(rowSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!rowBuf) {
        ESP_LOGE(TAG, "Failed to allocate row buffer");
        fclose(f);
        return false;
    }

    for (int y = 0; y < drawH; y++) {
        if (fread(rowBuf, 1, rowSize, f) != (size_t)rowSize) break;
        
        int destY = flip ? (drawH - 1 - y) : y;
        if (destY >= s_lcdDisplayHeight) continue;

        for (int x = 0; x < drawW; x++) {
            uint8_t b = rowBuf[x * 3 + 0];
            uint8_t g = rowBuf[x * 3 + 1];
            uint8_t r = rowBuf[x * 3 + 2];
            
            // RGB565 conversion
            uint16_t color = __builtin_bswap16(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            buffer[destY * s_lcdDisplayWidth + x] = color;
        }
    }

    free(rowBuf);
    fclose(f);
    display_present();
    return true;
}

void display_boot_test() {
    expander_set_led(true);
    // Play a short beep when LED flashes
    audio_play_tone(880, 120);
    vTaskDelay(pdMS_TO_TICKS(100));
    expander_set_led(false);
    
    if (!display_showSplash("/spiffs/splash.bmp")) {
        // Fallback to color bar if splash fails
        uint16_t* buffer = display_getBackBuffer();
        for (int y = 0; y < s_lcdDisplayHeight; y++) {
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                buffer[y * s_lcdDisplayWidth + x] = __builtin_bswap16(s_palette[(x / (s_lcdDisplayWidth / 8)) % 8]);
            }
        }
        display_present();
    }
    
    // Give the asynchronous transfer a moment to start before enabling backlight
    vTaskDelay(pdMS_TO_TICKS(50));
    expander_set_backlight(true);
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
    ESP_LOGD(TAG, "Overlay set: '%s' color=0x%04X", s_overlayText, color);
    if (s_overlay_mutex) xSemaphoreGive(s_overlay_mutex);
}

void display_pause_for_reset() {
    if (!s_videoTaskHandle || !s_video_pause_semaphore) return;

    s_pause_video = true;
    xTaskNotifyGive(s_videoTaskHandle);
    xSemaphoreTake(s_video_pause_semaphore, pdMS_TO_TICKS(500));
    s_trans_count = 0;
    ESP_LOGD(TAG, "Display paused for reset");
}

void display_resume_after_reset() {
    if (!s_videoTaskHandle) return;

    s_pause_video = false;
    display_clear();
    ESP_LOGD(TAG, "Display resumed after reset");
}
