#pragma once
#include <stdint.h>
#include <stddef.h>

class Beeper {
public:
    Beeper();
    void reset();
    void recordEvent(uint32_t tstates, uint8_t level);
    // Render up to target T-state
    void renderTo(uint32_t tstates);
    // Called at frame boundary to finalize and swap buffers
    void copyForFrame();
    // Get the finished buffer for the last frame
    void getFrameBuffer(int16_t* audio_buf, int num_samples);
    
    void setExternalEar(bool high) { m_externalEar = high; }
    bool getExternalEar() const { return m_externalEar; }
    uint8_t currentSpeakerLevel() const { return m_speakerLevel; }
    
    static constexpr int16_t AMPLITUDE = 3276; // ~10% of 32767
    static constexpr int SAMPLES_PER_FRAME = 882;

private:
    int16_t m_frameBuffer[SAMPLES_PER_FRAME];
    int16_t m_renderBuffer[SAMPLES_PER_FRAME];
    int m_renderedSamples;
    float m_nextSampleCorrection;

    uint8_t m_speakerLevel;
    bool m_externalEar;

    // DC blocker state
    float m_lastX;
    float m_lastY;

    // 2nd-order Butterworth Low-Pass (Analog Warmth)
    float m_lp_x1, m_lp_x2;
    float m_lp_y1, m_lp_y2;

    void renderSamples(int start, int end);
};
