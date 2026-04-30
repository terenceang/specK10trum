#include "SpectrumBase.h"
#include "SpectrumPalette.h"
#include <string.h>

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FIRST_ACTIVE_LINE = 64;

void SpectrumBase::renderBorder(uint16_t* buffer, int bufWidth, int bufHeight, int offset_x, int offset_y, int source_width, int source_height) {
    static BorderEvent local_events[MAX_BORDER_EVENTS];
    size_t local_count;
    uint8_t local_init_color;
    portENTER_CRITICAL(&m_borderMux);
    local_count = m_renderBorderEventCount;
    if (local_count > MAX_BORDER_EVENTS) local_count = MAX_BORDER_EVENTS;
    if (local_count) memcpy(local_events, m_renderBorderEvents, local_count * sizeof(BorderEvent));
    local_init_color = m_renderInitialBorderColor;
    portEXIT_CRITICAL(&m_borderMux);

    uint32_t border_pair[8];
    for (int i = 0; i < 8; ++i) {
        uint16_t c = spectrum_palette(i, false);
        border_pair[i] = ((uint32_t)c << 16) | c;
    }
    size_t evt_idx = 0;
    uint8_t cur_color = local_init_color & 0x07;
    uint32_t cur_pair = border_pair[cur_color];

    const int total_pairs = bufWidth / 2;
    const int left_pairs = offset_x / 2;
    const int active_end_pair = (offset_x + source_width) / 2;

    auto consume_until = [&](uint32_t tstate_excl) {
        while (evt_idx < local_count &&
               local_events[evt_idx].tstates < tstate_excl) {
            cur_color = local_events[evt_idx].color & 0x07;
            evt_idx++;
        }
        cur_pair = border_pair[cur_color];
    };

    for (int y = 0; y < bufHeight; ++y) {
        int spectrum_y = FIRST_ACTIVE_LINE + (y - offset_y);
        if (spectrum_y < 0) spectrum_y = 0;
        if (spectrum_y >= FRAME_LINES) spectrum_y = FRAME_LINES - 1;

        uint32_t tstate_base = (uint32_t)spectrum_y * T_STATES_PER_LINE;
        uint32_t* lineStart32 = (uint32_t*)&buffer[y * bufWidth];
        bool is_active_y = (y >= offset_y && y < offset_y + source_height);

        consume_until(tstate_base);

        if (!is_active_y) {
            for (int xp = 0; xp < total_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < local_count &&
                    local_events[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }
        } else {
            for (int xp = 0; xp < left_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < local_count &&
                    local_events[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }

            consume_until(tstate_base + (uint32_t)active_end_pair);

            for (int xp = active_end_pair; xp < total_pairs; ++xp) {
                uint32_t t = tstate_base + (uint32_t)xp;
                if (evt_idx < local_count &&
                    local_events[evt_idx].tstates <= t) {
                    consume_until(t + 1);
                }
                lineStart32[xp] = cur_pair;
            }
        }
    }
}

// Cached attribute LUTs, shared across renderActiveArea calls. Indexed by the
// low 7 bits of the Spectrum attribute byte (FLASH bit ignored). Each entry is
// 256 * 8 * 2 = 4 KiB; full table is 512 KiB when warm.
static uint16_t* s_attr_lut[128] = { 0 };

uint16_t* SpectrumBase::getOrBuildAttrLUT(int attr_index) {
    if ((unsigned)attr_index >= 128) return nullptr;
    uint16_t* table = s_attr_lut[attr_index];
    if (table) return table;

    size_t bytes = 256 * 8 * sizeof(uint16_t);
    uint16_t* alloc = (uint16_t*)allocateMemory(bytes, "Attr LUT");
    if (!alloc) return nullptr;

    bool bright = (attr_index & 0x40) != 0;
    uint16_t ink = spectrum_palette(attr_index & 0x07, bright);
    uint16_t paper = spectrum_palette((attr_index >> 3) & 0x07, bright);

    uint32_t ink32 = (ink << 16) | ink;
    uint32_t paper32 = (paper << 16) | paper;
    uint32_t diff32 = ink32 ^ paper32;

    for (int pix = 0; pix < 256; ++pix) {
        uint32_t* out32 = (uint32_t*)&alloc[pix * 8];
        for (int i = 0; i < 4; i++) {
            uint8_t two_bits = (pix >> (6 - i * 2)) & 0x03;
            uint32_t mask = (two_bits & 0x02) ? 0x0000FFFF : 0;
            mask |= (two_bits & 0x01) ? 0xFFFF0000 : 0;
            out32[i] = paper32 ^ (mask & diff32);
        }
    }

    s_attr_lut[attr_index] = alloc;
    return alloc;
}

void SpectrumBase::prewarmAttrLUTs() {
    for (int i = 0; i < 128; ++i) {
        getOrBuildAttrLUT(i);
    }
}

void SpectrumBase::renderActiveArea(uint16_t* buffer, int bufWidth, int bufHeight, int offset_x, int offset_y, int source_width, int source_height) {
    uint8_t* ramBank1 = m_videoPagePtr ? m_videoPagePtr : getPagePtr(1);
    if (!ramBank1) return;

    for (int y = 0; y < source_height; ++y) {
        uint16_t* linePtr = &buffer[(offset_y + y) * bufWidth + offset_x];
        uint16_t y_off = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16_t attr_off = 0x1800 + ((y >> 3) * 32);

        for (int xByte = 0; xByte < 32; ++xByte) {
            uint8_t pixels = ramBank1[y_off | xByte];
            uint8_t attr = ramBank1[attr_off | xByte];
            uint16_t* lut = getOrBuildAttrLUT(attr & 0x7F);
            if (lut) {
                uint64_t* src64 = (uint64_t*)&lut[pixels * 8];
                uint64_t* dst64 = (uint64_t*)linePtr;
                dst64[0] = src64[0]; dst64[1] = src64[1];
            } else {
                bool bright = (attr & 0x40) != 0;
                uint16_t ink = spectrum_palette(attr & 0x07, bright);
                uint16_t paper = spectrum_palette((attr >> 3) & 0x07, bright);
                uint32_t ink32 = (ink << 16) | ink;
                uint32_t paper32 = (paper << 16) | paper;
                uint32_t diff32 = ink32 ^ paper32;
                uint32_t* linePtr32 = (uint32_t*)linePtr;

                for (int i = 0; i < 4; i++) {
                    uint8_t two_bits = (pixels >> (6 - i * 2)) & 0x03;
                    uint32_t mask = (two_bits & 0x02) ? 0x0000FFFF : 0;
                    mask |= (two_bits & 0x01) ? 0xFFFF0000 : 0;
                    linePtr32[i] = paper32 ^ (mask & diff32);
                }
            }
            linePtr += 8;
        }
    }
}

void SpectrumBase::renderToRGB565(uint16_t* buffer, int bufWidth, int bufHeight) {
    if (!buffer) return;

    const int source_width = 256;
    const int source_height = 192;
    const int offset_x = (bufWidth - source_width) / 2;
    const int offset_y = (bufHeight - source_height) / 2;

    renderBorder(buffer, bufWidth, bufHeight, offset_x, offset_y, source_width, source_height);
    renderActiveArea(buffer, bufWidth, bufHeight, offset_x, offset_y, source_width, source_height);

    m_lastRenderedBorderColor = m_borderColor;
}
