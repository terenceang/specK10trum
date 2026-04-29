# Instaload: 128K Tape on 48K Machine

## Current Behavior

### What Instaload Does:
```cpp
for (int i = 0; i < m_num_blocks - 1; i++) {
    // Find header block
    if (getBlockContent(i, &hData, &hLen, &hFlag)) {
        uint8_t type = hData[0];
        uint16_t start = hData[13] | (hData[14] << 8);  // ← Address from header
        
        // Find data block
        if (getBlockContent(i + 1, &dData, &dLen)) {
            // Validate
            if (start >= 0x4000 && start <= 0x5AFF) continue;  // Display area
            if (start < 0x8000) continue;                       // ROM area
            if (start > (0x10000 - dLen)) continue;             // Overflow
            
            // Write directly to linear address
            for (uint16_t j = 0; j < dLen; j++) {
                spectrum->write(start + j, data[j]);
            }
        }
        i++;
    }
}
```

### Key Issue: No Bank Awareness

Instaload sees only **linear 64K address space (0x0000-0xFFFF)**.

It doesn't know about:
- 128K banking system
- Port 0x7FFD paging
- Separate RAM banks

---

## Scenario: 128K Game on 48K with Instaload

### Example: Jetpack (128K version)

**Tape Structure:**
```
Block 0: BASIC Header   (23755)
Block 1: BASIC Data     (23755)
Block 2: CODE Header    (start=0x8000) ← Bank 0
Block 3: CODE Data      (0x8000, 16KB)
Block 4: CODE Header    (start=0x8000) ← Bank 1  
Block 5: CODE Data      (0x8000, 16KB) ← SAME ADDRESS
Block 6: CODE Header    (start=0xC000) ← Bank 3
Block 7: CODE Data      (0xC000, 16KB)
```

### 48K Instaload Execution:

**Step 1: Load BASIC**
```
instaload:
  i=0: Find header @ 23755
  i=1: Find data @ 23755
  Write to 23755 ✓
  Skip to i=2
```

**Step 2: Load CODE Block 1**
```
instaload:
  i=2: Find header (start=0x8000)
  i=3: Find data
  Validate: 0x8000 >= 0x8000 ✓, not overflow ✓
  Write to 0x8000-0x9FFF (16KB) ✓
  Memory at 0x8000: [CODE_BANK_0_DATA]
  Skip to i=4
```

**Step 3: Load CODE Block 2 - THE PROBLEM**
```
instaload:
  i=4: Find header (start=0x8000) ← SAME ADDRESS!
  i=5: Find data
  Validate: 0x8000 >= 0x8000 ✓, not overflow ✓
  Write to 0x8000-0x9FFF (16KB)
  Memory at 0x8000: [CODE_BANK_1_DATA] ← OVERWRITES BLOCK 1!
  Skip to i=6
```

**Step 4: Load CODE Block 3**
```
instaload:
  i=6: Find header (start=0xC000)
  i=7: Find data
  Validate: 0xC000 ✓
  Write to 0xC000-0xDFFF (16KB) ✓
  Memory at 0xC000: [CODE_BANK_3_DATA]
```

### Result After Instaload:
```
Memory Layout on 48K:
  0x8000-0x9FFF: CODE_BANK_1_DATA (Block 2 overwritten by Block 3)
  0xC000-0xDFFF: CODE_BANK_3_DATA
  
Missing:
  CODE_BANK_0_DATA (lost due to overwrite)
```

---

## Why This Is a Problem

### The Program Expects:
```
During execution:
  1. OUT (0x7FFD), 0   ; Select bank 0 @ 0xC000
     LD HL, 0xC000
     LD A, (HL)        ; Get bank 0 data
     
  2. OUT (0x7FFD), 1   ; Select bank 1 @ 0xC000
     LD HL, 0xC000
     LD A, (HL)        ; Get bank 1 data
     
  3. OUT (0x7FFD), 3   ; Select bank 3 @ 0xC000
     LD HL, 0xC000
     LD A, (HL)        ; Get bank 3 data
```

### What 48K Actually Does:
```
During execution:
  1. OUT (0x7FFD), 0   ; 48K: IGNORED, still reading bank 0
     LD HL, 0xC000
     LD A, (HL)        ; Gets bank 0 data (or overwritten data)
     
  2. OUT (0x7FFD), 1   ; 48K: IGNORED, still same
     LD HL, 0xC000
     LD A, (HL)        ; Gets SAME data (not bank 1)
     ← Program logic fails! Expects different data
     
  3. Program crashes with wrong tile/sprite/data
```

---

## Validation Check Coverage

### ✅ What Our Validation DOES Catch:

