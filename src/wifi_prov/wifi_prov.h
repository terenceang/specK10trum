#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Apply credentials received by a provisioner. Saves to NVS and attempts
// to configure and connect the Wi‑Fi STA interface. This function is safe
// to call from a provisioning event handler.
bool wifi_prov_apply_credentials(const char* ssid, const char* password);

// Convenience helper: stops provisioning and optionally clears saved creds.
void wifi_prov_clear_and_stop(bool clear_saved_creds);

// Start BLE-based Wi-Fi provisioning (non-blocking). Returns true on success.
bool wifi_prov_start();

// Force-start BLE-based provisioning even if device is already marked provisioned.
// Useful when you want to re-provision without erasing saved credentials; a
// successful provisioning run will overwrite saved credentials.
bool wifi_prov_start_force();

// Stop provisioning and cleanup.
void wifi_prov_stop();

// Start a fallback SoftAP (Access Point) when no network is found.
// Creates an open network named "SpecK10trum-Connect" at 192.168.4.1.
bool wifi_prov_start_ap_fallback();

// Block until Wi-Fi is connected and an IP is obtained.
bool wifi_prov_wait_for_ip(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

// Note: Full BLE-based provisioning requires enabling Bluetooth and the
// IDF `wifi_provisioning` component in `sdkconfig`. The implementation
// in `wifi_prov.cpp` will compile to a no-op when those headers/options
// are not present so the project can still build.
