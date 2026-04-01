#pragma once

#include <Arduino.h>

namespace Flic {

class AnimationEngine {
public:
    bool begin();
    bool hasRealAnimations() const;
    bool isPlaying() const;
    bool playFirstAnimation();
    bool playAnimation(const char* fileName);
    bool generateFirstAnimationIfNeeded();

private:
    bool isPlaying_ = false;
    bool loadFirstAnimationFromDisk(String& filePath);
    bool renderAnimationFile(const String& filePath);
};

}  // namespace Flic
