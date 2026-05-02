#include "webserver/Webserver.h"
#include "webserver/index_html.h"
#include "input/Input.h"
#include "spectrum/Snapshot.h"
#include "spectrum/Spectrum48K.h"
#include "spectrum/Spectrum128K.h"
#include "display/Display.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "webserver/command_queue.h"
#include "../../test/test_config.h"

static const char* TAG = "Webserver";
static const char* WWW_ROOT = "/spiffs/www";
static const char* SPIFFS_ROOT = "/spiffs";
static httpd_handle_t s_server = NULL;
static SpectrumBase* s_spectrum = NULL;

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
    const char* uri = req->uri;
    bool wantSnapshots = strstr(uri, "/api/snapshots") != NULL;
    bool wantTapes = strstr(uri, "/api/tapes") != NULL;
    if (!wantSnapshots && !wantTapes) { wantSnapshots = wantTapes = true; }

    const char* dirPath;
    if (wantTapes) dirPath = "/spiffs/tapes";
    else if (wantSnapshots) dirPath = "/spiffs/snapshots";
    else dirPath = SPIFFS_ROOT;
    DIR* dir = opendir(dirPath);
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
    const char* ext = strrchr(filename, '.');
    const char* filePath = SPIFFS_ROOT;
    if (ext) {
        if (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0) {
            filePath = "/spiffs/tapes";
        } else if (strcasecmp(ext, ".z80") == 0 || strcasecmp(ext, ".sna") == 0) {
            filePath = "/spiffs/snapshots";
        }
    }
    
    char fullPath[512];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", filePath, filename);

    WebCommand command;
    command.type = WebCommandType::LoadFile;
    command.arg1 = fullPath;
    webserver_get_command_queue().push(command);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    WebCommand command;
    command.type = WebCommandType::Reset;
    webserver_get_command_queue().push(command);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t health_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    const char* response = "{\"status\":\"ok\",\"server\":\"running\"}";
    return httpd_resp_sendstr(req, response);
}

static esp_err_t model_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char buf[256];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        // GET current model
        nvs_handle_t nvs_h;
        esp_err_t ret = nvs_open("spectrum_config", NVS_READONLY, &nvs_h);
        char model[32] = "48k";
        if (ret == ESP_OK) {
            size_t len = sizeof(model);
            nvs_get_str(nvs_h, "model", model, &len);
            nvs_close(nvs_h);
        }
        char response[100];
        snprintf(response, sizeof(response), "{\"status\":\"ok\",\"model\":\"%s\"}", model);
        return httpd_resp_sendstr(req, response);
    }

    // POST to change model
    char model[32];
    if (httpd_query_key_value(buf, "model", model, sizeof(model)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing model parameter");
        return ESP_OK;
    }

    if (strcmp(model, "48k") != 0 && strcmp(model, "128k") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid model (must be 48k or 128k)");
        return ESP_OK;
    }

    nvs_handle_t nvs_h;
    esp_err_t ret = nvs_open("spectrum_config", NVS_READWRITE, &nvs_h);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS open failed");
        return ESP_OK;
    }

    ret = nvs_set_str(nvs_h, "model", model);
    if (ret != ESP_OK) {
        nvs_close(nvs_h);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_OK;
    }

    ret = nvs_commit(nvs_h);
    nvs_close(nvs_h);

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS commit failed");
        return ESP_OK;
    }

    WebCommand command;
    command.type = WebCommandType::ChangeModel;
    command.arg1 = model;
    webserver_get_command_queue().push(command);

    char response[100];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"model\":\"%s\"}", model);
    return httpd_resp_sendstr(req, response);
}

