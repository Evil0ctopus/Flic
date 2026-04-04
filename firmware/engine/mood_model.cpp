#include "mood_model.h"

#include <ArduinoJson.h>
#include <Preferences.h>

namespace Flic {
namespace {

constexpr float kNeutralLevels[static_cast<uint8_t>(MoodKind::Count)] = {0.45f, 0.11f, 0.11f, 0.11f, 0.11f, 0.11f};

MoodTraits moodTraitsFor(MoodKind mood) {
    MoodTraits traits;
    switch (mood) {
        case MoodKind::Happy:
            traits.blinkRateScale = 0.90f;
            traits.pupilBias = 0.08f;
            traits.microExpressionScale = 1.05f;
            traits.transitionSpeedScale = 0.92f;
            traits.happyWeight = 1.30f;
            traits.curiousWeight = 1.10f;
            traits.stressedWeight = 0.75f;
            traits.tiredWeight = 0.80f;
            traits.boredWeight = 0.75f;
            break;
        case MoodKind::Stressed:
            traits.blinkRateScale = 0.84f;
            traits.pupilBias = -0.06f;
            traits.microExpressionScale = 1.18f;
            traits.transitionSpeedScale = 0.86f;
            traits.happyWeight = 0.75f;
            traits.curiousWeight = 0.90f;
            traits.stressedWeight = 1.35f;
            traits.tiredWeight = 0.85f;
            traits.boredWeight = 0.80f;
            break;
        case MoodKind::Tired:
            traits.blinkRateScale = 1.28f;
            traits.pupilBias = -0.08f;
            traits.microExpressionScale = 0.78f;
            traits.transitionSpeedScale = 1.18f;
            traits.happyWeight = 0.72f;
            traits.curiousWeight = 0.80f;
            traits.stressedWeight = 0.85f;
            traits.tiredWeight = 1.35f;
            traits.boredWeight = 1.05f;
            break;
        case MoodKind::Curious:
            traits.blinkRateScale = 0.95f;
            traits.pupilBias = 0.10f;
            traits.microExpressionScale = 1.04f;
            traits.transitionSpeedScale = 0.90f;
            traits.happyWeight = 1.05f;
            traits.curiousWeight = 1.35f;
            traits.stressedWeight = 0.80f;
            traits.tiredWeight = 0.85f;
            traits.boredWeight = 0.90f;
            break;
        case MoodKind::Bored:
            traits.blinkRateScale = 1.22f;
            traits.pupilBias = -0.05f;
            traits.microExpressionScale = 0.80f;
            traits.transitionSpeedScale = 1.22f;
            traits.happyWeight = 0.70f;
            traits.curiousWeight = 0.90f;
            traits.stressedWeight = 0.90f;
            traits.tiredWeight = 1.10f;
            traits.boredWeight = 1.35f;
            break;
        case MoodKind::Calm:
        default:
            traits.blinkRateScale = 1.00f;
            traits.pupilBias = 0.00f;
            traits.microExpressionScale = 1.00f;
            traits.transitionSpeedScale = 1.00f;
            traits.happyWeight = 1.00f;
            traits.curiousWeight = 1.00f;
            traits.stressedWeight = 0.90f;
            traits.tiredWeight = 0.95f;
            traits.boredWeight = 0.95f;
            break;
    }
    return traits;
}

}  // namespace

bool MoodModel::begin(const String& storagePath) {
    storagePath_ = storagePath;
    preferencesOpen_ = preferences_.begin("flic_mood", false);
    return loadMood();
}

bool MoodModel::saveMood() const {
    if (!preferencesOpen_) {
        return false;
    }

    JsonDocument document;
    document["mood"] = getMood();
    document["decay_rate"] = decayRate_;
    JsonArray levels = document["levels"].to<JsonArray>();
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        levels.add(levels_[i]);
    }

    String payload;
    serializeJson(document, payload);
    return preferences_.putString("state", payload) > 0;
}

