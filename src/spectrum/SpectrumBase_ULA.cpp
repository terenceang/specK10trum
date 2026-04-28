#include "SpectrumBase.h"
#include <esp_log.h>

// static const char* TAG = "SpectrumBase"; // Unused

static constexpr int T_STATES_PER_LINE = 224;
static constexpr int FRAME_LINES = 312;
static constexpr int FRAME_T_STATES = FRAME_LINES * T_STATES_PER_LINE;
static constexpr int FIRST_ACTIVE_LINE = 64;
static constexpr int ACTIVE_LINES = 192;
static constexpr int ACTIVE_CYCLES_PER_LINE = 128;
static constexpr uint16_t SCREEN_BASE = 0x4000;
static constexpr uint16_t ATTR_BASE = 0x5800;

static uint16_t screenMemoryAddress(int line, int xByte) {
    return SCREEN_BASE
        | ((line & 0x07) << 11)
        | ((line & 0x38) << 5)
        | ((line & 0xC0) << 2)
        | xByte;
}

static uint16_t attributeMemoryAddress(int line, int xByte) {
    return ATTR_BASE + ((line >> 3) * 32) + xByte;
}

uint8_t SpectrumBase::getFloatingBusValue() {
    int line = m_ulaScanline;
    int cycle = m_ulaCycle;

    if (line < FIRST_ACTIVE_LINE || line >= FIRST_ACTIVE_LINE + ACTIVE_LINES) {
        return 0xFF;
    }

    if (cycle >= ACTIVE_CYCLES_PER_LINE) {
        return 0xFF;
    }

    int offset = cycle & 0x07;
    if (offset >= 4) {
        return 0xFF;
    }

    int cell = cycle >> 3;
    int xByte = (cell << 1) | ((offset >> 1) & 0x01);
    bool requestAttribute = (offset & 0x01) != 0;
    int lineInDisplay = line - FIRST_ACTIVE_LINE;

    uint16_t addr = requestAttribute
        ? attributeMemoryAddress(lineInDisplay, xByte)
        : screenMemoryAddress(lineInDisplay, xByte);

    uint8_t* page = m_memReadMap[addr >> 14];
    return page ? page[addr & 0x3FFF] : 0xFF;
}

void SpectrumBase::advanceULA(int tstates) {
    m_ulaClocks += (uint32_t)tstates;

    // Check for frame boundary
    if (m_ulaClocks >= FRAME_T_STATES) {
        // Ensure tape is advanced to the exact frame boundary for sound/sync
        flushTape();

        m_ulaClocks -= FRAME_T_STATES;

        // Publish this frame's border events for the renderer.
        portENTER_CRITICAL(&m_borderMux);
        memcpy(m_renderBorderEvents, m_borderEvents, m_borderEventCount * sizeof(BorderEvent));
        m_renderBorderEventCount = m_borderEventCount;
        m_renderInitialBorderColor = m_initialBorderColor;
        portEXIT_CRITICAL(&m_borderMux);

        // Copy beeper events into render buffer
        m_beeper.copyForFrame();

        // Reset border events for the new frame
        m_initialBorderColor = m_borderColor;
        m_borderEventCount = 0;

        // Trigger the 50Hz maskable interrupt
        z80_interrupt(&m_cpu, 0xFF);
    }

    m_ulaCycle += (uint16_t)tstates;
    while (m_ulaCycle >= T_STATES_PER_LINE) {
        m_ulaCycle -= T_STATES_PER_LINE;
        m_ulaScanline++;
        if (m_ulaScanline >= FRAME_LINES) {
            m_ulaScanline = 0;
        }
    }
}
