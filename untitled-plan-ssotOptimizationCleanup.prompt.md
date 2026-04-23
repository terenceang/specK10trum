## Plan: SSOT, optimization, cleanup

TL;DR: Consolidate duplicated hardware/constants (palettes, MADCTL, pins), extract renderer & memory-management responsibilities, and reduce runtime churn in the Z80 hot path and SPI/DMA code. Start with quick wins (palette, pins, test utils) then proceed to larger modular refactors (renderer module, MemoryManager, Z80 audit, SPI pooling). This plan focuses on minimal-risk incremental steps with verification for each phase.

**Steps**
1. Quick wins (safe, single-file or small-change edits)
   - Move palette arrays to `src/common/Palette.h` and update `Display` + `SpectrumBase`. (*depends on none*)
   - Extract MADCTL/orientation constants to `src/common/display_defs.h`. (*parallel with palette*)
   - Create `test/util/TestUtils.h/cpp` for shared `printSuiteBanner` / `logResult` and update tests. (*depends on tests*)
   - Create `src/board/board_pins.h` for pin definitions and update sources. (*parallel with above*)
2. Medium refactors (1-4 hours each)
   - Extract LUT cache & rendering helpers from `SpectrumBase` into `src/renderer/AttrLUTCache.*` and `src/renderer/Renderer.*`.
   - Create `src/memory/MemoryManager.*` to centralize allocation, ROM loading, and bank switching; refactor `Spectrum48K`/`Spectrum128K` to use it.
   - Move SPI/LCD helpers and DMA transaction pooling into `src/hw/spi_lcd.*` and reduce per-frame allocations.
3. Large tasks (multi-day)
   - Z80 emulator performance audit and targeted hot-path refactor with microbenchmarks.
   - Implement HAL/board abstraction and a mock HAL for running tests in host environment.
   - Add CI PlatformIO build + headless test runner.

**Relevant files**
- src/display/Display.cpp — rendering + SPI push, splash
- src/display/Display.h — display API
- src/spectrum/SpectrumBase.cpp — renderToRGB565 and attr LUT
- src/spectrum/Spectrum48K.cpp — memory/banking logic
- src/spectrum/Spectrum128K.cpp — memory/banking logic
- src/z80/z80.c — emulator core hot path
- src/expander/Expander.cpp — GPIO/I2C interaction
- src/instrumentation/Instrumentation.h — header linkage guards
- test/test_memory.cpp, test/test_128k_banking.cpp, test/test_runner.cpp — test duplication

**Verification**
1. Build: `platformio run -e unihiker_k10` (use VS Code task if preferred).
2. Run `run_all_tests` on device or test runner; ensure `test_memory` and `z80_test` pass before renderer changes.
3. After renderer refactor: compare frame outputs (hash or visual check) and re-run platform tests.
4. After MemoryManager: run banking tests and memory stress tests for fragmentation/failures.

**Decisions / Assumptions**
- Preserve existing public runtime APIs for `Display` and `Spectrum*` during incremental refactors; provide adapters when moving logic.
- Emphasize unit-testable modules for renderer and memory manager.
- Avoid ABI changes to `z80.c` until microbenchmarks confirm performance benefit.

**Further Considerations**
1. Option A: Start with palette + board pins + test utils (lowest risk). Option B: Start with renderer extraction (higher impact).
2. If PSRAM allocations are flaky, add allocation fallback and logging early.
3. For SPI DMA pooling, hardware testing is required — plan for device test run.
