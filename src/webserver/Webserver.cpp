#include "webserver/Webserver.h"
#include "webserver/index_html.h"
#include "input/Input.h"
#include "spectrum/Snapshot.h"
#include "display/Display.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "Webserver";
static const char* WWW_ROOT = "/spiffs/www";
static const char* SPIFFS_ROOT = "/spiffs";
static httpd_handle_t s_server = NULL;
static SpectrumBase* s_spectrum = NULL;

// Pending state changes posted by HTTP handlers
static volatile bool s_pending_reset = false;
static volatile bool s_pending_load  = false;
static volatile bool s_pending_instant_load = false;
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

static esp_err_t file_get_handler(httpd_req_t *req)
{
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
    char* q = strchr(path, '?');
    if (q) *q = '\0';
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (strcmp(req->uri, "/") == 0) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, INDEX_HTML_START, HTTPD_RESP_USE_STRLEN);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_OK;
    }
    httpd_resp_set_type(req, mime_for(path));
    if (strstr(path, ".js") || strstr(path, ".css") || strstr(path, ".png") || strstr(path, ".svg")) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000");
    } else if (strstr(path, ".html")) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    }
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

static esp_err_t files_list_handler(httpd_req_t *req)
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
    const char* uri = req->uri;
    bool wantSnapshots = strstr(uri, "/api/snapshots") != NULL;
    bool wantTapes = strstr(uri, "/api/tapes") != NULL;
    if (!wantSnapshots && !wantTapes) { wantSnapshots = wantTapes = true; }
    while ((ent = readdir(dir)) != NULL) {
        const char* ext = strrchr(ent->d_name, '.');
        if (!ext) continue;
        bool include = false;
        if (wantSnapshots && (strcasecmp(ext, ".z80") == 0 || strcasecmp(ext, ".sna") == 0)) include = true;
        if (wantTapes && (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0)) include = true;
        if (!include) continue;
        char buf[512];
        int len = snprintf(buf, sizeof(buf), "%s\"%s\"", first ? "" : ",", ent->d_name);
        httpd_resp_send_chunk(req, buf, len);
        first = false;
    }
    closedir(dir);
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t file_load_handler(httpd_req_t *req)
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
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    s_pending_reset = true;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

void webserver_apply_pending(SpectrumBase* spectrum)
{
    if (!spectrum) return;
    if (s_pending_load) {
        s_pending_load = false;
        display_setOverlayText("LOADING...", 0xFFFF);
        const char* ext = strrchr(s_pending_load_path, '.');
        if (ext && (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0)) {
            if (!spectrum->tape().load(s_pending_load_path)) display_setOverlayText("TAPE LOAD FAILED", 0xF800);
            else display_clearOverlay();
        } else if (ext && strcasecmp(ext, ".rom") == 0) {
            if (!spectrum->loadROM(s_pending_load_path)) display_setOverlayText("ROM LOAD FAILED", 0xF800);
            else {
                display_pause_for_reset();
                spectrum->reset();
                display_resume_after_reset();
                display_clearOverlay();
            }
        } else {
            spectrum->tape().stop();
            if (!Snapshot::load(spectrum, s_pending_load_path)) display_setOverlayText("SNAPSHOT LOAD FAILED", 0xF800);
            else display_clearOverlay();
        }
        s_pending_reset = false;
        return;
    }
    if (s_pending_reset) {
        s_pending_reset = false;
        display_clearOverlay();
        display_pause_for_reset();
        spectrum->reset();
        display_resume_after_reset();
        if (s_pending_instant_load) {
            Z80* cpu = spectrum->getCPU();
            const int max_tstates = 8000000;
            int spent = 0;
            while (spent < max_tstates && cpu->pc != 0x12A9) { spent += spectrum->step(); }
        }
    }
    if (s_pending_instant_load) {
        s_pending_instant_load = false;
        spectrum->tape().instaload(spectrum);
    }
}

