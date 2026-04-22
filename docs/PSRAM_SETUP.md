# PSRAM Setup for `unihiker_k10`

This document describes the confirmed working configuration for enabling PSRAM in this ESP32-S3 / PlatformIO / ESP-IDF project.

## Why this matters

The `board_build.psram = enable` setting alone is not enough for ESP-IDF builds. The ESP-IDF SDK config must also explicitly enable PSRAM and match the board hardware / pin mapping.

## Required changes

### 1. `platformio.ini`

Use the ESP-IDF SDK config file directly and add the recommended PSRAM build flag:

```ini
[env:unihiker_k10]
platform = unihiker
board = unihiker_k10
framework = espidf
board_build.flash_mode = dio
board_build.psram = enable
board_build.esp-idf.sdkconfig_path = sdkconfig.unihiker_k10
monitor_speed = 115200

build_unflags = -Os
build_flags =
    -O2
    -DCORE_DEBUG_LEVEL=1
    -DCONFIG_SPIRAM_CACHE_WORKAROUND
    -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE
```

Notes:
- `board_build.psram = enable` tells PlatformIO the board should use PSRAM.
- `board_build.esp-idf.sdkconfig_path` forces PlatformIO to use the intended ESP-IDF configuration file.
- `-DCONFIG_SPIRAM_CACHE_WORKAROUND` is the recommended ESP-IDF-style define for PSRAM builds, not `BOARD_HAS_PSRAM`.

### 2. `sdkconfig.unihiker_k10`

Ensure the SDK config contains the PSRAM options below.

Required entries:

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_CLK_IO=30
CONFIG_SPIRAM_CS_IO=26
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_SPEED=40
CONFIG_SPIRAM_BOOT_HW_INIT=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION=y
CONFIG_SPIRAM_USE_MALLOC=y
```

Optional / helpful entries:

```text
CONFIG_SPIRAM_MEMTEST=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
```

This last option is useful when the hardware may not expose PSRAM reliably. It allows the board to boot without external RAM and lets your app fall back to DRAM.

### 3. Code checks

In source code, only initialize PSRAM when ESP-IDF actually enables it:

```cpp
#ifdef CONFIG_SPIRAM
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM initialization failed!");
    }
#endif
```

And only allocate PSRAM memory if PSRAM initialization succeeded.

## Build and upload

Run the normal PlatformIO build command from the project root:

```powershell
pio run --environment unihiker_k10
```

If the build succeeds, the generated firmware will be available here:

```powershell
.pio\build\unihiker_k10\firmware.bin
```

To upload to the board:

```powershell
pio run --environment unihiker_k10 --target upload --upload-port COM5
```

## Monitor runtime output

After uploading, open the serial monitor to inspect boot messages and check for PSRAM-related crashes:

```powershell
pio device monitor --port COM5 --baud 115200
```

If the port is busy, close any existing serial monitor or terminal session that uses `COM5`.

## Common failure mode

If the device boots and shows:

```text
PSRAM ID read error: 0x00ffffff
Failed to init external RAM!
```

then the configuration is enabled, but the physical PSRAM hardware is not present, not connected correctly, or the PSRAM line mode / pins do not match the board wiring.

In that case:
- enable `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` to let the board boot without external RAM
- verify the board actually has PSRAM installed
- verify the board pin mapping for `CS`, `CLK`, and data lines
- verify the selected PSRAM mode is correct for the module
- if the board has no PSRAM, remove `board_build.psram = enable` from `platformio.ini`

If your app already supports DRAM fallback, `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` is the safest recovery path.

## Practical advice

- Prefer one source of truth: use `sdkconfig.unihiker_k10` for ESP-IDF config instead of depending on auto-generated config files.
- Do not keep both `-DBOARD_HAS_PSRAM` and `-DCONFIG_SPIRAM_CACHE_WORKAROUND` in the same ESP-IDF environment; use the ESP-IDF-specific define.
- On Windows, close any serial monitor before uploading to avoid `COM5` busy/permission errors.

## Summary

For this project, the correct PSRAM enablement flow is:
1. `board_build.psram = enable`
2. `board_build.esp-idf.sdkconfig_path = sdkconfig.unihiker_k10`
3. `CONFIG_SPIRAM=y` plus matching PSRAM settings in `sdkconfig.unihiker_k10`
4. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` when PSRAM may not be present or may fail initialization
5. `-DCONFIG_SPIRAM_CACHE_WORKAROUND` in `build_flags`
6. verify hardware support if initialization still fails
