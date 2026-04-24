#include "webserver/Webserver.h"
#include "webserver/index_html.h"
#include "input/Input.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static const char* TAG = "Webserver";
static const char* WWW_ROOT = "/spiffs/www";
static httpd_handle_t s_server = NULL;

/* Walk /spiffs/www and log each entry so we can verify what was actually
   flashed into the SPIFFS partition. Called once at server start. */
static void log_www_root(void)
{
    DIR* dir = opendir(WWW_ROOT);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed - SPIFFS not mounted, partition empty, "
                      "or directory missing. Did you run `pio run -t uploadfs`?",
                 WWW_ROOT);
        return;
    }

    ESP_LOGI(TAG, "Contents of %s:", WWW_ROOT);
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char p[256];
        snprintf(p, sizeof(p), "%s/%s", WWW_ROOT, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0) {
            ESP_LOGI(TAG, "  %-24s %8ld bytes", ent->d_name, (long)st.st_size);
        } else {
            ESP_LOGI(TAG, "  %-24s (stat failed)", ent->d_name);
        }
        count++;
    }
    closedir(dir);
    if (count == 0) {
        ESP_LOGW(TAG, "  (empty - flash spiffs with `pio run -t uploadfs`)");
    }
}

static const char* mime_for(const char* path)
{
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";
}

/* Wildcard GET handler: serves any file under /spiffs/www. Falls back to the
   embedded HTML only for "/" if index.html is missing on SPIFFS. */
static esp_err_t file_get_handler(httpd_req_t *req)
{
    /* Reject path traversal */
    if (strstr(req->uri, "..")) {
        ESP_LOGW(TAG, "rejecting traversal uri: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri");
        return ESP_FAIL;
    }

    char path[256];
    if (strcmp(req->uri, "/") == 0) {
        snprintf(path, sizeof(path), "%s/index.html", WWW_ROOT);
    } else {
        snprintf(path, sizeof(path), "%s%s", WWW_ROOT, req->uri);
    }

    /* Strip query string if present */
    char* q = strchr(path, '?');
    if (q) *q = '\0';

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (strcmp(req->uri, "/") == 0) {
            ESP_LOGW(TAG, "no %s on SPIFFS, serving embedded fallback", path);
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, INDEX_HTML_START, HTTPD_RESP_USE_STRLEN);
        }
        ESP_LOGW(TAG, "404 %s (looked for %s)", req->uri, path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
        return ESP_FAIL;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "fopen(%s) failed after stat OK", path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_for(path));
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=300");
    ESP_LOGI(TAG, "200 %s -> %s (%ld bytes)", req->uri, path, (long)st.st_size);

    /* httpd has a single worker by default, so a static scratch buffer is safe
       and avoids per-request malloc. 4 KiB matches typical TCP MSS well. */
    static char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) {
            ESP_LOGW(TAG, "send_chunk failed mid-transfer for %s", path);
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* WS /ws - keyboard events. Protocol: 3-byte binary frame [row, bit, pressed]. */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake from %s", req->uri);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    /* First call: discover frame size */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws_recv_frame (size probe) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WS close received");
        return ESP_OK;
    }
    if (ws_pkt.type == HTTPD_WS_TYPE_PING || ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        return ESP_OK;
    }
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "ws calloc(%d) failed", (int)ws_pkt.len);
        return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws_recv_frame (payload) failed: %s", esp_err_to_name(ret));
        free(buf);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && ws_pkt.len == 3) {
        uint8_t row     = buf[0];
        uint8_t bit     = buf[1];
        bool    pressed = buf[2] != 0;

        if (row < 8 && bit < 5) {
            uint8_t cur = input_getKeyboardRow(row);
            if (pressed) cur &= ~(1 << bit);   // active-low: pressed clears
            else         cur |=  (1 << bit);
            input_setKeyboardRow(row, cur);
            ESP_LOGD(TAG, "WS key row=%u bit=%u pressed=%d -> 0x%02X",
                     row, bit, pressed, cur);
        } else {
            ESP_LOGW(TAG, "WS key out of range: row=%u bit=%u", row, bit);
        }
    } else {
        ESP_LOGW(TAG, "unexpected WS frame: type=%d len=%d",
                 ws_pkt.type, (int)ws_pkt.len);
    }

    free(buf);
    return ESP_OK;
}

esp_err_t webserver_start()
{
    if (s_server) return ESP_OK;

    log_www_root();

    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;  // enable "*" matching
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;                      // headroom for fread+send

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register WS first so it isn't shadowed by the wildcard file handler. */
    httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket             = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol    = NULL,
    };
    err = httpd_register_uri_handler(s_server, &ws);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /ws failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "registered WS  /ws");
    }

    /* Wildcard catch-all for static files. */
    httpd_uri_t files = {
        .uri        = "/*",
        .method     = HTTP_GET,
        .handler    = file_get_handler,
        .user_ctx   = NULL,
        .is_websocket             = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol    = NULL,
    };
    err = httpd_register_uri_handler(s_server, &files);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /* failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "registered GET /*  (serves %s)", WWW_ROOT);
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
