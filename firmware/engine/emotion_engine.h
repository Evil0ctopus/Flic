#pragma once

#include <Arduino.h>

namespace Flic {

class LightEngine;
class MemoryManager;

class EmotionEngine {
public:
    bool begin(LightEngine* lightEngine, MemoryManager* memoryManager);
    void setEmotion(const String& emotion);
    void nudgeEmotion(const String& emotion, float strength);
    String getEmotion();
    void updateEmotion(float dt);

private:
    bool loadState();
    void saveState();

    LightEngine* lightEngine_ = nullptr;
    MemoryManager* memoryManager_ = nullptr;
    String currentEmotion_ = "calm";
    String targetEmotion_ = "calm";
    float transition_ = 1.0f;
};

}  // namespace Flic
