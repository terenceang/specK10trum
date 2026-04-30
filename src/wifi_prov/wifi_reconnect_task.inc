// Reconnect task: waits for requests, handles backoff and reconnect outside event loop
static void wifi_reconnect_task(void* pv) {
    int retry_count;
    while (1) {
        if (xQueueReceive(s_reconnect_queue, &retry_count, portMAX_DELAY) == pdTRUE) {
            if (retry_count > MAX_RECONNECT_RETRIES) {
                ESP_LOGE(TAG, "[reconnect task] Max retries reached (%d). Restarting BLE provisioning.", retry_count);
                display_setOverlayText("Wi-Fi Failed. Start BLE prov.", 0xF800);
                
                // Heavy work done in task context
                wifi_prov_stop();
                if (!wifi_prov_start()) {
                    ESP_LOGE(TAG, "[reconnect task] Failed to restart BLE provisioning");
                }
                continue;
            }

            uint32_t backoff_ms = get_reconnect_backoff_ms(retry_count);
            if (backoff_ms > 0) {
                ESP_LOGI(TAG, "[reconnect task] Waiting %lu ms before retry %d", backoff_ms, retry_count);
                char buf[64];
                snprintf(buf, sizeof(buf), "Wi-Fi Drop. Retry %d/%d (%lus)", 
                         retry_count, MAX_RECONNECT_RETRIES, backoff_ms/1000);
                display_setOverlayText(buf, 0xFFE0);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            }
            ESP_LOGI(TAG, "[reconnect task] Attempting WiFi reconnect (retry %d/%d)", retry_count, MAX_RECONNECT_RETRIES);
            esp_wifi_connect();
        }
    }
}
