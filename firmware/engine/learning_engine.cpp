#include "learning_engine.h"

#include "device_learning.h"
#include "memory_manager.h"

namespace Flic {

bool LearningEngine::begin(MemoryManager* memoryManager, DeviceLearning* deviceLearning) {
    memoryManager_ = memoryManager;
    deviceLearning_ = deviceLearning;
    return true;
}

void LearningEngine::observeTouch(const String& gesture) {
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_touch", gesture);
    }
}

void LearningEngine::observeVoice(const String& command) {
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_voice", command);
    }
}

void LearningEngine::observeMotion(const String& motionEvent) {
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_motion", motionEvent);
    }
}

void LearningEngine::observeUsb(const String& deviceId, const String& message) {
    if (deviceLearning_ != nullptr) {
        deviceLearning_->processMessage(deviceId, message);
    }
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_usb", deviceId + ":" + message);
    }
}

}  // namespace Flic
