#pragma once

#include <Arduino.h>

#include "mood_model.h"
#include "personality_memory.h"

namespace Flic {

struct AdaptiveExpressionModifiers {
    float blinkRateScale = 1.0f;
    float pupilScale = 1.0f;
    float microExpressionScale = 1.0f;
    float transitionSpeedScale = 1.0f;
    float glowScale = 1.0f;
    float jitterScale = 1.0f;
    int8_t yOffsetBias = 0;
};

class AdaptiveExpressionEngine {
public:
    void reset();
    AdaptiveExpressionModifiers update(const String& mood,
                                       const MoodTraits& moodTraits,
                                       const PersonalityMemory& memory,
                                       float dtSeconds);

private:
    static float clamp(float value, float minimumValue, float maximumValue);

    AdaptiveExpressionModifiers current_;
};

}  // namespace Flic
