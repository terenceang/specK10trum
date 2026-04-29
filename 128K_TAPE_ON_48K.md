# Loading 128K Tape on 48K Machine

## Short Answer
❌ **Usually fails** - Our validation will load it, but the program crashes during execution.

## What Happens With Instaload

### Validation Phase ✅
```
Tape file: 128K program with code @ 0x8000
Instaload checks:
  ✅ Address 0x8000 is NOT in display (0x4000-0x5AFF)
  ✅ Address 0x8000 >= 0x8000 (user area)
  ✅ Address + length <= 0x10000 (no overflow)
  ✅ All blocks pass validation
→ Program loads successfully
```

### Execution Phase ❌
```
Program starts at 0x8000
Program expects 128K features:
  ❌ Port 0x7FFD paging (doesn't exist on 48K)
  ❌ Data in banks 1,3,4,6,7 (don't exist on 48K)
  ❌ 128K ROM features (only 48K ROM available)
→ Program crashes or hangs
```

---

## The Problem in Detail

### Memory Layout Mismatch

**128K Program Assumptions:**
```
Bank 0: Code/data @ 0x8000
Bank 1: Code/data (expects: OUT(0x7FFD, 1))
Bank 2: Audio buffers
Bank 3: Sprite data
Bank 4: Level data
Bank 5: Display/workspace
...
```

**What 48K Actually Has:**
```
0x8000: Bank 1 (fixed - CAN use)
0xC000: Bank 2 (fixed - CAN use)
Bank switching port 0x7FFD: DOESN'T EXIST
```

### Example: Game tries to load sprite bank

**128K Program Code:**
```z80asm
LD A, 3              ; Load bank 3
OUT (0x7FFD), A      ; Switch to bank 3
LD HL, 0xC000        ; Read from switched bank
LD A, (HL)           ; Get sprite data
```

**48K Execution:**
```z80asm
LD A, 3              ; A = 3
OUT (0x7FFD), A      ; 48K: Port write IGNORED
                     ; Still reading from bank 0 (0xC000)
LD HL, 0xC000
LD A, (HL)           ; Gets WRONG data (not sprites)
                     ; Program crashes with bad sprite
```

---

## Types of 128K Programs

### Type 1: Simple Compiled Program
```
Single code block @ 0x8000
No bank switching
Fits in 48K space
```
**Result on 48K:** ✅ **MIGHT WORK** (if no banking)
**Example:** Simple compiled Z80 assembly program

### Type 2: Game with Banking
```
Multiple blocks loaded to different banks
Uses port 0x7FFD to switch RAM banks
```
**Result on 48K:** ❌ **FAILS** (banking ignored)
**Example:** Most 128K commercial games

### Type 3: Program with 128K ROM Features
```
Calls 128K-specific ROM routines
Expects editor ROM @ 0x0000 (if bit 4 of 0x7FFD set)
```
**Result on 48K:** ❌ **FAILS** (ROM different)
**Example:** Games using 128K toolkit

### Type 4: BASIC Program
```
BASIC @ 23755 (0x5CB3)
Code @ standard location
```
**Result on 48K:** ✅ **MIGHT WORK**
**Example:** Simple BASIC programs

---

## Detection: Can We Tell It's 128K?

**Our current validation doesn't detect machine requirement.**

### Hints That a Tape is 128K:
```
1. Multiple data blocks (suggests banking)
   - TAP: 4+ blocks with staggered addresses
   - TZX: Multiple 0x10/0x11 blocks

2. Large program size
   - Total > 48KB (obviously 128K only)
   - Detected: tape parsing would just fail naturally

3. Port 0x7FFD access in code
   - Can't detect without executing or disassembling
   - Would need Z80 analysis (complex)

4. Unusual start addresses
   - 0x0000: 128K ROM switching (unsafe)
   - 0x1B00-0x3FFF: 128K-specific ROM areas
```