static esp_err_t tape_handler(httpd_req_t *req)
{
    char buf[1024];
    char cmd[32];
    WebCommand command;
    bool hasQuery = httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK;
    bool hasCmd = hasQuery && httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) == ESP_OK;

    if (!hasCmd || strcmp(cmd, "status") == 0) {
        int loaded = 0;
        int playing = 0;
        int paused = 0;
        int currentBlock = 0;
        int totalBlocks = 0;
        const char* modeName = "unknown";
        if (s_spectrum) {
            auto& tape = s_spectrum->tape();
            loaded = tape.isLoaded() ? 1 : 0;
            playing = tape.isPlaying() ? 1 : 0;
            paused = tape.isPaused() ? 1 : 0;
            currentBlock = tape.totalBlocks() > 0 ? tape.currentBlockIndex() + 1 : 0;
            totalBlocks = tape.totalBlocks();
            switch (tape.getMode()) {
                case TapeMode::INSTANT: modeName = "instant"; break;
                case TapeMode::NORMAL: modeName = "normal"; break;
                case TapeMode::PLAYER: modeName = "player"; break;
            }
        }
        char response[256];
        int len = snprintf(response, sizeof(response), "{\"loaded\":%d,\"playing\":%d,\"paused\":%d,\"mode\":\"%s\",\"currentBlock\":%d,\"totalBlocks\":%d}",
            loaded, playing, paused, modeName, currentBlock, totalBlocks);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, response);
        return ESP_OK;
    }

    if (strcmp(cmd, "load") == 0) {
        char filename[256];
        if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) == ESP_OK) {
            char path[512];
            snprintf(path, sizeof(path), "/spiffs/tapes/%s", filename);
            command.type = WebCommandType::TapeCmd;
            command.arg1 = "load";
            command.arg2 = path;
        }
    } else if (strcmp(cmd, "play") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "play";
    } else if (strcmp(cmd, "stop") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "stop";
    } else if (strcmp(cmd, "rewind") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "rewind";
    } else if (strcmp(cmd, "ffwd") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "ffwd";
    } else if (strcmp(cmd, "pause") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "pause";
    } else if (strcmp(cmd, "eject") == 0) {
        command.type = WebCommandType::TapeCmd;
        command.arg1 = "eject";
    } else if (strcmp(cmd, "instant_load") == 0) {
        command.type = WebCommandType::TapeInstantLoad;
    }
    if (command.type != WebCommandType::None) {
        webserver_get_command_queue().push(command);
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
#if KEYBOARD_DEBUG
            ESP_LOGI(TAG, "KB WS RX row=%u bit=%u pressed=%u", (unsigned)row, (unsigned)bit, (unsigned)pressed);
#endif
            WebCommand command;
            command.type = WebCommandType::KeyboardInput;
            command.int_arg1 = row;
            command.int_arg2 = (bit & 0xFF) | ((pressed & 0xFF) << 8);
            webserver_get_command_queue().push(command);
        } else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            buffer[ws_pkt.len] = '\0';
            const char* json = (const char*)buffer;
            WebCommand command;
            if (strstr(json, "\"cmd\":\"tape_play\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "play"; }
            else if (strstr(json, "\"cmd\":\"tape_stop\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "stop"; }
            else if (strstr(json, "\"cmd\":\"tape_rewind\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "rewind"; }
            else if (strstr(json, "\"cmd\":\"tape_ffwd\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "ffwd"; }
            else if (strstr(json, "\"cmd\":\"tape_pause\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "pause"; }
            else if (strstr(json, "\"cmd\":\"tape_eject\"")) { command.type = WebCommandType::TapeCmd; command.arg1 = "eject"; }
            else if (strstr(json, "\"cmd\":\"tape_instaload\"")) { command.type = WebCommandType::TapeInstantLoad; }
            else if (strstr(json, "\"cmd\":\"tape_mode_instaload\"")) { command.type = WebCommandType::TapeSetMode; command.int_arg1 = (int)TapeMode::INSTANT; }
            else if (strstr(json, "\"cmd\":\"tape_mode_normal\"")) { command.type = WebCommandType::TapeSetMode; command.int_arg1 = (int)TapeMode::NORMAL; }
            else if (strstr(json, "\"cmd\":\"tape_mode_player\"")) { command.type = WebCommandType::TapeSetMode; command.int_arg1 = (int)TapeMode::PLAYER; }
            else if (strstr(json, "\"cmd\":\"tape_monitor_on\"")) { command.type = WebCommandType::TapeSetMonitor; command.int_arg1 = 1; }
            else if (strstr(json, "\"cmd\":\"tape_monitor_off\"")) { command.type = WebCommandType::TapeSetMonitor; command.int_arg1 = 0; }
            if (command.type != WebCommandType::None) {
                webserver_get_command_queue().push(command);
            }
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
    // If server is already running, don't start again
    if (s_server) {
        ESP_LOGW(TAG, "Webserver already running, skipping restart");
        s_spectrum = spectrum;
        return ESP_OK;
    }

    s_spectrum = spectrum;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 12;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;
    config.backlog_conn = 8;
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

    static bool keepalive_started = false;
    if (!keepalive_started) {
        keepalive_started = true;
        xTaskCreatePinnedToCore(ws_keepalive_task, "ws_keepalive", 4096, NULL, 5, NULL, 1);
        ESP_LOGI(TAG, "WebSocket keepalive task started");
    }

    static const httpd_uri_t uris[] = {
        { "/api/health",    HTTP_GET, health_handler,     NULL, false, false, NULL },
        { "/ws",            HTTP_GET, ws_handler,         NULL, true, false, NULL },
        { "/api/files",     HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/tapes",     HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/snapshots", HTTP_GET, files_list_handler, NULL, false, false, NULL },
        { "/api/load",      HTTP_GET, file_load_handler,  NULL, false, false, NULL },
        { "/api/reset",     HTTP_GET, reset_handler,     NULL, false, false, NULL },
        { "/api/tape",      HTTP_GET, tape_handler,      NULL, false, false, NULL },
        { "/api/model",     HTTP_GET, model_handler,      NULL, false, false, NULL },
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

bool webserver_is_running(void)
{
    return s_server != NULL;
}

bool webserver_wait_for_ws_client(uint32_t timeout_ms)
{
    if (!s_server) return false;
    uint32_t elapsed = 0;
    const uint32_t POLL_MS = 200;
    while (elapsed < timeout_ms) {
        size_t clients = 16;
        int fds[16];
        if (httpd_get_client_list(s_server, &clients, fds) == ESP_OK) {
            for (size_t i = 0; i < clients; i++) {
                if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                    return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        elapsed += POLL_MS;
    }
    return false;
}

bool webserver_is_ws_client_connected(void)
{
    if (!s_server) return false;
    size_t clients = 16;
    int fds[16];
    if (httpd_get_client_list(s_server, &clients, fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
                return true;
        }
    }
    return false;
}

esp_err_t webserver_ensure_started(SpectrumBase* spectrum)
{
    if (webserver_is_running()) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Webserver not running, starting now");
    return webserver_start(spectrum);
}

// Global command queue instance for HTTP/WebSocket to emulator communication
static WebCommandQueue s_commandQueue;

// Expose accessor for emulator task
WebCommandQueue& webserver_get_command_queue() {
    return s_commandQueue;
}
