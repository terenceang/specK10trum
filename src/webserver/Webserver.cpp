#include "webserver/Webserver.h"
#include "webserver/index_html.h"
#include "input/Input.h"
#include "spectrum/Snapshot.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static const char* TAG = "Webserver";
static const char* WWW_ROOT = "/spiffs/www";
static const char* SPIFFS_ROOT = "/spiffs";
static httpd_handle_t s_server = NULL;
static SpectrumBase* s_spectrum = NULL;

// Pending state changes posted by HTTP handlers, drained by the emulator task
// in webserver_apply_pending() between frames. Direct mutation from the HTTP
// task races the running CPU on the other core (memset of 48K RAM, z80_init,
// page-pointer rebuild) and was the cause of "Reset does nothing".
static volatile bool s_pending_reset = false;
static volatile bool s_pending_load  = false;
static char s_pending_load_path[512];

static const char* mime_for(const char* path)
{
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0) return "text/html";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript";
    if (strcasecmp(ext, ".css") == 0) return "text/css";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

/* Wildcard GET handler for static files. */
static esp_err_t file_get_handler(httpd_req_t *req)
{
    // Silently ignore favicon requests
    if (strcmp(req->uri, "/favicon.ico") == 0) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }

    if (strstr(req->uri, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK; 
    }

    char path[1024];
    if (strcmp(req->uri, "/") == 0) {
        snprintf(path, sizeof(path), "%s/index.html", WWW_ROOT);
    } else {
        snprintf(path, sizeof(path), "%s%s", WWW_ROOT, req->uri);
    }

    // Strip query string
    char* q = strchr(path, '?');
    if (q) *q = '\0';

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (strcmp(req->uri, "/") == 0) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, INDEX_HTML_START, HTTPD_RESP_USE_STRLEN);
        }
        ESP_LOGW(TAG, "File not found: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_OK;
    }

    httpd_resp_set_type(req, mime_for(path));
    static char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* API: List snapshots (*.sna, *.z80) */
