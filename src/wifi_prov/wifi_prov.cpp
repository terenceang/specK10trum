#include "wifi_prov/wifi_prov.h"
#include <esp_log.h>

static const char* TAG = "wifi_prov";

#if defined(CONFIG_BT_ENABLED) && __has_include("wifi_provisioning/manager.h")

#include <esp_err.h>
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include <esp_netif.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "display/Display.h"

static SemaphoreHandle_t s_state_mutex = NULL;

static bool s_provisioning_started = false;
static bool s_watcher_started = false;
static bool s_wifi_init_done = false;
static bool s_sta_started = false;
static bool s_ble_fallback_mode = false;
static esp_event_handler_instance_t s_prov_handler = nullptr;
static esp_event_handler_instance_t s_wifi_handler = nullptr;
static esp_event_handler_instance_t s_ip_handler = nullptr;
static SemaphoreHandle_t s_wifi_connected_sem = NULL;
static char s_last_ip_str[32] = {0};

// Periodically checks for unapplied credentials and applies them.
// Skips if provisioning is active to avoid race condition during BLE write.
static void nvs_watcher_task(void* pv)
{
    (void)pv;
    ESP_LOGI(TAG, "NVS watcher task started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool prov_active = s_provisioning_started;
            xSemaphoreGive(s_state_mutex);

            if (prov_active) {
                continue;
            }
        }

        nvs_handle_t h;
        if (nvs_open("wifi_prov", NVS_READONLY, &h) == ESP_OK) {
            int32_t applied = 0;
            if (nvs_get_i32(h, "applied", &applied) != ESP_OK || applied == 0) {
                char ssid[33] = {0};
                char pass[65] = {0};
                size_t s_len = sizeof(ssid), p_len = sizeof(pass);
                if (nvs_get_str(h, "ssid", ssid, &s_len) == ESP_OK) {
                    nvs_get_str(h, "pass", pass, &p_len);
                    ESP_LOGI(TAG, "Watcher found unapplied credentials in NVS. Applying...");
                    nvs_close(h);
                    wifi_prov_apply_credentials(ssid, pass[0] ? pass : nullptr);
                } else {
                    nvs_close(h);
                }
            } else {
                nvs_close(h);
            }
        }
    }
}

static int s_reconnect_retries = 0;
static const int MAX_RECONNECT_RETRIES = 10;

// Exponential backoff: 1s, 2s, 4s, 8s, 16s, 30s max
static uint32_t get_reconnect_backoff_ms(int retry_count)
{
    if (retry_count <= 0) return 0;
    uint32_t delay_s = 1 << (retry_count - 1);
    return (delay_s > 30 ? 30 : delay_s) * 1000;
}

static void wifi_event_cb(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        ESP_LOGI(TAG, "WiFi event: id=%ld", id);
        switch (id) {
        case WIFI_EVENT_STA_START: {
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);

            wifi_config_t conf;
            esp_err_t conf_err = esp_wifi_get_config(WIFI_IF_STA, &conf);
            if (conf_err == ESP_OK && strlen((const char*)conf.sta.ssid) == 0) {
                ESP_LOGW(TAG, "STA_START: No saved credentials. Starting BLE provisioning.");
                display_setOverlayText("No Wi-Fi creds. Start BLE prov.", 0xF800);
                if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    bool prov_active = s_provisioning_started;
                    xSemaphoreGive(s_state_mutex);
                    if (!prov_active) wifi_prov_start();
                }
                break;
            }
            if (conf_err == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi station started; connecting to SSID: %s", (const char*)conf.sta.ssid);
            } else {
                ESP_LOGI(TAG, "Wi-Fi station started; connecting (config err: %d)...", conf_err);
            }
            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_sta_started = true;
                xSemaphoreGive(s_state_mutex);
            }
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi connected to AP; waiting for IP...");
            esp_wifi_set_ps(WIFI_PS_NONE);
            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_reconnect_retries = 0;
                xSemaphoreGive(s_state_mutex);
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)data;
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason: %d)", disconnected->reason);

            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_last_ip_str[0] = '\0';
                s_reconnect_retries++;
                int current_retry = s_reconnect_retries;
                xSemaphoreGive(s_state_mutex);

                if (current_retry > MAX_RECONNECT_RETRIES) {
                    ESP_LOGE(TAG, "Max retries reached (%d). Restarting BLE provisioning.", current_retry);
                    display_setOverlayText("Wi-Fi Failed. Start BLE prov.", 0xF800);
                    wifi_prov_stop();
                    if (!wifi_prov_start()) {
                        ESP_LOGE(TAG, "Failed to restart BLE provisioning");
                    }
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Wi-Fi Drop (R:%d) Retry %d/%d",
                             disconnected->reason, current_retry, MAX_RECONNECT_RETRIES);
                    display_setOverlayText(buf, 0xFFE0);

                    uint32_t backoff_ms = get_reconnect_backoff_ms(current_retry);
                    if (backoff_ms > 0) {
                        ESP_LOGI(TAG, "Waiting %lu ms before retry %d", backoff_ms, current_retry);
                        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                    }
                    ESP_LOGI(TAG, "Attempting WiFi reconnect (retry %d/%d)", current_retry, MAX_RECONNECT_RETRIES);
                    esp_wifi_connect();
                }
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        ESP_LOGI(TAG, "IP event: id=%ld", id);
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* evt = (ip_event_got_ip_t*)data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));

        esp_wifi_set_ps(WIFI_PS_NONE);

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            snprintf(s_last_ip_str, sizeof(s_last_ip_str), "IP: " IPSTR, IP2STR(&evt->ip_info.ip));
            xSemaphoreGive(s_state_mutex);
        }
        display_setOverlayText(s_last_ip_str, 0xFFFF);

            if (s_wifi_connected_sem) xSemaphoreGive(s_wifi_connected_sem);
        }
    }
}

