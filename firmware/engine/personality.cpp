#include "personality.h"

namespace Flic {

namespace {
String normalizeEmotion(String emotion) {
    emotion.trim();
    emotion.toLowerCase();
    if (emotion.length() == 0) {
        return "neutral";
    }
    return emotion;
}
}

void StitchPersonalityLayer::begin() {
    enabled_ = true;
    lastUpdateMs_ = millis();
    utteranceCount_ = 0;
}

void StitchPersonalityLayer::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool StitchPersonalityLayer::enabled() const {
    return enabled_;
}

void StitchPersonalityLayer::update(unsigned long nowMs) {
    lastUpdateMs_ = nowMs;
}

PersonalitySpeechResult StitchPersonalityLayer::transformSpeech(const String& message, const String& emotion) {
    PersonalitySpeechResult result;
    result.text = message;
    result.speedScale = 1.10f;
    result.pitchScale = 1.10f;

    if (!enabled_) {
        return result;
    }

    String transformed = message;
    transformed.trim();
    if (transformed.length() == 0) {
        result.text = transformed;
        return result;
    }

    const String normalizedEmotion = normalizeEmotion(emotion);
    ++utteranceCount_;

    // Preserve natural cadence by default; only apply subtle punctuation shaping.
    if (normalizedEmotion == "surprised" && !transformed.endsWith("!")) {
        transformed += "!";
    } else if (normalizedEmotion == "sleepy" && !transformed.endsWith("...")) {
        transformed += "...";
    }

    // Keep delivery lively and creature-like with a slight lift.
    result.speedScale = 1.10f;
    result.pitchScale = 1.10f;
    result.text = transformed;
    return result;
}

}  // namespace Flic
