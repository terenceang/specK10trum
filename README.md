# specK10trum

**Status**: ✅ Working (tag: `working`)

## Latest (Full Audio Vectorised - 5b6abb9)

Stable release with vectorized audio processing and complete Spectrum emulation.

<p align="center">
	<img src="assets/baner.png" alt="SpecK10trum banner" />
</p>

**Author**: Terence Ang

## Overview
--------
specK10trum is an ESP32-targeted project implementing a ZX Spectrum emulator and related tooling.

Third-party attribution
-----------------------
- Z80 CPU core and parts of the Z80 / ZX Spectrum reference material are derived from the ZOT project by antirez (MIT): https://github.com/antirez/ZOT
- TAP / tape-format layout and the LD-BYTES ROM-trap approach used by the virtual cassette follow the public documentation on:
    - Sinclair Wiki: https://sinclair.wiki.zxnet.co.uk/wiki/TAP_format
    - Skoolkid's annotated ZX Spectrum ROM disassembly (LD-BYTES at `$0556`): https://skoolkid.github.io/rom/asm/0556.html
    - Kaitai Struct schema for ZX Spectrum TAP: https://formats.kaitai.io/zx_spectrum_tap/
- `.z80` snapshot parsing follows the public World of Spectrum format notes.
- ZOT is licensed under the MIT license. Where code or design ideas from ZOT are used (notably the Z80 core and the Z80/Spectrum spec materials), those portions remain under the original MIT terms.

License
-------
This repository is licensed under the MIT License — see [LICENSE](LICENSE) for details.

This project contains original code by Terence Ang and portions derived from ZOT (MIT). Unless otherwise noted in individual files, ZOT-derived code is distributed under the MIT license.

Build
-----
Build with PlatformIO (environment `unihiker_k10`):

```bash
platformio run --environment unihiker_k10
```

Development
-----------
Prerequisites: install PlatformIO (CLI or VS Code extension), Python 3.8+ and Git. For ESP-IDF based builds, ensure PlatformIO's ESP-IDF toolchain is available via PlatformIO.

- Build: `platformio run --environment unihiker_k10`
- Upload: `platformio run --environment unihiker_k10 --target upload --upload-port COM5` (adjust port)
- Monitor: `pio device monitor --port COM5 --baud 115200`

Configuration notes:
- Board-specific SDK config is `sdkconfig.unihiker_k10` — edit it to enable PSRAM, console routing, and other ESP-IDF options. See the development reference for recommended settings.
- See the full development guide in the docs: [K10 Development Reference](docs/K10_Development_Reference.md)

References
----------
- ZOT (Z80 / Spectrum / CP/M): https://github.com/antirez/ZOT
- Z80 and Spectrum specification documents (used as references) are available inside the ZOT repository under `z80-specs/` and `spectrum-specs/`.

AI Assistance
------------
Some changes in this repository (documentation reorganization, small scripts, and non-core refactors) were performed with assistance from large language models and AI coding tools. Tools used include Claude, Gemini, Deepseek, and GitHub Copilot. All generated suggestions were reviewed and approved by the author.

Status / What's Working
------------------------
Emulation
- **Z80 CPU**: full documented + undocumented opcode set (CB / ED / DD / FD / DDCB / FDCB prefixes). Hot dispatch placed in IRAM, page-mapped memory reads/writes inlined with `__builtin_expect` bias. See `src/z80/`.
- **ZX Spectrum 48K**: ROM + 48 KiB RAM, floating-bus reads, centred 256×192 render with per-scanline border events.
- **ZX Spectrum 128K**: 8× 16 KiB banked RAM, dual ROM paging via port `$7FFD`, AY register write/read port decoding. Select model in `src/main.cpp` (`SPECTRUM_MODEL_48K` / `SPECTRUM_MODEL_128K`).

Video
- ILI9341 over SPI at 80 MHz, landscape 320×240, double-buffered with a ping-pong DMA-strip pusher on core 1. Column window programmed once at init; each strip emits only the row window + payload.
- Renderer uses a 4 KiB DRAM pixel-mask LUT with SWAR pixel expansion; per-attribute paper/ink/diff recomputed only on attribute change.

Audio
- I2S stereo out at 44.1 kHz. Beeper events sampled per frame with a Q16 linear step; Q15 volume scaling; mute and 0‑100 volume controls routed to the expander buttons.

Storage / I/O
- SPIFFS partition for assets (`spiffs/`): `48k.rom`, `128k.rom`, optional splash BMP, `autoexec.z80`, `tape.tap`.
- **Snapshot loader**: `.z80` (v1/v2/v3), including 128K page mapping, RLE decode, and 48K snapshots loaded into a 128K machine via a fallback path. AY register state restored on 128K snapshots.
- **Virtual Cassette Player**: Full support for `.tap`, `.tzx`, and `.tsx` files with three loading modes:
    - **Instant**: Injects blocks directly into memory via ROM `LD-BYTES` trap at `$0556`.
    - **Normal**: Automatically starts playing authentic EAR pulses when the Spectrum enters the loading routine.
    - **Authentic Player**: Provides a virtual cassette deck UI with full transport controls (Play, Stop, Rewind, FFWD, Pause, Eject) and animated reels. Requires manual operation: type `LOAD ""` then click Play.
    - **Feedback**: Features authentic border stripes (Red/Cyan pilot, Yellow/Blue data) and loading sounds through the speakers.
- **Connectivity & Provisioning**:
    - **BLE Provisioning**: Easy Wi‑Fi setup using the "ESP Provisioning" app. Service name: `PROV_speck10` (POP: `12345678`).
    - **Wi‑Fi Virtual Keyboard & File Picker**: Low-latency WebSocket-based keyboard and a built-in file picker for ROMs, snapshots, and tapes accessible via browser. Clear IP overlays appear on the Spectrum screen when connected.
    - **Fallback SoftAP**: If configured Wi‑Fi is unavailable, the device creates an open hotspot named `SpecK10trum-Connect` (192.168.4.1) for direct access.
- XL9535 I²C expander drives the backlight, user LED, and the two front-panel buttons (volume ±, long-press both = mute).

Build / test
- PlatformIO `unihiker_k10` builds clean under ESP-IDF 5.5 with `-O2`.
- Self-test harness in `test/` runs the memory-map checks and a Z80 instruction test suite on boot (skipped when a snapshot is autoloaded).

Roadmap / TODO
--------------
Priority order:

1. SAVE support via `SA-BYTES` ($04C2) trap → write-back to a mounted TAP image.
2. AY-3-8912 PSG tone synthesis (chip is register-mapped today; no audio output yet).



Issues and PRs welcome.
