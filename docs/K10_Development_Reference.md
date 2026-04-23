# UniHiker K10 Development Reference

**DFR0992 · ESP32-S3-WROOM-1 · Revision V1.0**

This document is a unified hardware and PSRAM configuration reference for the UniHiker K10 platform. It covers board pin assignments, peripheral interfaces, power rails, on-board controls, and ESP-IDF / PlatformIO PSRAM setup guidance.

## 1. System Overview

The UniHiker K10 is a development platform centred on the ESP32-S3-WROOM-1 module. It integrates:

- ILI9341 SPI TFT display
- GC2145 camera module
- ES7243 audio codec + NS4168 amplifier
- XL9535 I2C GPIO expander
- RGB LEDs and onboard button controls
- Micro:bit-compatible edge connector pins
- SPI MicroSD + SPI font ROM
- Optional external SPI PSRAM

The board architecture separates high-speed native signals (display, camera, audio) from lower-speed I/O via the XL9535 expander.

## 2. Main MCU — ESP32-S3-WROOM-1

### 2.1 Power & Reset

| Pin | Net |
|---|---|
| 3V3 | 3.3 V rail |
| EN | Reset pull-up |
| GND | Ground |

### 2.2 USB

| ESP32 Pin | Net |
|---|---|
| GPIO19 | USB_D− |
| GPIO20 | USB_D+ |

### 2.3 Boot / UART

| ESP32 Pin | Net |
|---|---|
| GPIO0 | BOOT button |
| GPIO43 | TXD0 |
| GPIO44 | RXD0 |

### 2.4 Strapping Pins

| GPIO | Default |
|---|---|
| 0 | Pull UP |
| 3 | N/A |
| 45 | Pull Down |
| 46 | Pull Down |

## 3. System Buses

### 3.1 I2C Bus (Primary)

Used by the XL9535 expander, audio codec, onboard sensors, and external I2C headers.

| Signal | ESP32 Pin |
|---|---|
| I2C_SDA | GPIO47 → P20/SDA |
| I2C_SCL | GPIO48 → P19/SCL |

### 3.2 I2C Scan Results (Standard Components)

| Address | Major IC | Function | Notes |
|---|---|---|---|
| 0x11 | ES7243EU8 | Audio Codec | Microphone interface & control |
| 0x19 | SC7A20H | 3-axis Accelerometer | Motion/orientation sensing |
| 0x20 | XL9535QF24 | IO Expander | Backlight, buttons, edge connector |
| 0x29 | LTR-303ALS-01 | Ambient Light Sensor | Illuminance measurement |
| 0x38 | AHT20 | Temp & Humidity | Environmental sensing |

### 3.3 SPI Bus (LCD)

| Signal | Net / ESP32 Pin |
|---|---|
| LCD_SCLK | GPIO12 (`SCK`) |
| LCD_MOSI | GPIO21 (`MOSI`) |
| LCD_CS | GPIO14 |
| LCD_DC | GPIO13 |
| LCD_RST | Not driven by a dedicated GPIO in the Arduino board package (`TFT_RST = -1`) |
| LCD_EN | GPIO40 (`K10_TFT_ENABLE`) |
| LCD_BLK | Backlight control via XL9535 P00 (expander output, not a direct ESP32 GPIO) |

### 3.4 I2S Bus (Audio)

| Signal | Function |
|---|---|
| I2S_BLCK | Bit clock |
| I2S_LRCK | LR clock |
| I2S_MCLK | Master clock |
| I2S_SDI / I2S_ADCDAT | Microphone data (Codec → ESP32) |
| I2S_SDO | Speaker data (ESP32 → Amplifier) |

## 4. LCD Subsystem — ILI9341

The display is an ILI9341 SPI TFT driven by ESP32 GPIOs. The backlight is switched through the XL9535 expander and an external transistor.

| LCD Pin | Net |
|---|---|
| SCL | LCD_SCLK (GPIO12) |
| SDI | LCD_MOSI (GPIO21) |
| CS | LCD_CS (GPIO14) |
| DC | LCD_DC (GPIO13) |
| RST | LCD_RST (not mapped to a dedicated Arduino GPIO on the installed `unihiker_k10` board package) |
| EN | LCD_EN (GPIO40) |
| LED+ | Backlight (via MMBT3904 transistor) |
| LED− | GND |

> Backlight is controlled by XL9535 P00 through a transistor; it is not a direct ESP32 GPIO.

> GPIO40 is used as the panel enable line to release the display from reset/powerdown.

## 5. Camera Subsystem — GC2145

The GC2145 camera uses an 8-bit DVP interface and dedicated camera power rails.

