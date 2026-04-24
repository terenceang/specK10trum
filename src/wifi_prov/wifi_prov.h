#pragma once

#include <stdbool.h>
#include <stdint.h>

// Apply credentials received by a provisioner. Saves to NVS and attempts
// to configure and connect the Wi‑Fi STA interface. This function is safe
// to call from a provisioning event handler.
bool wifi_prov_apply_credentials(const char* ssid, const char* password);

// Convenience helper: stops provisioning and optionally clears saved creds.
void wifi_prov_clear_and_stop(bool clear_saved_creds);

// Start BLE-based Wi-Fi provisioning (non-blocking). Returns true on success.
bool wifi_prov_start();

// Stop provisioning and cleanup.
void wifi_prov_stop();

// Note: Full BLE-based provisioning requires enabling Bluetooth and the
// IDF `wifi_provisioning` component in `sdkconfig`. The implementation
// in `wifi_prov.cpp` will compile to a no-op when those headers/options
// are not present so the project can still build.
