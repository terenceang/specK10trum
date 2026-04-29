#include "Audio.h"
#include <esp_log.h>
#include <driver/i2s_common.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <stdlib.h>
#include <string.h>
#include "../../components/audio_stream/include/board_pins_config.h"

static const char* TAG = "Audio";
static const int SAMPLE_RATE = 44100;
static const int I2S_PORT = I2S_NUM_0;
#define SAMPLES_PER_FRAME SpectrumBase::SAMPLES_PER_FRAME

// I2S handle
static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_initialized = false;

// Software volume control (0-100, stored as gain in 0.0-1.0 range)
static float s_volume_gain = 0.7f;  // 70% default
static bool s_muted = false;

// Frame queue for async audio output
static QueueHandle_t s_frame_queue = NULL;
static const int FRAME_QUEUE_SIZE = 4;

// Audio frame structure
typedef struct {
    int16_t samples[SAMPLES_PER_FRAME * 2];  // Stereo interleaved
} audio_frame_t;

static void audio_output_task(void* arg) {
    audio_frame_t frame;

    while (s_tx_handle) {
        if (xQueueReceive(s_frame_queue, &frame, pdMS_TO_TICKS(100))) {
            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(s_tx_handle, &frame.samples,
                                              sizeof(frame.samples),
                                              &bytes_written,
                                              pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "i2s_channel_write failed: %d", ret);
            }
        }
    }
    vTaskDelete(NULL);
}

bool audio_init() {
    ESP_LOGI(TAG, "Initializing ESP-IDF I2S DMA audio...");

    if (s_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return true;
    }

    // Configure I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %d", ret);
        return false;
    }

    // Configure standard mode clock
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);

    // Configure standard mode slot (Philips/I2S format, stereo)
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    // Get I2S pin configuration from board config
    board_i2s_pin_t i2s_pins;
    if (get_i2s_pins(0, &i2s_pins) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get I2S pin configuration");
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return false;
    }

    // Configure GPIO pins
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = i2s_pins.mclk,
        .bclk = i2s_pins.bclk,
        .ws = i2s_pins.ws,
        .dout = i2s_pins.dout,
        .din = GPIO_NUM_NC,
        .invert_flags = {0},
    };

    // Combine into std config
    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %d", ret);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return false;
    }

    // Enable I2S channel
    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %d", ret);
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return false;
    }

    // Create frame queue and output task
    s_frame_queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(audio_frame_t));
    if (!s_frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return false;
    }

    // Start audio output task (needs large stack for frame buffer)
    if (xTaskCreate(audio_output_task, "audio_out", 8192, NULL, 10, NULL) == pdFAIL) {
        ESP_LOGE(TAG, "Failed to create audio output task");
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2S DMA audio initialized at %d Hz, volume=%d%%",
             SAMPLE_RATE, (int)(s_volume_gain * 100));
    return true;
}

static void apply_volume_gain(int16_t* stereo_buf, int samples, float gain) {
    if (gain >= 0.99f) return;  // No gain needed

    for (int i = 0; i < samples * 2; ++i) {
        int32_t s = (int32_t)stereo_buf[i];
        s = (int32_t)(s * gain);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        stereo_buf[i] = (int16_t)s;
    }
}

void audio_play_frame(SpectrumBase* spectrum) {
    if (!spectrum || !s_tx_handle || !s_frame_queue) return;

    audio_frame_t frame;
    int16_t* stereo_buf = frame.samples;

    // Buffers for beeper and PSG
    int16_t beeper_buf[SAMPLES_PER_FRAME];
    int16_t psg_buf[SAMPLES_PER_FRAME];

    // Render both audio sources
    spectrum->renderBeeperAudio(beeper_buf, SAMPLES_PER_FRAME);
    spectrum->renderPSGAudio(psg_buf, SAMPLES_PER_FRAME);

    // Mix and duplicate mono to stereo
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        // Simple additive mixing with saturation
        int32_t mixed = (int32_t)beeper_buf[i] + (int32_t)psg_buf[i];
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;

        int16_t s16 = (int16_t)mixed;
        stereo_buf[i * 2 + 0] = s16;
        stereo_buf[i * 2 + 1] = s16;
    }

    // Apply volume gain and mute
    if (s_muted) {
        memset(stereo_buf, 0, sizeof(frame.samples));
    } else {
        apply_volume_gain(stereo_buf, SAMPLES_PER_FRAME, s_volume_gain);
    }

    // Queue frame for output (non-blocking, drop if queue is full)
    xQueueSendToBack(s_frame_queue, &frame, 0);
}

void audio_play_tone(int freq_hz, int duration_ms) {
    if (freq_hz <= 0 || duration_ms <= 0 || !s_tx_handle) return;

    const int samp = SAMPLE_RATE;
    const int total_target = (duration_ms * samp) / 1000;
    int remaining = total_target;
    const int CHUNK = 256;

    int toggle_period = samp / (freq_hz * 2);
    int state = 0;
    int period_cnt = 0;

    int fade_samples = (samp * 8) / 1000;
    if (fade_samples * 2 > total_target) fade_samples = total_target / 2;
    const int peak_amp = 8000;
    int sample_idx = 0;

    while (remaining > 0) {
        int n = remaining > CHUNK ? CHUNK : remaining;

        audio_frame_t frame;
        int16_t* stereo = frame.samples;

        for (int i = 0; i < n; ++i) {
            if (period_cnt >= toggle_period) {
                state = 1 - state;
                period_cnt = 0;
            }
            period_cnt++;

            int amp = peak_amp;
            if (fade_samples > 0) {
                int from_end = total_target - 1 - sample_idx;
                if (sample_idx < fade_samples) {
                    amp = (peak_amp * sample_idx) / fade_samples;
                } else if (from_end < fade_samples) {
                    if (from_end < 0) from_end = 0;
                    amp = (peak_amp * from_end) / fade_samples;
                }
            }

            int32_t s = state ? amp : -amp;
            int16_t s16 = (int16_t)s;
            stereo[i * 2 + 0] = s16;
            stereo[i * 2 + 1] = s16;
            sample_idx++;
        }

        // Pad remaining if this is the last chunk
        if (n < CHUNK) {
            memset(stereo + n * 2, 0, (CHUNK - n) * 2 * sizeof(int16_t));
        }

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_handle, stereo, n * 2 * sizeof(int16_t),
                         &bytes_written, pdMS_TO_TICKS(100));
        remaining -= n;
    }
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    s_volume_gain = volume / 100.0f;
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
}

int audio_get_volume() {
    return (int)(s_volume_gain * 100);
}

void audio_set_mute(bool mute) {
    s_muted = mute;
    ESP_LOGI(TAG, "Audio %s", mute ? "muted" : "unmuted");
}

bool audio_get_mute() {
    return s_muted;
}
