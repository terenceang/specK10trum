#pragma once
#include "../src/spectrum/IMemoryBus.h"

class BankingTest {
public:
    static bool runAllTests(IMemoryBus* spectrum);
    
private:
    static bool testBankSwitching(IMemoryBus* spectrum);
    static bool testMiddleBankSwitching(IMemoryBus* spectrum);
    static bool testVideoBankFixed(IMemoryBus* spectrum);
    static bool testPagingLock(IMemoryBus* spectrum);
    static bool testBankIsolation(IMemoryBus* spectrum);
    
    static void logResult(const char* testName, bool passed);
};