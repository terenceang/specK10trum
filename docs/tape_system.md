# Tape Loading System

## Overview

The tape system emulates a ZX Spectrum cassette deck. It supports three distinct operating modes, each with different behaviour on load and during emulation. Files are stored in SPIFFS under `/spiffs/tapes/` and are selected from the web UI.

Supported formats: `.tap`, `.tzx`, `.tsx`

---

## Modes

Modes are defined in `src/spectrum/Tape.h` as `enum class TapeMode` and their behaviour is centralised in `TapeModeProfile`.

| Mode | `loadsInstantly` | `autoStartsPlayback` | `audibleTransport` | `exclusiveMonitorDuringRomLoad` |
|------|:---:|:---:|:---:|:---:|
| `MANUAL` | ✗ | ✗ | ✓ | ✗ |
| `AUTO` | ✗ | ✓ | ✓ | ✓ |
| `INSTANT` | ✓ | ✗ | ✗ | ✗ |

### MANUAL

The tape is inserted but stays stopped. The user types `LOAD ""` and presses **PLAY** manually in the web UI. The cassette deck animation is shown. Real-time tape audio is generated and fed into the EAR port.

### AUTO

Same as MANUAL but playback starts automatically when the Z80 PC enters the ROM LD-BYTES region (`0x0556–0x07FF`). The user only needs to type `LOAD ""` and press Enter. The tape monitor is enabled exclusively during ROM loading to suppress speaker noise.

### INSTANT

When a tape is selected, `instaload()` is called immediately. There is no real-time playback at all. The tape contents are injected directly into emulator memory and the Z80 CPU is set to the program's entry point. This is snapshot-style loading: no loading bars, no audio, no waiting.

---

## Mode Persistence and Selection

The active mode is persisted in the browser's `localStorage` under the key `zx_tape_mode`. Legacy values are migrated on startup:

| Old value | Mapped to |
|-----------|-----------|
| `normal`  | `auto`    |
| `player`  | `manual`  |

The web UI sends mode changes over WebSocket as `{"cmd":"tape_mode_<name>"}`. The backend parses this with `tapeModeFromName()` in `src/spectrum/Tape.cpp`.

---

## File Load Path

```
UI selects file
  → HTTP GET /api/tape?cmd=load&file=<name>
  → Webserver enqueues WebCommandType::TapeCmd (load, path=/spiffs/tapes/<name>)
  → main.cpp: tape.load(path)
  → handleLoadedTape(spectrum)
    → if INSTANT: tape.instaload(spectrum)
    → if AUTO:    tape.play()
    → if MANUAL:  tape.stop()  (insert only)
```

---

## Instant Load — `Tape::instaload()`

Defined in `src/spectrum/Tape.cpp`. Scans all tape blocks for standard header+data pairs and loads them directly into Z80 memory. Entry-point selection follows this priority:

### When a CODE block exists

The CODE block's `start` address from the tape header is used as the entry point. This is the most reliable source — it is what the tape author wrote when saving the program.

```
cpu->iy  = 0x5C3A    // ZX system-variable base (ROM ISR needs this)
cpu->sp  = 0xFF40    // typical 48K RAMTOP
cpu->pc  = lastCodeStart
```

If BASIC was also loaded (the common BASIC-loader+CODE-data pattern), the BASIC program is still written to memory at address 23755 and its system variables are set up, but execution starts at the CODE entry directly — the BASIC loader is bypassed.

### When only BASIC is present (no CODE block)

1. If the BASIC header has an autorun line (`LINE n` where `n < 32768`), the ROM `LINE-NEW` routine at `0x1B9E` is used to start that BASIC line.
2. Otherwise, if a `USR n` token is found in the BASIC program, the CPU jumps directly to that address.
3. If neither is found, the BASIC is in memory but execution is not started.

### BASIC system variables set on load

| System variable | Address | Value set |
|----------------|---------|-----------|
| `PROG`   | 23635 | 23755 (start of BASIC area) |
| `VARS`   | 23637 | 23755 + `vars offset` from header |
| `NEWPPC` | 23618 | autorun line number |
| `NSPPC`  | 23620 | 0 |
| `PPC`    | 23621 | autorun line number |
| `SUBPPC` | 23623 | 0 |
| `E-LINE` | 23641 | 23755 + program length |
| `WORKSP` | 23649 | E-LINE + 1 |
| `STKBOT` | 23651 | WORKSP |
| `STKEND` | 23653 | WORKSP |

A `0x80` end-of-program sentinel is written at E-LINE.

### Diagnostics

After each `instaload()` call the result is stored in `m_lastInstaloadDiag` and exposed in the tape status JSON as `instaloadDiag`. Serial log examples:

```
# BASIC + CODE tape (most games)
Tape: instaload summary: ok cstart=8000 usr=33792 code=1 basic=1 skip=0
Tape: instaload: executing CODE at 0x8000

# Pure BASIC tape
Tape: instaload summary: ok basic auto=10 skip=0
Tape: instaload: starting BASIC line 10 via ROM LINE-NEW

# Failure
Tape: instaload summary: failed basic=0 code=0 skip=4 bytes=0
```

