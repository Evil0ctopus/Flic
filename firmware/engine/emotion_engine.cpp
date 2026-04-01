#include "emotion_engine.h"

#include "../subsystems/light_engine.h"
#include "memory_manager.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kEmotionStatePath = "/ai/memory/emotion_state.json";
}

bool EmotionEngine::begin(LightEngine* lightEngine, MemoryManager* memoryManager) {
    lightEngine_ = lightEngine;
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
    return true;
}

void EmotionEngine::setEmotion(const String& emotion) {
    if (emotion.length() == 0 || emotion == targetEmotion_) {
        return;
    }

    targetEmotion_ = emotion;
    transition_ = 0.0f;
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("emotion_target", emotion);
    }
    saveState();
}

void EmotionEngine::nudgeEmotion(const String& emotion, float strength) {
    if (emotion.length() == 0 || strength <= 0.0f) {
        return;
    }

    if (strength >= 0.6f) {
        setEmotion(emotion);
        return;
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("emotion_nudge", emotion);
    }

    if (currentEmotion_ == "calm" && emotion == "curious") {
        targetEmotion_ = "curious";
    } else if (currentEmotion_ == "calm" && emotion == "happy") {
        targetEmotion_ = "happy";
    } else if (currentEmotion_ == "happy" && emotion == "curious") {
        targetEmotion_ = "curious";
    } else {
        targetEmotion_ = emotion;
    }

    transition_ = strength;
    saveState();
}

String EmotionEngine::getEmotion() {
    return currentEmotion_;
}

void EmotionEngine::updateEmotion(float dt) {
    if (currentEmotion_ == targetEmotion_) {
        if (lightEngine_ != nullptr) {
            lightEngine_->emotionColor(currentEmotion_);
        }
        return;
    }

    transition_ += dt * 0.35f;
    if (transition_ >= 1.0f) {
        currentEmotion_ = targetEmotion_;
        transition_ = 1.0f;
        if (memoryManager_ != nullptr) {
            memoryManager_->recordEvent("emotion", currentEmotion_);
        }
        saveState();
    }

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(currentEmotion_);
    }
}

bool EmotionEngine::loadState() {
    JsonDocument document;
    if (!SdManager::readJSON(kEmotionStatePath, document)) {
        return false;
    }

    currentEmotion_ = document["current"] | "calm";
    targetEmotion_ = document["target"] | currentEmotion_;
    transition_ = document["blend"] | 1.0f;
    return true;
}

void EmotionEngine::saveState() {
    JsonDocument document;
    document["current"] = currentEmotion_;
    document["target"] = targetEmotion_;
    document["blend"] = transition_;
    SdManager::writeJSON(kEmotionStatePath, document);
}

}  // namespace Flic
