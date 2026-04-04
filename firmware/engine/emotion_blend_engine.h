#pragma once

#include <Arduino.h>

namespace Flic {

class EmotionBlendEngine {
public:
    enum class Mode : uint8_t {
        Crossfade = 0,
        Morph = 1,
        Dissolve = 2,
    };

    struct Snapshot {
        String fromEmotion;
        String toEmotion;
        Mode mode = Mode::Crossfade;
        uint32_t durationMs = 0;
        uint32_t elapsedMs = 0;
        float progress = 0.0f;
        float weight = 0.0f;
        bool active = false;
    };

    void setMode(Mode mode);
    Mode mode() const;
    void start(const String& fromEmotion, const String& toEmotion, uint32_t durationMs, unsigned long nowMs);
    void stop();
    bool isActive() const;
    bool isActive(unsigned long nowMs) const;
    Snapshot snapshot(unsigned long nowMs) const;
    String selectEmotion(unsigned long nowMs, uint32_t frameIndex) const;

private:
    static float clamp01(float value);
    static float smoothStep(float value);
    static uint32_t hashString(const String& value);

    String fromEmotion_;
    String toEmotion_;
    uint32_t durationMs_ = 0;
    unsigned long startMs_ = 0;
    Mode mode_ = Mode::Crossfade;
    bool active_ = false;
};

}  // namespace Flic