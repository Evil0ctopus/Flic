#include "adaptive_expression_engine.h"

namespace Flic {

void AdaptiveExpressionEngine::reset() {
    current_ = AdaptiveExpressionModifiers{};
}

AdaptiveExpressionModifiers AdaptiveExpressionEngine::update(const String& mood,
                                                             const MoodTraits& moodTraits,
                                                             const PersonalityMemory& memory,
                                                             float dtSeconds) {
    AdaptiveExpressionModifiers target;
    target.blinkRateScale = moodTraits.blinkRateScale;
    target.pupilScale = 1.0f + moodTraits.pupilBias;
    target.microExpressionScale = moodTraits.microExpressionScale;
    target.transitionSpeedScale = moodTraits.transitionSpeedScale;
    target.glowScale = 1.0f;
    target.jitterScale = 1.0f;
    target.yOffsetBias = 0;

    const float volatility = memory.transitionVolatility();
    target.jitterScale += volatility * 0.20f;

    if (mood == "bored") {
        target.blinkRateScale *= 1.12f;
        target.transitionSpeedScale *= 1.15f;
        target.microExpressionScale *= 0.86f;
    } else if (mood == "curious") {
        target.transitionSpeedScale *= 0.90f;
        target.jitterScale *= 1.18f;
        target.pupilScale *= 1.06f;
    } else if (mood == "tired") {
        target.blinkRateScale *= 1.18f;
        target.microExpressionScale *= 0.82f;
        target.transitionSpeedScale *= 1.12f;
        target.yOffsetBias = 1;
    } else if (mood == "stressed") {
        target.blinkRateScale *= 0.86f;
        target.microExpressionScale *= 1.16f;
        target.transitionSpeedScale *= 0.88f;
        target.jitterScale *= 1.10f;
    } else if (mood == "happy") {
        target.glowScale *= 1.10f;
        target.transitionSpeedScale *= 0.94f;
        target.microExpressionScale *= 1.04f;
    }

    target.blinkRateScale = clamp(target.blinkRateScale, 0.70f, 1.55f);
    target.pupilScale = clamp(target.pupilScale, 0.75f, 1.30f);
    target.microExpressionScale = clamp(target.microExpressionScale, 0.60f, 1.40f);
    target.transitionSpeedScale = clamp(target.transitionSpeedScale, 0.75f, 1.35f);
    target.glowScale = clamp(target.glowScale, 0.75f, 1.25f);
    target.jitterScale = clamp(target.jitterScale, 0.70f, 1.40f);

    const float alpha = clamp(dtSeconds * 1.4f, 0.02f, 0.10f);
    current_.blinkRateScale += (target.blinkRateScale - current_.blinkRateScale) * alpha;
    current_.pupilScale += (target.pupilScale - current_.pupilScale) * alpha;
    current_.microExpressionScale += (target.microExpressionScale - current_.microExpressionScale) * alpha;
    current_.transitionSpeedScale += (target.transitionSpeedScale - current_.transitionSpeedScale) * alpha;
    current_.glowScale += (target.glowScale - current_.glowScale) * alpha;
    current_.jitterScale += (target.jitterScale - current_.jitterScale) * alpha;
    current_.yOffsetBias = target.yOffsetBias;
    return current_;
}

float AdaptiveExpressionEngine::clamp(float value, float minimumValue, float maximumValue) {
    if (value < minimumValue) {
        return minimumValue;
    }
    if (value > maximumValue) {
        return maximumValue;
    }
    return value;
}

}  // namespace Flic
