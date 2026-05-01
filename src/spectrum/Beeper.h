#pragma once
#include <stdint.h>
#include <stddef.h>

class Beeper {
public:
    Beeper();
    void reset();
    
    // Record hardware state changes (speaker bit or tape EAR)
    void recordSpeakerEvent(uint32_t tstates, uint8_t level);
    void recordTapeEvent(uint32_t tstates, uint8_t level);

    // Render up to target T-state
    void renderTo(uint32_t tstates);
    // Called at frame boundary to finalize and swap buffers
    void copyForFrame();
    // Get the finished buffer for the last frame
    void getFrameBuffer(int16_t* audio_buf, int num_samples);
    
    void setExternalEar(bool high) { m_externalEar = high; }
    bool getExternalEar() const { return m_externalEar; }
    uint8_t currentSpeakerLevel() const { return m_speakerLevel; }
    void setTapeMonitorEnabled(bool enabled) { m_tapeMonitorEnabled = enabled; }
    bool isTapeMonitorEnabled() const { return m_tapeMonitorEnabled; }
    void setSpeakerPathEnabled(bool enabled) { m_speakerPathEnabled = enabled; }
    bool isSpeakerPathEnabled() const { return m_speakerPathEnabled; }

    // Volume control for loading (0.0-1.0)
    void setVolume(float vol) { m_volume = vol > 1.0f ? 1.0f : (vol < 0.0f ? 0.0f : vol); }
    float getVolume() const { return m_volume; }
    
    static constexpr int16_t AMPLITUDE = 3276; // ~10% of 32767
    static constexpr float TAPE_GAIN = 0.35f;
    static constexpr int SAMPLES_PER_FRAME = 882;

private:
    int16_t m_frameBuffer[SAMPLES_PER_FRAME];
    int16_t m_renderBuffer[SAMPLES_PER_FRAME];
    int m_renderedSamples;
    float m_speakerNextCorrection;
    float m_tapeNextCorrection;

    uint8_t m_speakerLevel;
    uint8_t m_tapeLevel;
    bool m_externalEar;
    bool m_tapeMonitorEnabled = true;
    bool m_speakerPathEnabled = true;
    float m_volume = 1.0f;

    // Single-source amplitude helpers. Used by both steady-state rendering
    // and PolyBLEP transition math so the two paths cannot drift apart.
    float speakerAmp() const { return m_volume * (float)AMPLITUDE; }
    float tapeAmp()    const { return m_volume * (float)AMPLITUDE * TAPE_GAIN; }

    void renderSamples(int start, int end);
};
