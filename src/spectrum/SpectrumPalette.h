#pragma once
#include <stdint.h>

// Standard Spectrum colors (approximate)
// Normal: 0xD7 (215), Bright: 0xFF (255)

static const uint16_t SPECTRUM_PALETTE_NORMAL[8] = {
    0x0000, // black (#000000)
    0x0018, // blue (#0000D7 -> 0x001A) -> using 0x0018 for 75%
    0xC000, // red (#D70000 -> 0xD000) -> using 0xC000 for 75%
    0xC018, // magenta (#D700D7 -> 0xD01A) -> using 0xC018
    0x0600, // green (#00D700 -> 0x06A0) -> using 0x0600 (approx)
    0x0618, // cyan (#00D7D7 -> 0x06BA) -> using 0x0618
    0xC600, // yellow (#D7D700 -> 0xD6A0) -> using 0xC600
    0xC618, // white (#D7D7D7 -> 0xD6BA) -> using 0xC618
};

static const uint16_t SPECTRUM_PALETTE_BRIGHT[8] = {
    0x0000, // black
    0x001F, // bright blue (#0000FF)
    0xF800, // bright red (#FF0000)
    0xF81F, // bright magenta (#FF00FF)
    0x07E0, // bright green (#00FF00)
    0x07FF, // bright cyan (#00FFFF)
    0xFFE0, // bright yellow (#FFFF00)
    0xFFFF, // bright white (#FFFFFF)
};

static inline uint16_t spectrum_palette(uint8_t index, bool bright) {
    uint16_t color = bright ? SPECTRUM_PALETTE_BRIGHT[index & 0x07] : SPECTRUM_PALETTE_NORMAL[index & 0x07];
    // Swap for Big-Endian LCD
    return __builtin_bswap16(color);
}
