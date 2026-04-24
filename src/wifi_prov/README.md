Wi‑Fi provisioning (BLE) — setup and testing

Overview

This folder contains a lightweight BLE-backed provisioning helper for the
SPEC-K10-TRUM project. The implementation will compile to a no-op scaffold
unless Bluetooth and the IDF `wifi_provisioning` headers are available in
`sdkconfig`/IDF.

What to enable in sdkconfig

Using `menuconfig` or by editing `sdkconfig.unihiker_k10`, enable at least:

- CONFIG_BT_ENABLED=y
- CONFIG_BT_NIMBLE_ENABLED=y  # recommended stack

Also ensure your IDF version includes the `wifi_provisioning` component
(which is normally present in recent ESP-IDF releases). There are no single
`CONFIG_WIFI_PROV_*` keys in all IDF versions — provisioning is provided by
the `wifi_provisioning` component and runtime scheme modules.

Suggested sdkconfig fragment to apply (append or set in `sdkconfig.unihiker_k10`):

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
```

If you use PlatformIO, you can update `sdkconfig.unihiker_k10` directly or
launch `idf.py menuconfig` in a native IDF environment to toggle the same
flags; PlatformIO's IDF integration respects the `sdkconfig` file when
building.

How it works

- Call `wifi_prov_start()` early in `app_main()` to start BLE provisioning.
  When compiled with the IDF provisioning headers, this starts the
  `wifi_prov_mgr` with the BLE (protocomm) bearer.

- The helper `wifi_prov_apply_credentials(const char* ssid, const char* password)`
  is provided for provisioning event handlers: it persists the credentials
  to NVS (namespace `wifi_prov`) and attempts to configure the Wi‑Fi STA
  interface and connect.

- Automatic apply: the firmware now includes an NVS watcher task that looks
  for `ssid`/`pass` keys in the `wifi_prov` NVS namespace. When it detects
  saved credentials it automatically calls `wifi_prov_apply_credentials()`
  and attempts to connect; this provides a portable auto-apply behavior
  without needing to rely on provisioning manager event APIs.

- `wifi_prov_clear_and_stop(bool clear_saved_creds)` stops the manager and
  optionally clears stored credentials.

Testing with the Espressif mobile app

1. Build and flash the firmware with Bluetooth enabled (see above).
2. Use the Espressif "Wi‑Fi provisioning" mobile app (or `esp_provision`
   CLI) to discover the device advertising its BLE provisioning service
   (service name: `speck10`).
3. Follow the app flow to send SSID and password to the device.
4. The device will persist credentials to NVS and attempt to connect.

Notes and next steps

- The example intentionally keeps the provisioning manager usage minimal so
  the project builds even when BLE/provisioning is disabled.
- After you enable BLE and test provisioning, I can add an automatic
  event-based flow that registers with `wifi_prov_mgr` to receive a
  callback when credentials arrive and then call
  `wifi_prov_apply_credentials()` automatically.

If you want, I can now add that auto-registration and callback wiring
assuming your build environment enables the provisioning headers — say the
word and I'll proceed to register the manager event handler and test the
build.