---

## Real-time Playback — `Tape::advance()`

Used by MANUAL and AUTO modes. Called every emulation step via `SpectrumBase::flushTape()` with the number of T-states elapsed. Generates EAR signal transitions which are fed to:

1. The port `0xFE` EAR bit (read by the ROM LD-BYTES routine)
2. The beeper mixer when the tape monitor is on

### Standard 48K timings used

| Signal | T-states |
|--------|----------|
| Pilot tone pulse | 2168 |
| Sync 1 | 667 |
| Sync 2 | 735 |
| Bit 0 (single pulse) | 855 |
| Bit 1 (single pulse) | 1710 |
| Header pilot pulses | 8063 |
| Data pilot pulses | 3223 |

TZX turbo blocks (`0x11`) use the timing values stored in the block header.

---

## AUTO Mode — ROM Trap Detection

In AUTO mode, `SpectrumBase::step()` watches for the Z80 PC entering the LD-BYTES region:

```cpp
bool inLDBytes = (m_cpu.pc >= 0x0556 && m_cpu.pc < 0x0800);
if (!m_wasInLDBytes && inLDBytes) {
    m_tape.play();
}
```

On the rising edge (first entry into that region) the tape starts playing automatically.

---

## INSTANT Mode — LD-BYTES Interception

In INSTANT mode, `SpectrumBase::step()` intercepts before `z80_step()`:

```cpp
if (mode.loadsInstantly &&
    m_tape.isLoaded() &&
    isTapeRomActive() &&
    m_cpu.pc == Tape::LD_BYTES_ENTRY) {
    m_tape.stop();
    m_tape.instaload(this);
}
```

If the Z80 reaches `LD_BYTES_ENTRY` (`0x0556`) with a tape loaded, `instaload()` is called instead of letting the ROM routine run. This acts as a second trigger path on top of the on-load trigger in `handleLoadedTape()`.

---

## Tape Monitor

The tape monitor mixes the EAR signal into the audio output so the user can hear the tape loading. Controlled by the toggle in the web UI and persisted in `localStorage` as `zx_tape_monitor`.

In AUTO mode the monitor is exclusive during ROM loading: when the CPU is in the LD-BYTES region the speaker path is suppressed, preventing BASIC program sounds from mixing with the tape signal.

---

## HTTP / WebSocket API

### HTTP — `/api/tape`

| `cmd` query param | Action |
|-------------------|--------|
| `status` (default) | Returns JSON status object |
| `load&file=<name>` | Load tape from `/spiffs/tapes/<name>` |
| `play` | Start playback |
| `stop` | Stop and rewind |
| `pause` | Toggle pause |
| `rewind` | Rewind to start |
| `ffwd` | Advance one block |
| `eject` | Unload tape |
| `instant_load` | Run `instaload()` on current tape |

#### Status JSON

```json
{
  "loaded": 1,
  "playing": 0,
  "paused": 0,
  "mode": "instant",
  "currentBlock": 1,
  "totalBlocks": 4,
  "instaloadDiag": "ok cstart=8000 usr=33792 code=1 basic=1 skip=0"
}
```

### WebSocket — `/ws`

Text JSON commands:

| Command | Action |
|---------|--------|
| `{"cmd":"tape_play"}` | Play |
| `{"cmd":"tape_stop"}` | Stop |
| `{"cmd":"tape_pause"}` | Pause |
| `{"cmd":"tape_rewind"}` | Rewind |
| `{"cmd":"tape_ffwd"}` | Fast-forward |
| `{"cmd":"tape_eject"}` | Eject |
| `{"cmd":"tape_instaload"}` | Instant-load current tape |
| `{"cmd":"tape_mode_manual"}` | Switch to MANUAL mode |
| `{"cmd":"tape_mode_auto"}` | Switch to AUTO mode |
| `{"cmd":"tape_mode_instant"}` | Switch to INSTANT mode |
| `{"cmd":"tape_monitor_on"}` | Enable tape monitor audio |
| `{"cmd":"tape_monitor_off"}` | Disable tape monitor audio |

---

## Key Source Files

| File | Role |
|------|------|
| `src/spectrum/Tape.h` | `TapeMode` enum, `TapeModeProfile` struct, class interface |
| `src/spectrum/Tape.cpp` | Block parsing, EAR signal generation, `instaload()` |
| `src/spectrum/SpectrumBase.cpp` | Per-step ROM trap detection, `step()` |
| `src/spectrum/SpectrumBase_Tape.cpp` | `flushTape()`, beeper/monitor routing |
| `src/main.cpp` | `handleLoadedTape()`, command dispatch |
| `src/webserver/Webserver.cpp` | HTTP and WebSocket API handlers |
| `spiffs/www/tape-player.js` | Web UI state, mode persistence, status polling |
| `spiffs/www/index.html` | Tape player panel HTML |
