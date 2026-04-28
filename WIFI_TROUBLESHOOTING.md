# WiFi Connection Troubleshooting

## Common Issue: "No WiFi events logged, just timeout after 60s"

This means the WiFi connection attempt is failing to even start or is failing silently.

### Step 1: Check Serial Log for Disconnection Events

Look for lines like:
```
[wifi_prov] WIFI_EVENT_STA_DISCONNECTED (reason: X = REASON_NAME)
[wifi_prov] MIC_FAILURE (wrong password?)
[wifi_prov] 4WAY_HANDSHAKE_TIMEOUT
```

**What each reason means:**

| Reason | Meaning | Solution |
|--------|---------|----------|
| `MIC_FAILURE` | Wrong password | Re-provision via BLE with correct password |
| `4WAY_HANDSHAKE_TIMEOUT` | AP not responding / Signal too weak | Move device closer, check AP |
| `DISASSOC_PWRCAP_BAD` | Incompatible security settings | Check AP security type (WPA2/WPA3) |
| `ASSOC_TOOMANY` | AP at max clients | Disconnect other devices from AP |
| `(none - just timeout)` | Not even attempting to connect | See "No Connection Attempt" below |

### Step 2: Check Saved Credentials

The device should log:
```
[wifi_prov] Found saved WiFi config: SSID='YourSSID' (len=11)
[wifi_prov] Credentials found in WiFi config. SSID length: 11, Password length: 10
[wifi_prov] Wi-Fi station started; attempting to connect to SSID: 'YourSSID'
```

If you see: `No saved WiFi credentials found` → Credentials didn't persist, reprovision via BLE

### Step 3: Check Connection Attempt

After STA_START, look for:
```
[wifi_prov] SSID length: 11 bytes, Password configured: yes
[wifi_prov] Calling esp_wifi_connect()...
[wifi_prov] esp_wifi_connect() initiated successfully
```

If you see: `esp_wifi_connect() failed:` → WiFi driver error (check log error code)

### Step 4: Check AP Broadcasting

If NO WiFi events appear at all after connection attempt, the AP might:
- Have SSID hidden (enable SSID broadcast)
- Be using 5GHz only (device only supports 2.4GHz)
- Be turned off or in sleep mode
- Have wrong SSID saved (re-provision)

Test by moving device next to AP and retrying.

## "MIC_FAILURE" Error

```
[wifi_prov] Wi-Fi disconnected (reason: 14 = MIC_FAILURE (wrong password?))
```

**Cause:** Password mismatch during WPA handshake

**Fix:**
1. Note the SSID from logs
2. Verify password character-by-character (case-sensitive)
3. Re-provision via BLE with correct password
4. Confirm password in AP settings matches exactly

## "4WAY_HANDSHAKE_TIMEOUT" Error

```
[wifi_prov] Wi-Fi disconnected (reason: 15 = 4WAY_HANDSHAKE_TIMEOUT)
```

**Cause:** AP not responding during authentication

**Possible fixes:**
1. Move device closer to AP (signal strength)
2. Restart AP
3. Check AP logs for the device MAC address: `1c:db:d4:50:8c:2c`
4. Disable AP's 5GHz band (device is 2.4GHz only)
5. Reduce AP's security settings temporarily to test

## "No saved WiFi credentials" on Reboot

After successful BLE provisioning, next reboot shows:
```
[wifi_prov] No saved WiFi credentials found in config
```

**Causes:**
1. NVS (flash storage) corruption
2. Credentials weren't actually saved during provisioning
3. Provisioning event didn't fire correctly

**Fix:**
1. Erase all flash: `idf.py erase-flash`
2. Re-provision via BLE
3. Watch for: `[wifi_prov] Credentials successfully saved to NVS`
4. Reboot and verify credentials are found

## "Provisioning manager already initialized" Error

```
E (67766) wifi_prov_mgr: Provisioning manager already initialized
E (67766) wifi_prov: wifi_prov_mgr_init failed: 259
```

**Cause:** BLE provisioning is being started twice simultaneously

**Fix:** This is a race condition - usually clears on next reboot. If persistent, report as bug.

## Device Only Supports 2.4GHz WiFi

The ESP32 WiFi module in this device **only supports 2.4GHz**. If your AP is set to:
- 5GHz only → Device won't see SSID
- Dual-band (2.4+5GHz) → Device connects via 2.4GHz channel only

**Solution:** Enable 2.4GHz band in AP settings

## Signal Too Weak

Symptoms: Connection times out even with correct credentials

**Fix:**
1. Move device closer to AP (< 2 meters for testing)
2. Check for RF interference (microwaves, other WiFi networks)
3. Try different WiFi channel (1, 6, or 11 recommended for 2.4GHz)

## Quick Diagnostic Commands

Monitor WiFi events in real-time:
```bash
idf.py monitor -f esp32 | grep -i "wifi\|ip\|prov"
```

View only error messages:
```bash
idf.py monitor -f esp32 | grep -i "error\|failed\|reason"
```

Check what AP the device is configured to connect to:
```
# Device will log on startup:
# [wifi_prov] Found saved WiFi config: SSID='...'
```

## Still Not Working?

Provide these logs:
1. Full serial output from boot to WiFi timeout
2. SSID and password you're trying to use
3. AP model and security type (WPA2, WPA3, etc)
4. Any error codes or reason numbers from disconnection events
