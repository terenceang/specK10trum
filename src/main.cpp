#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_spiffs.h>
#include <esp_timer.h>
#include "display/Display.h"
#include "instrumentation/Instrumentation.h"
#include "expander/Expander.h"
#include "audio/Audio.h"
#include "spectrum/Tape.h"
#include "input/Input.h"
#include "wifi_prov/wifi_prov.h"
#include <esp_wifi.h>
#include "webserver/Webserver.h"

// Centralised test config (defines RUN_ALL_TESTS when not overridden)
#include "test_config.h"

// ============================================
// SELECT YOUR MODEL HERE
// ============================================
#define SPECTRUM_MODEL_48K
//#define SPECTRUM_MODEL_128K
// ============================================

#if defined(SPECTRUM_MODEL_128K)
    #include "spectrum/Spectrum128K.h"
    #define MODEL_NAME "128K"
    #define ROM_FILE "/spiffs/128k.rom"
    typedef Spectrum128K SpectrumHardware;
#elif defined(SPECTRUM_MODEL_48K)
    #include "spectrum/Spectrum48K.h"
    #define MODEL_NAME "48K"
    #define ROM_FILE "/spiffs/48k.rom"
    typedef Spectrum48K SpectrumHardware;
#else
    #error "Please define either SPECTRUM_MODEL_48K or SPECTRUM_MODEL_128K"
#endif

// Test runner declaration
#if RUN_ALL_TESTS
extern "C" void run_all_tests(IMemoryBus* spectrum, const char* modelName);
#else
static inline void run_all_tests(IMemoryBus* /*spectrum*/, const char* /*modelName*/) { /* no-op when tests disabled */ }
#endif

static const char* TAG = "Main";
static SpectrumBase* spectrum = nullptr;

// Simple timestamp logger for boot profiling (milliseconds since start)
static int64_t s_boot_start_us = 0;
static inline void log_ts(const char* tag, const char* msg) {
    int64_t now_us = esp_timer_get_time();
    if (s_boot_start_us == 0) s_boot_start_us = now_us;
    int64_t ms = (now_us - s_boot_start_us) / 1000;
    ESP_LOGI(tag, "%6lld ms: %s", ms, msg);
}

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

    DIR* dir = opendir("/spiffs");
    if (dir != NULL) {
        ESP_LOGI(TAG, "Contents of /spiffs:");
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  - %s", entry->d_name);
        }
        closedir(dir);
    } else {
        ESP_LOGW(TAG, "Failed to open /spiffs directory");
    }

    return true;
}

static void emulator_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Emulator task started on core %d", xPortGetCoreID());
    display_boot_log_hide();

    const int T_STATES_PER_FRAME = 69888; // 3.5 MHz / 50.08 Hz
    
    while (1) {
        // Apply any pending commands from the webserver (reset, snapshot load)
        webserver_apply_pending(spectrum);

        int tStates = 0;
        instr_cpu_start();
        while (tStates < T_STATES_PER_FRAME) {
            tStates += spectrum->step();
        }
        instr_cpu_end();

        display_trigger_frame(spectrum);
        // Render and play beeper audio for this frame.
        // This call blocks on the I2S stream buffer when it is full, which is
        // what synchronises the emulator to real time.
        audio_play_frame(spectrum);
        
        // Yield to allow other tasks (like Webserver) to run
        vTaskDelay(1);
    }
}

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    s_boot_start_us = esp_timer_get_time();
    log_ts(TAG, "app_main start");

    // 1. IO Expander
    expander_init();

    // 2. Display
    bool display_ready = false;
    display_boot_log_add_step(2, 16, "Display Init");
    if (!display_init()) {
        ESP_LOGE(TAG, "Display init FAILED");
    } else {
        display_ready = true;
        // Standalone LED blink after display init
        expander_set_led(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        expander_set_led(false);
    }

    // 3. PSRAM
    log_ts(TAG, "=== STEP 3/16: PSRAM Init ===");
    display_boot_log_add_step(3, 16, "PSRAM Init");
    vTaskDelay(pdMS_TO_TICKS(20));
#ifdef CONFIG_SPIRAM
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM init failed!");
    }
