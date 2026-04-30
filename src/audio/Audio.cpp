#include "Audio.h"
#include <esp_log.h>
#include <driver/i2s_common.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <string.h>
#include "../../components/audio_stream/include/board_pins_config.h"

static const char* TAG = "Audio";
static const int SAMPLE_RATE = 44100;
static const int I2S_PORT = I2S_NUM_0;
#define SAMPLES_PER_FRAME SpectrumBase::SAMPLES_PER_FRAME

// Audio diagnostics
struct AudioStats {
    uint32_t successful_writes;
    uint32_t timeout_writes;
    uint32_t error_writes;
    uint32_t bytes_written;
    uint32_t max_peak_sample;
    uint32_t near_clip_frames;  // frames whose peak exceeded NEAR_CLIP_THRESHOLD
    int64_t  last_log_ms;
};
static AudioStats s_stats = {0};

// Frames hotter than this are considered "near clipping" and counted for
// telemetry. Threshold is ~92% of int16 range, well above typical content
// peaks but below the hard saturation point.
static constexpr uint32_t NEAR_CLIP_THRESHOLD = 30000;

// I2S handle
static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_initialized = false;

// Software volume control (0-100, stored as gain in 0.0-1.0 range)
// Calibration: 25% is a more usable default than 5%
static float s_volume_gain = 0.25f;  
static bool s_muted = false;

// Mixer constants (scaled to provide headroom and prevent early clipping)
static constexpr float BEEPER_MIX_GAIN = 0.70f;
static constexpr float PSG_MIX_GAIN    = 0.60f;

// Static audio buffers to avoid large stack allocations
static int16_t s_frame_buffer[SAMPLES_PER_FRAME * 2];  // Stereo interleaved
static int16_t s_beeper_buf[SAMPLES_PER_FRAME];
static int16_t s_psg_buf[SAMPLES_PER_FRAME];

// I2S write statistics (errors only)
static int s_consecutive_failures = 0;

// Master audio filter state (Butterworth 2nd-order LPF @ 8kHz + DC blocker)
static float s_lp_x1 = 0, s_lp_x2 = 0;
static float s_lp_y1 = 0, s_lp_y2 = 0;
static float s_dc_lastX = 0, s_dc_lastY = 0;

static void reset_master_filter(void) {
    s_lp_x1 = s_lp_x2 = 0;
    s_lp_y1 = s_lp_y2 = 0;
    s_dc_lastX = s_dc_lastY = 0;
}

static void apply_master_filter(int16_t* stereo_buf, int samples) {
    const float b0 = 0.1804f, b1 = 0.3608f, b2 = 0.1804f;
    const float a1 = -0.4932f, a2 = 0.2149f;

    for (int i = 0; i < samples * 2; ++i) {
        float x = stereo_buf[i];
        float lp = b0 * x + b1 * s_lp_x1 + b2 * s_lp_x2 - a1 * s_lp_y1 - a2 * s_lp_y2;
        s_lp_x2 = s_lp_x1;
        s_lp_x1 = x;
        s_lp_y2 = s_lp_y1;
        s_lp_y1 = lp;

        float y = lp - s_dc_lastX + 0.995f * s_dc_lastY;
        s_dc_lastX = lp;
        s_dc_lastY = y;

        stereo_buf[i] = (int16_t)((y > 32767.0f) ? 32767 : (y < -32768.0f) ? -32768 : (int16_t)y);
    }
}

bool audio_init() {
    ESP_LOGI(TAG, "Initializing ESP-IDF I2S DMA audio...");

    if (s_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return true;
    }

    // TODO: Board-specific power management.
    // The Unihiker (NS4150 amplifier) may require explicit GPIO initialization 
    // to enable the speaker or set the gain mode. On ESP32-S3 boards, this 
    // is often GPIO 12 or 48. Since the exact pin for power-enable is not 
    // confirmed in this codebase, we proceed with standard I2S init.

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

    ESP_LOGI(TAG, "I2S pins - MCLK:%d BCLK:%d WS:%d DOUT:%d",
             i2s_pins.mclk, i2s_pins.bclk, i2s_pins.ws, i2s_pins.dout);

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
    ESP_LOGI(TAG, "I2S channel enabled successfully");

    s_initialized = true;
    reset_master_filter();
    ESP_LOGI(TAG, "I2S DMA audio initialized at %d Hz, volume=%d%%, frame size=%d samples",
             SAMPLE_RATE, (int)(s_volume_gain * 100), SAMPLES_PER_FRAME);
    return true;
}

