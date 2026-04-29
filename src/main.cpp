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

#include "display/Display.h"
#include "instrumentation/Instrumentation.h"
#include "expander/Expander.h"
#include "audio/Audio.h"
#include "spectrum/Tape.h"
#include "input/Input.h"
#include "wifi_prov/wifi_prov.h"
#include "webserver/Webserver.h"
#include "test_config.h"

static const char* TAG = "Main";

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

// Global state
static SpectrumBase* spectrum = nullptr;
static bool display_ready = false;

// Single Source of Truth for boot status: overlay only, log to console
static inline void boot_showStatus(int step, int total, const char* message, uint16_t color) {
    ESP_LOGI(TAG, "Boot %d/%d: %s", step, total, message);
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
    
    while (true) {
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
        audio_play_frame(spectrum);
        
        // Yield to allow other tasks (like Webserver) to run
        vTaskDelay(1);
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
        boot_showStatus(2, 16, "Display Init", 0xFFFF);
        showSplashScreen(); // Show splash immediately after display init
        expander_set_led(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        expander_set_led(false);
    } else {
        ESP_LOGE(TAG, "Display init FAILED");
    }

    // 3. PSRAM
    log_ts(TAG, "=== STEP 3/16: PSRAM Init ===");
    boot_showStatus(3, 16, "PSRAM Init", 0xFFFF);
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM init failed!");
    }

    // 4. SPIFFS
    log_ts(TAG, "=== STEP 4/16: SPIFFS Mount ===");
    boot_showStatus(4, 16, "SPIFFS Mount", 0xFFFF);
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed!");
    }

    // 5. Splash Screen (already shown after display init)
    log_ts(TAG, "=== STEP 5/16: Splash Screen ===");
    boot_showStatus(5, 16, "Splash Screen", 0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. Wi-Fi
    log_ts(TAG, "=== STEP 6/16: Wi-Fi Connect ===");
    boot_showStatus(6, 16, "Wi-Fi Connect", 0xFFFF);
    bool wifi_connected = false;
    if (!wifi_prov_start()) {
        ESP_LOGW(TAG, "wifi_prov_start() failed");
    } else {
        ESP_LOGI(TAG, "Waiting for Wi-Fi connection (60s timeout)...");
        if (wifi_prov_wait_for_ip(60000)) {
            wifi_connected = true;
            ESP_LOGI(TAG, "Wi-Fi connected!");
        } else {
            ESP_LOGW(TAG, "Wi-Fi connection timeout, starting BLE provisioning fallback...");
            boot_showStatus(61, 16, "BLE Prov", 0xFFE0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            boot_showStatus(62, 16, "Use BLE app to provision", 0xFFE0);
            wifi_prov_start_ble_fallback();
            
            boot_showStatus(63, 16, "Waiting BLE", 0xFFE0);
            if (wifi_prov_wait_for_ip(UINT32_MAX)) {
                wifi_connected = true;
                ESP_LOGI(TAG, "Wi-Fi connected after BLE provisioning!");
                boot_showStatus(65, 16, "Wi-Fi provisioned!", 0x07E0);
                vTaskDelay(pdMS_TO_TICKS(2000));
            } else {
                ESP_LOGE(TAG, "BLE provisioning failed or no IP obtained");
                boot_showStatus(6, 16, "Provisioning failed!", 0xF800);
                vTaskDelay(pdMS_TO_TICKS(2000));
                // Continue anyway, maybe we can run offline
            }
        }
    }

    // 7. ROM check
    log_ts(TAG, "=== STEP 7/16: ROM Check ===");
    boot_showStatus(7, 16, "ROM Check", 0xFFFF);
    struct stat st;
    if (stat(ROM_FILE, &st) != 0) {
        ESP_LOGE(TAG, "ROM %s not found! Aborting.", ROM_FILE);
        boot_showStatus(7, 16, "ROM MISSING!", 0xF800);
        return;
    }

    // 8. Spectrum CPU
    log_ts(TAG, "=== STEP 8/16: CPU Create ===");
    boot_showStatus(8, 16, "CPU Create", 0xFFFF);
    spectrum = new SpectrumHardware();
    if (!spectrum) {
        ESP_LOGE(TAG, "Failed to create Spectrum hardware!");
        boot_showStatus(8, 16, "CPU CREATE FAILED", 0xF800);
        return;
    }

    // 9. Load ROM + Reset
    log_ts(TAG, "=== STEP 9/16: Load ROM ===");
    boot_showStatus(9, 16, "Load ROM", 0xFFFF);
    if (!spectrum->loadROM(ROM_FILE)) {
        ESP_LOGE(TAG, "ROM load failed. Aborting.");
        boot_showStatus(9, 16, "ROM LOAD FAILED", 0xF800);
        return;
    }
    spectrum->reset();

#if RUN_ALL_TESTS
    run_all_tests(spectrum, MODEL_NAME);
    spectrum->reset();
#endif

    // 10. Webserver
    log_ts(TAG, "=== STEP 10/16: Webserver ===");
    boot_showStatus(10, 16, "Webserver", 0xFFFF);
    if (wifi_connected) {
        if (webserver_ensure_started(spectrum) == ESP_OK && webserver_is_running()) {
            ESP_LOGI(TAG, "Webserver started successfully");
        } else {
            ESP_LOGW(TAG, "Webserver failed to start");
        }
    }

    // 11. Input
    log_ts(TAG, "=== STEP 11/16: Input Init ===");
    boot_showStatus(11, 16, "Input Init", 0xFFFF);
    input_init();

    // 12. Audio
    log_ts(TAG, "=== STEP 12/16: Audio Init ===");
    boot_showStatus(12, 16, "Audio Init", 0xFFFF);
    if (!audio_init()) {
        ESP_LOGW(TAG, "Audio init failed; continuing without sound");
    }

    // 13. Keyboard Client
    log_ts(TAG, "=== STEP 13/16: Keyboard ===");
    boot_showStatus(13, 16, "Keyboard", 0xFFFF);
    if (wifi_connected && webserver_is_running()) {
        if (webserver_wait_for_ws_client(30000)) {
            ESP_LOGI(TAG, "WebSocket keyboard client connected");
        } else {
            ESP_LOGW(TAG, "No WebSocket keyboard connected within timeout");
            boot_showStatus(13, 16, "Connect virtual kbd", 0xFFE0);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    // 14. Beep
    log_ts(TAG, "=== STEP 14/16: Beep ===");
    boot_showStatus(14, 16, "Beep", 0xFFFF);
    audio_play_tone(880, 120);

    // 15. Start Emulator Task
    log_ts(TAG, "=== STEP 15/16: Emulator ===");
    display_boot_log_hide();
    xTaskCreatePinnedToCore(emulator_task, "emulator", 8192, NULL, 5, NULL, 0);

    // 16. Idle
    log_ts(TAG, "=== STEP 16/16: Boot Done ===");
    log_ts(TAG, "Boot complete. Entering idle loop.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
