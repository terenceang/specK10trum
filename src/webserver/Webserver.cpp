#include "webserver/Webserver.h"
#include "webserver/index_html.h"
#include "input/Input.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>

static const char* TAG = "Webserver";
static httpd_handle_t s_server = NULL;

/* GET / - Serve embedded index.html */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    ESP_LOGI(TAG, "HTTP GET / - serving index.html");
    return httpd_resp_send(req, INDEX_HTML_START, HTTPD_RESP_USE_STRLEN);
}

/* WS /ws - Handle virtual keyboard events */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    /* Get the frame length */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "WS frame len=%d type=%d", ws_pkt.len, ws_pkt.type);
    if (ws_pkt.len > 0) {
        buf = (uint8_t*)calloc(1, ws_pkt.len);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        /* 
           Protocol: [row(1 byte), bit(1 byte), pressed(1 byte)]
           row: 0-7
           bit: 0-4
           pressed: 1=pressed, 0=released
        */
        if (ws_pkt.len == 3) {
            uint8_t row = buf[0];
            uint8_t bit = buf[1];
            bool pressed = buf[2] != 0;

            if (row < 8 && bit < 5) {
                ESP_LOGI(TAG, "WS key: row=%d bit=%d pressed=%d", row, bit, pressed);
                uint8_t current = input_getKeyboardRow(row);
                if (pressed) {
                    current &= ~(1 << bit); // Clear bit (active low)
                } else {
                    current |= (1 << bit);  // Set bit (active low)
                }
                input_setKeyboardRow(row, current);
            }
        }
        free(buf);
    }
    return ESP_OK;
}

esp_err_t webserver_start()
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server: %s", esp_err_to_name(err));
        return err;
    }

    /* URI handler for root */
    httpd_uri_t root = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = root_get_handler,
        .user_ctx  = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
    };
    err = httpd_register_uri_handler(s_server, &root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register '/' handler: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Registered GET / handler");
    }

    /* URI handler for WS */
    httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
    };
    err = httpd_register_uri_handler(s_server, &ws);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register '/ws' handler: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Registered GET /ws handler");
    }

    ESP_LOGI(TAG, "Webserver ready. Open http://<device-ip>/ in a browser.");
    return ESP_OK;
}

void webserver_stop()
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
