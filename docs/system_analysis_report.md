# SpecK10trum E2E System Analysis Report

## 1. Video Pipeline (Display & ULA)

**Current Architecture:**
- Video rendering is split between the Spectrum emulator (Core 0) and a dedicated video_task pinned to Core 1.
- The ULA emulation uses an attribute Look-Up Table (LUT) for the active area with 64-bit writes, maximizing memory throughput.
- The display driver utilizes SPI DMA (80MHz) with ping-pong strip buffers in Internal RAM (IRAM) to ensure fast transfers without stalling the CPU. Framebuffers are kept in PSRAM to save internal memory.

**Reliability & Guards:**
- The video_task is strictly pinned to Core 1, preventing contention with the emulator loop on Core 0.
- SpectrumBase uses portMUX_TYPE for critical sections during border event swapping.

**Recommendations:**
- **Optimization:** Border rendering logic currently uses line-by-line loops with conditional event consumption. This could be optimized by pre-calculating border segments or using block fills for lines without events.
- **Pros:** Smoother border rendering, fewer CPU cycles wasted on Core 1.
- **Cons:** Slightly increased code complexity in the rendering loop.
- **User Impact:** More consistent frame pacing, especially in games that perform heavy border color changes.

## 2. Audio Pipeline (Beeper, PSG, I2S)

**Current Architecture:**
- Audio is processed at 44.1kHz stereo via ESP32 I2S DMA.
- Features software-based mixing for Beeper and PSG, a 2nd-order Butterworth Low-Pass Filter (LPF) at 8kHz, and a DC blocker.
- The audio buffer is synthesized in chunks inside Audio.cpp.

**Reliability & Guards:**
- The audio module tracks buffer underruns, consecutive I2S write failures, and near-clipping events via the AudioStats struct. This is robust.
- The samples are hard-clipped safely.

**Recommendations:**
- **Optimization:** The apply_master_filter function executes per-sample floating-point math in a loop. This is computationally expensive on a standard FPU.
- **Pros:** Offloads standard FPU processing to vector instructions.
- **Cons:** Requires pulling in the espressif/esp-dsp component and refactoring Audio.cpp to use DSP types.
- **User Impact:** Lower CPU utilization, allowing more headroom for complex 128K emulation or reducing thermal load.

## 3. Core Usage & Parallelization

**Current Architecture:**
- **Core 0:** Runs the main emulator_task (Z80 execution, audio synthesis, snapshot/tape loading).
- **Core 1:** Runs video_task (SPI DMA push) and ws_keepalive_task (WebSockets keepalive).
- **Unpinned (FreeRTOS Scheduled):** input_task, wifi_reconnect_task, prov_watcher.

**Reliability & Guards:**
- Cross-core communication (like the WebCommandQueue) relies on std::mutex and condition variables. This ensures thread-safe command injection from the webserver (Core 1/Unpinned) to the emulator (Core 0).

**Recommendations:**
- **Optimization:** Moving audio synthesis out of emulator_task into a dedicated audio_task (pinned to Core 1 or running at higher priority on Core 0) could prevent audio dropouts during heavy I/O operations (like loading from SPIFFS).
- **Pros:** Audio buffer remains constantly full, preventing stutter.
- **Cons:** Requires thread-safe ring buffers between the emulator and the audio task.
- **User Impact:** Uninterrupted audio even when the emulator stalls briefly to load a snapshot or save state.

## 4. Opportunity for ESP32-S3 Vector Instructions (ESP-DSP)

**Findings:**
The ESP32-S3 features vector instructions (PIE) that drastically accelerate math-heavy arrays. 
Currently, specK10trum performs audio mixing and filtering manually.

**Recommendations:**
1. **Biquad Filters:** Replace the Butterworth LPF and DC blocker in Audio.cpp with dsps_biquad_f32_ae32(). 
2. **Gain/Mixing:** Replace the manual volume scaling with dsps_mulc_f32_ae32() (multiply by constant) and dsps_add_f32_ae32() for combining PSG and Beeper tracks.

**Pros:** Operations that took hundreds of cycles per sample can be executed in parallel, reducing audio overhead by roughly 40-60%.
**Cons:** The audio buffers currently use int16_t. ESP-DSP works best with float arrays, requiring a conversion step before processing, and back to int16_t after.

## 5. WiFi & BLE Pipeline

**Current Architecture:**
- Implemented as an event-driven architecture using esp_netif and FreeRTOS event groups.
- A dedicated wifi_reconnect_task implements exponential backoff to avoid spamming the router.
- A nvs_watcher_task monitors provisioning credentials. 
- Blocking behavior during the initial Wi-Fi connection phase is intentional to ensure the web UI isn't started before the IP is acquired.

**Reliability & Guards:**
- Excellent use of event groups to synchronize tasks.
- Network tasks do not hold spinlocks, relying on proper RTOS delays.

**Recommendations:**
- **Improvement:** Ensure that high-priority network interrupts (which usually run on Core 0) do not preempt the Z80 emulator for too long. 
- **Pros:** Stabilizes emulation speed during heavy network traffic.
- **Cons:** None, standard RTOS tuning.
- **User Impact:** The emulation won't glitch or slow down when another user connects to the web interface or uploads a large tape file.

## 6. General Optimizations & Guards Check

- **Memory Placement:** The project correctly uses MALLOC_CAP_SPIRAM for large framebuffers and MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA for the I2S and SPI buffers. 
- **Null Checks:** Most allocations appear to be checked, but ensure that any new operator calls on large emulator objects (like Spectrum128K) check for std::bad_alloc or null, especially since PSRAM fragmentation can cause allocation failures over time.
- **Recommendation:** Implement a global memory monitoring task that logs free heap and minimum free heap (both internal and SPIRAM) every minute. This helps diagnose memory leaks during long gaming sessions.
