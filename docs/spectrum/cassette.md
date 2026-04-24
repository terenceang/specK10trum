# ZX Spectrum Cassette Format and Tape I/O

## Overview

The ZX Spectrum uses a simple audio-based cassette format for program and data
storage. Data is encoded as square-wave tones of different durations; the CPU
bit-bangs the MIC line (output) during saving and samples the EAR input when
loading.

## Basic Structure

- Leader pulse: long series of 0x00 (pilot tone) to allow the tape reader to
  stabilise.
- Sync header: alternating short/long pulses to mark start of data.
- Data blocks: bytes encoded LSB-first using a pair of pulse lengths per bit.
- Checksum: simple byte sum or CRC depending on format.

## Saving

When saving, the ROM toggles the MIC output and generates tones on the tape
output. The timing is critical; small changes in sample rate cause load
failures.

## Loading

Loading samples the EAR input and measures pulse lengths. Simple loading code
measures pulse width and converts to bits.

## Implementation Notes

Practical emulator approach:

- On save: write a .wav file or TAP image containing the encoded blocks.
- On load: accept TAP files or sample a provided WAV and decode pulses.
- Provide a "virtual cassette" UI to mount/unmount tapes and list files.

## This Project

Implemented: **TAP load via ROM trap** (`src/spectrum/Tape.{h,cpp}`).

- On boot, `/spiffs/tape.tap` (or `autoload.tap`) is mounted if present.
- `SpectrumBase::step()` checks for `PC == 0x0556` and, when the 48K BASIC
  ROM is paged, services the load directly from the TAP buffer: copies the
  block's data bytes into memory, adjusts IX/DE/A, sets the carry flag from
  the XOR parity, then pops PC to RET. Each block advances the tape cursor.
- For 128K, the trap is gated on bit 4 of port `$7FFD` (48K BASIC ROM
  selected). The 128K editor ROM at 0x0556 is left alone.
- Unsupported: TZX (variable-rate and turbo blocks), WAV, SAVE. Turbo
  loaders that bypass the ROM (e.g. Speedlock) are also out of scope.

TAP block layout (little-endian):

    u16 len              ; bytes that follow (flag + data + xor)
    u8  flag             ; 0x00 header, 0xFF data
    u8  data[len - 2]    ; payload
    u8  xor              ; XOR of flag + data -> parity; valid iff == 0

Header payload (17-byte blocks, flag = 0x00):

    u8  type             ; 0=program 1=num-array 2=char-array 3=bytes
    u8  filename[10]     ; space-padded
    u16 data_len
    u16 param1
    u16 param2

## Sources

- [Sinclair Wiki: TAP format](https://sinclair.wiki.zxnet.co.uk/wiki/TAP_format)
- [Kaitai schema for ZX Spectrum TAP](https://formats.kaitai.io/zx_spectrum_tap/)
- [ROM disassembly: LD-BYTES at 0x0556](https://skoolkid.github.io/rom/asm/0556.html)
- [Sinclair Wiki: Spectrum tape interface](https://sinclair.wiki.zxnet.co.uk/wiki/Spectrum_tape_interface)
