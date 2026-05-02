#include "Expander.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "Expander";

static const gpio_num_t PIN_SDA = GPIO_NUM_47;
static const gpio_num_t PIN_SCL = GPIO_NUM_48;

#define REG_INPUT_PORT0  0x00
#define REG_INPUT_PORT1  0x01
#define REG_OUTPUT_PORT0 0x02
#define REG_OUTPUT_PORT1 0x03
#define REG_CONFIG_PORT0 0x06
#define REG_CONFIG_PORT1 0x07

static uint8_t s_port0_output = 0;
static uint8_t s_port1_output = 0;

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_device = NULL;

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_i2c_device, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(s_i2c_device, &reg, 1, value, 1, 100);
}

bool expander_init() {
    ESP_LOGI(TAG, "Initializing XL9535 Expanders...");

    i2c_master_bus_config_t bus_conf = {0};
    bus_conf.i2c_port = I2C_NUM_0;
    bus_conf.sda_io_num = PIN_SDA;
    bus_conf.scl_io_num = PIN_SCL;
    bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_conf.glitch_ignore_cnt = 7;
    bus_conf.intr_priority = 0;
    bus_conf.trans_queue_depth = 4;
    bus_conf.flags.enable_internal_pullup = 1;
    bus_conf.flags.allow_pd = 0;

    esp_err_t err = i2c_new_master_bus(&bus_conf, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    i2c_device_config_t dev_conf;
    memset(&dev_conf, 0, sizeof(dev_conf));
    dev_conf.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_conf.device_address = XL9535_I2C_ADDR;
    dev_conf.scl_speed_hz = 400000;
    dev_conf.scl_wait_us = 0;

    err = i2c_master_bus_add_device(s_i2c_bus, &dev_conf, &s_i2c_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master add device failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return false;
    }

    // Configure Port 0: P00 (Backlight) as output, others as input for now
    if (write_reg(REG_CONFIG_PORT0, 0xFE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Port 0");
        return false;
    }
    // Configure Port 1: P17 (LED) as output, others as input
    if (write_reg(REG_CONFIG_PORT1, 0x7F) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Port 1");
        return false;
    }

    // Initial states
    s_port0_output = 0; // Backlight OFF
    s_port1_output = 0; // LED OFF

    write_reg(REG_OUTPUT_PORT0, s_port0_output);
    write_reg(REG_OUTPUT_PORT1, s_port1_output);

    ESP_LOGI(TAG, "XL9535 Expander initialized successfully");

    expander_blink_led(2, 100);

    return true;
}

esp_err_t expander_write_port0(uint8_t value) {
    s_port0_output = value;
    return write_reg(REG_OUTPUT_PORT0, value);
}

esp_err_t expander_write_port1(uint8_t value) {
    s_port1_output = value;
    return write_reg(REG_OUTPUT_PORT1, value);
}

esp_err_t expander_read_port0(uint8_t *value) {
    return read_reg(REG_INPUT_PORT0, value);
}

esp_err_t expander_read_port1(uint8_t *value) {
    return read_reg(REG_INPUT_PORT1, value);
}

void expander_set_backlight(bool on) {
    if (on) s_port0_output |= XL9535_P00_BACKLIGHT;
    else s_port0_output &= ~XL9535_P00_BACKLIGHT;
    write_reg(REG_OUTPUT_PORT0, s_port0_output);
}

void expander_set_led(bool on) {
    if (on) s_port1_output |= XL9535_P17_USER_LED;
    else s_port1_output &= ~XL9535_P17_USER_LED;
    write_reg(REG_OUTPUT_PORT1, s_port1_output);
}

void expander_blink_led(int count, int ms) {
    for (int i = 0; i < count; i++) {
        expander_set_led(true);
        vTaskDelay(pdMS_TO_TICKS(ms));
        expander_set_led(false);
        if (i < count - 1) vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