static void apply_volume_gain(int16_t* stereo_buf, int samples, float gain) {
    if (gain >= 0.99f) return;

    for (int i = 0; i < samples * 2; ++i) {
        int32_t s = (int32_t)(stereo_buf[i] * gain);
        stereo_buf[i] = (int16_t)((s > 32767) ? 32767 : (s < -32768) ? -32768 : s);
    }
}

void audio_play_frame(SpectrumBase* spectrum) {
    if (!spectrum || !s_tx_handle) return;

    int16_t* stereo_buf = s_frame_buffer;

    // Render both audio sources
    spectrum->renderBeeperAudio(s_beeper_buf, SAMPLES_PER_FRAME);
    spectrum->renderPSGAudio(s_psg_buf, SAMPLES_PER_FRAME);

    // Mix and duplicate mono to stereo
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        // Scaled additive mixing to provide headroom and prevent early clipping.
        // Formula: y = (Beeper * BEEPER_MIX_GAIN) + (PSG * PSG_MIX_GAIN)
        float b = (float)s_beeper_buf[i] * BEEPER_MIX_GAIN;
        float p = (float)s_psg_buf[i] * PSG_MIX_GAIN;
        float mixed = b + p;

        // Hard saturation to clamp final mix to 16-bit range
        if (mixed > 32767.0f) mixed = 32767.0f;
        else if (mixed < -32768.0f) mixed = -32768.0f;

        int16_t s16 = (int16_t)mixed;
        stereo_buf[i * 2 + 0] = s16;
        stereo_buf[i * 2 + 1] = s16;
    }

    // Apply master audio filter (LPF + DC blocker)
    apply_master_filter(stereo_buf, SAMPLES_PER_FRAME);

    // Apply volume gain and mute
    if (s_muted) {
        memset(stereo_buf, 0, SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
    } else {
        apply_volume_gain(stereo_buf, SAMPLES_PER_FRAME, s_volume_gain);
    }

    // Diagnostic peak tracking: Measure the final output actually sent to I2S.
    // Use 32-bit math to safely handle -32768.
    uint32_t frame_max = 0;
    for (int i = 0; i < SAMPLES_PER_FRAME * 2; i++) {
        int32_t s32 = (int32_t)stereo_buf[i];
        uint32_t abs_s = (s32 < 0) ? (uint32_t)-s32 : (uint32_t)s32;
        if (abs_s > frame_max) frame_max = abs_s;
    }
    if (frame_max > s_stats.max_peak_sample) s_stats.max_peak_sample = frame_max;
    if (frame_max > NEAR_CLIP_THRESHOLD) s_stats.near_clip_frames++;

    // Write to I2S with short timeout (non-blocking mode)
    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(s_tx_handle, stereo_buf,
                                      SAMPLES_PER_FRAME * 2 * sizeof(int16_t),
                                      &bytes_written, pdMS_TO_TICKS(10));

    if (ret == ESP_OK) {
        s_stats.successful_writes++;
        s_stats.bytes_written += bytes_written;
        s_consecutive_failures = 0;
    } else {
        if (ret == ESP_ERR_TIMEOUT) {
            s_stats.timeout_writes++;
        } else {
            s_stats.error_writes++;
            if (s_consecutive_failures == 0) {
                ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
            }
        }
        s_consecutive_failures++;
    }

    // Periodic diagnostics logging (every 5 seconds)
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - s_stats.last_log_ms > 5000) {
        if (s_stats.successful_writes > 0 || s_stats.timeout_writes > 0 || s_stats.error_writes > 0) {
            ESP_LOGI(TAG, "Stats: OK:%u TO:%u ERR:%u Peak:%u NearClip:%u Bytes:%u",
                     s_stats.successful_writes, s_stats.timeout_writes, s_stats.error_writes,
                     s_stats.max_peak_sample, s_stats.near_clip_frames, s_stats.bytes_written);
        }
        s_stats.last_log_ms = now_ms;
        s_stats.max_peak_sample = 0;
        s_stats.near_clip_frames = 0;
    }
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
    const int peak_amp = 3276;
    int sample_idx = 0;

    while (remaining > 0) {
        int n = remaining > CHUNK ? CHUNK : remaining;
        int16_t* stereo = s_frame_buffer;

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

        // Apply master audio filter
        apply_master_filter(stereo, n);

        // Apply volume gain to boot tone
        apply_volume_gain(stereo, n, s_volume_gain);

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_handle, stereo, n * 2 * sizeof(int16_t),
                         &bytes_written, pdMS_TO_TICKS(100));
        remaining -= n;
    }
    reset_master_filter();
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
