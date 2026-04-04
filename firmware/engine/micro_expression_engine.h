#pragma once

#include <Arduino.h>

#include "personality_state_machine.h"

namespace Flic {

struct MicroExpressionFrame {
    int8_t eyeJitterX = 0;
    int8_t eyeJitterY = 0;
    int8_t driftX = 0;
    int8_t driftY = 0;
    float pupilScale = 1.0f;
    float glowPulse = 1.0f;
    bool blink = false;
    bool eyelidTwitch = false;
};

class MicroExpressionEngine {
public:
    void reset();
    void setEnabled(bool enabled);
    bool enabled() const;
    void setIntensity(float intensity);
    float intensity() const;
    MicroExpressionFrame update(unsigned long nowMs,
                                float deltaSeconds,
                                const String& emotion,
                                const PersonalityProfile& profile);

private:
    static float clamp01(float value);
    static float clampRange(float value, float minimumValue, float maximumValue);

    bool enabled_ = true;
    float intensity_ = 0.35f;
    unsigned long lastBlinkMs_ = 0;
    unsigned long lastUpdateMs_ = 0;
    float driftPhase_ = 0.0f;
};

}  // namespace Flic