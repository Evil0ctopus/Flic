#pragma once

#include <Arduino.h>

namespace Flic {

class MemoryManager;
class DeviceLearning;

class LearningEngine {
public:
    bool begin(MemoryManager* memoryManager, DeviceLearning* deviceLearning);
    void observeTouch(const String& gesture);
    void observeVoice(const String& command);
    void observeMotion(const String& motionEvent);
    void observeUsb(const String& deviceId, const String& message);

private:
    MemoryManager* memoryManager_ = nullptr;
    DeviceLearning* deviceLearning_ = nullptr;
};

}  // namespace Flic
