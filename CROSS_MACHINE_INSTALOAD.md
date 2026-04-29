# Cross-Machine Instaload Compatibility Analysis

## Memory Layout Comparison

### 48K Spectrum
```
0x0000-0x3FFF: ROM (16K)
┌─────────────────────────┐
│   Bank 0 (0x4000-0x7FFF)│ ← Display & workspace
├─────────────────────────┤
│   Bank 1 (0x8000-0xBFFF)│ ← User program area
├─────────────────────────┤
│   Bank 2 (0xC000-0xFFFF)│ ← User program area
└─────────────────────────┘
Total RAM: 48 KB (48K contiguous)
```

### 128K Spectrum
```
0x0000-0x3FFF: ROM (16K, switchable)
┌─────────────────────────┐
│   Bank 5 (0x4000-0x7FFF)│ ← Display & workspace (FIXED)
├─────────────────────────┤
│   Bank 2 (0x8000-0xBFFF)│ ← Switchable
├─────────────────────────┤
│ Bank X (0xC000-0xFFFF)  │ ← Switchable (selected by port 0x7FFD)
└─────────────────────────┘
Total RAM: 128 KB (8 banks of 16K each)
Port 0x7FFD bit 0-2: Select bank for 0xC000-0xFFFF region
Port 0x7FFD bit 4:   Select ROM (0=normal 48K ROM, 1=128K editor ROM)
Port 0x7FFD bit 5:   Lock paging (if set, port 0x7FFD becomes read-only)
```

---

## Scenario 1: Load 48K Program on 128K Machine

### What Happens: ✅ USUALLY WORKS

The instaload function loads program code starting at the address specified in the tape header.

**Typical 48K game layout:**
- BASIC: 23755 (0x5CB3) - in bank 5, display area
- CODE: Usually 0x8000 or 0xC000

**On 128K with default bank configuration:**
- Bank configuration at startup: Bank 5 at 0x4000-0x7FFF, Bank 2 at 0x8000-0xBFFF, Bank 0 at 0xC000-0xFFFF
- Program loads into 0x8000 or 0xC000
- Works fine! These addresses are available

**Potential Issues:**
1. **If program expects to use banks:** No problem - it can read/write port 0x7FFD to switch banks
2. **If program reads BASIC variables:** Works - BASIC is at same address (23755)
3. **If program tries to page memory:** Works - 128K ROM has same paging mechanism

**Result:** ✅ 48K programs usually work perfectly on 128K

---

## Scenario 2: Load 128K Program on 48K Machine

### What Happens: ❌ LIKELY FAILS

**The Problem:**

A 128K program might:
1. Load code into banks beyond 0xFFFF (impossible on 48K)
2. Use port 0x7FFD paging (which doesn't exist on 48K)
3. Use 128K-specific ROM features (editor ROM, etc.)

**Example 1: Program that uses all 128KB**
```
48K program layout (SAFE):
- Code @ 0x8000 = 32KB
- Total = 32KB ✅ Fits in 48K

128K program layout (DANGEROUS):
- Code @ 0x8000 = 32KB
- Uses banks 1,3,4,6,7 = Additional 80KB
- Tries: spectrum->write(0xC000 + offset) with bank switching
- 48K ignores bank switch (port 0x7FFD does nothing)
- All writes go to bank 0 at 0xC000-0xFFFF (16KB only)
- Result: ❌ CRASH - program expects data in banks that don't exist
```

**Example 2: Program using port 0x7FFD paging**
```
Code: m_paging_bank = (data >> 0) & 0x07;  // Select bank
      port_out(0x7FFD, data);
      
48K sees: writePort(0x7FFD, value)
         // Port handler ignores it (48K has no paging)
         
128K sees: writePort(0x7FFD, value)
          // Updates paging immediately
          
Result: ❌ 48K doesn't switch banks, program crashes when it
            tries to read from "switched" bank
```

**Example 3: Program using 128K ROM**
```
Tape contains: Code with 128K editor ROM features
48K has: Only 48K ROM (different)
Result: ❌ Code might crash if it relies on 128K-specific ROM routines
```

---

## Current Instaload Implementation Analysis

Looking at `src/spectrum/Tape.cpp` line 407-465:

```cpp
uint16_t addr = (type == 0) ? 23755 : start;
for (uint16_t j = 0; j < dLen; j++) {
    spectrum->write((uint16_t)(addr + j), dData[j]);
}
```

**The write() function:**
```cpp
inline void write(uint16_t addr, uint8_t value) override {
    uint8_t* ptr = m_memWriteMap[addr >> 14];
    if (ptr) {
        ptr[addr & 0x3FFF] = value;
    }
}
```

- Uses virtual memory mapping
- 48K and 128K have different memory maps
- instaload writes to **logical addresses** (0x0000-0xFFFF)
- Each machine's write() maps these to physical memory correctly

**Result for instaload:**
- ✅ 48K on 128K: Works (logical addresses map to available physical banks)
- ⚠️  128K on 48K: Partial (only visible addresses work)

---

## The Real Problem: Self-Modifying Programs

Most commercial games don't just load and run. They:

1. **Check available memory:**
   ```z80asm
   LD A, (23756)  ; RAMTOP address
   ; Expects 128K: value changes based on bank
   ; On 48K: Returns fixed value
   ```

2. **Use paging for loading data:**
   ```z80asm
   LD A, 1
   LD (0x7FFD), A  ; Switch bank
   ; On 48K: This port write is ignored!
   ```

3. **Assume 128K address space:**
   ```z80asm
   LD HL, 0xC000
   LD DE, 0x4000
   ; Copy from bank 3 to bank 5
   ; On 48K: Gets wrong data (only bank 0 at 0xC000)
   ```

---

## Summary Table

| Scenario | Works | Why |
|----------|-------|-----|
| 48K program on 48K | ✅ Perfect | Native, designed for it |
| 48K program on 128K | ✅ Usually | 48K compatibility mode, banks available |
| 128K program on 128K | ✅ Perfect | Native, designed for it |
| 128K program on 48K | ❌ Often fails | Missing banks, paging doesn't work, ROM differences |

---

## What Instaload Actually Does

**Current instaload (after fixes):**
```cpp
addr = (type == 0) ? 23755 : start;
for (j = 0; j < dLen; j++) {
    spectrum->write(addr + j, data[j]);
}
```

**Instaload behavior:**
1. Reads tape header (type, start, length)
2. Validates address is in user area (0x8000-0xFFFF)
3. Directly writes bytes to that address
4. Sets CPU PC to start address
5. Lets program run

**Key limitation:** Instaload only works with simple programs that:
- Don't use bank paging (port 0x7FFD)
- Don't rely on disk/tape loading (no motor control)
- Don't need dynamic bank switching

---

## Enhancement: Cross-Machine Detection

**Future improvement:**
```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    // ... existing validation ...
    
    // IDEA: Check if this looks like a 128K program
    // Warning signs:
    // - Start address in multiple banks
    // - BASIC at 23755 but code >0x10000 (size)
    // - Multiple files suggesting bank layout
    
    bool likely_128k = false;
    for (int i = 0; i < m_num_blocks; i++) {
        // If total code > 48KB, must be 128K
        if (totalCodeSize > 49152) likely_128k = true;
    }
    
    if (likely_128k && !spectrum->is128k()) {
        // WARN: "This program requires 128K machine"
    }
}
```

---

## Conclusion

- ✅ **48K → 128K:** Works well (forward compatible)
- ❌ **128K → 48K:** Often fails (not backward compatible)
- 🔧 **Future:** Could add machine detection and warnings
