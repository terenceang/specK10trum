#include "instrumentation/Instrumentation.h"
#include <esp_timer.h>
#include <atomic>

static std::atomic<int64_t> s_cpu_accum_us{0};
static std::atomic<uint32_t> s_cpu_frames{0};
static std::atomic<int64_t> s_video_accum_us{0};
static std::atomic<uint32_t> s_video_frames{0};

static int64_t s_cpu_start_us = 0;
static int64_t s_video_start_us = 0;

void instr_cpu_start() {
    s_cpu_start_us = esp_timer_get_time();
}

void instr_cpu_end() {
    int64_t now = esp_timer_get_time();
    int64_t delta = now - s_cpu_start_us;
    s_cpu_accum_us.fetch_add(delta);
    s_cpu_frames.fetch_add(1);
}

void instr_video_start() {
    s_video_start_us = esp_timer_get_time();
}

void instr_video_end() {
    int64_t now = esp_timer_get_time();
    int64_t delta = now - s_video_start_us;
    s_video_accum_us.fetch_add(delta);
    s_video_frames.fetch_add(1);
}

void instr_snapshot_and_reset(int64_t* out_cpu_us, uint32_t* out_cpu_frames, int64_t* out_video_us, uint32_t* out_video_frames) {
    if (out_cpu_us) *out_cpu_us = s_cpu_accum_us.exchange(0);
    if (out_cpu_frames) *out_cpu_frames = s_cpu_frames.exchange(0);
    if (out_video_us) *out_video_us = s_video_accum_us.exchange(0);
    if (out_video_frames) *out_video_frames = s_video_frames.exchange(0);
}
