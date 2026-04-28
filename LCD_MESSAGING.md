# LCD Display & Messaging System

## Overview

The LCD display is updated asynchronously via a dedicated **video task** running on Core 1. Messages are displayed as a centralized overlay at the bottom of the screen.

## Display Update Flow

### 1. **Emulator Task** (Core 0, `src/main.cpp:92-119`)
```cpp
while (1) {
    webserver_apply_pending(spectrum);
    
    // CPU emulation
    instr_cpu_start();
    while (tStates < T_STATES_PER_FRAME) {
        tStates += spectrum->step();
    }
    instr_cpu_end();
    
    // Trigger video frame render
    display_trigger_frame(spectrum);
    
    // Block on audio (I2S buffer full = real-time throttle)
    audio_play_frame(spectrum);
    
    vTaskDelay(1);
}
```

### 2. **display_trigger_frame()** (src/display/Display.cpp:169)
```cpp
void display_trigger_frame(SpectrumBase* spectrum) {
    s_pendingSpectrum = spectrum;
    if (s_videoTaskHandle) {
        xTaskNotifyGive(s_videoTaskHandle);  // Wake video task
    }
}
```

### 3. **Video Task** (Core 1, src/display/Display.cpp:151)
```cpp
static void video_task(void* pvParameters) {
    while (1) {
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            if (s_pause_video) {
                xSemaphoreGive(s_video_pause_semaphore);
                continue;
            }
            if (s_pendingSpectrum) {
                instr_video_start();
                display_renderSpectrum(s_pendingSpectrum);  // Render Spectrum to buffer
                display_present();                          // Push to LCD + draw overlay
                instr_video_end();
            }
        }
    }
}
```

### 4. **display_present()** (src/display/Display.cpp:349)
```cpp
void display_present() {
    int presentingIndex = s_drawBuffer;
    s_drawBuffer = 1 - s_drawBuffer;  // Swap back buffer
    
    uint16_t* buf = s_frameBuffers[presentingIndex];
    
    // Clear overlay strip if needed
    int overlay_y = s_lcdDisplayHeight - 10;
    if (s_overlay_clear_frames > 0) {
        // Clear bottom 10 pixels
        for (int y = overlay_y; y < s_lcdDisplayHeight; y++) {
            for (int x = 0; x < s_lcdDisplayWidth; x++) {
                buf[y * s_lcdDisplayWidth + x] = 0x0000;
            }
        }
        s_overlay_clear_frames--;
    }
    
    // Draw overlay text at bottom
    if (s_overlayText[0]) {
        drawText(buf, 2, overlay_y, s_overlayText, s_overlayColor);
    }
    
    // Send frame to LCD via SPI (asynchronous, ping-pong DMA)
    lcd_push_frame_async_pingpong(buf);
}
```

## Centralized Messaging System

### Boot Messages (`src/main.cpp`)
**Function**: `display_boot_log_add(const char* message)`

Used throughout boot sequence to show status:
```cpp
display_boot_log_add("Boot: Display ready");
display_boot_log_add("Wi-Fi: Connecting...");
display_boot_log_add("Wi-Fi: Connected!");
display_boot_log_add("Boot: Emulator ready");
```

### Runtime Messages (`src/main.cpp`, `src/wifi_prov/wifi_prov.cpp`)
**Function**: `display_setOverlayText(const char* text, uint16_t color)`

Used to show status with color control:
```cpp
display_setOverlayText("Webserver ready", 0x07E0);      // Green
display_setOverlayText("Webserver failed", 0xF800);     // Red
display_setOverlayText(s_last_ip_str, 0xFFFF);          // White
display_setOverlayText("No Wi-Fi creds. Start BLE prov.", 0xF800); // Red
```

### Message Storage (`src/display/Display.cpp`)
```cpp
static char s_overlayText[128];      // Current overlay text
static uint16_t s_overlayColor;      // Overlay color (RGB565, byte-swapped)
static SemaphoreHandle_t s_overlay_mutex;  // Thread-safe access
```

## Architecture

### Synchronization
- **Overlay updates**: Protected by mutex (`s_overlay_mutex`)
- **Frame presents**: Video task notified via `xTaskNotifyGive()`
- **Pause/resume**: Used for reset operations via semaphore

### Frame Buffering
- **Double buffering**: Two 320×240×16-bit RGB565 buffers
- **Ping-pong DMA**: While one frame sends, CPU renders the next
- **Strip-based**: Frames sent in horizontal strips (8 lines each)

### Color Format
- **RGB565** in native byte order, then **byte-swapped** for SPI transmission
- Example: `0x07E0` (green) → `__builtin_bswap16(0x07E0)` → sent to LCD

## Message Lifecycle

1. **Boot phase**: `display_boot_log_add()` → `display_setOverlayText(text, white)`
2. **Emulator running**: Overlay redrawn every frame
3. **Status change**: Call `display_setOverlayText(new_text, color)` again
4. **Clear**: `display_setOverlayText(NULL, 0)` or `display_clearOverlay()`

## Key Notes

- **Overlay height**: Bottom 10 pixels of 240-pixel display
- **Overlay updates**: Non-blocking; take effect at next `display_present()`
- **Thread-safe**: Overlay mutex prevents corruption during updates
- **Performance**: Overlay drawing happens during frame push; minimal CPU overhead
- **Colors**: Standard RGB565; use macro-friendly hex values
  - `0xFFFF` = white
  - `0x07E0` = green
  - `0xF800` = red
  - `0x0000` = black

## Example Usage

```cpp
// Boot message
if (display_ready) display_boot_log_add("Waiting for keyboard...");

// Runtime status with color
if (webserver_is_running()) {
    display_setOverlayText("Webserver ready", 0x07E0);  // Green
} else {
    display_setOverlayText("Webserver failed", 0xF800); // Red
}

// Clear overlay
display_clearOverlay();
```
