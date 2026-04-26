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
    
    // Add caching for static assets to speed up keyboard load on refresh
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

/* API: List available files (ROMs, snapshots, tapes) */
static esp_err_t files_list_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Listing files from %s...", SPIFFS_ROOT);
    DIR* dir = opendir(SPIFFS_ROOT);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open spiffs root: %s", SPIFFS_ROOT);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage error");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_chunk(req, "[", 1);

    struct dirent* ent;
    bool first = true;
    int count = 0;

    // Decide which file types to expose based on the request URI.
    // If called via /api/snapshots -> only snapshot files (.z80, .sna)
    // If called via /api/tapes     -> only tape files (.tap, .tzx, .tsx)
    // If called via /api/files or unspecified -> expose snapshots and tapes (backwards-compatible)
    const char* uri = req->uri;
    bool wantSnapshots = false, wantTapes = false, wantRoms = false;
    if (strstr(uri, "/api/snapshots")) {
        wantSnapshots = true;
    } else if (strstr(uri, "/api/tapes")) {
        wantTapes = true;
    } else {
        // default: list snapshots and tapes (and .roms)
        wantSnapshots = true;
        wantTapes = true;
        wantRoms = true;
    }

    while ((ent = readdir(dir)) != NULL) {
        const char* ext = strrchr(ent->d_name, '.');
        if (!ext) continue;
        bool include = false;
        if (wantSnapshots && (strcasecmp(ext, ".z80") == 0 || strcasecmp(ext, ".sna") == 0)) include = true;
        if (wantTapes && (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0)) include = true;
        if (wantRoms && (strcasecmp(ext, ".rom") == 0)) include = true;
        if (!include) continue;

        ESP_LOGD(TAG, "Found valid file: %s", ent->d_name);
        char buf[512];
        int len = snprintf(buf, sizeof(buf), "%s\"%s\"", first ? "" : ",", ent->d_name);
        httpd_resp_send_chunk(req, buf, len);
        first = false;
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "Returned %d files", count);
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* API: Load a file (snapshot, ROM, or tape) */
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
        
        // Clear any stuck boot/status overlays as we're about to run content
        display_clearOverlay();
        // Wipe both framebuffers to ensure splash/boot artifacts are gone
        ESP_LOGI(TAG, "Clearing display for new content");
        display_clear();

        const char* ext = strrchr(s_pending_load_path, '.');
        if (ext && (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0)) {
            ESP_LOGI(TAG, "Loading tape: %s", s_pending_load_path);
            if (!spectrum->tape().load(s_pending_load_path)) {
                ESP_LOGW(TAG, "Tape load failed: %s", s_pending_load_path);
            }
        } else if (ext && strcasecmp(ext, ".rom") == 0) {
            ESP_LOGI(TAG, "Loading ROM: %s", s_pending_load_path);
            if (!spectrum->loadROM(s_pending_load_path)) {
                ESP_LOGW(TAG, "ROM load failed: %s", s_pending_load_path);
            }
            spectrum->reset();
        } else {
            ESP_LOGI(TAG, "Applying snapshot: %s", s_pending_load_path);
            if (!Snapshot::load(spectrum, s_pending_load_path)) {
                ESP_LOGW(TAG, "Snapshot load failed: %s", s_pending_load_path);
            }
        }
        // Loading a snapshot or ROM supersedes any queued reset.
        s_pending_reset = false;
        return;
    }

    if (s_pending_reset) {
        s_pending_reset = false;
        ESP_LOGI(TAG, "Applying reset");
        display_clearOverlay();
        display_clear();
        spectrum->reset();

        // If an instaload was queued by the same UI action, run the BASIC
        // ROM init now so the system variables, screen, IM 1 + EI, and stack
        // are set up before we splice user code into memory and JP to it.
        // Without this the user code lands on a zeroed sysvar area with
        // interrupts disabled and a SP of 0xFFFF — most games hang or crash.
        if (s_pending_instant_load) {
            Z80* cpu = spectrum->getCPU();
            const int max_tstates = 8000000; // ~2.3 s of emulated time, plenty
            int spent = 0;
            // 48K BASIC main loop entry. The ROM reaches this within
            // ~250K T-states the first time and re-enters every keystroke.
            // Stop on the first hit so we don't tie the emulator task up.
            while (spent < max_tstates && cpu->pc != 0x12A9) {
                spent += spectrum->step();
            }
            if (cpu->pc == 0x12A9) {
                ESP_LOGI(TAG, "ROM init complete after %d T-states (PC=0x12A9)", spent);
            } else {
                ESP_LOGW(TAG, "ROM init did not reach BASIC main loop (PC=0x%04X after %d T-states)",
                         cpu->pc, spent);
            }
        }
    }

    if (s_pending_instant_load) {
        s_pending_instant_load = false;
        ESP_LOGI(TAG, "Applying instaload");
        spectrum->tape().instaload(spectrum);
    }
}

