#include "Expander.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "Expander";

static const i2c_port_t I2C_PORT = I2C_NUM_0;
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

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9535_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

bool expander_init() {
    ESP_LOGI(TAG, "Initializing XL9535 Expanders...");
    
    i2c_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = PIN_SDA;
    conf.scl_io_num = PIN_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed");
        return false;
    }
    
    err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed");
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
