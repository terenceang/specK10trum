# Z80 Snapshot Format (SNA / Z80)

This document summarises the common Z80 snapshot formats used by Spectrum
emulators (.sna, .z80) and how state is stored for quick-loading.

## Typical Contents

- CPU registers (AF, BC, DE, HL, IX, IY, SP, PC)
- Interrupt mode and flags
- Memory pages (often 48K or 128K images)
- ULA state (border, contention phase)
- Hardware flags (tape motor, AY registers, peripherals)

## Implementation Notes

- Support multiple snapshot versions and detect by header magic
- For 48K SNA: store 48K RAM and CPU state; for 128K formats include paging
  registers
- Preserve ULA timing if possible to allow cycle-accurate resume

## Sources

- Z80 snapshot format documentation
- Emulator source code (Fuse, Spectemu)