/* API: Tape Control */
static esp_err_t tape_handler(httpd_req_t *req)
{
    char buf[1024];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        return ESP_OK;
    }

    char cmd[32];
    if (httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cmd");
        return ESP_OK;
    }

    if (strcmp(cmd, "load") == 0) {
        char filename[256];
        if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) == ESP_OK) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", SPIFFS_ROOT, filename);
            s_spectrum->tape().load(path);
        }
    } else if (strcmp(cmd, "mode") == 0) {
        char mode[16];
        if (httpd_query_key_value(buf, "mode", mode, sizeof(mode)) == ESP_OK) {
            if (strcmp(mode, "instant") == 0) s_spectrum->tape().setMode(TapeMode::INSTANT);
            else if (strcmp(mode, "normal") == 0) s_spectrum->tape().setMode(TapeMode::NORMAL);
            else if (strcmp(mode, "player") == 0) s_spectrum->tape().setMode(TapeMode::PLAYER);
        }
    } else if (strcmp(cmd, "play") == 0) {
        s_spectrum->tape().setMode(TapeMode::NORMAL);
        s_spectrum->tape().play();
    }
    else if (strcmp(cmd, "stop") == 0) s_spectrum->tape().stop();
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
    const int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        // This is the handshake call. 
        ESP_LOGI(TAG, "Keyboard handshake (fd=%d)", fd);
        // Clear IP overlay quietly.
        display_setOverlayText(NULL, 0);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    // Peek at the frame header to get type and length.
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        uint8_t buffer[128];
        size_t frame_len = ws_pkt.len;
        if (frame_len > sizeof(buffer) - 1) {
            ESP_LOGW(TAG, "WS frame too large (%zu); closing", frame_len);
            return ESP_FAIL; 
        }

        ws_pkt.payload = buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, frame_len);
        if (ret != ESP_OK) return ret;

        ESP_LOGI(TAG, "WS Received: type=%d, len=%zu", ws_pkt.type, frame_len);

        if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && frame_len == 3) {
            uint8_t row = buffer[0], bit = buffer[1], pressed = buffer[2];
            ESP_LOGI(TAG, "Keyboard binary: row=%d, bit=%d, pressed=%d", row, bit, pressed);
            if (row < 8 && bit < 5) {
                uint8_t cur = input_getKeyboardRow(row);
                if (pressed) cur &= ~(1 << bit); else cur |= (1 << bit);
                input_setKeyboardRow(row, cur);
            }
        } else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            buffer[frame_len] = '\0';
            const char* json = (const char*)buffer;
            ESP_LOGI(TAG, "WS Text: %s", json);
            // Crude JSON parsing for tape commands
            if (strstr(json, "\"cmd\":\"tape_play\"")) s_spectrum->tape().play();
            else if (strstr(json, "\"cmd\":\"tape_stop\"")) s_spectrum->tape().stop();
            else if (strstr(json, "\"cmd\":\"tape_rewind\"")) s_spectrum->tape().rewind();
            else if (strstr(json, "\"cmd\":\"tape_ffwd\"")) s_spectrum->tape().fastForward();
            else if (strstr(json, "\"cmd\":\"tape_pause\"")) s_spectrum->tape().pause();
            else if (strstr(json, "\"cmd\":\"tape_eject\"")) s_spectrum->tape().eject();
            else if (strstr(json, "\"cmd\":\"tape_instaload\"")) {
                s_pending_reset = true;
                s_pending_instant_load = true;
            }
            else if (strstr(json, "\"cmd\":\"tape_mode_instaload\"")) s_spectrum->tape().setMode(TapeMode::INSTANT);
            else if (strstr(json, "\"cmd\":\"tape_mode_normal\"")) s_spectrum->tape().setMode(TapeMode::NORMAL);
            else if (strstr(json, "\"cmd\":\"tape_mode_player\"")) s_spectrum->tape().setMode(TapeMode::PLAYER);
        } else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
            ESP_LOGI(TAG, "Keyboard session closed (fd=%d)", fd);
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
    config.max_uri_handlers = 16;
    config.stack_size       = 10240; // Increased stack
    config.recv_wait_timeout   = 60; // Increased timeout
    config.send_wait_timeout   = 30;
    config.keep_alive_enable   = true;
    config.keep_alive_idle     = 30;
    config.keep_alive_interval = 10;
    config.keep_alive_count    = 3;

    ESP_LOGI(TAG, "Starting Webserver...");
    if (httpd_start(&s_server, &config) != ESP_OK) return ESP_FAIL;

    httpd_uri_t ws = { "/ws", HTTP_GET, ws_handler, NULL, true, false, NULL };
    httpd_register_uri_handler(s_server, &ws);

    httpd_uri_t api_list = { "/api/files", HTTP_GET, files_list_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_list);

    /* Backwards-compatible alias expected by some clients */
    httpd_uri_t api_snapshots = { "/api/snapshots", HTTP_GET, files_list_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_snapshots);

    // Explicit tapes listing endpoint
    httpd_uri_t api_tapes = { "/api/tapes", HTTP_GET, files_list_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_tapes);

    httpd_uri_t api_load = { "/api/load", HTTP_GET, file_load_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_load);

    // Alias for old JS versions/cache
    httpd_uri_t api_load_old = { "/api/load-snapshot", HTTP_GET, file_load_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_load_old);

    httpd_uri_t api_reset = { "/api/reset", HTTP_GET, reset_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_reset);

    httpd_uri_t api_tape = { "/api/tape", HTTP_GET, tape_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &api_tape);

    httpd_uri_t files = { "/*", HTTP_GET, file_get_handler, NULL, false, false, NULL };
    httpd_register_uri_handler(s_server, &files);

    return ESP_OK;
}

void webserver_stop()
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
}