| Signal | Direction | Description |
|---|---|---|
| Camera_XCLK | ESP32 → Cam | Master clock |
| Camera_VSYNC | Cam → ESP32 | Frame sync |
| Camera_HREF / HSYNC | Cam → ESP32 | Line sync |
| Camera_PCLK | Cam → ESP32 | Pixel clock |
| Camera_D2 … D9 | Cam → ESP32 | Parallel image data |
| Camera_HOLD | XL9535 → Cam | Camera power/hold control via expander P01 |

Data is transferred directly into ESP32 memory via DMA.

## 6. Audio Subsystem

### 6.1 Audio Codec — ES7243EU8

The ES7243 audio codec is controlled over I2C and delivers microphone ADC data to the ESP32.

| Signal | Net |
|---|---|
| SCLK | I2S_BLCK |
| LRCK | I2S_LRCK |
| MCLK | I2S_MCLK |
| SDOUT / AD2 | I2S_ADCDAT |
| I2C SCL/SDA | P19/SCL, P20/SDA |

### 6.2 MEMS Microphones — MSM381ACT001

| Mic | Connection |
|---|---|
| Mic L (U10) | AINLP / AINLN |
| Mic R (U9) | AINRP / AINRN |

### 6.3 Speaker Amplifier — NS4168

| Pin | Net |
|---|---|
| LRCLK | I2S_LRCK |
| BCLK | I2S_BLCK |
| SDATA | I2S_SDI |
| VO+ / VO− | Speaker output |

## 7. XL9535 IO Expander

The XL9535 is the board’s 16-bit I2C GPIO expander at address `0x20`.

| Expander Pin | Net / Function |
|---|---|
| P00 | LCD_BLK (backlight control) |
| P01 | Camera_HOLD / power control |
| P02 | P11 / Key B |
| P03 | P12 |
| P04 | P13 |
| P05 | P14 |
| P06 | P15 |
| P07 | P2 |
| P10 | P8 |
| P11 | P9 |
| P12 | P10 |
| P13 | P6 |
| P14 | P5 / Key A |
| P15 | P4 |
| P16 | P3 |
| P17 | UserLed |
| INT | BUS_INT → ESP32 |
| SCL / SDA | I2C bus |

### 7.1 Register Summary

| Register | Purpose |
|---|---|
| 0x00 | Input Port 0 |
| 0x01 | Input Port 1 |
| 0x02 | Output Port 0 |
| 0x03 | Output Port 1 |
| 0x06 | Config Port 0 |
| 0x07 | Config Port 1 |

### 7.2 Useful Pin Mappings

- `P00` — LCD backlight enable
- `P01` — Camera hold / power control
- `P02` — Key B
- `P14` — Key A
- `P17` — User LED

### 7.3 Example Behavior

- Key inputs are active-low and typically read `0` when pressed.
- Many K10 projects configure P02/P14 as inputs and P17 as an output LED indicator.
- The expander is a good place to move low-speed GPIO off the ESP32.

## 8. Buttons and Edge Connector

The board provides three onboard buttons and a Micro:bit-compatible edge connector.

### 8.1 Onboard Buttons

| Button | Net | Notes |
|---|---|---|
| BOOT (K1) | GPIO0 | Strapping pin |
| RST (K2) | EN | Reset |
| Key A (K3) | P5 / KeyA | XL9535 P14 |
| Key B (K4) | P11 / KeyB | XL9535 P02 |

### 8.2 Edge Connector (J9A)

| Edge Pin | Signal / Function |
|---|---|
| P0 | Analog / GPIO |
| P1 | Analog / GPIO |
| P2 | Analog / GPIO |
| P3 | GPIO (via XL9535) |
| P4 | Light sensor / Analog |
| P5 | Button A |
| P6 | Buzzer |
| P7 | NeoPixel (RGB) |
| P8 | GPIO (via XL9535) |
| P9 | GPIO (via XL9535) |
| P10 | Sound / Analog |
| P11 | Button B |
| P12 | GPIO (via XL9535) |
| P13 | SPI SCK |
| P14 | SPI MISO |
| P15 | SPI MOSI |
| P16 | GPIO (via XL9535) |
| P19 | I2C SCL |
| P20 | I2C SDA |

### 8.3 Input Notes

- The K10 does not have a native D-pad playback on the board; external gamepad or software mapping is used for directional input.
- Onboard buttons are often disabled in software via a configuration macro when external input is preferred.

## 9. Onboard Sensors

| IC | Component | I2C Address | Function |
|---|---|---|---|
| U1 | AHT20 | 0x38 | Temperature & Humidity |
| U2 | LTR-303ALS-01 | 0x29 | Ambient Light |
| U3 | SC7A20H | 0x19 | 3-axis Accelerometer |
| U5 | XL9535QF24 | 0x20 | GPIO Expander |
| U8 | ES7243EU8 | 0x10+ | Audio Codec |

