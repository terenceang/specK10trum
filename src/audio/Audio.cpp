
#include "Audio.h"
#include <esp_log.h>
#include <driver/i2s_common.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <string.h>
#include <cmath>
#include "../../components/audio_stream/include/board_pins_config.h"
#include "esp_dsp.h"
#include "test_config.h"

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
static float s_volume_gain = 0.40f;  
static bool s_muted = false;

// Mixer constants (scaled to provide headroom and prevent early clipping)
static constexpr float BEEPER_MIX_GAIN = 0.70f;
static constexpr float PSG_MIX_GAIN    = 0.60f;

// Static audio buffers to avoid large stack allocations
static int16_t s_frame_buffer[SAMPLES_PER_FRAME * 2];  // Stereo interleaved
static int16_t s_beeper_buf[SAMPLES_PER_FRAME];
static int16_t s_psg_buf[SAMPLES_PER_FRAME];

// Vectorized mixing buffers (float space for ESP-DSP operations)
static float s_beeper_float[SAMPLES_PER_FRAME];
static float s_psg_float[SAMPLES_PER_FRAME];
static float s_mixed_float[SAMPLES_PER_FRAME];

// I2S write statistics (errors only)
static int s_consecutive_failures = 0;


// ESP-DSP biquad filter state and coefficients
static float s_bq_coeffs_dc[5];
static float s_bq_coeffs_lpf[5];
static float s_bq_state_dc[4] = {0};
static float s_bq_state_lpf[4] = {0};

static void reset_master_filter(void) {
    memset(s_bq_state_dc, 0, sizeof(s_bq_state_dc));
    memset(s_bq_state_lpf, 0, sizeof(s_bq_state_lpf));
}

static void apply_master_filter(int16_t* stereo_buf, int samples) {
    // Convert int16_t to float
    int count = samples * 2;
    static float fbuf[2 * 1024]; // supports up to 1024 stereo frames
    float* buf = fbuf;
    if (count > 2 * 1024) return; // safety
    for (int i = 0; i < count; ++i) buf[i] = (float)stereo_buf[i];

    // DC blocker (high-pass ~15 Hz)
    dsps_biquad_f32_ae32(buf, buf, count, s_bq_coeffs_dc, s_bq_state_dc);
    // LPF (8 kHz)
    dsps_biquad_f32_ae32(buf, buf, count, s_bq_coeffs_lpf, s_bq_state_lpf);

    // Convert back to int16_t with saturation
    for (int i = 0; i < count; ++i) {
        float y = buf[i];
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        stereo_buf[i] = (int16_t)y;
    }
}

