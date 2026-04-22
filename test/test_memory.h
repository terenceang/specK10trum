#pragma once
#include "../src/spectrum/IMemoryBus.h"

class MemoryTest {
public:
    // Run all memory-related tests
    static bool runAllTests(IMemoryBus* spectrum, const char* modelName);
    
    // Individual test functions
    static bool testBasicReadWrite(IMemoryBus* spectrum);
    static bool testROMProtection(IMemoryBus* spectrum);
    static bool testMemoryBounds(IMemoryBus* spectrum);
    static bool testContiguousMemory(IMemoryBus* spectrum);
    
private:
    static void logResult(const char* testName, bool passed);
};