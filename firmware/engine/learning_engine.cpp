#include "learning_engine.h"

#include "device_learning.h"
#include "face_engine.h"
#include "memory_manager.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace {
constexpr const char* kMemoryIndexPath = "/Flic/memory/memory_index.json";
constexpr size_t kMaxLearningEvents = 48;
constexpr unsigned long kVoicePersistCooldownMs = 1200;
constexpr bool kDisableSdPersistence = true;
}

namespace Flic {

bool LearningEngine::begin(MemoryManager* memoryManager, DeviceLearning* deviceLearning, FaceEngine* faceEngine) {
    memoryManager_ = memoryManager;
    deviceLearning_ = deviceLearning;
    faceEngine_ = faceEngine;

    if (kDisableSdPersistence) {
        return true;
    }

    JsonDocument document;
    if (SdManager::readJSON(kMemoryIndexPath, document)) {
        JsonObject counters = document["learning_counters"].as<JsonObject>();
        if (!counters.isNull()) {
            touchCount_ = counters["touch"] | 0;
            voiceCount_ = counters["voice"] | 0;
            motionCount_ = counters["motion"] | 0;
            usbCount_ = counters["usb"] | 0;
        }
    }

    return true;
}

void LearningEngine::observeTouch(const String& gesture) {
    ++touchCount_;
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion("curious");
    }
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_touch", gesture);
    }
    persistLearningEvent("touch", gesture);
}

void LearningEngine::observeVoice(const String& command) {
    if (!shouldPersistVoiceCommand(command)) {
        return;
    }

    ++voiceCount_;
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion("curious");
    }
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_voice", command);
    }
    persistLearningEvent("voice", command);
}

bool LearningEngine::shouldPersistVoiceCommand(const String& command) {
    String normalized = command;
    normalized.trim();
    normalized.toLowerCase();

    if (normalized.length() == 0) {
        return false;
    }

    if (normalized == "wake" || normalized == "listen") {
        return false;
    }

    const unsigned long now = millis();
    if (normalized == lastVoiceCommand_ && (now - lastVoicePersistMs_) < kVoicePersistCooldownMs) {
        return false;
    }

    lastVoiceCommand_ = normalized;
    lastVoicePersistMs_ = now;
    return true;
}

void LearningEngine::observeMotion(const String& motionEvent) {
    ++motionCount_;
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion("curious");
    }
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_motion", motionEvent);
    }
    persistLearningEvent("motion", motionEvent);
}

void LearningEngine::observeUsb(const String& deviceId, const String& message) {
    ++usbCount_;
    if (deviceLearning_ != nullptr) {
        deviceLearning_->processMessage(deviceId, message);
    }
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion("curious");
    }
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("learn_usb", deviceId + ":" + message);
    }
    persistLearningEvent("usb", message, deviceId);
}

void LearningEngine::persistLearningEvent(const String& source, const String& detail, const String& deviceId) {
    if (kDisableSdPersistence) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kMemoryIndexPath, document);
    document["_schema"] = "flic.memory_index.v1";
    document["_updated_at"] = millis();

    JsonArray learningEvents = document["learning_events"].to<JsonArray>();
    JsonObject entry = learningEvents.add<JsonObject>();
    entry["source"] = source;
    entry["detail"] = detail;
    if (deviceId.length() > 0) {
        entry["device_id"] = deviceId;
    }
    entry["timestamp"] = millis();

    while (learningEvents.size() > kMaxLearningEvents) {
        learningEvents.remove(0);
    }

    JsonObject counters = document["learning_counters"].to<JsonObject>();
    counters["touch"] = touchCount_;
    counters["voice"] = voiceCount_;
    counters["motion"] = motionCount_;
    counters["usb"] = usbCount_;
    counters["total"] = touchCount_ + voiceCount_ + motionCount_ + usbCount_;

    if (source == "usb") {
        JsonArray usbObservations = document["usb_observations"].to<JsonArray>();
        JsonObject usbEntry = usbObservations.add<JsonObject>();
        usbEntry["device_id"] = deviceId;
        usbEntry["message"] = detail;
        usbEntry["timestamp"] = millis();
        while (usbObservations.size() > kMaxLearningEvents) {
            usbObservations.remove(0);
        }
    }

    SdManager::writeJSON(kMemoryIndexPath, document);
}

}  // namespace Flic