## 10. External Memory & Storage

### 10.1 SPI PSRAM / External RAM

The board supports ESP32-S3 external SPI PSRAM, usually configured for octal SPI mode.

Example SDKCONFIG settings:

- `CONFIG_SPIRAM=y`
- `CONFIG_SPIRAM_MODE_OCT=y`
- `CONFIG_SPIRAM_TYPE_AUTO=y`
- `CONFIG_SPIRAM_CLK_IO=30`
- `CONFIG_SPIRAM_CS_IO=26`
- `CONFIG_SPIRAM_SPEED_80M=y`
- `CONFIG_SPIRAM_BOOT_INIT=y`
- `CONFIG_SPIRAM_USE_MALLOC=y`
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`
- `CONFIG_SOC_PSRAM_DMA_CAPABLE=y`
- `CONFIG_SOC_AHB_GDMA_SUPPORT_PSRAM=y`
- `CONFIG_SOC_SPIRAM_SUPPORTED=y`
- `CONFIG_SOC_SPIRAM_XIP_SUPPORTED=y`
- `CONFIG_SOC_MEMSPI_CORE_CLK_SHARED_WITH_PSRAM=y`
- `CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND=y`

PSRAM clock uses GPIO30 and CS uses GPIO26.

### 10.2 MicroSD and Font ROM

- MicroSD card is connected on SPI3 via `CS3`, `MOSI3`, `SCLK3`, `MISO3`.
- A GT30L24A3W font ROM also shares the SPI3 bus.
- `CS3` is shared between the MicroSD and camera data line `Camera_D2`, so SD transactions must finish and release CS before camera use.

## 11. Power Management & Startup

### 11.1 Power Rails

| Rail | Source | Consumers |
|---|---|---|
| VUSB | USB connector / Battery | System input |
| 3.3 V | LDO regulator | ESP32-S3, XL9535, most ICs |
| 2.8 V | U6 BL8555-28PRA | GC2145 camera AVDD |
| 1.8 V | U7 AP7343Q-18W5-7 | GC2145 camera DOVDD |
| 1.2 V | Internal to GC2145 | GC2145 DVDD |

### 11.2 Startup Sequence

1. Power enters the system; the 3.3 V LDO provides the main logic rail.
2. The ESP32-S3 can initialize I2C and configure the XL9535 for backlight, camera hold/power, and other low-speed controls.
3. I2C sensors and SPI/I2S peripherals are brought up after power and reset are stable.

## 12. Video & Display Development Notes

- The board can support 80 MHz SPI to the ILI9341 for low-latency display updates.
- The usable active area is 224×288 pixels within the 240×320 panel.
- Backlight control is implemented through XL9535 P00, so software must update the expander rather than toggling an ESP32 GPIO.
- If using DMA for rendering, keep per-transfer size within the ESP32-S3 SPI limit (~32 KB), which is why 48-pixel-high strips are common.

## 13. Development Notes

### 13.1 XL9535 I2C Example

The XL9535 is accessed by address `0x20` over the primary I2C bus. Use the following register mapping as a reference:

- Input Port 0: `0x00`
- Input Port 1: `0x01`
- Output Port 0: `0x02`
- Output Port 1: `0x03`
- Config Port 0: `0x06`
- Config Port 1: `0x07`

### 13.2 Button Input Notes

- Onboard buttons are typically active-low.
- Many firmware projects invert the XL9535 read values so pressed = `1` in software.
- When onboard buttons are disabled, external BLE or WiFi gamepad input is usually used instead.

### 13.3 Pin Sharing Constraints

Because the ESP32-S3 has a finite number of GPIOs, some pins are multiplexed or shared:

| Shared Resource | Functions | Constraint |
|---|---|---|
| Pin 40 / CS3 | MicroSD CS and Camera_D2 | SD card CS must be released before camera can use the pin |
| Pins 42 & 45 | I2S clocks and camera data lines | High-quality audio recording and camera streaming cannot run simultaneously |

## 14. Reference Scope

This document is intended as a unified K10 development reference. It is board-focused and omits detailed application-specific software logic, while still providing the pinout, power, peripheral, and expander details needed for firmware and hardware design.

## 15. PSRAM Setup for `unihiker_k10`

This section describes the confirmed working configuration for enabling PSRAM in this ESP32-S3 / PlatformIO / ESP-IDF project.

### 15.1 Why this matters

The `board_build.psram = enable` setting alone is not enough for ESP-IDF builds. The ESP-IDF SDK config must also explicitly enable PSRAM and match the board hardware / pin mapping.

### 15.2 Required changes

#### 15.2.1 `platformio.ini`

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

#### 15.2.2 `sdkconfig.unihiker_k10`

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

#### 15.2.3 Code checks

In source code, only initialize PSRAM when ESP-IDF actually enables it:

```cpp
#ifdef CONFIG_SPIRAM
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM initialization failed!");
    }
