#include "micro_expression_engine.h"

#include <cmath>

namespace Flic {

namespace {
unsigned long blinkPeriodFor(const String& emotion, const PersonalityProfile& profile, float intensity) {
    float period = 2900.0f * profile.blinkRateScale;
    if (emotion == "sleepy") {
        period = 4200.0f;
    } else if (emotion == "curious") {
        period = 2200.0f;
    } else if (emotion == "happy" || emotion == "excited") {
        period = 2400.0f;
    } else if (emotion == "surprised" || emotion == "confused") {
        period = 1800.0f;
    }

    period *= (1.0f - (intensity * 0.28f));
    if (period < 650.0f) {
        period = 650.0f;
    }
    return static_cast<unsigned long>(period);
}
}  // namespace

void MicroExpressionEngine::reset() {
    lastBlinkMs_ = 0;
    lastUpdateMs_ = 0;
    driftPhase_ = 0.0f;
}

void MicroExpressionEngine::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        reset();
    }
}

bool MicroExpressionEngine::enabled() const {
    return enabled_;
}

void MicroExpressionEngine::setIntensity(float intensity) {
    intensity_ = clamp01(intensity);
}

float MicroExpressionEngine::intensity() const {
    return intensity_;
}

MicroExpressionFrame MicroExpressionEngine::update(unsigned long nowMs,
                                                    float deltaSeconds,
                                                    const String& emotion,
                                                    const PersonalityProfile& profile) {
    MicroExpressionFrame frame;
    if (!enabled_) {
        return frame;
    }

    if (deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }

    if (lastUpdateMs_ == 0) {
        lastUpdateMs_ = nowMs;
        lastBlinkMs_ = nowMs;
    }

    driftPhase_ += deltaSeconds * (1.7f + intensity_ * 1.3f);
    if (driftPhase_ > 1000.0f) {
        driftPhase_ -= 1000.0f;
    }

    const float stateIntensity = clamp01(intensity_ * profile.microExpressionScale);
    const float emotionFactor = emotion == "sleepy" ? 0.65f
                                 : (emotion == "surprised" ? 0.85f
                                                            : (emotion == "happy" ? 0.90f
                                                                                   : (emotion == "curious" ? 1.0f : 0.75f)));
    const float effectiveIntensity = clamp01(stateIntensity * emotionFactor);
    const unsigned long blinkPeriod = blinkPeriodFor(emotion, profile, effectiveIntensity);
    const unsigned long blinkElapsed = nowMs - lastBlinkMs_;
    const unsigned long blinkHold = static_cast<unsigned long>(90.0f + (1.0f - effectiveIntensity) * 120.0f);

    frame.blink = blinkElapsed >= blinkPeriod && blinkElapsed < (blinkPeriod + blinkHold);
    if (blinkElapsed >= (blinkPeriod + blinkHold)) {
        lastBlinkMs_ = nowMs;
    }

    const float jitterPhase = driftPhase_ + static_cast<float>((nowMs % 997U)) * 0.0062f;
    const float jitterScale = effectiveIntensity * (profile.pupilScale * 0.55f + 0.45f);
    frame.eyeJitterX = static_cast<int8_t>(std::round(std::sin(jitterPhase * 1.9f) * jitterScale * 2.0f));
    frame.eyeJitterY = static_cast<int8_t>(std::round(std::cos(jitterPhase * 1.3f) * jitterScale * 1.4f));
    frame.driftX = static_cast<int8_t>(std::round(std::sin(jitterPhase * 0.5f) * (emotion == "focused" ? 0.5f : 1.0f) * jitterScale));
    frame.driftY = static_cast<int8_t>(std::round(std::cos(jitterPhase * 0.37f) * (emotion == "tired" ? 1.4f : 0.8f) * jitterScale));
    frame.pupilScale = clampRange(profile.pupilScale * (1.0f + (effectiveIntensity * 0.16f)), 0.72f, 1.28f);
    frame.glowPulse = clampRange(0.90f + 0.18f * std::sin(jitterPhase * 0.9f) * (0.35f + effectiveIntensity), 0.70f, 1.15f);
    frame.eyelidTwitch = effectiveIntensity > 0.55f && ((nowMs / 197U) % 11U == 0U);
    lastUpdateMs_ = nowMs;
    return frame;
}

float MicroExpressionEngine::clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float MicroExpressionEngine::clampRange(float value, float minimumValue, float maximumValue) {
    if (value < minimumValue) {
        return minimumValue;
    }
    if (value > maximumValue) {
        return maximumValue;
    }
    return value;
}

}  // namespace Flic