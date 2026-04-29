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

    // Volume control for loading (0.0-1.0)
    void setVolume(float vol) { m_volume = vol > 1.0f ? 1.0f : (vol < 0.0f ? 0.0f : vol); }
    float getVolume() const { return m_volume; }
    
    static constexpr int16_t AMPLITUDE = 3276; // ~10% of 32767
    static constexpr int SAMPLES_PER_FRAME = 882;

private:
    int16_t m_frameBuffer[SAMPLES_PER_FRAME];
    int16_t m_renderBuffer[SAMPLES_PER_FRAME];
    int m_renderedSamples;
    float m_nextSampleCorrection;

    uint8_t m_speakerLevel;
    bool m_externalEar;
    float m_volume = 1.0f;

    void renderSamples(int start, int end);
};
