# Instaload Bug Analysis

## Problem
Instaload causes:
- Screen corruption (garbage/artifacts)
- System hang

## Root Causes Found

### 🔴 BUG #1: No Memory Address Validation (CRITICAL)

**Code (line 423-424):**
```cpp
uint16_t addr = (type == 0) ? 23755 : start;
for (uint16_t j = 0; j < dLen; j++) spectrum->write((uint16_t)(addr + j), dData[j]);
```

**Issue:**
- For CODE blocks (type 3+), `start` comes directly from tape header
- **No validation** that `start` is in valid memory range
- If `start` = 0x4000 (display memory), writing to it **corrupts the screen**

**ZX Spectrum Memory Layout:**
```
0x0000-0x3FFF: ROM (read-only)
0x4000-0x57FF: DISPLAY FILE (6144 bytes) ← ⚠️ VISIBLE SCREEN
0x5800-0x5AFF: COLOUR ATTRIBUTES (768 bytes)
0x5B00-0x7FFF: WORKSPACE/BASIC (10752 bytes)
0x8000-0xFFFF: USER PROGRAM (32768 bytes)
```

**Fix Needed:**
Validate address ranges:
- CODE blocks: Must be in 0x8000-0xFFFF (user program area)
- BASIC: Always uses 23755 (0x5CB3) - OK
- Reject if address is in display (0x4000-0x5AFF)

---

### 🟡 BUG #2: Data Length Truncation Without Bounds Check

**Code (line 422):**
```cpp
uint32_t dLen;
if (getBlockContent(i + 1, &dData, &dLen)) {
    if (dLen > len) dLen = len;  // ← Truncates silently
```

**Issues:**
1. If `dLen` > 65535 (uint16_t max), `dLen = len` assignment may overflow
2. No check if `addr + dLen` exceeds 0x10000 (memory boundary)
3. Tape file could claim to have 1MB of data - no validation

**Example:**
```
start = 0x8000, len = 40000 (claimed)
dLen = 100000 (actual data on tape)
dLen = 40000 (truncated)
addr + dLen = 0x8000 + 40000 = 0xB83F ← Inside valid range, OK this time
But what if len = 60000?
addr + dLen = 0x8000 + 60000 = 0xFB00 ← Still OK (< 0x10000)
```

But if `start` is malicious:
```
start = 0xF000, len = 4000
addr + dLen = 0xF000 + 4000 = 0xFEE0 ← OK

start = 0xFE00, len = 1000  
addr + dLen = 0xFE00 + 1000 = 0x101E8 ← OVERFLOW! Wraps to 0x01E8 (ROM)
```

---

### 🟡 BUG #3: Loop Block Skipping (Line 427)

**Code (line 413, 427):**
```cpp
for (int i = 0; i < (m_num_blocks - 1); i++) {
    // ... load header i, data i+1 ...
    if (getBlockContent(i + 1, &dData, &dLen)) {
        // ... process ...
        i++;  // ← MANUAL INCREMENT
    }
}
// ← FOR LOOP ALSO INCREMENTS
```

**Issue:**
Loop structure is:
1. Check `i < m_num_blocks - 1`
2. Execute body (including `i++`)
3. For-loop does `i++` again

**Result:** Blocks may be skipped if manual i++ doesn't always execute

**Scenario with 5 blocks (0-4):**
```
Iteration 1: i=0, load header 0, data 1, i++→1, for-loop i++→2
Iteration 2: i=2, load header 2, data 3, i++→3, for-loop i++→4
Iteration 3: i=4, condition 4 < 4? FALSE, loop ends
→ Block 4 (potential data) is never processed
```

---

### 🟡 BUG #4: No Type Validation

**Code (line 416):**
```cpp
uint8_t type = hData[0];
uint16_t len = hData[11] | (hData[12] << 8);
uint16_t start = hData[13] | (hData[14] << 8);
```

