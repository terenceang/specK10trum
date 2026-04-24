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
extern "C" void run_all_tests(IMemoryBus* spectrum, const char* modelName);

static const char* TAG = "Main";
static SpectrumHardware* spectrum = nullptr;

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
        int tStates = 0;
        instr_cpu_start();
        while (tStates < T_STATES_PER_FRAME) {
            tStates += spectrum->step();
        }
        instr_cpu_end();

        display_trigger_frame(spectrum);
        // Render and play beeper audio for this frame
        // This call will block until there is space in the I2S buffer, 
        // effectively synchronizing the emulator with the audio hardware.
        audio_play_frame(spectrum);

        // Yield to allow other tasks to run
        vTaskDelay(1);
    }
}



extern "C" void app_main(void) {
    // Give the USB serial/JTAG console time to enumerate before printing startup logs.
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize expander first
    expander_init();

    // Mount SPIFFS immediately so assets (splash, ROMs) are available
    if (!mountSPIFFS()) {
        ESP_LOGE(TAG, "SPIFFS mount failed! System may not boot correctly.");
    }

    // Record start time for minimum splash duration
    int64_t splashStartTime = esp_timer_get_time();

    // Initialize display as soon as possible for early visual feedback
    if (!display_init()) {
        ESP_LOGE(TAG, "Display initialization FAILED");
    } else {
        ESP_LOGI(TAG, "Display initialization STARTED");
        // Initialize audio early so boot beep can play during display boot test
        if (!audio_init()) {
            ESP_LOGW(TAG, "Audio initialization failed; continuing without sound");
        }
        // Start BLE Wi-Fi provisioning scaffold (no-op unless BT enabled)
        wifi_prov_start();
        display_boot_test();
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

    // If an autoexec snapshot exists in SPIFFS, attempt to load it now
    bool snapshot_loaded = spectrum->loadAutoexec();

    // Mount a virtual cassette if one is present on SPIFFS. LOAD "" from
    // BASIC then hits the ROM trap and gets the blocks instantly.
    if (Tape::autoload(spectrum->tape())) {
        ESP_LOGI(TAG, "Virtual tape mounted; type LOAD \"\" to play.");
    }

    // Run all tests while splash is showing unless we loaded a snapshot.
    if (!snapshot_loaded) {
        run_all_tests(spectrum, MODEL_NAME);
        // Reset again after tests to ensure a clean state for the emulator
        spectrum->reset();
    } else {
        ESP_LOGI(TAG, "Skipping test suite because snapshot was loaded.");
    }
    
    // Ensure splash shows for at least 5 seconds
    int64_t elapsedMs = (esp_timer_get_time() - splashStartTime) / 1000;
    if (elapsedMs < 5000) {
        vTaskDelay(pdMS_TO_TICKS(5000 - elapsedMs));
    }

    // Clear both frame buffers before starting emulator to prevent splash screen ghosting
    display_clear();
    
    ESP_LOGI(TAG, "✓ Initialization complete! Starting emulator task.");
    
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