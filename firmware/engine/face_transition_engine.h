#pragma once

#include <Arduino.h>

namespace Flic {

class FaceTransitionEngine {
public:
    enum class Mode : uint8_t {
        DirectCut = 0,
        Crossfade = 1,
        Morph = 2,
        Dissolve = 3,
        FadeToBlack = 4,
    };

    void setMode(Mode mode);
    Mode mode() const;
    bool setModeFromString(const String& modeName);
    String modeName() const;

    void setFrameRate(float fps);
    float frameRate() const;
    uint16_t frameDelayMs() const;

private:
    Mode mode_ = Mode::DirectCut;
    float frameRate_ = 60.0f;
};

}  // namespace Flic
