#pragma once

#include <esp_err.h>

/**
 * @brief Start the webserver and websocket handler for virtual keyboard
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t webserver_start();

/**
 * @brief Stop the webserver
 */
void webserver_stop();
