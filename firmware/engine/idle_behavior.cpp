#include "idle_behavior.h"

#include "emotion_engine.h"
#include "../ui/personality_ui.h"
#include "../subsystems/light_engine.h"

#include <Arduino.h>

namespace Flic {

void IdleBehavior::begin(LightEngine* lightEngine, EmotionEngine* emotionEngine, PersonalityUI* personalityUi) {
    lightEngine_ = lightEngine;
    emotionEngine_ = emotionEngine;
    personalityUi_ = personalityUi;
    lastUpdateMs_ = millis();
    level_ = 6;
    delta_ = 1;
}

void IdleBehavior::update(bool animationPlaying) {
    if (personalityUi_ != nullptr) {
        personalityUi_->update(animationPlaying);
    }

    if (lightEngine_ == nullptr || emotionEngine_ == nullptr || animationPlaying) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastUpdateMs_ < 80) {
        return;
    }
    lastUpdateMs_ = now;

    const String emotion = emotionEngine_->getEmotion();
    if (now % 12000UL < 80UL) {
        if (emotion == "calm") {
            emotionEngine_->nudgeEmotion("sleepy", 0.25f);
        } else if (emotion == "sleepy") {
            emotionEngine_->nudgeEmotion("curious", 0.25f);
        }
    }

    lightEngine_->emotionColor(emotion);

    int floorLevel = 6;
    int ceilingLevel = 26;
    int step = 1;
    if (emotion == "sleepy") {
        floorLevel = 3;
        ceilingLevel = 10;
        step = 1;
    } else if (emotion == "curious") {
        floorLevel = 8;
        ceilingLevel = 20;
        step = 2;
    } else if (emotion == "happy") {
        floorLevel = 10;
        ceilingLevel = 24;
        step = 2;
    } else if (emotion == "surprised") {
        floorLevel = 18;
        ceilingLevel = 28;
        step = 1;
    }

    lightEngine_->setBrightness(static_cast<uint8_t>(level_));

    level_ += delta_ * step;
    if (level_ >= ceilingLevel) {
        level_ = ceilingLevel;
        delta_ = -1;
    } else if (level_ <= floorLevel) {
        level_ = floorLevel;
        delta_ = 1;
    }
}

}  // namespace Flic