static esp_err_t tape_handler(httpd_req_t *req)
{
    char buf[1024];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return ESP_FAIL;
    char cmd[32];
    if (httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) != ESP_OK) return ESP_FAIL;
    if (strcmp(cmd, "load") == 0) {
        char filename[256];
        if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) == ESP_OK) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", SPIFFS_ROOT, filename);
            s_spectrum->tape().load(path);
        }
    } else if (strcmp(cmd, "play") == 0) {
        s_spectrum->tape().setMode(TapeMode::NORMAL);
        s_spectrum->tape().play();
    } else if (strcmp(cmd, "stop") == 0) s_spectrum->tape().stop();
    else if (strcmp(cmd, "rewind") == 0) s_spectrum->tape().rewind();
    else if (strcmp(cmd, "ffwd") == 0) s_spectrum->tape().fastForward();
    else if (strcmp(cmd, "pause") == 0) s_spectrum->tape().pause();
    else if (strcmp(cmd, "eject") == 0) s_spectrum->tape().eject();
    else if (strcmp(cmd, "instant_load") == 0) {
        s_pending_reset = true;
        s_pending_instant_load = true;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        display_clearOverlay();
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;
    if (ws_pkt.len > 0) {
        uint8_t buffer[128];
        if (ws_pkt.len > sizeof(buffer) - 1) return ESP_FAIL; 
        ws_pkt.payload = buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) return ret;
        if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && ws_pkt.len == 3) {
            uint8_t row = buffer[0], bit = buffer[1], pressed = buffer[2];
            if (row < 8 && bit < 5) {
                uint8_t cur = input_getKeyboardRow(row);
                if (pressed) cur &= ~(1 << bit); else cur |= (1 << bit);
                input_setKeyboardRow(row, cur);

                // Verification: Log key events and verify they were stored
                uint8_t verified = input_getKeyboardRow(row);
                if (row == 5 || row == 7 || verified != cur) {
                    ESP_LOGI(TAG, "Key: row=%d bit=%d pressed=%d row_val=0x%02X verified=0x%02X",
                             row, bit, pressed, cur, verified);
                }            }
        } else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            buffer[ws_pkt.len] = '\0';
            const char* json = (const char*)buffer;
            if (strstr(json, "\"cmd\":\"tape_play\"")) s_spectrum->tape().play();
            else if (strstr(json, "\"cmd\":\"tape_stop\"")) s_spectrum->tape().stop();
            else if (strstr(json, "\"cmd\":\"tape_rewind\"")) s_spectrum->tape().rewind();
            else if (strstr(json, "\"cmd\":\"tape_ffwd\"")) s_spectrum->tape().fastForward();
            else if (strstr(json, "\"cmd\":\"tape_pause\"")) s_spectrum->tape().pause();
            else if (strstr(json, "\"cmd\":\"tape_eject\"")) s_spectrum->tape().eject();
            else if (strstr(json, "\"cmd\":\"tape_instaload\"")) { s_pending_reset = true; s_pending_instant_load = true; }
            else if (strstr(json, "\"cmd\":\"tape_mode_instaload\"")) s_spectrum->tape().setMode(TapeMode::INSTANT);
            else if (strstr(json, "\"cmd\":\"tape_mode_normal\"")) s_spectrum->tape().setMode(TapeMode::NORMAL);
            else if (strstr(json, "\"cmd\":\"tape_mode_player\"")) s_spectrum->tape().setMode(TapeMode::PLAYER);
        }
    }
    return ESP_OK;
}

static void ws_keepalive_task(void* arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!s_server) continue;

        size_t clients = 16;
        int fds[16];
        esp_err_t ret = httpd_get_client_list(s_server, &clients, fds);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "httpd_get_client_list failed: %s", esp_err_to_name(ret));
            continue;
        }

        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                const char* msg = "ping";
                httpd_ws_frame_t frame = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t*)msg,
                    .len = strlen(msg)
                };
                esp_err_t send_ret = httpd_ws_send_frame_async(s_server, fds[i], &frame);
                if (send_ret != ESP_OK) {
                    ESP_LOGD(TAG, "WebSocket keepalive send failed for fd %d: %s", fds[i], esp_err_to_name(send_ret));
                }
            }
        }
    }
}

esp_err_t webserver_start(SpectrumBase* spectrum)
{
    s_spectrum = spectrum;

    // If server is already running, don't start again
    if (s_server) {
        ESP_LOGW(TAG, "Webserver already running, skipping restart");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = false;  // Disable LRU purge to prevent unexpected disconnects
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 2;
    config.keep_alive_count = 3;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s (%d)", esp_err_to_name(ret), ret);
        s_server = NULL;
        return ret;
    }

    // Start keepalive ping task to keep WebSocket connections alive
    static bool keepalive_started = false;
    if (!keepalive_started) {
        keepalive_started = true;
        xTaskCreatePinnedToCore(ws_keepalive_task, "ws_keepalive", 4096, NULL, 5, NULL, 1);
        ESP_LOGI(TAG, "WebSocket keepalive task started");
    }

    static const httpd_uri_t uris[] = {
        { "/ws",            HTTP_GET, ws_handler,         NULL, true, false, NULL },
        { "/api/files",     HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/tapes",     HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/snapshots", HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/load",      HTTP_GET, file_load_handler,  NULL, false, false, NULL },
        { "/api/reset",     HTTP_GET, reset_handler,     NULL, false, false, NULL },
        { "/api/tape",      HTTP_GET, tape_handler,      NULL, false, false, NULL },
        { "/*",             HTTP_GET, file_get_handler,   NULL, false, false, NULL }
    };

    for (size_t i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) {
        ret = httpd_register_uri_handler(s_server, &uris[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register URI handler: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Webserver started successfully");
    return ESP_OK;
}

void webserver_stop()
{
    if (s_server) {
        esp_err_t ret = httpd_stop(s_server);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Webserver stopped successfully");
        } else {
            ESP_LOGW(TAG, "httpd_stop failed: %s", esp_err_to_name(ret));
        }
    }
    s_server = NULL;
}
