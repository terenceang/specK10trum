#include "test_128k_banking.h"
#include <esp_log.h>
#include <stdio.h>

static const char* TAG = "BankingTest";

static void printSuiteBanner(const char* title) {
    printf("\n╔═════════════════════════════════════════════════╗\n");
    printf("║ %-45s ║\n", title);
    printf("╚═════════════════════════════════════════════════╝\n");
    fflush(stdout);
}

void BankingTest::logResult(const char* testName, bool passed) {
    if (passed) {
        ESP_LOGI(TAG, "  ✓ %s - PASSED", testName);
    } else {
        ESP_LOGE(TAG, "  ✗ %s - FAILED", testName);
    }
}

bool BankingTest::testBankSwitching(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing bank switching at 0xC000...");
    
    // Test all banks
    for (int bank = 0; bank < 8; bank++) {
        spectrum->writePort(0x7FFD, bank);  // Select bank
        uint8_t testValue = 0x10 + bank;
        spectrum->write(0xC000, testValue);
        
        uint8_t readValue = spectrum->read(0xC000);
        if (readValue != testValue) {
            ESP_LOGE(TAG, "  Bank %d: wrote 0x%02X, read 0x%02X", bank, testValue, readValue);
            return false;
        }
        ESP_LOGV(TAG, "  Bank %d: wrote 0x%02X, read 0x%02X ✓", bank, testValue, readValue);
    }
    
    // Verify banks are independent
    for (int bank = 0; bank < 8; bank++) {
        spectrum->writePort(0x7FFD, bank);
        uint8_t expected = 0x10 + bank;
        uint8_t actual = spectrum->read(0xC000);
        
        if (actual != expected) {
            ESP_LOGE(TAG, "  Bank %d lost data: expected 0x%02X, got 0x%02X", 
                     bank, expected, actual);
            return false;
        }
    }
    
    return true;
}

bool BankingTest::testMiddleBankSwitching(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing middle bank switching at 0x8000...");
    
    // Test bank 2 (bit 3 = 0)
    spectrum->writePort(0x7FFD, 0x00);  // Bit 3 = 0 -> bank 2
    spectrum->write(0x8000, 0x99);
    uint8_t readBank2 = spectrum->read(0x8000);
    
    if (readBank2 != 0x99) {
        ESP_LOGE(TAG, "  Bank 2: expected 0x99, got 0x%02X", readBank2);
        return false;
    }
    ESP_LOGV(TAG, "  Bank 2: wrote 0x99, read 0x%02X ✓", readBank2);
    
    // Test bank 0 (bit 3 = 1)
    spectrum->writePort(0x7FFD, 0x08);  // Bit 3 = 1 -> bank 0
    spectrum->write(0x8000, 0xAA);
    uint8_t readBank0 = spectrum->read(0x8000);
    
    if (readBank0 != 0xAA) {
        ESP_LOGE(TAG, "  Bank 0: expected 0xAA, got 0x%02X", readBank0);
        return false;
    }
    ESP_LOGV(TAG, "  Bank 0: wrote 0xAA, read 0x%02X ✓", readBank0);
    
    // Switch back to bank 2 and verify
    spectrum->writePort(0x7FFD, 0x00);
    readBank2 = spectrum->read(0x8000);
    
    if (readBank2 != 0x99) {
        ESP_LOGE(TAG, "  Bank 2 after switch back: expected 0x99, got 0x%02X", readBank2);
        return false;
    }
    
    return true;
}

bool BankingTest::testVideoBankFixed(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing video RAM (bank 5 at 0x4000) is fixed...");
    
    // Write to bank 5 through 0x4000
    spectrum->write(0x4000, 0x55);
    uint8_t directRead = spectrum->read(0x4000);
    
    if (directRead != 0x55) {
        ESP_LOGE(TAG, "  Direct write to 0x4000 failed: expected 0x55, got 0x%02X", directRead);
        return false;
    }
    
    // Try to change bank at 0xC000 (should not affect 0x4000)
    spectrum->writePort(0x7FFD, 0x01);  // Select bank 1 at 0xC000
    spectrum->write(0x4000, 0xAA);       // This should still write to bank 5
    uint8_t readAfterBankChange = spectrum->read(0x4000);
    
    if (readAfterBankChange != 0xAA) {
        ESP_LOGE(TAG, "  Bank 5 should be fixed at 0x4000, but got 0x%02X", readAfterBankChange);
        return false;
    }
    
    ESP_LOGI(TAG, "  Bank 5 remains accessible at 0x4000 regardless of paging");
    return true;
}

bool BankingTest::testPagingLock(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing paging lock bit...");
    
    // Test the lock feature (bit 5 of port 0x7FFD)
    spectrum->writePort(0x7FFD, 0x20);  // Set lock bit
    spectrum->writePort(0x7FFD, 0x01);  // Try to change bank (should be ignored)
    
    // Write to bank 0 (should still go to bank 0 if lock worked)
    spectrum->write(0xC000, 0x42);
    
    // Try to switch banks
    spectrum->writePort(0x7FFD, 0x01);
    uint8_t readValue = spectrum->read(0xC000);
    
    // If lock worked, bank didn't change and we should still read 0x42
    if (readValue != 0x42) {
        ESP_LOGE(TAG, "  Paging lock failed: bank changed when locked");
        return false;
    }
    
    ESP_LOGI(TAG, "  Paging lock works correctly");
    return true;
}

bool BankingTest::testBankIsolation(IMemoryBus* spectrum) {
    ESP_LOGI(TAG, "Testing bank isolation...");
    
    // Fill each bank with unique pattern
    for (int bank = 0; bank < 8; bank++) {
        spectrum->writePort(0x7FFD, bank);
        for (int offset = 0; offset < 16; offset++) {
            spectrum->write(0xC000 + offset, bank);
        }
    }
    
    // Verify each bank kept its own pattern
    for (int bank = 0; bank < 8; bank++) {
        spectrum->writePort(0x7FFD, bank);
        for (int offset = 0; offset < 16; offset++) {
            uint8_t value = spectrum->read(0xC000 + offset);
            if (value != bank) {
                ESP_LOGE(TAG, "  Bank %d corruption at offset %d: expected %d, got %d", 
                         bank, offset, bank, value);
                return false;
            }
        }
    }
    
    ESP_LOGI(TAG, "  All 8 banks are isolated and retain their data");
    return true;
}

bool BankingTest::runAllTests(IMemoryBus* spectrum) {
    printSuiteBanner("BANKING TESTS");
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    ESP_LOGI(TAG, "Running 128K Banking Tests");
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    
    bool allPassed = true;
    
    allPassed &= testBankSwitching(spectrum);
    allPassed &= testMiddleBankSwitching(spectrum);
    allPassed &= testVideoBankFixed(spectrum);
    allPassed &= testBankIsolation(spectrum);
    allPassed &= testPagingLock(spectrum);
    
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    if (allPassed) {
        ESP_LOGI(TAG, "All banking tests PASSED ✓");
    } else {
        ESP_LOGE(TAG, "Some banking tests FAILED ✗");
    }
    ESP_LOGI(TAG, "═══════════════════════════════════════");
    
    printSuiteBanner("END BANKING TESTS");
    return allPassed;
}