bool audio_init() {
    ESP_LOGD(TAG, "Initializing ESP-IDF I2S DMA audio...");

    if (s_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return true;
    }

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

    // Initialize ESP-DSP biquad coefficients for DC blocker and LPF
    // DC blocker: high-pass, cutoff ~15 Hz
    dsps_biquad_gen_hpf_f32(s_bq_coeffs_dc, 15.0f / (float)SAMPLE_RATE, sqrtf(0.5f));
    // LPF: cutoff 8 kHz
    dsps_biquad_gen_lpf_f32(s_bq_coeffs_lpf, 8000.0f / (float)SAMPLE_RATE, sqrtf(0.5f));
    reset_master_filter();
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

    ESP_LOGD(TAG, "I2S pins - MCLK:%d BCLK:%d WS:%d DOUT:%d",
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
    ESP_LOGD(TAG, "I2S channel enabled successfully");

    s_initialized = true;
    ESP_LOGD(TAG, "I2S DMA audio initialized at %d Hz, volume=%d%%, frame size=%d samples",
             SAMPLE_RATE, (int)(s_volume_gain * 100), SAMPLES_PER_FRAME);
    return true;
}

static void mix_audio_vectorized(const int16_t* beeper, const int16_t* psg,
                                 int16_t* out_mono, int samples) {
    // Convert int16_t to float for vectorized processing
    for (int i = 0; i < samples; ++i) {
        s_beeper_float[i] = (float)beeper[i];
        s_psg_float[i] = (float)psg[i];
    }

    // Scale beeper with ESP-DSP (in-place): beeper_float *= BEEPER_MIX_GAIN
    dsps_mulc_f32_ae32(s_beeper_float, s_beeper_float, samples, BEEPER_MIX_GAIN, 1, 1);

    // Scale PSG with ESP-DSP (in-place): psg_float *= PSG_MIX_GAIN
    dsps_mulc_f32_ae32(s_psg_float, s_psg_float, samples, PSG_MIX_GAIN, 1, 1);

    // Add scaled signals with ESP-DSP: mixed = beeper_float + psg_float
    dsps_add_f32_ae32(s_beeper_float, s_psg_float, s_mixed_float, samples, 1, 1, 1);

    // Convert back to int16_t with saturation
    for (int i = 0; i < samples; ++i) {
        float y = s_mixed_float[i];
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        out_mono[i] = (int16_t)y;
    }
}

static void apply_volume_gain_vectorized(int16_t* stereo_buf, int samples, float gain) {
    if (gain >= 0.99f) return;

    int count = samples * 2;
    static float fbuf[2 * 1024]; // Supports up to 1024 stereo frames
    float* buf = fbuf;
    if (count > 2 * 1024) return; // Safety check

    // Convert int16_t to float
    for (int i = 0; i < count; ++i) {
        buf[i] = (float)stereo_buf[i];
    }

    // Vectorized multiply by constant: buf *= gain
    dsps_mulc_f32_ae32(buf, buf, count, gain, 1, 1);

    // Convert back to int16_t with saturation
    for (int i = 0; i < count; ++i) {
        float y = buf[i];
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        stereo_buf[i] = (int16_t)y;
    }
}

void audio_render_frame(SpectrumBase* spectrum) {
    if (!spectrum) return;

    int16_t* stereo_buf = s_frame_buffer;

    // Render both audio sources
    spectrum->renderBeeperAudio(s_beeper_buf, SAMPLES_PER_FRAME);
    spectrum->renderPSGAudio(s_psg_buf, SAMPLES_PER_FRAME);

    // Mix audio sources using vectorized ESP-DSP operations
    // This replaces the scalar mixing loop with SIMD-optimized operations
    int16_t mono_buf[SAMPLES_PER_FRAME];
    mix_audio_vectorized(s_beeper_buf, s_psg_buf, mono_buf, SAMPLES_PER_FRAME);

    // Duplicate mono to stereo
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
        stereo_buf[i * 2 + 0] = mono_buf[i];
        stereo_buf[i * 2 + 1] = mono_buf[i];
    }

    // Apply master audio filter (LPF + DC blocker)
    apply_master_filter(stereo_buf, SAMPLES_PER_FRAME);

    // Apply volume gain and mute
    if (s_muted) {
        memset(stereo_buf, 0, SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
    } else {
        apply_volume_gain_vectorized(stereo_buf, SAMPLES_PER_FRAME, s_volume_gain);
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
}

void audio_write_frame() {
    if (!s_tx_handle) return;

    int16_t* stereo_buf = s_frame_buffer;

    const size_t total_bytes = SAMPLES_PER_FRAME * 2 * sizeof(int16_t);
    size_t offset = 0;
    bool timed_out = false;
    bool had_error = false;

    // Drain the full frame to DMA; partial writes are retried so we do not
    // drop tail samples under transient scheduler pressure.
    while (offset < total_bytes) {
        size_t chunk_written = 0;
        esp_err_t ret = i2s_channel_write(
            s_tx_handle,
            ((const uint8_t*)stereo_buf) + offset,
            total_bytes - offset,
            &chunk_written,
            pdMS_TO_TICKS(25));

        if (ret == ESP_OK && chunk_written > 0) {
            offset += chunk_written;
            continue;
        }

        if (ret == ESP_ERR_TIMEOUT || chunk_written == 0) {
            timed_out = true;
        } else {
            had_error = true;
            if (s_consecutive_failures == 0) {
                ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
            }
        }
        break;
    }

    if (offset == total_bytes) {
        s_stats.successful_writes++;
        s_stats.bytes_written += (uint32_t)total_bytes;
        s_consecutive_failures = 0;
    } else {
        if (timed_out) {
            s_stats.timeout_writes++;
        }
        if (had_error) {
            s_stats.error_writes++;
        }
        s_consecutive_failures++;
    }

#if AUDIO_DIAGNOSTICS_DEBUG
    // Periodic diagnostics logging (every 5 seconds)
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - s_stats.last_log_ms > 5000) {
        if (s_stats.successful_writes > 0 || s_stats.timeout_writes > 0 || s_stats.error_writes > 0) {
            ESP_LOGD(TAG, "Stats: OK:%u TO:%u ERR:%u Peak:%u NearClip:%u Bytes:%u",
                     s_stats.successful_writes, s_stats.timeout_writes, s_stats.error_writes,
                     s_stats.max_peak_sample, s_stats.near_clip_frames, s_stats.bytes_written);
        }
        s_stats.last_log_ms = now_ms;
        s_stats.max_peak_sample = 0;
        s_stats.near_clip_frames = 0;
    }
#endif
}

void audio_play_frame(SpectrumBase* spectrum) {
    audio_render_frame(spectrum);
    audio_write_frame();
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
        apply_volume_gain_vectorized(stereo, n, s_volume_gain);

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
    ESP_LOGD(TAG, "Volume set to %d%%", volume);
}

int audio_get_volume() {
    return (int)(s_volume_gain * 100);
}

void audio_set_mute(bool mute) {
    s_muted = mute;
    ESP_LOGD(TAG, "Audio %s", mute ? "muted" : "unmuted");
}

bool audio_get_mute() {
    return s_muted;
}