#endif

    // 4. SPIFFS (must be before splash screen)
    log_ts(TAG, "=== STEP 4/16: SPIFFS Mount ===");
    display_boot_log_add_step(4, 16, "SPIFFS Mount");
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed!");
    }

    // 5. Splash Screen (now SPIFFS is available)
    log_ts(TAG, "=== STEP 5/16: Splash Screen ===");
    display_boot_log_add_step(5, 16, "Splash Screen");
    vTaskDelay(pdMS_TO_TICKS(20));
    if (display_ready) {
        display_showBootScreen();
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. Wi-Fi (blocking until connected or user provisions via BLE)
    bool wifi_connected = false;
    log_ts(TAG, "=== STEP 6/16: Wi-Fi Connect ===");
    display_boot_log_add_step(6, 16, "Wi-Fi Connect");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!wifi_prov_start()) {
        ESP_LOGW(TAG, "wifi_prov_start() failed");
    } else {
        ESP_LOGI(TAG, "Waiting for Wi-Fi connection (60s timeout). If device was previously provisioned via BLE, it will attempt to reconnect to the saved network.");
        if (wifi_prov_wait_for_ip(60000)) {
            wifi_connected = true;
            ESP_LOGI(TAG, "Wi-Fi connected!");
        } else {
            ESP_LOGW(TAG, "Wi-Fi connection timeout after 60 seconds");
            ESP_LOGW(TAG, "Troubleshooting: Check the serial log for WIFI_EVENT or disconnection reasons (e.g., 'MIC_FAILURE' = wrong password)");
            ESP_LOGW(TAG, "Diagnostic checklist:");
            ESP_LOGW(TAG, "  1. SSID Broadcasting: Verify AP broadcasts the SSID (not hidden)");
            ESP_LOGW(TAG, "  2. Band Support: Device is 2.4GHz only (not 5GHz)");
            ESP_LOGW(TAG, "  3. Password: Exact match required (case-sensitive)");
            ESP_LOGW(TAG, "  4. AP Availability: Router reachable and not in sleep mode");
            ESP_LOGW(TAG, "  5. Signal Strength: Device may be too far from AP");
            ESP_LOGW(TAG, "  6. Security Type: WPA2/WPA3 supported; WEP/Open networks work");
            ESP_LOGW(TAG, "  If MIC_FAILURE appears in logs: password is incorrect");
            log_ts(TAG, "=== STEP 6/16: BLE Prov ===");
            display_boot_log_add_step(6, 16, "BLE Prov");
            if (display_ready) display_boot_update();
            vTaskDelay(pdMS_TO_TICKS(20));
            if (display_ready) display_setOverlayText("Use BLE app to provision", 0xFFE0);
            if (display_ready) display_boot_update();
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGI(TAG, "Starting BLE provisioning fallback...");
            wifi_prov_start_ble_fallback();

            log_ts(TAG, "=== STEP 6/16: Waiting BLE ===");
            display_boot_log_add_step(6, 16, "Waiting BLE");
            if (display_ready) display_boot_update();
            vTaskDelay(pdMS_TO_TICKS(20));
            if (display_ready) display_setOverlayText("Waiting for Wi-Fi credentials", 0xFFE0);
            if (display_ready) display_boot_update();
            ESP_LOGI(TAG, "Blocking until Wi-Fi credentials provisioned via BLE...");
            if (wifi_prov_wait_for_ip(UINT32_MAX)) {
                wifi_connected = true;
                ESP_LOGI(TAG, "Wi-Fi connected after BLE provisioning!");
                if (display_ready) display_setOverlayText("Wi-Fi provisioned!", 0x07E0);
                if (display_ready) display_boot_update();
                vTaskDelay(pdMS_TO_TICKS(2000));
            } else {
                ESP_LOGE(TAG, "BLE provisioning failed or no IP obtained");
                if (display_ready) display_setOverlayText("Provisioning failed!", 0xF800);
                if (display_ready) display_boot_update();
                vTaskDelay(pdMS_TO_TICKS(2000));
                return;
            }
        }
    }

    // 7. ROM check
    log_ts(TAG, "=== STEP 7/16: ROM Check ===");
    display_boot_log_add_step(7, 16, "ROM Check");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    struct stat st;
    if (stat(ROM_FILE, &st) != 0) {
        ESP_LOGE(TAG, "ROM %s not found! Aborting.", ROM_FILE);
        return;
    }

    // 8. Spectrum CPU
    log_ts(TAG, "=== STEP 8/16: CPU Create ===");
    display_boot_log_add_step(8, 16, "CPU Create");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    spectrum = new SpectrumHardware();
    if (!spectrum) {
        ESP_LOGE(TAG, "Failed to create Spectrum hardware!");
        return;
    }

    // 9. Load ROM + Reset
    log_ts(TAG, "=== STEP 9/16: Load ROM ===");
    display_boot_log_add_step(9, 16, "Load ROM");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!spectrum->loadROM(ROM_FILE)) {
        ESP_LOGE(TAG, "ROM load failed. Aborting.");
        return;
    }
    spectrum->reset();

