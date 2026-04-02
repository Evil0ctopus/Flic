#include "emotion_engine.h"

#include "animation_engine.h"
#include "../diagnostics/webui_event_hook.h"
#include "memory_manager.h"
#include "../subsystems/light_engine.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>
#include <SD.h>

namespace Flic {
namespace {
constexpr const char* kEmotionStatePath = "/ai/memory/emotion_state.json";
constexpr bool kDisableSdPersistence = true;
constexpr float kMinimumStrength = 0.05f;
constexpr float kStrongStrength = 0.7f;
constexpr float kMinimumBias = -1.0f;
constexpr float kMaximumBias = 1.0f;
constexpr float kDefaultTransitionSeconds = 0.8f;
constexpr float kDefaultDecaySeconds = 3.5f;
constexpr float kCalmHoldSeconds = 2.5f;
constexpr float kCuriousHoldSeconds = 2.2f;
constexpr float kHappyHoldSeconds = 3.1f;
constexpr float kSleepyHoldSeconds = 3.6f;
constexpr float kSurprisedHoldSeconds = 1.2f;

String toLowerCopy(String value) {
    value.toLowerCase();
    return value;
}

String stateToString(EmotionState state) {
    switch (state) {
        case EmotionState::Transitioning:
            return "transitioning";
        case EmotionState::Holding:
            return "holding";
        case EmotionState::Decaying:
            return "decaying";
        case EmotionState::Calm:
        default:
            return "calm";
    }
}

EmotionState stateFromString(const String& state) {
    if (state == "transitioning") {
        return EmotionState::Transitioning;
    }
    if (state == "holding") {
        return EmotionState::Holding;
    }
    if (state == "decaying") {
        return EmotionState::Decaying;
    }
    return EmotionState::Calm;
}
}  // namespace

bool EmotionEngine::begin(LightEngine* lightEngine, MemoryManager* memoryManager, AnimationEngine* animationEngine) {
    lightEngine_ = lightEngine;
    animationEngine_ = animationEngine;
    memoryManager_ = memoryManager;

    loadState();
    if (memoryManager_ != nullptr) {
        const size_t milestones = memoryManager_->countEventsOfType("milestone");
        const size_t proposals = memoryManager_->countEventsOfType("proposal");
        if (milestones > proposals && currentEmotion_ == "calm") {
            targetEmotion_ = "happy";
        } else if (proposals > milestones && currentEmotion_ == "calm") {
            targetEmotion_ = "curious";
        }
    }

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(currentEmotion_);
    }

    lastUpdateMs_ = millis();
    lastTriggerMs_ = lastUpdateMs_;
    holdSeconds_ = holdSecondsFor(currentEmotion_, 1.0f);
    decaySeconds_ = decaySecondsFor(currentEmotion_);
    state_ = currentEmotion_ == targetEmotion_ ? EmotionState::Holding : EmotionState::Transitioning;
    return true;
}

void EmotionEngine::setEmotionBias(float bias) {
    if (bias < kMinimumBias) {
        bias = kMinimumBias;
    } else if (bias > kMaximumBias) {
        bias = kMaximumBias;
    }
    emotionBias_ = bias;
}

void EmotionEngine::setEmotion(const String& emotion) {
    requestEmotion(emotion, 1.0f, "set");
}

void EmotionEngine::nudgeEmotion(const String& emotion, float strength) {
    requestEmotion(emotion, strength, "nudge");
}

void EmotionEngine::observeTouch(const String& gesture, const String& meaning) {
    if (meaning == "excitement") {
        requestEmotion("happy", 0.9f, String("touch:") + gesture);
    } else if (meaning == "comfort") {
        requestEmotion("calm", 0.4f, String("touch:") + gesture);
    } else if (meaning == "cancel") {
        requestEmotion("surprised", 1.0f, String("touch:") + gesture);
    } else if (meaning == "continue" || meaning == "attention") {
        requestEmotion("curious", 0.55f, String("touch:") + gesture);
    } else if (meaning == "dismiss") {
        requestEmotion("sleepy", 0.5f, String("touch:") + gesture);
    } else if (meaning == "drag") {
        requestEmotion("curious", 0.4f, String("touch:") + gesture);
    } else {
        requestEmotion("calm", 0.2f, String("touch:") + gesture);
    }
}

