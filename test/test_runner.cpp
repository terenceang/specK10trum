#include "test_memory.h"
#include "test_128k_banking.h"
#include "z80_test.h"
#include "test_config.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>

// Only compile the test runner when RUN_ALL_TESTS is enabled
#if RUN_ALL_TESTS

class TestRunner {
public:
    static void runAllTests(IMemoryBus* spectrum, const char* modelName) {
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║              STARTING TEST SUITE               ║\n");
        printf("║              Model: %-10s               ║\n", modelName);
        printf("╚══════════════════════════════════════════════════╝\n");
        fflush(stdout);
        
        // Run systematic CPU tests
        int cpuFailures = run_cpu_tests();
        bool cpuTestsPassed = (cpuFailures == 0);

        // Run memory tests (both 48K and 128K)
        bool memoryTestsPassed = MemoryTest::runAllTests(spectrum, modelName);
        
        // Run banking tests only for 128K
        bool bankingTestsPassed = true;
        if (strcmp(modelName, "128K") == 0) {
            bankingTestsPassed = BankingTest::runAllTests(spectrum);
        }
        
        // Final summary
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║                  TEST SUMMARY                  ║\n");
        printf("╚══════════════════════════════════════════════════╝\n");
        printf("  CPU Core Tests:  %s\n", cpuTestsPassed ? "PASSED ✓" : "FAILED ✗");
        printf("  Memory Tests:    %s\n", memoryTestsPassed ? "PASSED ✓" : "FAILED ✗");
        
        if (strcmp(modelName, "128K") == 0) {
            printf("  Banking Tests:   %s\n", bankingTestsPassed ? "PASSED ✓" : "FAILED ✗");
        }
        
        printf("\n");
        
        if (cpuTestsPassed && memoryTestsPassed && bankingTestsPassed) {
            printf("✓ All tests passed! System is ready.\n");
        } else {
            printf("✗ Some tests failed. Check system configuration.\n");
        }
        fflush(stdout);
    }
};

// C-compatible wrapper for use from main.cpp
extern "C" {
    void run_all_tests(IMemoryBus* spectrum, const char* modelName) {
        TestRunner::runAllTests(spectrum, modelName);
    }
}

#else
// When tests are disabled, don't emit the test runner code.
#endif