#if RUN_ALL_TESTS
    run_all_tests(spectrum, MODEL_NAME);
    spectrum->reset();
#endif

    // 10. Webserver (only if Wi-Fi up)
    log_ts(TAG, "=== STEP 10/16: Webserver ===");
    display_boot_log_add_step(10, 16, "Webserver");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (wifi_connected) {
        if (webserver_ensure_started(spectrum) == ESP_OK && webserver_is_running()) {
            ESP_LOGI(TAG, "Webserver started successfully");
        } else {
            ESP_LOGW(TAG, "Webserver failed to start");
        }
    }

    // 11. Input
    log_ts(TAG, "=== STEP 11/16: Input Init ===");
    display_boot_log_add_step(11, 16, "Input Init");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    input_init();

    // 12. Audio
    log_ts(TAG, "=== STEP 12/16: Audio Init ===");
    display_boot_log_add_step(12, 16, "Audio Init");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!audio_init()) {
        ESP_LOGW(TAG, "Audio init failed; continuing without sound");
    }

    // 13. Wait for WebSocket keyboard client (only if webserver running)
    log_ts(TAG, "=== STEP 13/16: Keyboard ===");
    display_boot_log_add_step(13, 16, "Keyboard");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (wifi_connected && webserver_is_running()) {
        if (webserver_wait_for_ws_client(30000)) {
            ESP_LOGI(TAG, "WebSocket keyboard client connected");
        } else {
            ESP_LOGW(TAG, "No WebSocket keyboard connected within timeout; continuing.");
            if (display_ready) display_setOverlayText("Connect virtual keyboard", 0xF800);
            if (display_ready) display_boot_update();
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    // 14. Beep
    log_ts(TAG, "=== STEP 14/16: Beep ===");
    display_boot_log_add_step(14, 16, "Beep");
    if (display_ready) display_boot_update();
    vTaskDelay(pdMS_TO_TICKS(20));
    audio_play_tone(880, 120);

    // 15. Start Emulator Task
    log_ts(TAG, "=== STEP 15/16: Emulator ===");
    display_boot_log_hide();
    xTaskCreatePinnedToCore(emulator_task, "emulator", 8192, NULL, 5, NULL, 0);

    // 16. Idle
    log_ts(TAG, "=== STEP 16/16: Boot Done ===");
    log_ts(TAG, "Boot complete. Entering idle loop.");

    // 16. Idle
    log_ts(TAG, "Boot complete. Entering idle loop.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