static void prov_event_cb(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)base;
    switch (id) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started; waiting for credentials");
        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t* sta = (wifi_sta_config_t*)data;
        if (sta) {
            ESP_LOGI(TAG, "Received SSID='%s' via provisioning", (const char*)sta->ssid);
            // Persist to our own namespace too, for the watcher/CLI path.
            nvs_handle_t h;
            if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_str(h, "ssid", (const char*)sta->ssid);
                nvs_set_str(h, "pass", (const char*)sta->password);
                nvs_set_i32(h, "applied", 1);
                nvs_commit(h);
                nvs_close(h);
            }
        }
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t* reason = (wifi_prov_sta_fail_reason_t*)data;
        ESP_LOGW(TAG, "Provisioning credentials failed (reason=%d)", reason ? *reason : -1);
        // Reset to allow the user to retry without reflashing.
        wifi_prov_mgr_reset_sm_state_on_failure();
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning credentials accepted");
        break;
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "Provisioning finished; releasing manager resources");
        wifi_prov_mgr_deinit();
        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_provisioning_started = false;
            s_ble_fallback_mode = false;
            xSemaphoreGive(s_state_mutex);
        }
        break;
    default:
        break;
    }
}

static esp_err_t ensure_wifi_initialized()
{
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool already_init = s_wifi_init_done;
        xSemaphoreGive(s_state_mutex);
        if (already_init) return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    ESP_LOGI(TAG, "esp_netif_init: %s", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif_init returned ESP_ERR_INVALID_STATE (already initialized)");
    }

    err = esp_event_loop_create_default();
    ESP_LOGI(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default returned ESP_ERR_INVALID_STATE (already created)");
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_cb, NULL, &s_wifi_handler);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WIFI_EVENT handler registered");
    } else {
        ESP_LOGW(TAG, "WIFI_EVENT register failed: %s", esp_err_to_name(err));
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_cb, NULL, &s_ip_handler);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IP_EVENT handler registered");
    } else {
        ESP_LOGW(TAG, "IP_EVENT register failed: %s", esp_err_to_name(err));
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_wifi_init_done = true;
        xSemaphoreGive(s_state_mutex);
    }
    return ESP_OK;
}

