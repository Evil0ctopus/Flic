#include "emotion_blend_engine.h"

#include <algorithm>

namespace Flic {

void EmotionBlendEngine::setMode(Mode mode) {
    mode_ = mode;
}

EmotionBlendEngine::Mode EmotionBlendEngine::mode() const {
    return mode_;
}

void EmotionBlendEngine::start(const String& fromEmotion, const String& toEmotion, uint32_t durationMs, unsigned long nowMs) {
    fromEmotion_ = fromEmotion;
    toEmotion_ = toEmotion;
    durationMs_ = durationMs == 0 ? 1 : durationMs;
    startMs_ = nowMs;
    active_ = true;
}

void EmotionBlendEngine::stop() {
    active_ = false;
    durationMs_ = 0;
    startMs_ = 0;
}

bool EmotionBlendEngine::isActive() const {
    return active_;
}

bool EmotionBlendEngine::isActive(unsigned long nowMs) const {
    if (!active_) {
        return false;
    }
    if (durationMs_ == 0) {
        return false;
    }
    return (nowMs - startMs_) < durationMs_;
}

EmotionBlendEngine::Snapshot EmotionBlendEngine::snapshot(unsigned long nowMs) const {
    Snapshot snapshot;
    snapshot.fromEmotion = fromEmotion_;
    snapshot.toEmotion = toEmotion_;
    snapshot.mode = mode_;
    snapshot.durationMs = durationMs_;

    if (!active_ || durationMs_ == 0) {
        snapshot.active = false;
        snapshot.elapsedMs = 0;
        snapshot.progress = 1.0f;
        snapshot.weight = 1.0f;
        return snapshot;
    }

    const uint32_t elapsedMs = static_cast<uint32_t>(nowMs - startMs_);
    const float rawProgress = static_cast<float>(elapsedMs) / static_cast<float>(durationMs_);
    const float clamped = clamp01(rawProgress);
    snapshot.active = elapsedMs < durationMs_;
    snapshot.elapsedMs = elapsedMs;
    snapshot.progress = clamped;
    snapshot.weight = smoothStep(clamped);
    return snapshot;
}

String EmotionBlendEngine::selectEmotion(unsigned long nowMs, uint32_t frameIndex) const {
    const Snapshot current = snapshot(nowMs);
    if (!current.active) {
        return current.toEmotion.length() > 0 ? current.toEmotion : current.fromEmotion;
    }

    const float weight = current.weight;
    const uint32_t hash = hashString(current.fromEmotion + "|" + current.toEmotion + "|" + String(frameIndex));
    const float deterministic = static_cast<float>(hash & 0xFFFFu) / 65535.0f;

    if (current.mode == Mode::Dissolve) {
        return deterministic < weight ? current.toEmotion : current.fromEmotion;
    }

    if (current.mode == Mode::Morph) {
        const float morphWeight = clamp01(weight * 0.85f + 0.15f * deterministic);
        return morphWeight < 0.5f ? current.fromEmotion : current.toEmotion;
    }

    return deterministic < weight ? current.toEmotion : current.fromEmotion;
}

float EmotionBlendEngine::clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float EmotionBlendEngine::smoothStep(float value) {
    value = clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

uint32_t EmotionBlendEngine::hashString(const String& value) {
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < static_cast<size_t>(value.length()); ++index) {
        hash ^= static_cast<uint8_t>(value.charAt(index));
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace Flic