### Future Enhancement:
```cpp
bool looksLike128K() {
    // Heuristics to detect 128K programs
    if (m_num_blocks > 3) return true;  // Multiple blocks
    if (totalCodeSize > 49152) return true;  // >48KB
    
    // Check for unusual addresses
    for (int i = 0; i < m_num_blocks; i++) {
        uint16_t addr = m_blocks[i].start;
        if (addr < 0x8000) return true;  // ROM area
    }
    return false;
}

void instaload(SpectrumBase* spectrum) {
    if (looksLike128K() && !spectrum->is128k()) {
        // WARN: "This looks like a 128K program"
        // Allow user to cancel
    }
    // ... continue loading
}
```

---

## Current Behavior with Our Validation

### Single-Block 128K Program
```
Tape: 1 block, CODE type, 32KB, @ 0x8000
48K validation:
  ✅ Pass: Address in valid range
  ✅ Pass: Checksum valid
  ✅ Pass: Size fits
→ Loads successfully
→ Runs until it tries to use paging
→ ❌ CRASH
```

### Multi-Block 128K Program
```
Tape: Block 1 (CODE @ 0x8000, 16KB)
      Block 2 (CODE @ 0x8000, 16KB) - wait, same address?
      Block 3 (CODE @ 0x8000, 16KB)
48K validation:
  ✅ Pass: Each block within bounds
  ✅ Pass: Address in valid range
→ Loads Block 1
→ Loads Block 2 (overwrites Block 1)
→ Loads Block 3 (overwrites Block 2)
→ Ends up with only Block 3 loaded
→ ❌ Program doesn't work
```

**Why?** 128K uses multiple banks so same address in different blocks means different data.

---

## What We Should Do

### Option 1: Detect and Warn
```cpp
// Before instaload
if (detectMachine128K() && !spectrum->is128k()) {
    showWarning("This tape requires 128K Spectrum");
    return false;  // Don't load
}
```

**Pros:** Prevents user confusion
**Cons:** May reject valid simple programs

### Option 2: Load But Warn
```cpp
// After loading
if (detectMachine128K() && !spectrum->is128k()) {
    logWarning("Program may not work (detected 128K-only features)");
    // Let it try anyway
}
```

**Pros:** Power users can experiment
**Cons:** May confuse casual users

### Option 3: Current Behavior (No Detection)
```cpp
// Just load it
// User finds out by running it
```

**Pros:** Simple, no false rejects
**Cons:** Bad UX if program crashes

---

## Summary Table

| Scenario | Loads? | Runs? | Why |
|----------|--------|-------|-----|
| Simple 128K program | ✅ Yes | ✅ Maybe | No banking needed |
| 128K game with banking | ✅ Yes | ❌ No | Port 0x7FFD ignored |
| 128K program (large) | ✅ Yes | ❌ No | Data in non-existent banks |
| 128K BASIC program | ✅ Yes | ✅ Maybe | BASIC portable |
| Multi-block 128K | ✅ Yes | ❌ No | Blocks collide, overwrite |

---

## Test Case: What Actually Happens

### Example: Manic Miner (128K version)
```
File: manic128.tzx
Blocks: [Header] [Data@0x8000] [Data@0x8000] [Data@0xC000]

Loading on 48K:
  Block 1: Load to 0x8000 ✅
  Block 2: Load to 0x8000 (overwrites 1) ✅
  Block 3: Load to 0xC000 ✅
  
Program starts at 0x8000
  - Expects to find Bank 1 @ 0x8000 via bank switch
  - Finds Bank 0 @ 0x8000 (from Block 2 only)
  - Graphics wrong/corrupted
  - ❌ Game unplayable
```

---

## Recommendation

**Add gentle machine detection:**

```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    // ... existing validation ...
    
    // NEW: Warn if looks like 128K on 48K
    bool multi_block_code = false;
    uint32_t total_code = 0;
    for (int i = 0; i < m_num_blocks; i++) {
        if (isDataBlock(i)) {
            if (total_code > 0) multi_block_code = true;
            total_code += m_blocks[i].length;
        }
    }
    
    if ((total_code > 49152 || multi_block_code) && !spectrum->is128k()) {
        // Log warning but continue
        ESP_LOGW("Tape", "Program may be 128K-only, but loading anyway");
    }
    
    // ... continue loading ...
}
```

**Pros:**
- Helps user understand failures
- Non-blocking (doesn't prevent load)
- Simple to implement
- No false rejects

**Status:** Not yet implemented (could be future enhancement)