void EmotionEngine::observeVoice(const String& text) {
    requestEmotion(inferEmotionFromText(text), 0.6f, "voice");
}

void EmotionEngine::observeMotion(const String& source, const String& event, const String& detail) {
    requestEmotion(inferEmotionFromSensor(source, event, detail), 0.65f, source);
}

void EmotionEngine::observeUsb(const String& message) {
    requestEmotion(inferEmotionFromText(message), 0.45f, "usb");
}

String EmotionEngine::getEmotion() {
    return currentEmotion_;
}

void EmotionEngine::updateEmotion(float dt) {
    if (dt <= 0.0f) {
        return;
    }

    const unsigned long now = millis();
    lastUpdateMs_ = now;

    if (currentEmotion_ != targetEmotion_) {
        state_ = EmotionState::Transitioning;
        const float duration = transitionSecondsFor(targetEmotion_, 1.0f);
        transition_ += dt / duration;
        if (transition_ >= 1.0f) {
            currentEmotion_ = targetEmotion_;
            transition_ = 1.0f;
            state_ = EmotionState::Holding;
            holdSeconds_ = holdSecondsFor(currentEmotion_, 1.0f);
            decaySeconds_ = decaySecondsFor(currentEmotion_);
            onEmotionCommitted();
            saveState();
        }
    } else if (currentEmotion_ == "calm") {
        state_ = EmotionState::Calm;
    } else {
        if (state_ == EmotionState::Transitioning) {
            state_ = EmotionState::Holding;
        }

        if ((now - lastTriggerMs_) >= static_cast<unsigned long>(holdSeconds_ * 1000.0f)) {
            targetEmotion_ = "calm";
            transition_ = 0.0f;
            state_ = EmotionState::Decaying;
            saveState();
        }
    }

    if (state_ == EmotionState::Decaying) {
        const float duration = decaySeconds_ < kDefaultDecaySeconds ? kDefaultDecaySeconds : decaySeconds_;
        transition_ += dt / duration;
        if (transition_ >= 1.0f) {
            currentEmotion_ = "calm";
            targetEmotion_ = "calm";
            transition_ = 1.0f;
            state_ = EmotionState::Calm;
            holdSeconds_ = holdSecondsFor(currentEmotion_, 1.0f);
            decaySeconds_ = decaySecondsFor(currentEmotion_);
            onEmotionCommitted();
            saveState();
        }
    }

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(currentEmotion_);
        lightEngine_->setEmotionBrightness(brightnessForEmotion(currentEmotion_));
    }
}

float EmotionEngine::applyEmotionBias(const String& emotion, float strength) const {
    const float clampedStrength = strength < kMinimumStrength ? kMinimumStrength : (strength > 1.0f ? 1.0f : strength);
    const float positiveBias = emotionBias_ > 0.0f ? emotionBias_ : 0.0f;
    const float negativeBias = emotionBias_ < 0.0f ? -emotionBias_ : 0.0f;

    if (emotion == "happy" || emotion == "curious") {
        return clampedStrength * (1.0f + positiveBias * 0.4f);
    }
    if (emotion == "sleepy" || emotion == "calm") {
        return clampedStrength * (1.0f + negativeBias * 0.35f);
    }
    if (emotion == "surprised") {
        return clampedStrength * (1.0f + (positiveBias + negativeBias) * 0.1f);
    }
    return clampedStrength;
}

String EmotionEngine::normalizeEmotion(const String& emotion) const {
    String normalized = toLowerCopy(emotion);
    if (normalized.length() == 0) {
        return "calm";
    }
    if (normalized == "warning" || normalized == "surprise") {
        return "surprised";
    }
    if (normalized == "excited") {
        return "happy";
    }
    return normalized;
}

