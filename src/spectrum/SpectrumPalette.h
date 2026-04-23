#pragma once
#include <stdint.h>

static const uint16_t SPECTRUM_PALETTE_NORMAL[8] = {
    0x0000, // black
    0x001B, // blue
    0xF800, // red
    0xF81F, // magenta
    0x07E0, // green
    0x07FF, // cyan
    0xFFE0, // yellow
    0xFFFF, // white
};

static const uint16_t SPECTRUM_PALETTE_BRIGHT[8] = {
    0x0000, // black
    0x001F, // bright blue
    0xF800, // bright red
    0xF81F, // bright magenta
    0x07E0, // bright green
    0x07FF, // bright cyan
    0xFFE0, // bright yellow
    0xFFFF, // bright white
};

static inline uint16_t spectrum_palette(uint8_t index, bool bright) {
    return bright ? SPECTRUM_PALETTE_BRIGHT[index & 0x07] : SPECTRUM_PALETTE_NORMAL[index & 0x07];
}
