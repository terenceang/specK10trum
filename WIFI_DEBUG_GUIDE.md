# Wi-Fi Reconnection Troubleshooting Guide

## Changes Made

### 1. Enhanced Logging in `wifi_prov.cpp`
- **When credentials are provisioned via BLE**: Logs the SSID and password length, then confirms successful NVS save
- **When device boots with saved credentials**: Displays the SSID that was found
- **When WiFi connects**: Shows which SSID the device successfully connected to
- **If NVS save fails**: Logs the error code so you can diagnose write issues

### 2. Increased Initial Timeout in `main.cpp`
- **Before**: 15 seconds for saved credentials to connect on reboot
- **After**: 30 seconds (more time for AP to respond)
- **Rationale**: If the AP is slow to start or there's initial RF noise, 15s might not be enough

### 3. Diagnostic Messages on Timeout
When the initial 30-second timeout expires, you'll see:
```
Wi-Fi connection timeout after 30 seconds
Note: If you provisioned via BLE but device can't reconnect, check that:
  1. The SSID and password were saved correctly during BLE provisioning
  2. The access point is available and in range
  3. No connectivity issues with the saved network
  Providing credentials again via BLE will overwrite the saved ones.
```

## Debugging Steps

### Step 1: Monitor Boot Sequence
Flash and monitor the device:
```bash
idf.py flash monitor
```

Look for these key log lines during boot:

**Good (found saved credentials):**
```
[wifi_prov] Found saved WiFi config: SSID='MyNetwork' (len=9)
[wifi_prov] Device already provisioned. SSID: 'MyNetwork'
[wifi_prov] Credentials found in WiFi config. SSID length: 9, Password length: 10
[Main] Waiting for Wi-Fi connection (30s timeout)...
[wifi_prov] Wi-Fi station started; connecting to SSID: MyNetwork
[wifi_prov] Wi-Fi connected to AP: SSID='MyNetwork'
[wifi_prov] Got IP: 192.168.1.x
```

**Bad (no saved credentials found):**
```
[wifi_prov] No saved WiFi credentials found in config
[Main] Starting BLE provisioning fallback...
```

### Step 2: Verify BLE Provisioning Saves Correctly
1. Clear any saved credentials first (optional):
   - In the code, you can add a flag to call `wifi_prov_clear_and_stop(true)` at startup
2. Provision via BLE
3. Look for these logs:
   ```
   [wifi_prov] Received SSID='YourNetwork' (pass length=12) via BLE provisioning
   [wifi_prov] Credentials successfully saved to NVS
   [wifi_prov] Wi-Fi connected after BLE provisioning!
   ```

### Step 3: Check Reboot Behavior
1. Power down the device
2. Make sure your AP is running
3. Power up the device
4. Look at the serial output:
   - Does it find the saved credentials?
   - Does it connect within 30 seconds?
   - Or does it timeout and start BLE provisioning again?

## Common Issues

### Issue: "No saved WiFi credentials found" on reboot after BLE provisioning

**Possible causes:**
1. **NVS not committing properly** → Look for "Failed to commit credentials to NVS" error
   - Check if NVS partition is full or corrupted
   - Try erasing NVS: `idf.py erase-flash` (clears all settings)

2. **WiFi config not being applied** → The credentials might be saved to NVS but not applied to the WiFi driver
   - The `nvs_watcher_task` should detect unapplied credentials and apply them
   - Look for "Watcher found unapplied credentials" log

3. **Provisioning event not received** → The BLE provisioning might complete but credentials never get saved
   - Look for "Received SSID" log during BLE provisioning
   - If you don't see it, the BLE app might not be sending credentials correctly

### Issue: Device connects but then disconnects

**Possible causes:**
1. **WiFi password incorrect** → Check that password matches exactly (case-sensitive)
2. **AP not accepting connection** → Check MAC filtering, SSID broadcast, security settings
3. **Signal too weak** → Move device closer to AP
4. **Interference** → Too many devices on 2.4GHz, try changing WiFi channel

## Network Diagnostics

Enable additional WiFi logging by adjusting log level in `sdkconfig`:
```bash
idf.py menuconfig
```
Navigate to: `Component config → Log output → Default log verbosity` and set to DEBUG

This will show WiFi driver events and help diagnose connection issues.

## Next Steps if Problem Persists

1. **Create a minimal BLE provisioning test** that just saves and reloads credentials
2. **Monitor NVS reads/writes** to ensure data persists
3. **Check SSID format** — some APs have special characters that might not serialize correctly
4. **Verify AP is configured** for the saved credentials (SSID, password, security type)
