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

## Sources

- World of Spectrum: Tape Formats
- TAP file specifications
