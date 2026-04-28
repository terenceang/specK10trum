# WebServer Connection Issues - Investigation & Fixes

## Problems Found

### 1. **Zombie Keepalive Task** (Critical)
**Issue**: The WebSocket keepalive task is created only once and persists indefinitely. If `httpd_start()` fails, the task continues to run and tries to ping a NULL or invalid server handle.

**Impact**: 
- Keepalive task keeps running but can't actually send pings
- Silent failures in frame sending with no error logging
- Connection drops go unnoticed by the server

**Fix**: Added error handling and logging in the keepalive task to catch and report failures.

---

### 2. **No Error Logging on httpd_start Failure** (Critical)
**Issue**: When `httpd_start()` fails, the error code is not logged, making it impossible to diagnose why the server wouldn't start.

**Impact**: 
- Users reboot the ESP32 without knowing the actual problem
- Could be memory, socket allocation, or initialization issue
- No recovery possible without rebooting

**Fix**: Added detailed error logging:
```cpp
esp_err_t ret = httpd_start(&s_server, &config);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s (%d)", esp_err_to_name(ret), ret);
    s_server = NULL;
    return ret;
}
```

---

### 3. **LRU Purge Enabled** (High)
**Issue**: `config.lru_purge_enable = true` allows the HTTP server to automatically disconnect clients when it runs out of connection slots.

**Impact**: 
- WebSocket connections drop unexpectedly even when the client is still connected
- Appears as "webserver doesn't connect"
- Can happen when handling many rapid requests

**Fix**: Disabled LRU purge:
```cpp
config.lru_purge_enable = false;  // Prevent unexpected disconnects
```

---

### 4. **Silent WebSocket Send Failures** (Medium)
**Issue**: In `ws_keepalive_task()`, if `httpd_ws_send_frame_async()` fails, the error is silently ignored.

**Impact**: 
- Keepalive pings fail without notice
- Client connections may timeout
- No visibility into network problems

**Fix**: Added error logging for send failures:
```cpp
esp_err_t send_ret = httpd_ws_send_frame_async(s_server, fds[i], &frame);
if (send_ret != ESP_OK) {
    ESP_LOGD(TAG, "WebSocket keepalive send failed for fd %d: %s", 
             fds[i], esp_err_to_name(send_ret));
}
```

---

### 5. **Double-Start Not Prevented** (Medium)
**Issue**: If `webserver_start()` is called twice (e.g., on reconnection), it could attempt to start a second server or leak the old handle.

**Impact**: 
- Resource leaks
- Unexpected behavior on network reconnect
- Potential crashes

**Fix**: Added guard to prevent double-start:
```cpp
if (s_server) {
    ESP_LOGW(TAG, "Webserver already running, skipping restart");
    return ESP_OK;
}
```

---

### 6. **Silent Failure in httpd_stop()** (Low)
**Issue**: If `httpd_stop()` fails, the error is not logged.

**Fix**: Added error logging:
```cpp
esp_err_t ret = httpd_stop(s_server);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Webserver stopped successfully");
} else {
    ESP_LOGW(TAG, "httpd_stop failed: %s", esp_err_to_name(ret));
}
```

---

## Testing Recommendations

1. **Monitor logs during WiFi reconnect** - Look for `httpd_start failed` messages
2. **Check keepalive messages** - Enable debug logging to see if pings are being sent
3. **Trigger WebSocket disconnects** - Kill WiFi and reconnect to test recovery
4. **Watch for LRU purge events** - With the fix, you shouldn't see "connection reset" without a good reason

## Expected Behavior After Fixes

- **On webserver startup failure**: Clear error message logged with reason code
- **On connection drop**: Either a real network error or explicit close, nothing silent
- **On keepalive timeout**: Log message showing which file descriptor failed
- **On reconnect**: Server properly reinitializes instead of getting stuck

## Related Code

- `src/webserver/Webserver.cpp` - Main webserver implementation
- `src/main.cpp` - Startup sequence in `wifi_and_webserver_task()`
- `src/wifi_prov/wifi_prov.cpp` - WiFi provisioning (also reviewed for issues)