static esp_err_t snapshots_list_handler(httpd_req_t *req)
{
    DIR* dir = opendir(SPIFFS_ROOT);
    if (!dir) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage error");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_chunk(req, "[", 1);

    struct dirent* ent;
    bool first = true;
    while ((ent = readdir(dir)) != NULL) {
        const char* ext = strrchr(ent->d_name, '.');
        if (!ext) continue;
        if (strcasecmp(ext, ".z80") == 0 || strcasecmp(ext, ".sna") == 0) {
            char buf[512];
            int len = snprintf(buf, sizeof(buf), "%s\"%s\"", first ? "" : ",", ent->d_name);
            httpd_resp_send_chunk(req, buf, len);
            first = false;
        }
    }
    closedir(dir);
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* API: Load a snapshot */
static esp_err_t snapshot_load_handler(httpd_req_t *req)
{
    char buf[1024];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        return ESP_OK;
    }

    char filename[256];
    if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file");
        return ESP_OK;
    }

    snprintf(s_pending_load_path, sizeof(s_pending_load_path), "%s/%s", SPIFFS_ROOT, filename);
    s_pending_load = true;

    ESP_LOGI(TAG, "API Load queued: %s", s_pending_load_path);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* API: Reset the emulator */
static esp_err_t reset_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Reset queued");
    s_pending_reset = true;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

void webserver_apply_pending(SpectrumBase* spectrum)
{
    if (!spectrum) return;

    if (s_pending_load) {
        s_pending_load = false;
        ESP_LOGI(TAG, "Applying snapshot: %s", s_pending_load_path);
        if (!Snapshot::load(spectrum, s_pending_load_path)) {
            ESP_LOGW(TAG, "Snapshot load failed: %s", s_pending_load_path);
        }
        // Loading a snapshot supersedes any queued reset.
        s_pending_reset = false;
        return;
    }

    if (s_pending_reset) {
        s_pending_reset = false;
        ESP_LOGI(TAG, "Applying reset");
        spectrum->reset();
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    // First call: peek at the frame header.
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        // The framework calls us once with method == HTTP_GET right after the
        // upgrade handshake completes; there is no frame yet, so recv_frame
        // returns an error. Treat that as the connect event.
        if (req->method == HTTP_GET) {
            ESP_LOGI(TAG, "Keyboard connected (fd=%d)", fd);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Keyboard disconnected (fd=%d, recv err=%d)", fd, ret);
        return ret;
    }

    if (ws_pkt.len > 0) {
        // Key frames are 3-byte BINARY; keep a small stack buffer to avoid
        // heap churn on the hot path. We must always drain the payload bytes
        // we said we'd read, otherwise they sit in the kernel buffer and the
        // next header read mis-parses them ("WS frame is not properly
        // masked"), desynchronising the stream.
        uint8_t payload[16];

        // Browser tab close arrives as a CLOSE frame (type=8, 2-byte status
        // payload). Drain it and return OK so httpd unwinds the session
        // without logging "uri handler execution failed".
        if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
            uint16_t status = 0;
            if (ws_pkt.len >= 2 && ws_pkt.len <= sizeof(payload)) {
                ws_pkt.payload = payload;
                if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) == ESP_OK) {
                    status = (uint16_t)((payload[0] << 8) | payload[1]);
                }
            }
            ESP_LOGI(TAG, "Keyboard disconnected (fd=%d, status=%u)", fd, status);
            return ESP_OK;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_BINARY ||
            ws_pkt.len > sizeof(payload)) {
            ESP_LOGW(TAG, "Unexpected WS frame type=%d len=%u; closing",
                     (int)ws_pkt.type, (unsigned)ws_pkt.len);
            return ESP_FAIL;
        }
        ws_pkt.payload = payload;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            // EAGAIN/ENOTCONN here usually means the client tore the socket
            // down mid-frame; httpd will close it on our return.
            ESP_LOGW(TAG, "Keyboard disconnected (fd=%d, payload err=%d)", fd, ret);
            return ret;
        }

        if (ws_pkt.len == 3) {
            uint8_t row = payload[0], bit = payload[1];
            bool pressed = payload[2] != 0;
            if (row < 8 && bit < 5) {
                uint8_t cur = input_getKeyboardRow(row);
                uint8_t old = cur;
                if (pressed) cur &= ~(1 << bit); else cur |= (1 << bit);
                if (cur != old) {
                    input_setKeyboardRow(row, cur);
                    ESP_LOGI(TAG, "Key %s: row=%u bit=%u row_state=0x%02X",
                             pressed ? "DOWN" : "UP  ",
                             (unsigned)row, (unsigned)bit, (unsigned)cur);
                }
            } else {
                ESP_LOGW(TAG, "Invalid WS key: row=%u bit=%u", (unsigned)row, (unsigned)bit);
            }
        } else {
            ESP_LOGW(TAG, "Unexpected BINARY len=%u (want 3)", (unsigned)ws_pkt.len);
        }
    }
    return ESP_OK;
}

esp_err_t webserver_start(SpectrumBase* spectrum)
{
    s_spectrum = spectrum;
    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;
    config.stack_size       = 8192;
    // Default recv_wait_timeout is 5 s. With marginal Wi-Fi the 9-byte WS
    // keystroke frame can split across segments and the payload arrives
    // late, so we let it breathe. TCP keepalive culls truly dead sockets
    // before the LRU sweep does.
    config.recv_wait_timeout   = 30;
    config.send_wait_timeout   = 10;
    config.keep_alive_enable   = true;
    config.keep_alive_idle     = 30;
    config.keep_alive_interval = 10;
    config.keep_alive_count    = 3;

    ESP_LOGI(TAG, "Starting Webserver...");
    if (httpd_start(&s_server, &config) != ESP_OK) return ESP_FAIL;

    httpd_uri_t ws = { "/ws", HTTP_GET, ws_handler, NULL, true, false, NULL };
    httpd_register_uri_handler(s_server, &ws);

    httpd_uri_t api_list = { "/api/snapshots", HTTP_GET, snapshots_list_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_list);

    httpd_uri_t api_load = { "/api/load-snapshot", HTTP_GET, snapshot_load_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_load);

    httpd_uri_t api_reset = { "/api/reset", HTTP_GET, reset_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_reset);

    httpd_uri_t files = { "/*", HTTP_GET, file_get_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &files);

    return ESP_OK;
}

void webserver_stop()
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