// Start BLE-based Wi-Fi provisioning, or connect to saved credentials if provisioned.
bool wifi_prov_start()
{
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (!s_state_mutex) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            return false;
        }
    }

    if (!s_wifi_connected_sem) {
        s_wifi_connected_sem = xSemaphoreCreateBinary();
    }

    if (!s_watcher_started) {
        xTaskCreate(nvs_watcher_task, "prov_watcher", 3072, NULL, 3, NULL);
        s_watcher_started = true;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool already_running = s_provisioning_started;
        xSemaphoreGive(s_state_mutex);
        if (already_running) {
            ESP_LOGI(TAG, "Provisioning already running");
            return true;
        }
    }

    if (ensure_wifi_initialized() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi subsystem init failed; cannot start provisioning");
        return false;
    }

    if (s_prov_handler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, s_prov_handler);
        s_prov_handler = nullptr;
    }

    esp_err_t err = esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                        prov_event_cb, NULL, &s_prov_handler);
    if (err != ESP_OK) ESP_LOGW(TAG, "WIFI_PROV_EVENT register failed: %d", err);

    wifi_prov_mgr_config_t cfg = {};
    cfg.scheme = wifi_prov_scheme_ble;
    cfg.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT;

    err = wifi_prov_mgr_init(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %d", err);
        return false;
    }

    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);

    // Check if there are actual saved credentials
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    bool has_credentials = (strlen((const char*)conf.sta.ssid) > 0);

    if (provisioned && !has_credentials) {
        ESP_LOGW(TAG, "Provisioning flag set but SSID is empty! Resetting...");
        wifi_prov_mgr_reset_provisioning();
        provisioned = false;
    }

    // If in BLE fallback mode (WiFi failed), skip WiFi and go directly to BLE
    bool in_ble_fallback = false;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        in_ble_fallback = s_ble_fallback_mode;
        xSemaphoreGive(s_state_mutex);
    }

    if (!has_credentials || in_ble_fallback) {
        ESP_LOGW(TAG, "No saved credentials. Starting BLE provisioning.");
        wifi_prov_mgr_deinit();

        const char* service_name = "PROV_speck10";
        const char* pop = "12345678";

        err = esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                  prov_event_cb, NULL, &s_prov_handler);
        if (err != ESP_OK) ESP_LOGW(TAG, "WIFI_PROV_EVENT register failed: %d", err);

        wifi_prov_mgr_config_t cfg = {};
        cfg.scheme = wifi_prov_scheme_ble;
        cfg.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT;

        err = wifi_prov_mgr_init(cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %d", err);
            return false;
        }

        if (in_ble_fallback) {
            ESP_LOGI(TAG, "Starting BLE provisioning (WiFi connection failed)");
        } else {
            ESP_LOGI(TAG, "Starting BLE provisioning (no saved credentials)");
        }
        err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, service_name, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_prov_mgr_start_provisioning failed: %d", err);
            wifi_prov_mgr_deinit();
            return false;
        }

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_provisioning_started = true;
            xSemaphoreGive(s_state_mutex);
        }
        return true;
    }

    if (provisioned && has_credentials) {
        ESP_LOGI(TAG, "Device already provisioned. SSID: '%s'", (char*)conf.sta.ssid);
        ESP_LOGI(TAG, "Already provisioned; releasing manager and connecting to saved AP");
        wifi_prov_mgr_deinit();

        esp_err_t m_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (m_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_set_mode(STA) failed: %d", m_err);
            return false;
        }

        wifi_mode_t current_mode;
        if (esp_wifi_get_mode(&current_mode) == ESP_OK && current_mode == WIFI_MODE_STA) {
            ESP_LOGI(TAG, "Wi-Fi already in STA mode; disconnecting and reconnecting");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(100));
            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_sta_started = true;
                xSemaphoreGive(s_state_mutex);
            }
        } else {
            ESP_LOGI(TAG, "Starting Wi-Fi in STA mode");
            esp_err_t s_err = esp_wifi_start();
            if (s_err != ESP_OK && s_err != ESP_ERR_WIFI_CONN) {
                ESP_LOGE(TAG, "esp_wifi_start failed: %d", s_err);
                return false;
            }
            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_sta_started = true;
                xSemaphoreGive(s_state_mutex);
            }
        }

        ESP_LOGI(TAG, "Initiating WiFi connection attempt");
        esp_wifi_connect();
        return true;
    }

    return false;
}

// Stop Wi-Fi provisioning and cleanup resources.
void wifi_prov_stop()
{
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_provisioning_started) {
            xSemaphoreGive(s_state_mutex);
            ESP_LOGI(TAG, "Provisioning not running");
            return;
        }
        s_provisioning_started = false;
        s_ble_fallback_mode = false;
        xSemaphoreGive(s_state_mutex);
    }

    if (s_prov_handler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, s_prov_handler);
        s_prov_handler = nullptr;
    }

    (void)wifi_prov_mgr_stop_provisioning();
    (void)wifi_prov_mgr_deinit();
    ESP_LOGI(TAG, "Wi-Fi provisioning stopped");
}


