# Linear Boot Sequence

All steps execute sequentially on a single thread. No concurrent initialization occurs before the emulator task launches.

## Boot Steps

1. **Initialize IO Expander** — XL9535 I2C GPIO; LED blink for power-on indicator
2. **Initialize Display** — SPI ILI9341; allocate frame buffers; spawn video task
3. **Initialize PSRAM** — Conditionally, if `CONFIG_SPIRAM` enabled
4. **Mount SPIFFS** — Register filesystem at `/spiffs` (required before splash screen)
5. **Show Splash Screen** — Load `/spiffs/splash.bmp` and display it
6. **Initialize Wi-Fi** — **Blocks until valid IP obtained**
   - Attempt to connect to saved AP (15 s timeout)
   - If timeout: start BLE provisioning and **block indefinitely** until user provisions credentials
   - Abort boot if Wi-Fi init fails or no IP obtained after BLE provisioning
7. **Check for ROM file** — Abort boot if ROM not found
8. **Create Spectrum CPU object** — Instantiate 48K or 128K hardware
9. **Load ROM and Reset CPU** — Load ROM binary; reset emulation state
10. **Initialize Webserver** — Start HTTP/WebSocket server
11. **Initialize Input** — Reset keyboard rows; spawn input task
12. **Initialize Audio** — Setup ESP-ADF pipeline; set volume to 70
13. **Wait for WebSocket Keyboard** — Block up to 30 s for client connection
14. **Beep** — Play 880 Hz tone for 120 ms (audible, after audio init)
15. **Start Emulator Task** — Spawn `emulator_task` on Core 0
16. **Enter Idle Loop** — Main app parks here; emulator runs on Core 0, video/input tasks on Core 1

## Blocking Constraints

- **Step 6 (Wi-Fi)**: 
  - First attempt: 60 second timeout to connect to saved AP
  - If timeout: starts BLE provisioning and **blocks indefinitely**
  - Boot cannot proceed without a valid IP address
  - User must provision Wi-Fi credentials via BLE if initial connection fails
  - Note: If device was previously provisioned via BLE, it will try to reconnect to the saved network first
  
- **Step 13 (WebSocket Keyboard)**: Blocks up to 30 seconds for first WebSocket client; timeout is non-fatal—boot continues.

## Task Spawning

- **Step 2**: `video` task (Core 1, priority 6)
- **Step 6**: `prov_watcher` task (NVS credential checker, spawned by `wifi_prov_start`)
- **Step 11**: `input_task` (unbound core, priority 4)
- **Step 12**: ESP-ADF pipeline internal tasks
- **Step 15**: `emulator` task (Core 0, priority 5)

## Notes

- SPIFFS is mounted **before** splash screen so the BMP file can be loaded.
- Wi-Fi is a **hard requirement** for boot. The device will not proceed to ROM loading or emulator startup without a valid IP.
- BLE provisioning starts automatically if the saved AP connection times out.
- Webserver and keyboard wait execute after Wi-Fi is guaranteed to be up.
- The boot beep (step 14) is guaranteed to play because audio is fully initialized at step 12.
