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

// Background task to show splash and run display boot sequence without blocking
static void splash_task(void* pvParameters) {
    (void)pvParameters;
    log_ts(TAG, "Splash task started");
    display_boot_test();
    log_ts(TAG, "Splash task finished");
    vTaskDelete(NULL);
}

// Background task to handle Wi-Fi connection attempts and start webserver
static void wifi_and_webserver_task(void* pvParameters) {
    (void)pvParameters;
    log_ts(TAG, "Wi-Fi task started");

    if (!wifi_prov_start()) {
        ESP_LOGW(TAG, "wifi_prov_start() failed to initialize Wi‑Fi subsystem");
    }

    // Wait for IP with a reasonable timeout. If we have saved credentials, 
    // the driver will auto-connect in the background.
    display_setOverlayText("Connecting Wi-Fi...", 0xFFFF);
    if (wifi_prov_wait_for_ip(15000)) {
        ESP_LOGI(TAG, "Wi-Fi connected. Starting webserver.");
        if (webserver_start(spectrum) != ESP_OK) {
            ESP_LOGW(TAG, "Webserver failed to start");
            display_setOverlayText("Webserver failed", 0xF800);
        }
    } else {
        // If we didn't get an IP in 10s, it might be first boot or lost signal.
        ESP_LOGW(TAG, "Wi-Fi connection timeout. BLE provisioning will restart if needed.");
        display_setOverlayText("Wi-Fi Timeout", 0xF800);
        // No fallback AP. BLE provisioning will handle recovery.
    }

    log_ts(TAG, "Wi-Fi task finished");
    vTaskDelete(NULL);
}


extern "C" void app_main(void) {
    // Give the USB serial/JTAG console time to enumerate before printing startup logs.
    vTaskDelay(pdMS_TO_TICKS(2000));

    // mark boot start for timestamps
    s_boot_start_us = esp_timer_get_time();
    log_ts(TAG, "app_main start");

    // Initialize expander first
    expander_init();

    // Mount SPIFFS immediately so ROMs and other assets are available
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed! System may not boot correctly.");
    }
    log_ts(TAG, "SPIFFS mounted");

    // Initialize display as soon as possible for early visual feedback
    if (!display_init()) {
        ESP_LOGE(TAG, "Display initialization FAILED");
    } else {
        ESP_LOGI(TAG, "Display initialization STARTED");
        // Initialize audio early so boot beep can play during display boot test
        if (!audio_init()) {
            ESP_LOGW(TAG, "Audio initialization failed; continuing without sound");
        }
        // Run the boot splash asynchronously so we don't block the main startup path
        xTaskCreatePinnedToCore(splash_task, "splash", 3072, NULL, 5, NULL, 1);
    }

    ESP_LOGI(TAG, "**************************************");
    ESP_LOGI(TAG, "       SPEC-K10-TRUM STARTING");
    ESP_LOGI(TAG, "**************************************");

    ESP_LOGI(TAG, "Model: %s", MODEL_NAME);

    // Initialize PSRAM only when the SDK config enables it.
#ifdef CONFIG_SPIRAM
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "PSRAM initialization failed!");
    } else {
        size_t psramSize = esp_psram_get_size();
        ESP_LOGI(TAG, "PSRAM initialized: %zu bytes", psramSize);
    }
#else
    ESP_LOGW(TAG, "PSRAM support is disabled in sdkconfig. Skipping initialization.");
#endif

    // Check if ROM file exists before initializing hardware
    struct stat st;
    if (stat(ROM_FILE, &st) != 0) {
        ESP_LOGE(TAG, "ROM file %s NOT FOUND in SPIFFS! Aborting.", ROM_FILE);
        return;
    }

    // Create the spectrum hardware
    spectrum = new SpectrumHardware();
    if (!spectrum) {
        ESP_LOGE(TAG, "Failed to create Spectrum hardware!");
        return;
    }
    
    // Load ROM selected by model
    if (!spectrum->loadROM(ROM_FILE)) {
        ESP_LOGE(TAG, "ROM load failed from %s, aborting emulator startup.", ROM_FILE);
        return;
    }
    
    // Reset the system
    spectrum->reset();

#if RUN_ALL_TESTS
    // Run all tests while splash is showing.
    run_all_tests(spectrum, MODEL_NAME);
    // Reset again after tests to ensure a clean state for the emulator
    spectrum->reset();
#else
    ESP_LOGI(TAG, "Tests are disabled at build time (RUN_ALL_TESTS==0); skipping test suite.");
#endif

    log_ts(TAG, "Continuing startup (splash in background)");

    // Start Wi‑Fi and webserver in background so emulator can start sooner
    display_setOverlayText("Waiting for Wi-Fi...", 0xFFFF);
    xTaskCreatePinnedToCore(wifi_and_webserver_task, "wifi_ws", 8192, NULL, 6, NULL, 1);

    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "✓ Initialization complete! Starting emulator task.");
    log_ts(TAG, "Emulator starting");
    
    // Initialize input subsystem (spawns input task)
    input_init();

    // Create emulator task on core 0
    xTaskCreatePinnedToCore(
        emulator_task,
        "emulator",
        8192,
        NULL,
        5,
        NULL,
        0
    );
    
    // Main task idles
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
