#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_spiffs.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#include "display/Display.h"
#include "instrumentation/Instrumentation.h"
#include "expander/Expander.h"
#include "audio/Audio.h"
#include "spectrum/Tape.h"
#include "spectrum/Snapshot.h"
#include "spectrum/Spectrum48K.h"
#include "spectrum/Spectrum128K.h"
#include "input/Input.h"
#include "wifi_prov/wifi_prov.h"
#include "webserver/Webserver.h"
#include "../test/test_config.h"

static const char* TAG = "Main";

// Memory monitoring task: logs free heap and min free heap (internal & SPIRAM) every minute
#if MEMMON_DEBUG
static void memory_monitor_task(void* pvParameters) {
    (void)pvParameters;
    while (1) {
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t min_free_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
#if CONFIG_SPIRAM_SUPPORT
        size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t min_free_spiram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
#else
        size_t free_spiram = 0;
        size_t min_free_spiram = 0;
#endif
        ESP_LOGI("MemMon", "Heap (INTERNAL): free=%u, min=%u | Heap (SPIRAM): free=%u, min=%u",
            (unsigned)free_internal, (unsigned)min_free_internal,
            (unsigned)free_spiram, (unsigned)min_free_spiram);
        vTaskDelay(pdMS_TO_TICKS(60000)); // 1 minute
    }
}
#endif

// Get the stored model preference from NVS, default to 48K
static bool getStoredModelPreference(const char*& modelName, const char*& romFile) {
    nvs_handle_t nvs_h;
    esp_err_t ret = nvs_open("spectrum_config", NVS_READONLY, &nvs_h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, defaulting to 48K model");
        modelName = "48K";
        romFile = "/spiffs/48k.rom";
        return false;
    }

    char model[32];
    size_t len = sizeof(model);
    ret = nvs_get_str(nvs_h, "model", model, &len);
    nvs_close(nvs_h);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Model preference not found in NVS, defaulting to 48K");
        modelName = "48K";
        romFile = "/spiffs/48k.rom";
        return false;
    }

    if (strcmp(model, "128k") == 0) {
        modelName = "128K";
        romFile = "/spiffs/128k.rom";
        return true;
    } else {
        modelName = "48K";
        romFile = "/spiffs/48k.rom";
        return false;
    }
}

// Create spectrum instance based on model preference
static SpectrumBase* createSpectrumInstance(bool is128K) {
    if (is128K) {
        ESP_LOGI(TAG, "Creating Spectrum 128K instance");
        return new Spectrum128K();
    } else {
        ESP_LOGI(TAG, "Creating Spectrum 48K instance");
        return new Spectrum48K();
    }
}

// Global state
static SpectrumBase* spectrum = nullptr;
static bool display_ready = false;

// Universal boot status printer with separator
static inline void boot_printStatus(int step, int total, const char* message, uint16_t color) {
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "Boot %d/%d: %s", step, total, message);
    ESP_LOGI(TAG, "--------------------------------------------------");
    if (display_ready) {
        display_setOverlayText(message, color);
        display_boot_update();
    }
}

// Simple timestamp logger for boot profiling
static int64_t s_boot_start_us = 0;
static inline void log_ts(const char* tag, const char* msg) {
    int64_t now_us = esp_timer_get_time();
    if (s_boot_start_us == 0) s_boot_start_us = now_us;
    int64_t ms = (now_us - s_boot_start_us) / 1000;
    ESP_LOGI(tag, "%6lld ms: %s", ms, msg);
}

// Test runner declaration
#if RUN_ALL_TESTS
extern "C" void run_all_tests(IMemoryBus* spectrum, const char* modelName);
#else
static inline void run_all_tests(IMemoryBus* /*spectrum*/, const char* /*modelName*/) { /* no-op when tests disabled */ }
#endif

static bool mountSPIFFS() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mounted but failed to query info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs (total: %u, used: %u)", (unsigned)total, (unsigned)used);
    }
    return true;
}

