// Simple instrumentation helper for measuring CPU and Video times per-frame
#pragma once
#include <stdint.h>

// Call around CPU/emulator work for each frame
void instr_cpu_start();
void instr_cpu_end();

// Call around video rendering/present work for each frame
void instr_video_start();
void instr_video_end();

// Snapshot and reset accumulators (returns microseconds and frame counts)
void instr_snapshot_and_reset(int64_t* out_cpu_us, uint32_t* out_cpu_frames, int64_t* out_video_us, uint32_t* out_video_frames);