String EmotionEngine::inferEmotionFromText(const String& text) const {
    const String lower = toLowerCopy(text);
    if (lower.indexOf("error") >= 0 || lower.indexOf("fail") >= 0 || lower.indexOf("warn") >= 0 || lower.indexOf("stop") >= 0) {
        return "surprised";
    }
    if (lower.indexOf("ok") >= 0 || lower.indexOf("done") >= 0 || lower.indexOf("success") >= 0 || lower.indexOf("thanks") >= 0 || lower.indexOf("great") >= 0) {
        return "happy";
    }
    if (lower.indexOf("quiet") >= 0 || lower.indexOf("sleep") >= 0 || lower.indexOf("idle") >= 0 || lower.indexOf("rest") >= 0) {
        return "sleepy";
    }
    if (lower.indexOf("motion") >= 0 || lower.indexOf("sensor") >= 0 || lower.indexOf("voice") >= 0 || lower.indexOf("look") >= 0 || lower.indexOf("what") >= 0) {
        return "curious";
    }
    return "calm";
}

String EmotionEngine::inferEmotionFromSensor(const String& source, const String& event, const String& detail) const {
    const String sourceLower = toLowerCopy(source);
    const String eventLower = toLowerCopy(event);
    const String detailLower = toLowerCopy(detail);

    if (sourceLower == "camera" && eventLower == "motion") {
        if (detailLower.indexOf("fast") >= 0 || detailLower.indexOf("close") >= 0 || detailLower.indexOf("sudden") >= 0) {
            return "surprised";
        }
        return "curious";
    }
    if (sourceLower == "imu" && (eventLower == "shake" || eventLower == "impact")) {
        return "surprised";
    }
    if (sourceLower == "imu" && (eventLower == "pickup" || eventLower == "wake")) {
        return "happy";
    }
    if (sourceLower == "imu" && (eventLower == "stillness" || eventLower == "idle")) {
        return "sleepy";
    }
    if (sourceLower == "audio" && detailLower.indexOf("loud") >= 0) {
        return "surprised";
    }
    if (sourceLower == "audio" && (detailLower.indexOf("quiet") >= 0 || detailLower.indexOf("soft") >= 0)) {
        return "sleepy";
    }
    if (sourceLower == "light" && eventLower == "hand_wave") {
        return "curious";
    }
    if (sourceLower == "light" && (eventLower == "brighten" || eventLower == "flash")) {
        return "happy";
    }
    if (sourceLower == "touch" && eventLower == "drag") {
        return "curious";
    }
    return inferEmotionFromText(event + " " + detail);
}

float EmotionEngine::transitionSecondsFor(const String& emotion, float strength) const {
    const float clampedStrength = strength < kMinimumStrength ? kMinimumStrength : (strength > 1.0f ? 1.0f : strength);
    if (emotion == "surprised") {
        return 0.25f + (1.0f - clampedStrength) * 0.25f;
    }
    if (emotion == "happy") {
        return 0.45f + (1.0f - clampedStrength) * 0.35f;
    }
    if (emotion == "curious") {
        return 0.55f + (1.0f - clampedStrength) * 0.3f;
    }
    if (emotion == "sleepy") {
        return 0.9f + (1.0f - clampedStrength) * 0.35f;
    }
    return 0.7f + (1.0f - clampedStrength) * 0.35f;
}

float EmotionEngine::holdSecondsFor(const String& emotion, float strength) const {
    const float clampedStrength = strength < kMinimumStrength ? kMinimumStrength : (strength > 1.0f ? 1.0f : strength);
    if (emotion == "happy") {
        return kHappyHoldSeconds + clampedStrength * 1.4f;
    }
    if (emotion == "curious") {
        return kCuriousHoldSeconds + clampedStrength * 1.0f;
    }
    if (emotion == "sleepy") {
        return kSleepyHoldSeconds + clampedStrength * 0.8f;
    }
    if (emotion == "surprised") {
        return kSurprisedHoldSeconds + clampedStrength * 0.2f;
    }
    return kCalmHoldSeconds;
}

float EmotionEngine::decaySecondsFor(const String& emotion) const {
    if (emotion == "surprised") {
        return 1.2f;
    }
    if (emotion == "happy") {
        return 3.0f;
    }
    if (emotion == "curious") {
        return 2.0f;
    }
    if (emotion == "sleepy") {
        return 3.4f;
    }
    return kDefaultDecaySeconds;
}

uint8_t EmotionEngine::brightnessForEmotion(const String& emotion) const {
    if (emotion == "surprised") {
        return 28;
    }
    if (emotion == "happy") {
        return 24;
    }
    if (emotion == "curious") {
        return 20;
    }
    if (emotion == "sleepy") {
        return 8;
    }
    return 16;
}

