#include "display/Display.h"
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <esp_timer.h>

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

// ILI9341 MADCTL values for rotation
static const uint8_t MADCTL_PORTRAIT = 0x48;
static const uint8_t MADCTL_LANDSCAPE = 0x28;
static const uint8_t MADCTL_PORTRAIT_FLIP = 0x88;
static const uint8_t MADCTL_LANDSCAPE_FLIP = 0xE8;
static uint8_t s_lcdOrientation = MADCTL_LANDSCAPE;

static spi_device_handle_t s_spi = NULL;
static uint16_t* s_frameBuffers[2] = { NULL, NULL };
static int s_drawBuffer = 0;
static TaskHandle_t s_videoTaskHandle = NULL;
static SpectrumBase* s_pendingSpectrum = NULL;

static void video_task(void* pvParameters) {
    ESP_LOGI(TAG, "Video task started on core %d", xPortGetCoreID());
    while (1) {
        // Wait for a notification from the emulator task
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            if (s_pendingSpectrum) {
                display_renderSpectrum(s_pendingSpectrum);
                display_present();
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

static const uint16_t s_paletteNormal[8] = {
    0x0000, // black
    0x001B, // blue
    0xF800, // red
    0xF81F, // magenta
    0x07E0, // green
    0x07FF, // cyan
    0xFFE0, // yellow
    0xFFFF, // white
};

static const uint16_t s_paletteBright[8] = {
    0x0000, // black
    0x001F, // bright blue
    0xF800, // bright red
    0xF81F, // bright magenta
    0x07E0, // bright green
    0x07FF, // bright cyan
    0xFFE0, // bright yellow
    0xFFFF, // bright white
};

static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void lcd_send_command(uint8_t command) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &command;
    gpio_set_level(LCD_PIN_DC, 0);
    spi_device_transmit(s_spi, &t);
}

static void lcd_send_data(const uint8_t* data, int len) {
    if (len <= 0) return;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    gpio_set_level(LCD_PIN_DC, 1);
    spi_device_transmit(s_spi, &t);
}

static void lcd_set_window(int x0, int y0, int x1, int y1) {
    uint8_t data[4];

    lcd_send_command(0x2A);
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    lcd_send_data(data, 4);

    lcd_send_command(0x2B);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    lcd_send_data(data, 4);

    lcd_send_command(0x2C);
}

static void ili9341_init() {
    gpio_set_level(LCD_PIN_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    lcd_send_command(0x01); // Software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_send_command(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t init_data1[] = { 0x00, 0xC1, 0x30 };
    lcd_send_command(0xCF);
    lcd_send_data(init_data1, sizeof(init_data1));

    const uint8_t init_data2[] = { 0x64, 0x03, 0x12, 0x81 };
    lcd_send_command(0xED);
    lcd_send_data(init_data2, sizeof(init_data2));

    const uint8_t init_data3[] = { 0x85, 0x00, 0x78 };
    lcd_send_command(0xE8);
    lcd_send_data(init_data3, sizeof(init_data3));

    const uint8_t init_data4[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    lcd_send_command(0xCB);
    lcd_send_data(init_data4, sizeof(init_data4));

    const uint8_t init_data5[] = { 0x20 };
    lcd_send_command(0xF7);
    lcd_send_data(init_data5, sizeof(init_data5));

    const uint8_t init_data6[] = { 0x00, 0x00 };
    lcd_send_command(0xEA);
    lcd_send_data(init_data6, sizeof(init_data6));

    const uint8_t init_data7[] = { 0x23 };
    lcd_send_command(0xC0);
    lcd_send_data(init_data7, sizeof(init_data7));

    const uint8_t init_data8[] = { 0x10 };
    lcd_send_command(0xC1);
    lcd_send_data(init_data8, sizeof(init_data8));

    const uint8_t init_data9[] = { 0x3E, 0x28 };
    lcd_send_command(0xC5);
    lcd_send_data(init_data9, sizeof(init_data9));

    const uint8_t init_data10[] = { 0x86 };
    lcd_send_command(0xC7);
    lcd_send_data(init_data10, sizeof(init_data10));

    const uint8_t init_data11[] = { s_lcdOrientation };
    lcd_send_command(0x36);
    lcd_send_data(init_data11, sizeof(init_data11));

    const uint8_t init_data12[] = { 0x55 };
    lcd_send_command(0x3A);
    lcd_send_data(init_data12, sizeof(init_data12));

    const uint8_t init_data13[] = { 0x00, 18 };
    lcd_send_command(0xB1);
    lcd_send_data(init_data13, sizeof(init_data13));

    const uint8_t init_data14[] = { 0x08, 0x82, 0x27 };
    lcd_send_command(0xB6);
    lcd_send_data(init_data14, sizeof(init_data14));

    const uint8_t init_data15[] = { 0x00 };
    lcd_send_command(0xF2);
    lcd_send_data(init_data15, sizeof(init_data15));

    const uint8_t init_data16[] = { 0x01 };
    lcd_send_command(0x26);
    lcd_send_data(init_data16, sizeof(init_data16));

    const uint8_t init_data17[] = {
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00
    };
    lcd_send_command(0xE0);
    lcd_send_data(init_data17, sizeof(init_data17));

    const uint8_t init_data18[] = {
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F
    };
    lcd_send_command(0xE1);
    lcd_send_data(init_data18, sizeof(init_data18));

    lcd_send_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_send_command(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static const i2c_port_t XL9535_I2C_PORT = I2C_NUM_0;
static const gpio_num_t XL9535_SDA = GPIO_NUM_47;
static const gpio_num_t XL9535_SCL = GPIO_NUM_48;
static const uint8_t XL9535_ADDR = 0x20;
static const uint8_t XL9535_REG_OUTPUT_PORT0 = 0x02;
static const uint8_t XL9535_REG_OUTPUT_PORT1 = 0x03;
static const uint8_t XL9535_REG_CONFIG_PORT0 = 0x06;
static const uint8_t XL9535_REG_CONFIG_PORT1 = 0x07;
static const uint8_t XL9535_BACKLIGHT_MASK = 0x01;
static const uint8_t XL9535_USER_LED_MASK = 0x80;
static const uint8_t XL9535_PORT0_OUTPUT_CONFIG = 0xFE;
static const uint8_t XL9535_PORT1_OUTPUT_CONFIG = 0x7F;

static esp_err_t xl9535_write_register(uint8_t reg, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(XL9535_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t xl9535_read_register(uint8_t reg, uint8_t* value) {
    if (!value) return ESP_ERR_INVALID_ARG;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(XL9535_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static bool xl9535_init() {
    ESP_LOGI(TAG, "XL9535: Starting I2C Scan on Port %d (SDA:%d, SCL:%d)...", XL9535_I2C_PORT, XL9535_SDA, XL9535_SCL);

    auto setup_i2c_driver = []() -> esp_err_t {
        i2c_config_t conf;
        memset(&conf, 0, sizeof(conf));
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = XL9535_SDA;
        conf.scl_io_num = XL9535_SCL;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = 100000;

        esp_err_t err = i2c_param_config(XL9535_I2C_PORT, &conf);
        if (err != ESP_OK) return err;

        err = i2c_driver_install(XL9535_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            i2c_set_pin(XL9535_I2C_PORT, XL9535_SDA, XL9535_SCL, GPIO_PULLUP_ENABLE, GPIO_PULLUP_ENABLE, I2C_MODE_MASTER);
            return ESP_OK;
        }
        return err;
    };

    if (setup_i2c_driver() != ESP_OK) {
        ESP_LOGE(TAG, "XL9535: I2C driver setup failed");
        return false;
    }

    // I2C Scanner
    ESP_LOGI(TAG, "XL9535: Scanning I2C bus...");
    int devices_found = 0;
    for (int i = 1; i < 127; i++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(XL9535_I2C_PORT, cmd, pdMS_TO_TICKS(10));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, " -> Found device at 0x%02X", i);
            devices_found++;
        }
    }
    if (devices_found == 0) {
        ESP_LOGW(TAG, " -> No I2C devices found!");
    }

    auto run_test = []() -> bool {
        ESP_LOGI(TAG, "XL9535: Writing Port Configs (All Output)...");
        if (xl9535_write_register(XL9535_REG_CONFIG_PORT0, 0x00) != ESP_OK) return false;
        if (xl9535_write_register(XL9535_REG_CONFIG_PORT1, 0x00) != ESP_OK) return false;

        // Turn backlight ON immediately and keep it steady
        xl9535_write_register(XL9535_REG_OUTPUT_PORT0, XL9535_BACKLIGHT_MASK);

        ESP_LOGI(TAG, "XL9535: Running LED blink test (2 cycles)...");
        for (int i = 0; i < 2; i++) {
            xl9535_write_register(XL9535_REG_OUTPUT_PORT1, 0xFF);
            vTaskDelay(pdMS_TO_TICKS(100));
            xl9535_write_register(XL9535_REG_OUTPUT_PORT1, 0x00);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "XL9535: Restoring operational state...");
        xl9535_write_register(XL9535_REG_CONFIG_PORT0, XL9535_PORT0_OUTPUT_CONFIG);
        xl9535_write_register(XL9535_REG_CONFIG_PORT1, XL9535_PORT1_OUTPUT_CONFIG);
        xl9535_write_register(XL9535_REG_OUTPUT_PORT0, XL9535_BACKLIGHT_MASK);
        xl9535_write_register(XL9535_REG_OUTPUT_PORT1, XL9535_USER_LED_MASK);

        // Verification Readback
        uint8_t cfg0 = 0, cfg1 = 0;
        if (xl9535_read_register(XL9535_REG_CONFIG_PORT0, &cfg0) == ESP_OK &&
            xl9535_read_register(XL9535_REG_CONFIG_PORT1, &cfg1) == ESP_OK) {
            ESP_LOGI(TAG, "XL9535: Verified Configs - P0:0x%02X, P1:0x%02X", cfg0, cfg1);
        }

        return true;
    };

    if (!run_test()) {
        ESP_LOGE(TAG, "XL9535: Communication FAILED. Expander likely not responding at 0x20.");
        return false;
    }

    ESP_LOGI(TAG, "XL9535: Initialization SUCCESS");
    return true;
}

static uint16_t* allocate_frame_buffer(size_t size) {
    uint16_t* buffer = (uint16_t*)heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = (uint16_t*)heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    return buffer;
}

static void update_display_dimensions() {
    if (s_lcdOrientation == MADCTL_LANDSCAPE || s_lcdOrientation == MADCTL_LANDSCAPE_FLIP) {
        s_lcdDisplayWidth = LCD_PANEL_WIDTH;
        s_lcdDisplayHeight = LCD_PANEL_HEIGHT;
    } else {
        s_lcdDisplayWidth = LCD_PANEL_HEIGHT;
        s_lcdDisplayHeight = LCD_PANEL_WIDTH;
    }
}

void display_setOrientation(uint8_t madctl) {
    s_lcdOrientation = madctl;
    update_display_dimensions();
}

bool display_init() {
    update_display_dimensions();
    size_t bufferSize = s_lcdDisplayWidth * s_lcdDisplayHeight * sizeof(uint16_t);
    s_frameBuffers[0] = allocate_frame_buffer(bufferSize);
    s_frameBuffers[1] = allocate_frame_buffer(bufferSize);
    if (!s_frameBuffers[0] || !s_frameBuffers[1]) {
        ESP_LOGE(TAG, "Failed to allocate double-buffered LCD framebuffer");
        return false;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    // Set LCD_PIN_ENABLE high early so the backlight can work during xl9535_init
    gpio_set_level(LCD_PIN_ENABLE, 1);
    gpio_set_level(LCD_PIN_DC, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num = LCD_PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = LCD_PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    // Configure bus to support strips (32KB is plenty for a 40-line strip)
    buscfg.max_transfer_sz = 32768;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %d", err);
        return false;
    }

    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(devcfg));
    // Increase SPI clock for faster LCD transfers (was 26MHz -> 40MHz)
    // Raised to 80MHz to improve throughput; ensure display wiring and panel support this.
    devcfg.clock_speed_hz = 80000000;
    devcfg.mode = 0;
    devcfg.spics_io_num = LCD_PIN_CS;
    // Allow a larger transaction queue so we can pipeline multiple DMA transfers
    devcfg.queue_size = 8;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus add device failed: %d", err);
        return false;
    }

    if (!xl9535_init()) {
        ESP_LOGW(TAG, "Backlight init failed; display may be dark.");
    }

    ili9341_init();

    memset(s_frameBuffers[0], 0, bufferSize);
    memset(s_frameBuffers[1], 0, bufferSize);

    s_drawBuffer = 0;

    // Create video task on Core 0
    xTaskCreatePinnedToCore(
        video_task,
        "video",
        4096,
        NULL,
        6, // Slightly higher priority than emulator
        &s_videoTaskHandle,
        0 // Core 0
    );

    return true;
}

uint16_t* display_getBackBuffer() {
    return s_frameBuffers[s_drawBuffer];
}

static inline uint16_t spectrum_palette(uint8_t index, bool bright) {
    if (bright) {
        return s_paletteBright[index & 0x07];
    }
    return s_paletteNormal[index & 0x07];
}

void display_renderSpectrum(SpectrumBase* spectrum) {
    if (s_frameBuffers[0] == NULL || s_frameBuffers[1] == NULL || spectrum == NULL) {
        return;
    }

    uint16_t* buffer = display_getBackBuffer();
    if (!buffer) {
        return;
    }
    // Centralized rendering: let the Spectrum object populate the RGB565 buffer
    spectrum->renderToRGB565(buffer, s_lcdDisplayWidth, s_lcdDisplayHeight);
}

static void lcd_push_frame(const uint16_t* buffer) {
    if (s_spi == NULL || buffer == NULL) {
        return;
    }

    // Synchronous transmit per strip to avoid spi driver assert when mixing queued and
    // synchronous calls. Using 6 strips balances transfer size and DMA limits.
    const int numStrips = 6;
    int linesPerStrip = s_lcdDisplayHeight / numStrips;

    for (int i = 0; i < numStrips; i++) {
        int startY = i * linesPerStrip;
        int height = (i == numStrips - 1) ? (s_lcdDisplayHeight - startY) : linesPerStrip;

        lcd_set_window(0, startY, s_lcdDisplayWidth - 1, startY + height - 1);

        gpio_set_level(LCD_PIN_DC, 1);
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = s_lcdDisplayWidth * height * 16; // bits
        t.tx_buffer = (void*)&buffer[startY * s_lcdDisplayWidth];
        t.flags = 0;

        esp_err_t err = spi_device_transmit(s_spi, &t);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to transmit LCD strip %d: %d", i, err);
            break;
        }
    }
}

void display_present() {
    if (s_frameBuffers[0] == NULL || s_frameBuffers[1] == NULL) {
        return;
    }

    // Ping-pong: presentingIndex is the buffer we just finished rendering
    int presentingIndex = s_drawBuffer;
    // Swap draw buffer so the emulator can start rendering the next frame in the other buffer
    s_drawBuffer = 1 - s_drawBuffer;

    lcd_push_frame(s_frameBuffers[presentingIndex]);

    // FPS counter: log to serial once per second
    static uint32_t frame_count = 0;
    static int64_t last_time_us = 0;
    frame_count++;
    int64_t now = esp_timer_get_time();
    if (last_time_us == 0) last_time_us = now;
    int64_t delta = now - last_time_us;
    if (delta >= 1000000) {
        double fps = (double)frame_count * 1000000.0 / (double)delta;
        ESP_LOGI(TAG, "FPS: %.2f", fps);
        frame_count = 0;
        last_time_us = now;
    }
}

void display_test_pattern() {
    ESP_LOGI(TAG, "Starting 10-second checkered test pattern...");
    for (int cycle = 0; cycle < 20; cycle++) {
        uint16_t* buffer = display_getBackBuffer();
        if (!buffer) break;

        uint16_t color1 = s_paletteNormal[cycle % 8];
        uint16_t color2 = s_paletteNormal[(cycle + 4) % 8]; // Use a distant color

        for (int y = 0; y < s_lcdDisplayHeight; y++) {
            uint16_t* line = &buffer[y * s_lcdDisplayWidth];
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                if (((x / 32) + (y / 32)) % 2 == 0) {
                    line[x] = color1;
                } else {
                    line[x] = color2;
                }
            }
        }
        display_present();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "Test pattern complete.");
}

// Blink the user LED twice via the XL9535 and show a 1-second color bar
void display_boot_test() {
    // Blink user LED twice
    for (int i = 0; i < 2; ++i) {
        xl9535_write_register(XL9535_REG_OUTPUT_PORT1, XL9535_USER_LED_MASK);
        vTaskDelay(pdMS_TO_TICKS(150));
        xl9535_write_register(XL9535_REG_OUTPUT_PORT1, 0x00);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    // Show a simple vertical color bar using normal palette
    uint16_t* buffer = display_getBackBuffer();
    if (!buffer) return;

    const int w = s_lcdDisplayWidth;
    const int h = s_lcdDisplayHeight;
    const int numColors = 8;
    int bandWidth = w / numColors;

    for (int y = 0; y < h; ++y) {
        uint16_t* line = &buffer[y * w];
        for (int c = 0; c < numColors; ++c) {
            uint16_t color = s_paletteNormal[c];
            int x0 = c * bandWidth;
            int x1 = (c == numColors - 1) ? w : x0 + bandWidth;
            for (int x = x0; x < x1; ++x) line[x] = color;
        }
    }

    display_present();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Clear back to black after test
    memset(buffer, 0, w * h * sizeof(uint16_t));
    display_present();
}