bool MoodModel::loadMood() {
    if (!preferencesOpen_) {
        normalizeLevels();
        return false;
    }

    const String payload = preferences_.getString("state", "");
    if (payload.length() == 0) {
        normalizeLevels();
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error || !document.is<JsonObject>()) {
        normalizeLevels();
        return false;
    }

    if (document["decay_rate"].is<float>() || document["decay_rate"].is<double>()) {
        setDecayRate(document["decay_rate"].as<float>());
    }

    bool loadedLevels = false;
    if (document["levels"].is<JsonArray>()) {
        JsonArray arr = document["levels"].as<JsonArray>();
        uint8_t idx = 0;
        for (JsonVariant value : arr) {
            if (idx >= static_cast<uint8_t>(MoodKind::Count)) {
                break;
            }
            levels_[idx++] = clamp01(value.as<float>());
        }
        loadedLevels = idx == static_cast<uint8_t>(MoodKind::Count);
    }

    if (!loadedLevels && document["mood"].is<const char*>()) {
        setMood(String(document["mood"].as<const char*>()));
        return true;
    }

    normalizeLevels();
    return loadedLevels;
}

bool MoodModel::setMood(const String& moodName) {
    const MoodKind mood = moodFromName(moodName);
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        levels_[i] = 0.04f;
    }
    levels_[static_cast<uint8_t>(mood)] = 0.76f;
    levels_[static_cast<uint8_t>(MoodKind::Calm)] += 0.06f;
    normalizeLevels();
    return true;
}

String MoodModel::getMood() const {
    uint8_t bestIndex = 0;
    float best = levels_[0];
    for (uint8_t i = 1; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        if (levels_[i] > best) {
            best = levels_[i];
            bestIndex = i;
        }
    }
    return moodToName(static_cast<MoodKind>(bestIndex));
}

void MoodModel::adjustMood(float delta) {
    if (delta > 0.0f) {
        applyMoodDelta(MoodKind::Happy, delta * 0.60f);
        applyMoodDelta(MoodKind::Curious, delta * 0.40f);
        applyMoodDelta(MoodKind::Stressed, -delta * 0.40f);
        applyMoodDelta(MoodKind::Bored, -delta * 0.20f);
    } else if (delta < 0.0f) {
        const float amount = -delta;
        applyMoodDelta(MoodKind::Stressed, amount * 0.58f);
        applyMoodDelta(MoodKind::Tired, amount * 0.30f);
        applyMoodDelta(MoodKind::Happy, -amount * 0.35f);
        applyMoodDelta(MoodKind::Curious, -amount * 0.20f);
    }
    normalizeLevels();
}

void MoodModel::update(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        return;
    }
    const float step = decayRate_ * deltaSeconds;
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        const float target = kNeutralLevels[i];
        const float current = levels_[i];
        levels_[i] = current + (target - current) * step;
    }
    normalizeLevels();
}

void MoodModel::setDecayRate(float decayRate) {
    if (decayRate < 0.0005f) {
        decayRate_ = 0.0005f;
    } else if (decayRate > 0.10f) {
        decayRate_ = 0.10f;
    } else {
        decayRate_ = decayRate;
    }
}

float MoodModel::decayRate() const {
    return decayRate_;
}

void MoodModel::enableAdaptation(bool enabled) {
    adaptationEnabled_ = enabled;
}

bool MoodModel::adaptationEnabled() const {
    return adaptationEnabled_;
}

void MoodModel::noteUserInteraction() {
    if (!adaptationEnabled_) {
        return;
    }
    applyMoodDelta(MoodKind::Happy, 0.020f);
    applyMoodDelta(MoodKind::Curious, 0.018f);
    applyMoodDelta(MoodKind::Bored, -0.016f);
    normalizeLevels();
}

void MoodModel::noteLongIdle() {
    if (!adaptationEnabled_) {
        return;
    }
    applyMoodDelta(MoodKind::Tired, 0.016f);
    applyMoodDelta(MoodKind::Bored, 0.020f);
    applyMoodDelta(MoodKind::Happy, -0.012f);
    normalizeLevels();
}

void MoodModel::noteRapidEmotionSwitch() {
    if (!adaptationEnabled_) {
        return;
    }
    applyMoodDelta(MoodKind::Stressed, 0.024f);
    applyMoodDelta(MoodKind::Calm, -0.015f);
    normalizeLevels();
}

void MoodModel::noteTaskSuccess() {
    if (!adaptationEnabled_) {
        return;
    }
    applyMoodDelta(MoodKind::Happy, 0.020f);
    applyMoodDelta(MoodKind::Stressed, -0.015f);
    normalizeLevels();
}

