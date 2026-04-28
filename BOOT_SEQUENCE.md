# Linear Boot Sequence

All steps execute sequentially on a single thread. No concurrent initialization occurs before the emulator task launches.

## Boot Steps

1. **Initialize IO Expander** — XL9535 I2C GPIO; LED blink for power-on indicator
2. **Initialize Display** — SPI ILI9341; allocate frame buffers; spawn video task
3. **Show Splash Screen** — Load `/spiffs/splash.bmp` and display it
4. **Initialize PSRAM** — Conditionally, if `CONFIG_SPIRAM` enabled
5. **Mount SPIFFS** — Register filesystem at `/spiffs`
6. **Initialize Wi-Fi** — Blocking wait for IP (15 s timeout) or start BLE fallback
7. **Check for ROM file** — Abort boot if ROM not found
8. **Create Spectrum CPU object** — Instantiate 48K or 128K hardware
9. **Load ROM and Reset CPU** — Load ROM binary; reset emulation state
10. **Initialize Webserver** — Start HTTP/WebSocket server (only if Wi-Fi connected)
11. **Initialize Input** — Reset keyboard rows; spawn input task
12. **Initialize Audio** — Setup ESP-ADF pipeline; set volume to 70
13. **Wait for WebSocket Keyboard** — Block up to 30 s for client connection (only if webserver running)
14. **Beep** — Play 880 Hz tone for 120 ms (now audible, after audio init)
15. **Start Emulator Task** — Spawn `emulator_task` on Core 0
16. **Enter Idle Loop** — Main app parks here; emulator runs on Core 0, video/input tasks on Core 1

## Blocking Operations

- **Step 6 (Wi-Fi)**: Blocks up to 15 seconds waiting for IP assignment. If timeout, starts BLE provisioning fallback asynchronously and continues.
- **Step 13 (WebSocket)**: Blocks up to 30 seconds waiting for the first WebSocket client. Timeout is non-fatal; boot continues anyway.

## Conditional Steps

- **Step 10 (Webserver)**: Only runs if Wi-Fi connection succeeded at step 6.
- **Step 13 (WebSocket wait)**: Only runs if both Wi-Fi is connected AND webserver is running.

## Task Spawning

- **Step 2**: `video` task (Core 1, priority 6)
- **Step 11**: `input_task` (unbound core, priority 4)
- **Step 12**: ESP-ADF pipeline internal tasks
- **Step 15**: `emulator` task (Core 0, priority 5)
- **Background**: `prov_watcher` task (NVS credential checker, spawned by `wifi_prov_start`)

## Notes

- The boot beep (step 14) is now guaranteed to play because audio is fully initialized at step 12.
- Wi-Fi is synchronous until step 6 completes; no concurrent background negotiation before emulator starts.
- WebSocket keyboard support allows remote control via browser; timeout is graceful—no hard dependency on keyboard presence.
- If Wi-Fi fails completely, BLE provisioning starts asynchronously; emulator launches regardless.
