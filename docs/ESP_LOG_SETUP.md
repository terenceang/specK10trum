# Getting ESP-IDF Logging (ESP_LOG) Working on UniHiker K10

## The Problem

By default, `ESP_LOGI()`, `ESP_LOGD()`, and other ESP-IDF log macros produce no visible output on the UniHiker K10 serial monitor.

## Root Cause

The ESP32-S3 has **two separate USB interfaces**:

| Interface | Type | Description |
|-----------|------|-------------|
| USB Serial/JTAG | Hardware | Built-in USB-to-serial bridge, always available |
| USB OTG (CDC) | Software | Requires a TinyUSB or CDC software stack |

The UniHiker K10 connects to the host PC via the **USB Serial/JTAG** interface (confirmed by `ARDUINO_USB_MODE=1` and USB PID `0x303A:0x1001` in the board definition). However, the default sdkconfig routes console output to **USB CDC** (`CONFIG_ESP_CONSOLE_USB_CDC=y`), which is a completely different USB peripheral that isn't connected to anything on this board.

The result: all `ESP_LOG` output is sent to a USB interface that nobody is listening on.

## The Fix

### 1. Update `sdkconfig.unihiker_k10_spectrum`

Find the console configuration section (around line 1301) and change it to:

```
# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set
# CONFIG_ESP_CONSOLE_USB_CDC is not set
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
# CONFIG_ESP_CONSOLE_UART_CUSTOM is not set
# CONFIG_ESP_CONSOLE_NONE is not set
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
CONFIG_ESP_CONSOLE_UART_NUM=-1
```

**Important:** You must edit this file directly. The `board_build.sdkconfig_options` in `platformio.ini` only provides defaults — it does **not** override values already present in the sdkconfig file.

### 2. Update `platformio.ini`

Set the sdkconfig option as a default (useful for clean builds):

```ini
board_build.sdkconfig_options =
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### 3. Clean Build

After changing the console configuration, you **must** do a full clean build. The sdkconfig is cached in `.pio/build/`:

```
pio run -t clean
pio run
```

Or manually delete `.pio/build/unihiker_k10_spectrum/` to force full regeneration.

### 4. Set the Correct COM Port

The UniHiker K10 appears as a "USB Serial Device" in Windows Device Manager. Set `upload_port` and optionally `monitor_port` in `platformio.ini`:

```ini
upload_port = COM16
```

Check Device Manager > Ports (COM & LPT) to find the correct port number.

## Software Kill Switch

The ESPectrum firmware has a runtime logging kill switch in `ESPectrum.cpp`:

```cpp
if (!Config::CDCOnBoot) {
    esp_log_level_set("*", ESP_LOG_NONE);
}
```

If `CDCOnBoot` is saved as `false` in NVS (via the OSD menu), **all** ESP_LOG output is silenced even if the console is configured correctly. Toggle it back on via the OSD menu, or erase NVS to reset to the default (`true`).

## Why USB CDC Doesn't Work

- `CONFIG_ESP_CONSOLE_USB_CDC` uses the USB OTG peripheral with a software CDC ACM driver
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` uses the dedicated hardware USB-to-serial bridge
- These are different physical peripherals on the ESP32-S3
- The UniHiker K10 board wires the USB connector to the Serial/JTAG peripheral, not the OTG one
- Enabling TinyUSB (`CONFIG_TINYUSB_ENABLED=y`) alongside USB CDC console also causes conflicts, as both try to claim the same USB OTG peripheral

## Working `platformio.ini` Reference

```ini
[env:unihiker_k10_spectrum]
platform = https://github.com/DFRobot/platform-unihiker.git
framework = espidf
board = unihiker_k10
upload_port = COM16
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
extra_scripts = extra_script.py
board_build.partitions = src/partitions.csv
build_flags = -Wno-error
build_unflags = -Werror
board_build.sdkconfig_options =
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
board_build.littlefs_dir = littlefs_data
```