void MoodModel::noteError() {
    if (!adaptationEnabled_) {
        return;
    }
    applyMoodDelta(MoodKind::Stressed, 0.030f);
    applyMoodDelta(MoodKind::Happy, -0.018f);
    normalizeLevels();
}

MoodTraits MoodModel::currentTraits() const {
    MoodTraits merged{};
    float weightSum = 0.0f;
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        const float weight = levels_[i];
        const MoodTraits& traits = traitsFor(static_cast<MoodKind>(i));
        merged.blinkRateScale += (traits.blinkRateScale - 1.0f) * weight;
        merged.pupilBias += traits.pupilBias * weight;
        merged.microExpressionScale += (traits.microExpressionScale - 1.0f) * weight;
        merged.transitionSpeedScale += (traits.transitionSpeedScale - 1.0f) * weight;
        merged.happyWeight += (traits.happyWeight - 1.0f) * weight;
        merged.curiousWeight += (traits.curiousWeight - 1.0f) * weight;
        merged.stressedWeight += (traits.stressedWeight - 1.0f) * weight;
        merged.tiredWeight += (traits.tiredWeight - 1.0f) * weight;
        merged.boredWeight += (traits.boredWeight - 1.0f) * weight;
        weightSum += weight;
    }
    if (weightSum <= 0.0f) {
        return traitsFor(MoodKind::Calm);
    }
    if (merged.blinkRateScale < 0.60f) {
        merged.blinkRateScale = 0.60f;
    } else if (merged.blinkRateScale > 1.60f) {
        merged.blinkRateScale = 1.60f;
    }
    if (merged.microExpressionScale < 0.60f) {
        merged.microExpressionScale = 0.60f;
    } else if (merged.microExpressionScale > 1.40f) {
        merged.microExpressionScale = 1.40f;
    }
    if (merged.transitionSpeedScale < 0.75f) {
        merged.transitionSpeedScale = 0.75f;
    } else if (merged.transitionSpeedScale > 1.30f) {
        merged.transitionSpeedScale = 1.30f;
    }
    return merged;
}

float MoodModel::clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

MoodKind MoodModel::moodFromName(const String& moodName) {
    String normalized = moodName;
    normalized.trim();
    normalized.toLowerCase();
    if (normalized == "happy") {
        return MoodKind::Happy;
    }
    if (normalized == "stressed") {
        return MoodKind::Stressed;
    }
    if (normalized == "tired") {
        return MoodKind::Tired;
    }
    if (normalized == "curious") {
        return MoodKind::Curious;
    }
    if (normalized == "bored") {
        return MoodKind::Bored;
    }
    return MoodKind::Calm;
}

String MoodModel::moodToName(MoodKind mood) {
    switch (mood) {
        case MoodKind::Happy:
            return "happy";
        case MoodKind::Stressed:
            return "stressed";
        case MoodKind::Tired:
            return "tired";
        case MoodKind::Curious:
            return "curious";
        case MoodKind::Bored:
            return "bored";
        case MoodKind::Calm:
        default:
            return "calm";
    }
}

const MoodTraits& MoodModel::traitsFor(MoodKind mood) {
    static MoodTraits cache[static_cast<uint8_t>(MoodKind::Count)];
    static bool initialized = false;
    if (!initialized) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
            cache[i] = moodTraitsFor(static_cast<MoodKind>(i));
        }
        initialized = true;
    }
    return cache[static_cast<uint8_t>(mood)];
}

void MoodModel::normalizeLevels() {
    float sum = 0.0f;
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        levels_[i] = clamp01(levels_[i]);
        sum += levels_[i];
    }
    if (sum <= 0.0001f) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
            levels_[i] = kNeutralLevels[i];
        }
        return;
    }
    for (uint8_t i = 0; i < static_cast<uint8_t>(MoodKind::Count); ++i) {
        levels_[i] /= sum;
    }
}

void MoodModel::applyMoodDelta(MoodKind mood, float amount) {
    const uint8_t idx = static_cast<uint8_t>(mood);
    levels_[idx] = clamp01(levels_[idx] + amount);
}

float MoodModel::levelFor(MoodKind mood) const {
    return levels_[static_cast<uint8_t>(mood)];
}

}  // namespace Flic