1. **Overflow Check:**
   ```cpp
   if (start > (0x10000 - dLen)) continue;
   ```
   Prevents: `start=0xF000, len=0x2000 → 0x11000` (wraps)

2. **Display Protection:**
   ```cpp
   if (start >= 0x4000 && start <= 0x5AFF) continue;
   ```
   Prevents: Program loading over screen data

3. **ROM Protection:**
   ```cpp
   if (start < 0x8000) continue;
   ```
   Prevents: Loading over ROM

4. **Checksum Validation:**
   ```cpp
   if (checksum != expected) return false;
   ```
   Prevents: Corrupted data

### ❌ What Our Validation DOESN'T Catch:

1. **Duplicate Addresses:**
   ```cpp
   // Two blocks at 0x8000
   // Validation passes both (each is valid on its own)
   // But they overwrite each other ← Problem!
   ```

2. **Total Program Size:**
   ```cpp
   // Block 1: 16KB @ 0x8000
   // Block 2: 16KB @ 0x8000 (overwrites)
   // Block 3: 16KB @ 0xC000
   // Total: 48KB, but only 32KB survives
   // Validation: Each block OK individually
   ```

3. **Banking Requirements:**
   ```cpp
   // No way to detect from header:
   // "This program needs bank switching"
   // Validation assumes linear addressing
   ```

---

## Detection: Can We Improve?

### Current Limitations:

**Block Header Doesn't Specify Bank:**
```
Standard Spectrum tape header (17 bytes):
  [0] Type (0=BASIC, 3=CODE, etc.)
  [1-2] Length
  [3-4] Start address
  [5-16] Metadata
  
No field for: "This goes to bank X"
```

**Detection Heuristics (Possible):**
```cpp
bool looks128K() {
    // Heuristic 1: Multiple blocks at same address
    std::map<uint16_t, int> address_count;
    for (int i = 0; i < m_num_blocks; i++) {
        if (isDataBlock(i)) {
            address_count[m_blocks[i].start]++;
        }
    }
    for (auto& pair : address_count) {
        if (pair.second > 1) return true;  // Multiple blocks at same addr
    }
    
    // Heuristic 2: Large total size
    uint32_t total = 0;
    for (int i = 0; i < m_num_blocks; i++) {
        if (isDataBlock(i)) total += m_blocks[i].length;
    }
    if (total > 49152) return true;  // Larger than 48K
    
    return false;
}
```

---

## What Should Happen?

### Option A: Strict Validation
```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    // NEW: Reject 128K on 48K
    if (looks128K() && !spectrum->is128k()) {
        ESP_LOGE("Tape", "128K program requires 128K machine");
        return;
    }
    // ... rest of instaload
}
```
**Pro:** Prevents confusing failures
**Con:** May reject simple multi-block programs

### Option B: Warning Only
```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    // NEW: Warn but allow
    if (looks128K() && !spectrum->is128k()) {
        ESP_LOGW("Tape", "Program may be 128K-only");
    }
    // ... rest of instaload (load anyway)
}
```
**Pro:** Doesn't block, informs user
**Con:** User confused when it crashes

### Option C: Current Behavior (No Detection)
```cpp
// Just load it, let user figure it out
```
**Pro:** Simple, no false rejects
**Con:** Bad UX when multi-block 128K fails

---

## Current Status

### ✅ Implemented:
- Address validation (no display overwrite)
- Overflow protection
- Checksum validation
- ROM protection

### ⚠️ Not Implemented:
- 128K tape detection
- Machine requirement checking
- Duplicate address detection
- User warning

### 📋 Recommendation:

Add gentle detection before instaload:
```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    if (!m_enabled) return;
    
    // NEW: Check if looks like 128K program
    bool multi_block = 0;
    uint16_t prev_addr = 0;
    uint32_t total_size = 0;
    
    for (int i = 0; i < m_num_blocks; i++) {
        if (isDataBlock(i)) {
            if (m_blocks[i].start == prev_addr && prev_addr != 0) {
                multi_block = true;  // Same address twice = banking
            }
            prev_addr = m_blocks[i].start;
            total_size += m_blocks[i].length;
        }
    }
    
    if ((multi_block || total_size > 49152) && !spectrum->is128k()) {
        ESP_LOGW("Tape", "Warning: Program appears to be 128K-only");
        // But continue anyway - let user try
    }
    
    // ... rest of instaload
}
```

---

## Summary

| Aspect | Status |
|--------|--------|
| Loads on 48K | ✅ Yes (if no overflow) |
| Runs on 48K | ❌ No (blocks overwrite) |
| Validation detects problem | ❌ No |
| User warned | ❌ No |
| Program crashes | ✅ Yes |

**Bottom Line:** Instaload loads 128K tapes on 48K machines, but they crash because of block overwriting and missing bank switching support.
