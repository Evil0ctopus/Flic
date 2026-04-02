#pragma once

#include <Arduino.h>

namespace Flic {

class MemoryManager;
class DeviceLearning;
class FaceEngine;

class LearningEngine {
public:
    bool begin(MemoryManager* memoryManager, DeviceLearning* deviceLearning, FaceEngine* faceEngine = nullptr);
    void observeTouch(const String& gesture);
    void observeVoice(const String& command);
    void observeMotion(const String& motionEvent);
    void observeUsb(const String& deviceId, const String& message);

private:
    void persistLearningEvent(const String& source, const String& detail, const String& deviceId = String());
    bool shouldPersistVoiceCommand(const String& command);

    MemoryManager* memoryManager_ = nullptr;
    DeviceLearning* deviceLearning_ = nullptr;
    FaceEngine* faceEngine_ = nullptr;
    uint32_t touchCount_ = 0;
    uint32_t voiceCount_ = 0;
    uint32_t motionCount_ = 0;
    uint32_t usbCount_ = 0;
    String lastVoiceCommand_;
    unsigned long lastVoicePersistMs_ = 0;
};

}  // namespace Flic
