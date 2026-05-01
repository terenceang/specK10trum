#include "SpectrumBase.h"
#include "input/Input.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

// static const char* TAG = "SpectrumBase"; // Unused
// static uint16_t screenMemoryAddress(int line, int xByte) { ... } // Unused
// static uint16_t attributeMemoryAddress(int line, int xByte) { ... } // Unused

void SpectrumBase::writePortFE(uint8_t value) {
    uint8_t newColor = value & 0x07;
    if (newColor != m_borderColor) {
        if (m_borderEventCount < MAX_BORDER_EVENTS) {
            m_borderEvents[m_borderEventCount++] = { m_ulaClocks, newColor };
        }
        m_borderColor = newColor;
    }

    // Speaker (bit 4) handling: delegate to Beeper
    m_lastSpeakerBit = (value >> 4) & 0x01;
    m_beeper.recordSpeakerEvent(m_ulaClocks, m_lastSpeakerBit);
}

uint8_t SpectrumBase::readPortFE(uint16_t port) {
    uint8_t val = 0xBF; // Bits 0-4 are columns (active low), Bit 6 (EAR) is 0 by default, others 1

    // Ensure tape is advanced to current CPU time before sampling EAR.
    flushTape();

    // Standard Spectrum keyboard: Address bits A8-A15 select rows.
    uint8_t select = ~(port >> 8);
    uint8_t kbd = 0xFF;
    if (select & 0x01) kbd &= input_getKeyboardRow(0);
    if (select & 0x02) kbd &= input_getKeyboardRow(1);
    if (select & 0x04) kbd &= input_getKeyboardRow(2);
    if (select & 0x08) kbd &= input_getKeyboardRow(3);
    if (select & 0x10) kbd &= input_getKeyboardRow(4);
    if (select & 0x20) kbd &= input_getKeyboardRow(5);
    if (select & 0x40) kbd &= input_getKeyboardRow(6);
    if (select & 0x80) kbd &= input_getKeyboardRow(7);

    val &= kbd;

    // EAR input is bit 6.
    bool current_ear = m_tape.getEar();
    if (current_ear) val |= 0x40;
    else             val &= ~0x40;

    // Bits 5 and 7 are usually 1 or floating (we'll keep them 1)
    val |= 0xA0;

    return val;
}

void SpectrumBase::setKeyboardRow(uint8_t row, uint8_t columns) {
    input_setKeyboardRow(row, columns);
}