// Block until Wi-Fi is connected and an IP is assigned, or timeout expires.
bool wifi_prov_wait_for_ip(uint32_t timeout_ms)
{
    if (!s_wifi_connected_sem) {
        s_wifi_connected_sem = xSemaphoreCreateBinary();
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_last_ip_str[0] != '\0') {
            char ip_buf[32];
            strncpy(ip_buf, s_last_ip_str, sizeof(ip_buf) - 1);
            xSemaphoreGive(s_state_mutex);
            display_setOverlayText(ip_buf, 0xFFFF);
            return true;
        }
        xSemaphoreGive(s_state_mutex);
    }

    if (xSemaphoreTake(s_wifi_connected_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            display_setOverlayText(s_last_ip_str, 0xFFFF);
            xSemaphoreGive(s_state_mutex);
        }
        return true;
    }
    return false;
}

// Apply credentials from NVS, provisioning, or CLI. Saves to NVS and connects.
bool wifi_prov_apply_credentials(const char* ssid, const char* password)
{
    if (!ssid) return false;

    if (ensure_wifi_initialized() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi subsystem init failed; cannot apply credentials");
        return false;
    }

    nvs_handle_t h;
    if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        if (password) nvs_set_str(h, "pass", password);
        else nvs_erase_key(h, "pass");
        nvs_set_i32(h, "applied", 1);
        nvs_commit(h);
        nvs_close(h);
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "Applying Wi-Fi credentials for SSID '%s'", ssid);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %d", err);
        return false;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %d", err);
        return false;
    }

    bool sta_started = false;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        sta_started = s_sta_started;
        xSemaphoreGive(s_state_mutex);
    }

    if (!sta_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %d", err);
            return false;
        }
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
    return true;
}

// Start BLE provisioning when WiFi fails (e.g., IP wait timeout).
// Stops WiFi and resets retry counter to start fresh.
bool wifi_prov_start_ble_fallback()
{
    ESP_LOGI(TAG, "WiFi failed; starting BLE provisioning fallback");

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_reconnect_retries = 0;
        s_ble_fallback_mode = true;
        xSemaphoreGive(s_state_mutex);
    }

    esp_wifi_stop();
    return wifi_prov_start();
}

// Convenience function to clear saved credentials and stop provisioning.
void wifi_prov_clear_and_stop(bool clear_saved_creds)
{
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_provisioning_started) {
            s_provisioning_started = false;
            xSemaphoreGive(s_state_mutex);

            if (s_prov_handler != nullptr) {
                esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, s_prov_handler);
                s_prov_handler = nullptr;
            }

            (void)wifi_prov_mgr_stop_provisioning();
            (void)wifi_prov_mgr_deinit();
        } else {
            xSemaphoreGive(s_state_mutex);
        }
    }

    if (clear_saved_creds) {
        nvs_handle_t h;
        if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            (void)nvs_commit(h);
            nvs_close(h);
        }
        wifi_prov_mgr_config_t cfg = {};
        cfg.scheme = wifi_prov_scheme_ble;
        if (wifi_prov_mgr_init(cfg) == ESP_OK) {
            wifi_prov_mgr_reset_provisioning();
            wifi_prov_mgr_deinit();
        }
        ESP_LOGI(TAG, "Cleared saved provisioning credentials");
    }
}

#else

extern "C" {

bool wifi_prov_start() {
    ESP_LOGW(TAG, "BLE provisioning not enabled in sdkconfig or headers missing.");
    return false;
}

void wifi_prov_stop() {
    ESP_LOGI(TAG, "Provisioning not active (BLE disabled).");
}

bool wifi_prov_apply_credentials(const char* ssid, const char* password)
{
    (void)ssid; (void)password;
    ESP_LOGW(TAG, "wifi_prov_apply_credentials: provisioning disabled or headers missing");
    return false;
}

void wifi_prov_clear_and_stop(bool clear_saved_creds)
{
    (void)clear_saved_creds;
    ESP_LOGI(TAG, "wifi_prov_clear_and_stop: provisioning disabled or headers missing");
}

bool wifi_prov_wait_for_ip(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true; // Don't block if provisioning is disabled
}

bool wifi_prov_start_ble_fallback()
{
    ESP_LOGW(TAG, "BLE fallback not available (BLE disabled or headers missing)");
    return false;
}

} // extern "C"

#endif
