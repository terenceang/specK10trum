#include "Audio.h"
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "Audio";
static const int SAMPLE_RATE = 44100;
// Samples per Spectrum frame: 44100 / 50 = 882.
// Using 882 ensures we stay ahead of the audio clock.
static const int SAMPLES_PER_FRAME = 882;

static i2s_chan_handle_t tx_handle = NULL;
static int s_volume = 30;
static bool s_muted = false;
static StreamBufferHandle_t s_audio_stream = NULL;
static StaticStreamBuffer_t s_audio_stream_struct;
static uint8_t* s_audio_stream_buffer = NULL;
static const size_t FRAMES_OF_BUFFER = 8; // number of frames to hold in ring

static void audio_writer_task(void* arg) {
    (void)arg;
    size_t bytes_per_frame = SAMPLES_PER_FRAME * 2 * sizeof(int16_t);
    int16_t* buf = (int16_t*)malloc(bytes_per_frame);
    if (!buf) vTaskDelete(NULL);
    for (;;) {
        size_t received = 0;
        // Ensure we always read a full frame
        while (received < bytes_per_frame) {
            size_t r = xStreamBufferReceive(s_audio_stream, ((uint8_t*)buf) + received,
                                            bytes_per_frame - received, portMAX_DELAY);
            if (r == 0) continue;
            received += r;
        }
        size_t written = 0;
        esp_err_t werr = i2s_channel_write(tx_handle, buf, bytes_per_frame, &written, portMAX_DELAY);
        if (werr != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write failed in task: %d", werr);
        }
    }
    free(buf);
    vTaskDelete(NULL);
}

bool audio_init() {
    ESP_LOGI(TAG, "Initializing modern I2S audio...");

    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = I2S_NUM_0;
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;
    
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %d", err);
        return false;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = GPIO_NUM_3;
    std_cfg.gpio_cfg.bclk = GPIO_NUM_0;
    std_cfg.gpio_cfg.ws = GPIO_NUM_38;
    std_cfg.gpio_cfg.dout = GPIO_NUM_45;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

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

    // Prefill DMA with silence so the DAC doesn't clock out uninitialised
    // descriptor contents before the first real write. Covers both the boot
    // gap before the splash beep and the gap before the emulator task starts.
    {
        // BSS, not stack — keeps the main task off an overflow path.
        static int16_t silence[512 * 2];
        size_t written = 0;
        i2s_channel_write(tx_handle, silence, sizeof(silence), &written, pdMS_TO_TICKS(100));
    }

    // Create stream buffer in PSRAM to save internal RAM.
    size_t frame_bytes = SAMPLES_PER_FRAME * 2 * sizeof(int16_t);
    size_t buffer_size = frame_bytes * FRAMES_OF_BUFFER;
    s_audio_stream_buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_stream_buffer) {
        ESP_LOGW(TAG, "Failed to allocate audio buffer in PSRAM; falling back to internal RAM");
        s_audio_stream = xStreamBufferCreate(buffer_size, frame_bytes);
    } else {
        s_audio_stream = xStreamBufferCreateStatic(buffer_size, frame_bytes, s_audio_stream_buffer, &s_audio_stream_struct);
    }

    if (!s_audio_stream) {
        ESP_LOGW(TAG, "Failed to create audio stream buffer; audio will block");
    } else {
        // Create audio task that pulls frames from stream buffer and writes to I2S.
        // Higher priority and pinned to Core 1 (separate from emulator) ensures smooth playback.
        xTaskCreatePinnedToCore(audio_writer_task, "audio_writer", 4096, NULL, 10, NULL, 1);
    }

    ESP_LOGI(TAG, "Audio initialized (Modern I2S, sample_rate=%d)", SAMPLE_RATE);
    return true;
}

void audio_play_frame(SpectrumBase* spectrum) {
    if (!spectrum || !tx_handle) return;

    // Mono samples from beeper
    int16_t mono_buf[SAMPLES_PER_FRAME];
    // Stereo interleaved buffer (L,R)
    int16_t stereo_buf[SAMPLES_PER_FRAME * 2];

    // Render beeper into mono buffer
    spectrum->renderBeeperAudio(mono_buf, SAMPLES_PER_FRAME);

    // Apply volume/mute and duplicate to stereo
    int vol = s_muted ? 0 : s_volume;
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        int32_t s = (int32_t)mono_buf[i] * vol / 100;
        int16_t s16 = (int16_t)s;
        stereo_buf[i * 2 + 0] = s16;
        stereo_buf[i * 2 + 1] = s16;
    }

    size_t bytes_to_write = SAMPLES_PER_FRAME * 2 * sizeof(int16_t);

    // If stream buffer exists, enqueue frame with blocking wait; this naturally 
    // throttles the emulator to match real-time audio playback.
    if (s_audio_stream) {
        size_t sent = xStreamBufferSend(s_audio_stream, stereo_buf, bytes_to_write, portMAX_DELAY);
        if (sent != bytes_to_write) {
            ESP_LOGW(TAG, "Audio stream timeout (sent=%d expected=%d)", (int)sent, (int)bytes_to_write);
        }
    } else {
        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, stereo_buf, bytes_to_write, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write failed: %d", err);
        }
    }
}

void audio_play_tone(int freq_hz, int duration_ms) {
    if (freq_hz <= 0 || duration_ms <= 0 || !tx_handle) return;
    const int samp = SAMPLE_RATE;
    const int total_target = (duration_ms * samp) / 1000;
    int remaining = total_target;
    const int CHUNK = 256;
    int16_t stereo[CHUNK * 2];

    int toggle_period = samp / (freq_hz * 2); // half-period in samples
    int state = 0;
    int period_cnt = 0;

    int vol = s_muted ? 0 : s_volume;

    // Linear fade at start/end avoids step-discontinuity clicks when the DAC
    // transitions between silence and the square wave.
    int fade_samples = (samp * 8) / 1000; // 8 ms
    if (fade_samples * 2 > total_target) fade_samples = total_target / 2;
    const int peak_amp = 8000; // halved from 16000 to match beeper headroom
    int sample_idx = 0;

    while (remaining > 0) {
        int n = remaining > CHUNK ? CHUNK : remaining;
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
            s = s * vol / 100;
            int16_t s16 = (int16_t)s;
            stereo[i * 2 + 0] = s16;
            stereo[i * 2 + 1] = s16;
            sample_idx++;
        }

        size_t bytes = n * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_channel_write(tx_handle, stereo, bytes, &written, portMAX_DELAY);
        remaining -= n;
    }
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    ESP_LOGI(TAG, "Volume set to %d", s_volume);
}

int audio_get_volume() {
    return s_volume;
}

void audio_set_mute(bool mute) {
    s_muted = mute;
    ESP_LOGI(TAG, "Audio %s", s_muted ? "muted" : "unmuted");
}

bool audio_get_mute() {
    return s_muted;
}
