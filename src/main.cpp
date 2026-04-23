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

// ============================================
// SELECT YOUR MODEL HERE
// ============================================
//#define SPECTRUM_MODEL_48K
#define SPECTRUM_MODEL_48K
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
    ESP_LOGI(TAG, "Emulator task started on core %d", xPortGetCoreID());
    
    const int T_STATES_PER_FRAME = 69888; // 3.5 MHz / 50.08 Hz
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frameInterval = pdMS_TO_TICKS(20); // ~50 Hz
    
    int frameCount = 0;
    
    while (1) {
        int tStates = 0;
        instr_cpu_start();
        while (tStates < T_STATES_PER_FRAME) {
            tStates += spectrum->step();
        }
        instr_cpu_end();

        display_trigger_frame(spectrum);

        frameCount++;

        // Use vTaskDelayUntil alone for precise timing
        vTaskDelayUntil(&lastWakeTime, frameInterval);
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
        display_boot_test();
    }

    ESP_LOGI(TAG, "**************************************");
    ESP_LOGI(TAG, "       SPEC-K10-TRUM STARTING");
    ESP_LOGI(TAG, "**************************************");

    ESP_LOGI(TAG, "Model: %s", MODEL_NAME);
    fflush(stdout);

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
    struct stat st_snap;
    bool snapshot_loaded = false;
    if (stat("/spiffs/autoexec.z80", &st_snap) == 0) {
        ESP_LOGI(TAG, "autoexec.z80 found in SPIFFS, attempting to load snapshot...");
        if (spectrum->loadSnapshot("/spiffs/autoexec.z80")) {
            ESP_LOGI(TAG, "Snapshot applied successfully.");
            snapshot_loaded = true;
        } else {
            ESP_LOGW(TAG, "Snapshot present but failed to load.");
        }
    }

    // Run all tests while splash is showing unless we loaded a snapshot.
    // Running the test suite mutates CPU and RAM which would overwrite the
    // restored snapshot state. If you want to keep the snapshot active at
    // boot, disable tests or remove the autoexec.z80 file from SPIFFS.
    if (!snapshot_loaded) {
        run_all_tests(spectrum, MODEL_NAME);
    } else {
        ESP_LOGI(TAG, "Skipping test suite because snapshot was loaded.");
    }
    
    // Ensure splash shows for at least 5 seconds
    int64_t elapsedMs = (esp_timer_get_time() - splashStartTime) / 1000;
    if (elapsedMs < 5000) {
        vTaskDelay(pdMS_TO_TICKS(5000 - elapsedMs));
    }

    // Clear screen before starting emulator
    uint16_t* buffer = display_getBackBuffer();
    memset(buffer, 0, 320 * 240 * 2); // Assuming standard resolution for clearing
    display_present();
    
    // Dump first few bytes for debugging
    ESP_LOGI(TAG, "First 16 bytes of ROM:");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", spectrum->read(i));
    }
    printf("\n");
    fflush(stdout);
    
    ESP_LOGI(TAG, "✓ Initialization complete! Starting emulator task.");
    
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