static void emulator_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Emulator task started on core %d", xPortGetCoreID());

    const int T_STATES_PER_FRAME = 69888; // 3.5 MHz / 50.08 Hz
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t frameTicks = pdMS_TO_TICKS(20);

    while (true) {
        // Process all commands in the queue (replaces webserver_apply_pending)
        WebCommand cmd;
        auto& queue = webserver_get_command_queue();
        while (queue.pop(cmd)) {
            switch (cmd.type) {
                case WebCommandType::Reset:
                    display_clearOverlay();
                    display_pause_for_reset();
                    spectrum->reset();
                    display_resume_after_reset();
                    ESP_LOGI(TAG, "Spectrum Reset");
                    break;
                case WebCommandType::ChangeModel:
                {
                    display_setOverlayText("CHANGING MODEL...", 0xFFFF);
                    bool is128K = (cmd.arg1 == "128k");
                    const char* romFile = is128K ? "/spiffs/128k.rom" : "/spiffs/48k.rom";
                    SpectrumBase* newSpectrum = createSpectrumInstance(is128K);
                    if (newSpectrum) {
                        if (newSpectrum->loadROM(romFile)) {
                            delete spectrum;
                            spectrum = newSpectrum;
                            display_pause_for_reset();
                            spectrum->reset();
                            display_resume_after_reset();
                            display_clearOverlay();
                            ESP_LOGI(TAG, "Model changed to %s", cmd.arg1.c_str());
                        } else {
                            delete newSpectrum;
                            display_setOverlayText("ROM LOAD FAILED", 0xF800);
                            ESP_LOGE(TAG, "Failed to load ROM for model change");
                        }
                    }
                    break;
                }
                case WebCommandType::LoadFile:
                {
                    const char* path = cmd.arg1.c_str();
                    display_setOverlayText("LOADING...", 0xFFFF);
                    const char* ext = strrchr(path, '.');
                    if (ext && (strcasecmp(ext, ".tap") == 0 || strcasecmp(ext, ".tzx") == 0 || strcasecmp(ext, ".tsx") == 0)) {
                        if (!spectrum->tape().load(path)) display_setOverlayText("TAPE LOAD FAILED", 0xF800);
                        else display_clearOverlay();
                    } else if (ext && strcasecmp(ext, ".rom") == 0) {
                        if (!spectrum->loadROM(path)) display_setOverlayText("ROM LOAD FAILED", 0xF800);
                        else {
                            display_pause_for_reset();
                            spectrum->reset();
                            display_resume_after_reset();
                            display_clearOverlay();
                        }
                    } else {
                        spectrum->tape().stop();
                        if (!Snapshot::load(spectrum, path)) display_setOverlayText("SNAPSHOT LOAD FAILED", 0xF800);
                        else display_clearOverlay();
                    }
                    break;
                }
                case WebCommandType::TapeCmd:
                    if (cmd.arg1 == "load") {
                        spectrum->tape().load(cmd.arg2.c_str());
                    } else if (cmd.arg1 == "play") {
                        spectrum->tape().play();
                    } else if (cmd.arg1 == "stop") {
                        spectrum->tape().stop();
                    } else if (cmd.arg1 == "rewind") {
                        spectrum->tape().rewind();
                    } else if (cmd.arg1 == "ffwd") {
                        spectrum->tape().fastForward();
                    } else if (cmd.arg1 == "pause") {
                        spectrum->tape().pause();
                    } else if (cmd.arg1 == "eject") {
                        spectrum->tape().eject();
                    }
                    break;
                case WebCommandType::TapeInstantLoad:
                    spectrum->tape().setMode(TapeMode::INSTANT);
                    spectrum->tape().play();
                    break;
                case WebCommandType::TapeSetMode:
                    spectrum->tape().setMode(static_cast<TapeMode>(cmd.int_arg1));
                    break;
                case WebCommandType::TapeSetMonitor:
                    spectrum->setTapeMonitorEnabled(cmd.int_arg1 != 0);
                    break;
                case WebCommandType::KeyboardInput:
                {
                    uint8_t row = (uint8_t)cmd.int_arg1;
                    uint8_t bit = (uint8_t)(cmd.int_arg2 & 0xFF);
                    bool pressed = (bool)((cmd.int_arg2 >> 8) & 0xFF);
#if KEYBOARD_DEBUG
                    ESP_LOGI(TAG, "KB CMD row=%u bit=%u pressed=%u", (unsigned)row, (unsigned)bit, (unsigned)pressed);
#endif
                    if (row == 0xFF) {
                        input_setJoystickBit(bit, pressed);
                    } else {
                        input_update_key(row, bit, pressed);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        int tStates = 0;
        instr_cpu_start();
        while (tStates < T_STATES_PER_FRAME) {
            tStates += spectrum->step();
        }
        instr_cpu_end();

        display_trigger_frame(spectrum);

        // Render and play beeper audio for this frame.
        audio_play_frame(spectrum);

        // Keep frame cadence near 50Hz and reduce scheduler-induced audio jitter.
        vTaskDelayUntil(&lastWake, frameTicks);
    }
}

extern "C" void app_main(void) {
    // Delay a bit to allow serial monitor to connect
    vTaskDelay(pdMS_TO_TICKS(2000));
    s_boot_start_us = esp_timer_get_time();
    log_ts(TAG, "app_main start");

    // 1. IO Expander
    log_ts(TAG, "=== STEP 1/16: IO Expander ===");
    expander_init();

    // 2. Display
    log_ts(TAG, "=== STEP 2/16: Display Init ===");
    if (display_init()) {
        display_ready = true;
        showSplashScreen();
        boot_printStatus(2, 16, "Display Init", 0xFFFF);
        expander_set_led(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        expander_set_led(false);
    } else {
        ESP_LOGE(TAG, "Display init FAILED");
    }

    // 3. PSRAM
    log_ts(TAG, "=== STEP 3/16: PSRAM Init ===");
    boot_printStatus(3, 16, "PSRAM Init", 0xFFFF);
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM init failed!");
    }

    // 4. SPIFFS
    log_ts(TAG, "=== STEP 4/16: SPIFFS Mount ===");
    boot_printStatus(4, 16, "SPIFFS Mount", 0xFFFF);
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed!");
    }

    // 5. Splash Screen (already shown after display init)
    log_ts(TAG, "=== STEP 5/16: Splash Screen ===");
    boot_printStatus(5, 16, "Splash Screen", 0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. Wi-Fi
    log_ts(TAG, "=== STEP 6/16: Wi-Fi Connect ===");

    bool wifi_connected = false;
    if (!wifi_prov_start()) {
        ESP_LOGE(TAG, "wifi_prov_start() failed");
        boot_printStatus(6, 16, "Wi-Fi Init Failed!", 0xF800);
        while(1) vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
        boot_printStatus(6, 16, "Wi-Fi Connect", 0xFFFF);
        
        uint32_t elapsed = 0;
        bool ble_fallback_started = false;

        while (!wifi_connected) {
            if (wifi_prov_wait_for_ip(500)) {
                wifi_connected = true;
                break;
            }
            
            elapsed += 500;
            
            // After 30 seconds of failing to connect with saved credentials, 
            // ensure BLE provisioning is active if not already.
            if (elapsed >= 30000 && !ble_fallback_started) {
                ESP_LOGW(TAG, "Wi-Fi connection taking long, ensuring BLE provisioning is active...");
                boot_printStatus(6, 16, "BLE Provisioning", 0xFFE0);
                wifi_prov_start_ble_fallback();
                ble_fallback_started = true;
            }

            // Optional: Provide some visual feedback every few seconds
            if (elapsed % 5000 == 0) {
                if (ble_fallback_started) {
                    ESP_LOGI(TAG, "Still waiting for BLE provisioning... (%us)", (unsigned)(elapsed / 1000));
                } else {
                    ESP_LOGI(TAG, "Still trying saved Wi-Fi... (%us)", (unsigned)(elapsed / 1000));
                }
            }
        }

        if (wifi_connected) {
            boot_printStatus(6, 16, "Wi-Fi ✓", 0x07E0);
            ESP_LOGI(TAG, "Wi-Fi connected!");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // 7. Model selection and ROM check
    log_ts(TAG, "=== STEP 7/16: Model & ROM Check ===");
    boot_printStatus(7, 16, "Model Selection", 0xFFFF);
    const char* modelName;
    const char* romFile;
    bool is128K = getStoredModelPreference(modelName, romFile);
    ESP_LOGI(TAG, "Using Spectrum %s model", modelName);

    struct stat st;
    if (stat(romFile, &st) != 0) {
        ESP_LOGE(TAG, "ROM %s not found! Aborting.", romFile);
        boot_printStatus(7, 16, "ROM MISSING!", 0xF800);
        return;
    }

    // 8. Spectrum CPU
    log_ts(TAG, "=== STEP 8/16: CPU Create ===");
    boot_printStatus(8, 16, "CPU Create", 0xFFFF);
    spectrum = createSpectrumInstance(is128K);
    if (!spectrum) {
        ESP_LOGE(TAG, "Failed to create Spectrum hardware!");
        boot_printStatus(8, 16, "CPU CREATE FAILED", 0xF800);
        return;
    }

    // 9. Load ROM + Reset
    log_ts(TAG, "=== STEP 9/16: Load ROM ===");
    boot_printStatus(9, 16, "Load ROM", 0xFFFF);
    if (!spectrum->loadROM(romFile)) {
        ESP_LOGE(TAG, "ROM load failed. Aborting.");
        boot_printStatus(9, 16, "ROM LOAD FAILED", 0xF800);
        return;
    }
    spectrum->reset();

    // Pre-build all 128 attribute LUTs so the renderer never allocates on the
    // hot path (avoids first-frame stalls when a snapshot exposes new attrs).
    // (Lazy-built in renderActiveArea via static cache instead)

#if RUN_ALL_TESTS
    run_all_tests(spectrum, modelName);
    spectrum->reset();
#endif

    // 10. Webserver
    log_ts(TAG, "=== STEP 10/16: Webserver ===");
    boot_printStatus(10, 16, "Webserver", 0xFFFF);
    if (wifi_connected) {
        if (webserver_ensure_started(spectrum) == ESP_OK && webserver_is_running()) {
            ESP_LOGI(TAG, "Webserver started successfully");
        } else {
            ESP_LOGW(TAG, "Webserver failed to start");
        }
    }

    // 11. Input
    log_ts(TAG, "=== STEP 11/16: Input Init ===");
    boot_printStatus(11, 16, "Input Init", 0xFFFF);
    input_init();

    // 12. Audio
    log_ts(TAG, "=== STEP 12/16: Audio Init ===");
    boot_printStatus(12, 16, "Audio Init", 0xFFFF);
    if (!audio_init()) {
        ESP_LOGW(TAG, "Audio init failed; continuing without sound");
    }

    // 13. Keyboard Client
    log_ts(TAG, "=== STEP 13/16: Keyboard ===");
    if (wifi_connected && webserver_is_running()) {
        // Get the device IP address
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        char ip_str[16] = "unknown";
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        }

        char kb_msg[64];
        snprintf(kb_msg, sizeof(kb_msg), "Go to http://%s", ip_str);
        boot_printStatus(13, 16, kb_msg, 0xFFFF);

        ESP_LOGI(TAG, "Waiting for WebSocket keyboard client at http://%s", ip_str);
        while (!webserver_is_ws_client_connected()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        boot_printStatus(13, 16, "Keyboard ✓", 0x07E0);
        ESP_LOGI(TAG, "WebSocket keyboard client connected");
    } else {
        boot_printStatus(13, 16, "Keyboard (none)", 0xFFE0);
        ESP_LOGW(TAG, "No keyboard available (Wi-Fi or webserver not running)");
    }

    // 14. Beep
    log_ts(TAG, "=== STEP 14/16: Beep ===");
    boot_printStatus(14, 16, "Beep", 0xFFFF);
    audio_play_tone(880, 120);

    // 15. Start Emulator Task
    log_ts(TAG, "=== STEP 15/16: Emulator ===");
    display_boot_log_hide();
    xTaskCreatePinnedToCore(emulator_task, "emulator", 8192, NULL, 5, NULL, 0);

    // 16. Idle
    log_ts(TAG, "=== STEP 16/16: Boot Done ===");
    log_ts(TAG, "Boot complete. Entering idle loop.");
    // Start memory monitoring task
#if MEMMON_DEBUG
    xTaskCreatePinnedToCore(memory_monitor_task, "mem_mon", 3072, NULL, 1, NULL, tskNO_AFFINITY);
#endif
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
