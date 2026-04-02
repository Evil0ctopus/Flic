#pragma once

#include <Arduino.h>

namespace Flic {

class LightEngine;
class AnimationEngine;
class MemoryManager;

enum class EmotionState : uint8_t {
    Calm = 0,
    Transitioning = 1,
    Holding = 2,
    Decaying = 3,
};

class EmotionEngine {
public:
    bool begin(LightEngine* lightEngine, MemoryManager* memoryManager, AnimationEngine* animationEngine = nullptr);
    void setEmotionBias(float bias);
    void setEmotion(const String& emotion);
    void nudgeEmotion(const String& emotion, float strength);
    void observeTouch(const String& gesture, const String& meaning);
    void observeVoice(const String& text);
    void observeMotion(const String& source, const String& event, const String& detail);
    void observeUsb(const String& message);
    String getEmotion();
    void updateEmotion(float dt);

private:
    String normalizeEmotion(const String& emotion) const;
    String inferEmotionFromText(const String& text) const;
    String inferEmotionFromSensor(const String& source, const String& event, const String& detail) const;
    float transitionSecondsFor(const String& emotion, float strength) const;
    float holdSecondsFor(const String& emotion, float strength) const;
    float decaySecondsFor(const String& emotion) const;
    uint8_t brightnessForEmotion(const String& emotion) const;
    float applyEmotionBias(const String& emotion, float strength) const;
    void requestEmotion(const String& emotion, float strength, const String& reason);
    void onEmotionCommitted();
    void playEmotionAnimation(const String& emotion);
    bool loadState();
    void saveState();

    LightEngine* lightEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    MemoryManager* memoryManager_ = nullptr;
    String currentEmotion_ = "calm";
    String targetEmotion_ = "calm";
    float emotionBias_ = 0.0f;
    float transition_ = 1.0f;
    float holdSeconds_ = 0.0f;
    float decaySeconds_ = 0.0f;
    unsigned long lastUpdateMs_ = 0;
    unsigned long lastTriggerMs_ = 0;
    EmotionState state_ = EmotionState::Calm;
};

}  // namespace Flic
