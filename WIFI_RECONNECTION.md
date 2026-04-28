# WiFi Reconnection Behavior

## Current Behavior

### First Boot (No Saved Credentials)
```
1. WiFi provisioning starts → BLE app shows "PROV_speck10"
2. User enters SSID + password
3. Credentials saved to NVS + WiFi driver config
4. Device connects and gets IP
```

### Subsequent Boots (Saved Credentials)
```
1. Device checks NVS for saved credentials
2. If found: Loads into WiFi driver config
3. Initiates connection to known AP (no scanning needed)
4. Target: Should connect in <10 seconds with responsive AP
```

## Why Not "Fast Login"?

Even with saved credentials, reconnection may take 30-60 seconds because:

### 1. **AP Not Responding Immediately**
- AP might be rebooting or slow to respond
- WiFi radio might need time to scan for beacons
- Signal might be weak on startup

### 2. **ESP32 WiFi Driver Behavior**
- Connection attempts have built-in timeouts per retry
- Multiple authentication retries (WPA handshake) take time
- DHCP lease negotiation adds 2-3 seconds

### 3. **No Fast-Fail Mechanism**
- If AP is unavailable, the driver will retry for the full timeout period
- There's no "fail fast" to detect unavailable networks quickly

## How to Improve Speed

### Option 1: Shorter Timeout + Auto-Retry
Change to 15s timeout, if fails → try BLE → user can reprovision

### Option 2: Implement Fast AP Check
```cpp
// Before connect, scan to verify AP is actually broadcasting
wifi_scan_config_t scan = {...};
esp_wifi_scan_start(&scan, true);
// If SSID found: connect
// If SSID NOT found: skip to BLE immediately (no 60s waste)
```

### Option 3: Store AP Channel/BSSID
Save the AP's channel and BSSID during provisioning, then:
```cpp
conf.sta.channel = saved_channel;  // Skip channel scanning
memcpy(conf.sta.bssid, saved_bssid, 6);  // Connect to known AP
```

## Current Optimizations in Place

1. **Power Saving Disabled**: `esp_wifi_set_ps(WIFI_PS_NONE)` - faster WiFi response
2. **Known Network**: Credentials loaded, no AP scanning
3. **Extended Timeout**: 60 seconds allows AP time to stabilize
4. **Exponential Backoff**: After connection fails, retries with increasing delays (1s, 2s, 4s, 8s, 16s, 30s max)

## Recommended Action

If you're experiencing slow reconnection after BLE provisioning:

1. **Check AP status** - Is it rebooting or in sleep mode?
2. **Check signal strength** - Move device closer to AP
3. **Reduce 60s timeout** - If you want faster BLE fallback, change to 30s
4. **Implement fast scan** (advanced) - See Option 2 above

## Log Output to Monitor

Watch for these during boot:
```
[wifi_prov] Found saved WiFi config: SSID='MyNetwork'
[wifi_prov] Wi-Fi station started; connecting to SSID: MyNetwork
[wifi_prov] Wi-Fi connected to AP: SSID='MyNetwork'
[wifi_prov] Got IP: 192.168.1.x
```

If you see connection timeout instead, the AP is not responding within the timeout window.
