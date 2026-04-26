#pragma once
#include <stdint.h>
#include <stddef.h>

class Beeper {
public:
    Beeper();
    void reset();
    void recordEvent(uint32_t tstates, uint8_t level);
    // Called at frame boundary to copy events for rendering
    void copyForFrame();
    // Render recorded events from last frame into PCM samples
    void renderFrame(int16_t* audio_buf, int num_samples);
    void setExternalEar(bool high) { m_externalEar = high; }
    bool getExternalEar() const { return m_externalEar; }
    uint8_t currentSpeakerLevel() const { return m_speakerLevel; }
    
    static constexpr int16_t AMPLITUDE = 3276; // ~10% of 32767

private:
    struct BeeperEvent { uint32_t tstates; uint8_t level; };
    static constexpr size_t MAX_BEEPER_EVENTS = 2048;
    BeeperEvent m_events[MAX_BEEPER_EVENTS];
    size_t m_eventCount;

    BeeperEvent m_renderEvents[MAX_BEEPER_EVENTS];
    size_t m_renderEventCount;

    uint8_t m_initialLevel;
    uint8_t m_speakerLevel;
    bool m_externalEar;

    // DC blocker state
    float m_lastX;
    float m_lastY;
};
