#pragma once

#include <Arduino.h>

namespace Flic {

class AnimationEngine {
public:
    bool begin();
    bool hasRealAnimations() const;
    bool isPlaying() const;
    float playbackSpeed() const;
    void setPlaybackSpeed(float speed);
    bool playFirstAnimation();
    bool playAnimation(const char* fileName);
    bool playPreset(const String& preset);
    bool playEmotionCue(const String& emotion);
    bool playMicroGesture(const String& gesture);
    bool generateFirstAnimationIfNeeded();

private:
    float playbackSpeed_ = 1.0f;
    bool loadFirstAnimationFromDisk(String& filePath);
};

}  // namespace Flic