void EmotionEngine::requestEmotion(const String& emotion, float strength, const String& reason) {
    const String normalized = normalizeEmotion(emotion);
    if (normalized.length() == 0) {
        return;
    }

    const float clampedStrength = applyEmotionBias(normalized, strength);
    lastTriggerMs_ = millis();

    if (normalized == currentEmotion_ && normalized == targetEmotion_) {
        holdSeconds_ = holdSecondsFor(normalized, clampedStrength);
        state_ = EmotionState::Holding;
        saveState();
        return;
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("emotion_trigger", reason + ":" + normalized);
    }

    if (clampedStrength >= kStrongStrength) {
        currentEmotion_ = normalized;
        targetEmotion_ = normalized;
        transition_ = 1.0f;
        state_ = EmotionState::Holding;
        holdSeconds_ = holdSecondsFor(normalized, clampedStrength);
        decaySeconds_ = decaySecondsFor(normalized);
        onEmotionCommitted();
        saveState();
        return;
    }

    targetEmotion_ = normalized;
    transition_ = 0.0f;
    state_ = EmotionState::Transitioning;
    holdSeconds_ = holdSecondsFor(normalized, clampedStrength);
    decaySeconds_ = decaySecondsFor(normalized);

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("emotion_target", normalized);
    }
    saveState();
}

void EmotionEngine::onEmotionCommitted() {
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("emotion", currentEmotion_);
    }

    WebUiEventHook::emit("emotion", String("{\"current\":\"") + currentEmotion_ + "\",\"target\":\"" + targetEmotion_ +
                                        "\",\"state\":\"" + stateToString(state_) + "\"}");

    if (lightEngine_ != nullptr) {
        if (currentEmotion_ == "surprised") {
            lightEngine_->flash(255, 255, 255, 1);
        } else if (currentEmotion_ == "happy") {
            lightEngine_->flash(0, 255, 80, 1);
        } else if (currentEmotion_ == "curious") {
            lightEngine_->pulse(0, 80, 255, 14);
        } else if (currentEmotion_ == "sleepy") {
            lightEngine_->pulse(80, 0, 80, 24);
        } else {
            lightEngine_->setEmotionBrightness(brightnessForEmotion(currentEmotion_));
        }
    }

    playEmotionAnimation(currentEmotion_);
}

void EmotionEngine::playEmotionAnimation(const String& emotion) {
    if (animationEngine_ == nullptr) {
        return;
    }

    animationEngine_->playEmotionCue(emotion);
}

bool EmotionEngine::loadState() {
    if (kDisableSdPersistence) {
        return false;
    }

    JsonDocument document;
    if (!SdManager::readJSON(kEmotionStatePath, document)) {
        return false;
    }

    currentEmotion_ = normalizeEmotion(document["current"] | "calm");
    targetEmotion_ = normalizeEmotion(document["target"] | currentEmotion_);
    transition_ = document["blend"] | 1.0f;
    if (transition_ < 0.0f) {
        transition_ = 0.0f;
    } else if (transition_ > 1.0f) {
        transition_ = 1.0f;
    }

    const String savedState = document["state"] | String();
    state_ = savedState.length() > 0 ? stateFromString(savedState) : (currentEmotion_ == targetEmotion_ ? EmotionState::Holding : EmotionState::Transitioning);
    holdSeconds_ = document["hold"] | holdSecondsFor(currentEmotion_, 1.0f);
    decaySeconds_ = document["decay"] | decaySecondsFor(currentEmotion_);
    lastTriggerMs_ = document["last_trigger"] | 0UL;
    return true;
}

void EmotionEngine::saveState() {
    if (kDisableSdPersistence) {
        return;
    }

    JsonDocument document;
    document["current"] = currentEmotion_;
    document["target"] = targetEmotion_;
    document["blend"] = transition_;
    document["state"] = stateToString(state_);
    document["hold"] = holdSeconds_;
    document["decay"] = decaySeconds_;
    document["last_trigger"] = lastTriggerMs_;
    SdManager::writeJSON(kEmotionStatePath, document);
}

}  // namespace Flic
