#include <stdio.h>
#include <sys/stat.h>
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
    if (!display_init()) {
        ESP_LOGE(TAG, "Display init FAILED");
    } else {
        display_ready = true;
        display_boot_log_add("Boot: Display ready");
    }

    // 3. Splash Screen
    if (display_ready) {
        display_boot_test();
    }

    // 4. PSRAM
    if (display_ready) display_boot_log_add("Boot: PSRAM init");
#ifdef CONFIG_SPIRAM
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM init failed!");
        if (display_ready) display_boot_log_add("Boot: PSRAM failed!");
    } else {
        if (display_ready) display_boot_log_add("Boot: PSRAM ready");
    }
#endif

    // 5. SPIFFS
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed!");
    }
    if (display_ready) display_boot_log_add("Boot: SPIFFS mounted");

    // 6. Wi-Fi (blocking)
    bool wifi_connected = false;
    if (display_ready) display_boot_log_add("Wi-Fi: Initializing...");
    if (!wifi_prov_start()) {
        ESP_LOGW(TAG, "wifi_prov_start() failed");
        if (display_ready) display_boot_log_add("Wi-Fi: Init failed!");
    } else {
        if (display_ready) display_boot_log_add("Wi-Fi: Connecting...");
        if (wifi_prov_wait_for_ip(15000)) {
            wifi_connected = true;
            if (display_ready) display_boot_log_add("Wi-Fi: Connected!");
        } else {
            ESP_LOGW(TAG, "Wi-Fi timeout. Starting BLE fallback.");
            if (display_ready) display_boot_log_add("Wi-Fi: Timeout! BLE fallback...");
            wifi_prov_start_ble_fallback();
        }
    }

    // 7. ROM check
    if (display_ready) display_boot_log_add("Boot: ROM check");
    struct stat st;
    if (stat(ROM_FILE, &st) != 0) {
        ESP_LOGE(TAG, "ROM %s not found! Aborting.", ROM_FILE);
        if (display_ready) display_boot_log_add("Boot: ROM not found!");
        return;
    }

    // 8. Spectrum CPU
    if (display_ready) display_boot_log_add("Boot: CPU init");
    spectrum = new SpectrumHardware();
    if (!spectrum) {
        ESP_LOGE(TAG, "Failed to create Spectrum hardware!");
        return;
    }

    // 9. Load ROM + Reset
    if (display_ready) display_boot_log_add("Boot: ROM loading");
    if (!spectrum->loadROM(ROM_FILE)) {
        ESP_LOGE(TAG, "ROM load failed. Aborting.");
        return;
    }
    spectrum->reset();
    if (display_ready) display_boot_log_add("Boot: CPU reset");

#if RUN_ALL_TESTS
    run_all_tests(spectrum, MODEL_NAME);
    spectrum->reset();
#endif

    // 10. Webserver (only if Wi-Fi up)
    if (wifi_connected) {
        if (display_ready) display_boot_log_add("Webserver: Starting...");
        if (webserver_ensure_started(spectrum) == ESP_OK && webserver_is_running()) {
            if (display_ready) display_boot_log_add("Webserver: Ready");
            display_setOverlayText("Webserver ready", 0x07E0);
        } else {
            ESP_LOGW(TAG, "Webserver failed to start");
            if (display_ready) display_boot_log_add("Webserver: Failed!");
        }
    }

    // 11. Input
    if (display_ready) display_boot_log_add("Boot: Input init");
    input_init();

    // 12. Audio
    if (display_ready) display_boot_log_add("Boot: Audio init");
    if (!audio_init()) {
        ESP_LOGW(TAG, "Audio init failed; continuing without sound");
        if (display_ready) display_boot_log_add("Boot: Audio failed!");
    } else {
        if (display_ready) display_boot_log_add("Boot: Audio ready");
    }

    // 13. Wait for WebSocket keyboard client (only if webserver running)
    if (wifi_connected && webserver_is_running()) {
        if (display_ready) display_boot_log_add("Boot: Waiting for keyboard...");
        if (webserver_wait_for_ws_client(30000)) {
            if (display_ready) display_boot_log_add("Boot: Keyboard connected!");
        } else {
            ESP_LOGW(TAG, "No WebSocket keyboard connected within timeout; continuing.");
            if (display_ready) display_boot_log_add("Boot: Keyboard timeout");
        }
    }

    // 14. Beep
    audio_play_tone(880, 120);

    // 15. Start Emulator Task
    if (display_ready) display_boot_log_add("Boot: Emulator ready");
    xTaskCreatePinnedToCore(emulator_task, "emulator", 8192, NULL, 5, NULL, 0);

    // 16. Idle
    log_ts(TAG, "Boot complete. Entering idle loop.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