#endif
```

And only allocate PSRAM memory if PSRAM initialization succeeded.

### 15.3 Build and upload

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

### 15.4 Monitor runtime output

After uploading, open the serial monitor to inspect boot messages and check for PSRAM-related crashes:

```powershell
pio device monitor --port COM5 --baud 115200
```

If the port is busy, close any existing serial monitor or terminal session that uses `COM5`.

### 15.5 Common failure mode

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

### 15.6 Practical advice

- Prefer one source of truth: use `sdkconfig.unihiker_k10` for ESP-IDF config instead of depending on auto-generated config files.
- Do not keep both `-DBOARD_HAS_PSRAM` and `-DCONFIG_SPIRAM_CACHE_WORKAROUND` in the same ESP-IDF environment; use the ESP-IDF-specific define.
- On Windows, close any serial monitor before uploading to avoid `COM5` busy/permission errors.

### 15.7 Summary

For this project, the correct PSRAM enablement flow is:
1. `board_build.psram = enable`
2. `board_build.esp-idf.sdkconfig_path = sdkconfig.unihiker_k10`
3. `CONFIG_SPIRAM=y` plus matching PSRAM settings in `sdkconfig.unihiker_k10`
4. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` when PSRAM may not be present or may fail initialization
5. `-DCONFIG_SPIRAM_CACHE_WORKAROUND` in `build_flags`
6. verify hardware support if initialization still fails

## 16. ESP-IDF Logging (ESP_LOG) Setup

### The Problem

By default, `ESP_LOGI()`, `ESP_LOGD()`, and other ESP-IDF log macros produce no visible output on the UniHiker K10 serial monitor.

### Root Cause

The ESP32-S3 has two separate USB interfaces:

| Interface | Type | Description |
|-----------|------|-------------|
| USB Serial/JTAG | Hardware | Built-in USB-to-serial bridge, always available |
| USB OTG (CDC) | Software | Requires a TinyUSB or CDC software stack |

The UniHiker K10 connects to the host PC via the **USB Serial/JTAG** interface (confirmed by `ARDUINO_USB_MODE=1` and USB PID `0x303A:0x1001` in the board definition). However, the default sdkconfig routes console output to **USB CDC** (`CONFIG_ESP_CONSOLE_USB_CDC=y`), which is a different USB peripheral that isn't connected on this board. The result: all `ESP_LOG` output is sent to a USB interface that nobody is listening on.

### The Fix

1. Update the SDK config used for the environment (for example, `sdkconfig.unihiker_k10`):

```
# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set
# CONFIG_ESP_CONSOLE_USB_CDC is not set
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
# CONFIG_ESP_CONSOLE_UART_CUSTOM is not set
# CONFIG_ESP_CONSOLE_NONE is not set
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
CONFIG_ESP_CONSOLE_UART_NUM=-1
```

Important: edit the SDK config file directly. `board_build.sdkconfig_options` in `platformio.ini` provides defaults and does not override values already present in an existing cached sdkconfig.

2. Update `platformio.ini` to provide the default option (useful for clean builds):

```ini
board_build.sdkconfig_options =
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

3. Do a clean build to ensure the sdkconfig is regenerated (the sdkconfig is cached under `.pio/build/`):

```powershell
pio run -t clean
pio run
```

Or remove the environment build folder manually, for example:

```powershell
rm -r .pio/build/unihiker_k10_spectrum/
```

4. Set the correct COM port in `platformio.ini` (the device appears as "USB Serial Device" in Windows Device Manager):

```ini
upload_port = COM16
```

5. Be aware of the runtime logging kill-switch: the firmware checks `Config::CDCOnBoot` and may call `esp_log_level_set("*", ESP_LOG_NONE);` if it was saved as `false` in NVS. Toggle via the OSD menu or erase NVS to restore logging.

### Why USB CDC Doesn't Work on K10

- `CONFIG_ESP_CONSOLE_USB_CDC` uses the USB OTG peripheral with a software CDC ACM driver.
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` uses the dedicated hardware USB-to-serial bridge.
- The K10 routes the connector to the Serial/JTAG peripheral, not the OTG one.
- Enabling TinyUSB (`CONFIG_TINYUSB_ENABLED=y`) alongside USB CDC can cause conflicts.

### Example `platformio.ini` Snippet

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
