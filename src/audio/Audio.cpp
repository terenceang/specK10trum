#include "Audio.h"
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "Audio";
static const int SAMPLE_RATE = 44100;
// Samples per Spectrum frame ~= 69888 / (3.5MHz/44100) ~= 880
static const int SAMPLES_PER_FRAME = 880;

static i2s_chan_handle_t tx_handle = NULL;

bool audio_init() {
    ESP_LOGI(TAG, "Initializing modern I2S audio...");

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 256,
        .auto_clear = true
    };
    
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %d", err);
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_3,
            .bclk = GPIO_NUM_0,
            .ws = GPIO_NUM_38,
            .dout = GPIO_NUM_45,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %d", err);
        return false;
    }

    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %d", err);
        return false;
    }

    ESP_LOGI(TAG, "Audio initialized (Modern I2S, sample_rate=%d)", SAMPLE_RATE);
    return true;
}

void audio_play_frame(SpectrumBase* spectrum) {
    if (!spectrum || !tx_handle) return;

    // Mono samples from beeper
    static int16_t mono_buf[SAMPLES_PER_FRAME];
    // Stereo interleaved buffer (L,R)
    static int16_t stereo_buf[SAMPLES_PER_FRAME * 2];

    // Render beeper into mono buffer
    spectrum->renderBeeperAudio(mono_buf, SAMPLES_PER_FRAME);

    // Duplicate to stereo
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        int16_t s = mono_buf[i];
        stereo_buf[i * 2 + 0] = s;
        stereo_buf[i * 2 + 1] = s;
    }

    size_t bytes_to_write = SAMPLES_PER_FRAME * 2 * sizeof(int16_t);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx_handle, stereo_buf, bytes_to_write, &written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_write failed: %d", err);
    }
}

void audio_play_tone(int freq_hz, int duration_ms) {
    if (freq_hz <= 0 || duration_ms <= 0 || !tx_handle) return;
    const int samp = SAMPLE_RATE;
    int total_samples = (duration_ms * samp) / 1000;
    const int CHUNK = 256;
    int16_t mono[CHUNK];
    int16_t stereo[CHUNK * 2];

    int toggle_period = samp / (freq_hz * 2); // half-period in samples
    int state = 0;
    int period_cnt = 0;

    while (total_samples > 0) {
        int n = total_samples > CHUNK ? CHUNK : total_samples;
        for (int i = 0; i < n; ++i) {
            if (period_cnt >= toggle_period) {
                state = 1 - state;
                period_cnt = 0;
            }
            period_cnt++;
            mono[i] = state ? 16000 : -16000;
            stereo[i * 2 + 0] = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }

        size_t bytes = n * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_channel_write(tx_handle, stereo, bytes, &written, portMAX_DELAY);
        total_samples -= n;
    }
}