**Issue:**
- Any value of `type` is accepted
- No validation of `len` (could be 0xFFFF = 65535)
- No validation of `start` (checked in BUG #1)

---

### 🟡 BUG #5: Tape File Could Be Corrupted or Malformed

**Code (line 408):**
```cpp
if (!m_enabled || m_num_blocks == 0) return;
```

**Issue:**
- No validation that `m_num_blocks` is reasonable
- No validation that blocks actually contain data
- Corrupted tape file could cause buffer overruns in `getBlockContent()`

---

## Impact

| Bug | Impact | Severity |
|-----|--------|----------|
| #1: No address validation | Screen corruption, RAM corruption | 🔴 CRITICAL |
| #2: Data overflow | Writes beyond 0xFFFF, wraps to ROM | 🟡 HIGH |
| #3: Block skipping | Some programs don't load | 🟡 MEDIUM |
| #4: No type validation | Unknown program types accepted | 🟡 MEDIUM |
| #5: Malformed tape | Undefined behavior | 🟡 MEDIUM |

---

## Solution

Add comprehensive validation in instaload():

```cpp
void Tape::instaload(SpectrumBase* spectrum) {
    if (!m_enabled || m_num_blocks == 0) return;
    
    Z80* cpu = spectrum->getCPU();
    uint16_t lastCodeStart = 0, totalProgLen = 0;
    bool hasCode = false, hasBasic = false;

    for (int i = 0; i < m_num_blocks - 1; i++) {
        const uint8_t* hData; uint32_t hLen; uint8_t hFlag;
        if (!getBlockContent(i, &hData, &hLen, &hFlag)) continue;
        if (hLen != 17 || hFlag != 0x00) continue;
        
        uint8_t type = hData[0];
        uint16_t len = hData[11] | (hData[12] << 8);
        uint16_t start = hData[13] | (hData[14] << 8);
        
        // VALIDATE: length must be reasonable
        if (len == 0 || len > 0x8000) continue;
        
        const uint8_t* dData; uint32_t dLen;
        if (!getBlockContent(i + 1, &dData, &dLen)) continue;
        
        // VALIDATE: clamp data length
        if (dLen > len) dLen = len;
        if (dLen > 0x8000) dLen = 0x8000;  // Safety cap
        
        uint16_t addr;
        if (type == 0) {
            // BASIC: Always 23755, safe
            addr = 23755;
            hasBasic = true;
            totalProgLen = (uint16_t)dLen;
        } else if (type == 3) {
            // CODE: VALIDATE address range
            // Display (0x4000-0x5AFF) is forbidden
            // Valid: 0x8000-0xFFFF (user program)
            if (start >= 0x4000 && start <= 0x5AFF) {
                continue;  // Skip - would corrupt display
            }
            if (start < 0x8000) {
                continue;  // Skip - outside user program area
            }
            // VALIDATE: start + length doesn't overflow
            if (start > 0x10000 - dLen) {
                continue;  // Would overflow 64K address space
            }
            addr = start;
            lastCodeStart = start;
            hasCode = true;
        } else {
            continue;  // Unknown type
        }
        
        // Safe to load now
        for (uint16_t j = 0; j < dLen; j++) {
            spectrum->write((uint16_t)(addr + j), dData[j]);
        }
        
        i++;  // Skip data block
    }

    if (hasBasic) {
        uint16_t vars = (uint16_t)(23755 + totalProgLen);
        uint16_t eLine = (uint16_t)(vars + 1);
        spectrum->write(23635, 23755 & 0xFF);
        spectrum->write(23636, 23755 >> 8);
        spectrum->write(23627, vars & 0xFF);
        spectrum->write(23628, vars >> 8);
        spectrum->write(23641, eLine & 0xFF);
        spectrum->write(23642, eLine >> 8);
        spectrum->write(23645, eLine & 0xFF);
        spectrum->write(23646, eLine >> 8);
        spectrum->write(23647, eLine & 0xFF);
        spectrum->write(23648, eLine >> 8);
    }
    if (hasCode) {
        cpu->sp = 0xFFFE;
        cpu->iff1 = cpu->iff2 = 1;
        cpu->im = 1;
        cpu->halted = 0;
        cpu->pc = lastCodeStart;
    } else if (hasBasic) {
        cpu->pc = 23755;
    }
}
```

---

## Summary

**Root Cause:** No validation of tape file header values
- Start addresses can point to display memory (0x4000-0x5AFF)
- Data lengths not checked against address space bounds
- Program types not validated

**Screen Corruption:** Writing to 0x4000-0x5AFF corrupts the framebuffer
**Hang:** Invalid PC or stack pointer after CPU state corruption

**Fix:** Validate ALL header fields before writing
