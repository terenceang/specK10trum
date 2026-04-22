#pragma once
#include <stdint.h>
#include <stddef.h>

class IMemoryBus {
public:
    virtual ~IMemoryBus() {}
    
    // Memory access
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
    
    // I/O port access
    virtual void writePort(uint16_t port, uint8_t value) = 0;
    virtual uint8_t readPort(uint16_t port) = 0;
    
    // ROM loading
    virtual bool loadROM(const char* filepath) = 0;
    
    // Debug
    virtual void dumpMemory(uint16_t start, uint16_t end) = 0;
    
    // Reset
    virtual void reset() = 0;
};