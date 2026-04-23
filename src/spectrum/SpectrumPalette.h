#pragma once
#include <stdint.h>

// Standard Spectrum colors (approximate)
// Normal: 0xD7 (215), Bright: 0xFF (255)
// Swapped for Big-Endian LCD (val = (LowByte << 8) | HighByte)

static const uint16_t SPECTRUM_PALETTE_NORMAL[8] = {
    0x0000, // black (#000000)
    0x1800, // blue (#0000D7 -> 0x001A, swapped 0x1A00) -> using 0x1800 for 75%
    0x00C0, // red (#D70000 -> 0xD000, swapped 0x00D0) -> using 0x00C0 for 75%
    0x18C0, // magenta (#D700D7 -> 0xD01A, swapped 0x1AD0) -> using 0x18C0
    0x2006, // green (#00D700 -> 0x06A0, swapped 0xA006) -> using 0x2006 (approx)
    0x3806, // cyan (#00D7D7 -> 0x06BA, swapped 0xBA06) -> using 0x3806
    0x20C6, // yellow (#D7D700 -> 0xD6A0, swapped 0xA0D6) -> using 0x20C6
    0x38C6, // white (#D7D7D7 -> 0xD6BA, swapped 0xBAD6) -> using 0x38C6
};

static const uint16_t SPECTRUM_PALETTE_BRIGHT[8] = {
    0x0000, // black
    0x1F00, // bright blue (#0000FF -> 0x001F, swapped 0x1F00)
    0x00F8, // bright red (#FF0000 -> 0xF800, swapped 0x00F8)
    0x1FF8, // bright magenta (#FF00FF -> 0xF81F, swapped 0x1FF8)
    0xE007, // bright green (#00FF00 -> 0x07E0, swapped 0xE007)
    0xFF07, // bright cyan (#00FFFF -> 0x07FF, swapped 0xFF07)
    0xE0FF, // bright yellow (#FFFF00 -> 0xFFE0, swapped 0xE0FF)
    0xFFFF, // bright white (#FFFFFF -> 0xFFFF, swapped 0xFFFF)
};

static inline uint16_t spectrum_palette(uint8_t index, bool bright) {
    return bright ? SPECTRUM_PALETTE_BRIGHT[index & 0x07] : SPECTRUM_PALETTE_NORMAL[index & 0x07];
}
