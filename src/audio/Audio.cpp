#include "Audio.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "raw_stream.h"

static const char* TAG = "Audio";
static const int SAMPLE_RATE = 44100;
static const int SAMPLES_PER_FRAME = 882;

static audio_pipeline_handle_t s_pipeline = NULL;
static audio_element_handle_t s_raw_write = NULL;
static audio_element_handle_t s_i2s_writer = NULL;

static int s_volume = 70; // Increased default volume
static bool s_muted = false;

bool audio_init() {
    ESP_LOGI(TAG, "Initializing ESP-ADF audio pipeline...");

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!s_pipeline) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        return false;
    }

    // raw_stream must be AUDIO_STREAM_WRITER to accept raw_stream_write from application
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER; 
    raw_cfg.out_rb_size = 16 * 1024;
    s_raw_write = raw_stream_init(&raw_cfg);
    if (!s_raw_write) {
        ESP_LOGE(TAG, "Failed to initialize raw stream");
        return false;
    }

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.chan_cfg.id = I2S_NUM_0;
    i2s_cfg.use_alc = true;
    
    // Modern IDF I2S config (UniHiker K10 pins)
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
    i2s_cfg.std_cfg.gpio_cfg.bclk = GPIO_NUM_0;
    i2s_cfg.std_cfg.gpio_cfg.ws = GPIO_NUM_38;
    i2s_cfg.std_cfg.gpio_cfg.dout = GPIO_NUM_45;
    i2s_cfg.std_cfg.gpio_cfg.mclk = GPIO_NUM_3;

    s_i2s_writer = i2s_stream_init(&i2s_cfg);
    if (!s_i2s_writer) {
        ESP_LOGE(TAG, "Failed to initialize I2S stream");
        return false;
    }

    audio_pipeline_register(s_pipeline, s_raw_write, "raw");
    audio_pipeline_register(s_pipeline, s_i2s_writer, "i2s");

    const char *link_tag[2] = {"raw", "i2s"};
    if (audio_pipeline_link(s_pipeline, &link_tag[0], 2) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to link audio pipeline");
        return false;
    }

    // Set audio info so ALC and I2S know what they are processing
    audio_element_set_music_info(s_raw_write, SAMPLE_RATE, 2, 16);
    audio_element_set_music_info(s_i2s_writer, SAMPLE_RATE, 2, 16);

    if (audio_pipeline_run(s_pipeline) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run audio pipeline");
        return false;
    }

    audio_set_volume(s_volume);

    ESP_LOGI(TAG, "ADF Audio pipeline initialized and running (Vol=%d)", s_volume);
    return true;
}

void audio_play_frame(SpectrumBase* spectrum) {
    if (!spectrum || !s_raw_write) return;

    // Mono samples from beeper
    int16_t mono_buf[SAMPLES_PER_FRAME];
    // Stereo interleaved buffer (L,R)
    int16_t stereo_buf[SAMPLES_PER_FRAME * 2];

    // Render beeper into mono buffer
    spectrum->renderBeeperAudio(mono_buf, SAMPLES_PER_FRAME);

    // Duplicate mono to stereo (ADF ALC will handle volume)
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        stereo_buf[i * 2 + 0] = mono_buf[i];
        stereo_buf[i * 2 + 1] = mono_buf[i];
    }

    int ret = raw_stream_write(s_raw_write, (char*)stereo_buf, sizeof(stereo_buf));
    if (ret < 0) {
        ESP_LOGW(TAG, "raw_stream_write error: %d", ret);
    }
}

void audio_play_tone(int freq_hz, int duration_ms) {
    if (freq_hz <= 0 || duration_ms <= 0 || !s_raw_write) return;
    const int samp = SAMPLE_RATE;
    const int total_target = (duration_ms * samp) / 1000;
    int remaining = total_target;
    const int CHUNK = 256;
    int16_t stereo[CHUNK * 2];

    int toggle_period = samp / (freq_hz * 2); 
    int state = 0;
    int period_cnt = 0;

    int fade_samples = (samp * 8) / 1000;
    if (fade_samples * 2 > total_target) fade_samples = total_target / 2;
    const int peak_amp = 8000; 
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
            int16_t s16 = (int16_t)s;
            stereo[i * 2 + 0] = s16;
            stereo[i * 2 + 1] = s16;
            sample_idx++;
        }

        raw_stream_write(s_raw_write, (char*)stereo, n * 2 * sizeof(int16_t));
        remaining -= n;
    }
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    
    if (s_i2s_writer) {
        int db_gain;
        if (s_muted || s_volume == 0) {
            db_gain = -64;
        } else {
            // Map 1-100 to -40 to 0 dB
            db_gain = (s_volume * 40 / 100) - 40;
        }
        i2s_alc_volume_set(s_i2s_writer, db_gain);
        ESP_LOGI(TAG, "Volume set to %d (%d dB)", s_volume, db_gain);
    }
}

int audio_get_volume() {
    return s_volume;
}

void audio_set_mute(bool mute) {
    s_muted = mute;
    audio_set_volume(s_volume);
}

bool audio_get_mute() {
    return s_muted;
}
