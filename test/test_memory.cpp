#include "test_memory.h"
#include <esp_log.h>
#include <stdio.h>

static const char* TAG = "MemoryTest";

static void printSuiteBanner(const char* title) {
    printf("\n╔═════════════════════════════════════════════════╗\n");
    printf("║ %-45s ║\n", title);
    printf("╚═════════════════════════════════════════════════╝\n");
    fflush(stdout);
}

void MemoryTest::logResult(const char* testName, bool passed) {
    if (passed) {
        ESP_LOGI(TAG, "  ✓ %s - PASSED", testName);
    } else {
        ESP_LOGE(TAG, "  ✗ %s - FAILED", testName);
    }
}

bool MemoryTest::testBasicReadWrite(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing basic read/write operations...");
    
    // Test different memory regions
    struct TestRegion {
        uint16_t addr;
        uint8_t value;
        const char* name;
    };
    
    TestRegion regions[] = {
        {0x4000, 0xAA, "RAM start (0x4000)"},
        {0x5A00, 0xBB, "Video RAM (0x5A00)"},
        {0x8000, 0xCC, "Middle RAM (0x8000)"},
        {0xC000, 0xDD, "Upper RAM (0xC000)"},
        {0xFFFF, 0xEE, "RAM end (0xFFFF)"}
    };
    
    for (const auto& region : regions) {
        spectrum->write(region.addr, region.value);
        uint8_t readValue = spectrum->read(region.addr);
        
        if (readValue != region.value) {
            ESP_LOGE(TAG, "  Failed at %s: wrote 0x%02X, read 0x%02X", 
                     region.name, region.value, readValue);
            return false;
        }
        ESP_LOGV(TAG, "  %s: wrote 0x%02X, read 0x%02X ✓", 
                 region.name, region.value, readValue);
    }
    
    return true;
}

bool MemoryTest::testROMProtection(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing ROM protection...");
    
    // Save original ROM bytes
    uint16_t addr1 = 0x0000;
    uint16_t addr2 = 0x1000;
    uint8_t original1 = spectrum->read(addr1);
    uint8_t original2 = spectrum->read(addr2);
    
    // Try to write to ROM
    spectrum->write(addr1, 0x12);
    spectrum->write(addr2, 0x34);
    uint8_t afterWrite1 = spectrum->read(addr1);
    uint8_t afterWrite2 = spectrum->read(addr2);
    
    bool passed = (afterWrite1 == original1) && (afterWrite2 == original2);
    
    if (!passed) {
        ESP_LOGE(TAG, "  ROM write protection failed:");
        if (afterWrite1 != original1) {
            ESP_LOGE(TAG, "    Addr 0x%04X: original=0x%02X, after=0x%02X", addr1, original1, afterWrite1);
        }
        if (afterWrite2 != original2) {
            ESP_LOGE(TAG, "    Addr 0x%04X: original=0x%02X, after=0x%02X", addr2, original2, afterWrite2);
        }
    }
    
    return passed;
}

bool MemoryTest::testMemoryBounds(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing memory bounds...");
    
    // Test writing at various boundaries
    uint16_t boundaries[] = {0x3FFF, 0x4000, 0x7FFF, 0x8000, 0xBFFF, 0xC000};
    uint8_t testValue = 0x55;
    
    for (uint16_t addr : boundaries) {
        spectrum->write(addr, testValue);
        uint8_t readValue = spectrum->read(addr);
        
        // For ROM boundary (0x3FFF), it should be read-only
        if (addr == 0x3FFF) {
            if (readValue == testValue) {
                ESP_LOGE(TAG, "  ROM write succeeded at 0x%04X (should be protected)", addr);
                return false;
            }
        } else {
            if (readValue != testValue) {
                ESP_LOGE(TAG, "  RAM write failed at 0x%04X", addr);
                return false;
            }
        }
        ESP_LOGV(TAG, "  Boundary 0x%04X: wrote 0x%02X, read 0x%02X ✓", 
                 addr, testValue, readValue);
    }
    
    return true;
}

bool MemoryTest::testContiguousMemory(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing contiguous memory access...");
    
    // Fill a block with pattern
    uint16_t start = 0x4000;
    uint16_t end = 0x5000;
    
    for (uint16_t addr = start; addr < end; addr++) {
        spectrum->write(addr, (addr & 0xFF));
    }
    
    // Verify pattern
    for (uint16_t addr = start; addr < end; addr++) {
        uint8_t expected = (addr & 0xFF);
        uint8_t actual = spectrum->read(addr);
        
        if (actual != expected) {
            ESP_LOGE(TAG, "  Pattern mismatch at 0x%04X: expected 0x%02X, got 0x%02X", 
                     addr, expected, actual);
            return false;
        }
    }
    
    ESP_LOGI(TAG, "  Pattern verified for %d bytes", end - start);
    return true;
}

bool MemoryTest::runAllTests(IMemoryBus* spectrum, const char* modelName) {
    printSuiteBanner("MEMORY TESTS");
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    ESP_LOGI(TAG, "Running Memory Tests for %s", modelName);
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    
    bool allPassed = true;
    
    allPassed &= testBasicReadWrite(spectrum);
    allPassed &= testROMProtection(spectrum);
    allPassed &= testMemoryBounds(spectrum);
    allPassed &= testContiguousMemory(spectrum);
    
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    if (allPassed) {
        ESP_LOGI(TAG, "All memory tests PASSED ✓");
    } else {
        ESP_LOGE(TAG, "Some memory tests FAILED ✗");
    }
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    
    printSuiteBanner("END MEMORY TESTS");
    return allPassed;
}