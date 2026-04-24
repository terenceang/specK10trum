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
#include "esp_netif.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static bool s_provisioning_started = false;
static bool s_watcher_started = false;
static bool s_wifi_init_done = false;
static bool s_sta_started = false;
static esp_event_handler_instance_t s_prov_handler = nullptr;
static esp_event_handler_instance_t s_wifi_handler = nullptr;
static esp_event_handler_instance_t s_ip_handler = nullptr;

static void nvs_watcher_task(void* pv)
{
    (void)pv;
    ESP_LOGI(TAG, "NVS watcher task started");
    while (1) {
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
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void wifi_event_cb(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            s_sta_started = true;
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
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
        s_provisioning_started = false;
        break;
    default:
        break;
    }
}

static esp_err_t ensure_wifi_initialized()
{
    if (s_wifi_init_done) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %d", err);
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %d", err);
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %d", err);
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_cb, NULL, &s_wifi_handler);
    if (err != ESP_OK) ESP_LOGW(TAG, "WIFI_EVENT register failed: %d", err);
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_cb, NULL, &s_ip_handler);
    if (err != ESP_OK) ESP_LOGW(TAG, "IP_EVENT register failed: %d", err);

    s_wifi_init_done = true;
    return ESP_OK;
}

static bool start_sta_from_saved_config()
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) failed: %d", err);
        return false;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %d", err);
        return false;
    }
    // esp_wifi_connect() is triggered by WIFI_EVENT_STA_START in wifi_event_cb.
    return true;
}

bool wifi_prov_start()
{
    if (!s_watcher_started) {
        xTaskCreate(nvs_watcher_task, "prov_watcher", 3072, NULL, 3, NULL);
        s_watcher_started = true;
    }

    if (s_provisioning_started) {
        ESP_LOGI(TAG, "Provisioning already running");
        return true;
    }

    if (ensure_wifi_initialized() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi subsystem init failed; cannot start provisioning");
        return false;
    }

    esp_err_t err = esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                        prov_event_cb, NULL, &s_prov_handler);
    if (err != ESP_OK) ESP_LOGW(TAG, "WIFI_PROV_EVENT register failed: %d", err);

    wifi_prov_mgr_config_t cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };

    err = wifi_prov_mgr_init(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %d", err);
        return false;
    }

    /* 
     * For testing: We skip the "already provisioned" check so the device
     * always broadcasts its setup service on boot.
     */
    bool provisioned = false;
    if (wifi_prov_mgr_is_provisioned(&provisioned) == ESP_OK && provisioned) {
        ESP_LOGI(TAG, "Device already provisioned, but starting BLE anyway for discovery...");
    }

    const char* service_name = "PROV_speck10";
    const char* pop = NULL;

    // Use SECURITY_0 (no password) for maximum compatibility during initial setup
    err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0, pop, service_name, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_start_provisioning failed: %d", err);
        wifi_prov_mgr_deinit();
        return false;
    }

    s_provisioning_started = true;
    ESP_LOGI(TAG, "Wi-Fi provisioning started (BLE). Service: %s", service_name);
    return true;
}

void wifi_prov_stop()
{
    if (!s_provisioning_started) {
        ESP_LOGI(TAG, "Provisioning not running");
        return;
    }
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_deinit();
    s_provisioning_started = false;
    ESP_LOGI(TAG, "Wi-Fi provisioning stopped");
}

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
        else          nvs_erase_key(h, "pass");
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
    if (!s_sta_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %d", err);
            return false;
        }
        // esp_wifi_connect() will fire from WIFI_EVENT_STA_START.
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
    return true;
}

void wifi_prov_clear_and_stop(bool clear_saved_creds)
{
    if (s_provisioning_started) {
        wifi_prov_mgr_stop_provisioning();
        wifi_prov_mgr_deinit();
        s_provisioning_started = false;
    }

    if (clear_saved_creds) {
        // Clear our helper namespace.
        nvs_handle_t h;
        if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
        // Clear the manager's own record so next boot re-enters provisioning.
        // (Safe even if manager is deinitted — it touches NVS directly.)
        wifi_prov_mgr_config_t cfg = { .scheme = wifi_prov_scheme_ble };
        if (wifi_prov_mgr_init(cfg) == ESP_OK) {
            wifi_prov_mgr_reset_provisioning();
            wifi_prov_mgr_deinit();
        }
        ESP_LOGI(TAG, "Cleared saved provisioning credentials");
    }
}

#else

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

#endif
