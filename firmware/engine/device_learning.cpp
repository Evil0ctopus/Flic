#include "device_learning.h"

#include "memory_manager.h"
#include "proposal_system.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kDevicePatternsPath = "/ai/memory/device_patterns.json";
}

bool DeviceLearning::begin(MemoryManager* memoryManager, ProposalSystem* proposalSystem) {
    memoryManager_ = memoryManager;
    proposalSystem_ = proposalSystem;
    loadPatterns();
    return true;
}

bool DeviceLearning::processMessage(const String& deviceId, const String& message) {
    if (deviceId.length() == 0 || message.length() == 0) {
        return false;
    }

    const String kind = classifyMessage(message);
    recordPattern(deviceId, message, kind);

    lastDeviceId_ = deviceId;
    lastPattern_ = message;
    lastLearningNote_ = "Observed " + kind + " message from " + deviceId;

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("device_pattern", deviceId + ":" + kind);
    }

    const size_t observedCount = patternCountForDevice(deviceId, message, kind);
    if (proposalSystem_ != nullptr && kind == "identity" && observedCount >= 1) {
        proposalSystem_->proposeUsbDevice(deviceId, "Identified device connected over USB-C");
        lastLearningNote_ = "I identified a new device: " + deviceId;
        learnedSomething_ = true;
        savePatterns();
        return true;
    }

    if (proposalSystem_ != nullptr && observedCount >= 2) {
        if (kind == "command") {
            String cmd = message;
            if (message.startsWith("CMD:")) {
                cmd = message.substring(4);
            }
            proposalSystem_->proposeUsbCommand(deviceId, cmd, "Detected repeated usage");
            lastLearningNote_ = "I detected a new command: " + cmd + ". Should I store it?";
            learnedSomething_ = true;
            savePatterns();
            return true;
        } else if (kind == "sensor") {
            proposalSystem_->proposeUsbSensor(deviceId, message, "Detected repeating sensor value");
            lastLearningNote_ = "I detected a repeating sensor value. Should I track it?";
            learnedSomething_ = true;
            savePatterns();
            return true;
        } else if (kind == "capability") {
            const String capabilityList = message.substring(13);
            proposalSystem_->proposeUsbCapability(deviceId, capabilityList, "Device reports capabilities");
            lastLearningNote_ = "This device reports capabilities. Should I create actions?";
            learnedSomething_ = true;
            savePatterns();
            return true;
        } else if (kind == "status") {
            proposalSystem_->proposeUsbResponse(deviceId, message.substring(7), "Detected repeated usage");
            lastLearningNote_ = "I detected a status message. Should I create a reaction?";
            learnedSomething_ = true;
            savePatterns();
            return true;
        } else if (kind == "identity") {
            proposalSystem_->proposeUsbDevice(deviceId, "Identified device connected over USB-C");
            lastLearningNote_ = "I identified a new device: " + deviceId;
            learnedSomething_ = true;
            savePatterns();
            return true;
        }
    }

    learnedSomething_ = (kind != "unknown" && kind != "binary" && observedCount <= 1);
    savePatterns();
    return learnedSomething_;
}

bool DeviceLearning::learnedSomething() const {
    return learnedSomething_;
}

String DeviceLearning::lastLearningNote() const {
    return lastLearningNote_;
}

String DeviceLearning::lastDeviceId() const {
    return lastDeviceId_;
}

String DeviceLearning::lastPattern() const {
    return lastPattern_;
}

void DeviceLearning::clearLearningSignal() {
    learnedSomething_ = false;
    lastLearningNote_ = String();
    lastPattern_ = String();
}

void DeviceLearning::loadPatterns() {
    JsonDocument document;
    if (!SdManager::readJSON(kDevicePatternsPath, document)) {
        return;
    }

    JsonArray devices = document["devices"].as<JsonArray>();
    if (devices.isNull()) {
        return;
    }
}

void DeviceLearning::savePatterns() {
    JsonDocument document;
    SdManager::readJSON(kDevicePatternsPath, document);

    JsonArray devices = document["devices"].to<JsonArray>();
    JsonObject targetDevice;
    for (JsonVariant value : devices) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["device_id"] | "") == lastDeviceId_) {
            targetDevice = entry;
            break;
        }
    }

    if (targetDevice.isNull()) {
        targetDevice = devices.add<JsonObject>();
        targetDevice["device_id"] = lastDeviceId_;
        targetDevice["raw_messages"] = JsonArray();
        targetDevice["commands"] = JsonArray();
        targetDevice["responses"] = JsonArray();
        targetDevice["statuses"] = JsonArray();
    }

    targetDevice["last_pattern"] = lastPattern_;
    targetDevice["note"] = lastLearningNote_;
    targetDevice["timestamp"] = millis();

    SdManager::writeJSON(kDevicePatternsPath, document);
}

void DeviceLearning::recordPattern(const String& deviceId, const String& message, const String& kind) {
    JsonDocument document;
    SdManager::readJSON(kDevicePatternsPath, document);

    JsonArray devices = document["devices"].to<JsonArray>();
    JsonObject device;
    for (JsonVariant value : devices) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["device_id"] | "") == deviceId) {
            device = entry;
            break;
        }
    }

    if (device.isNull()) {
        device = devices.add<JsonObject>();
        device["device_id"] = deviceId;
    }

    JsonArray rawMessages = device["raw_messages"].to<JsonArray>();
    JsonArray commands = device["commands"].to<JsonArray>();
    JsonArray responses = device["responses"].to<JsonArray>();
    JsonArray statuses = device["statuses"].to<JsonArray>();
    JsonArray capabilities = device["capabilities"].to<JsonArray>();
    JsonArray sensors = device["sensors"].to<JsonArray>();
    JsonArray unknownFormats = device["unknown_formats"].to<JsonArray>();

    JsonObject pattern = rawMessages.add<JsonObject>();
    pattern["message"] = message;
    pattern["kind"] = kind;
    pattern["count"] = patternCountForDevice(deviceId, message, kind) + 1;
    pattern["timestamp"] = millis();

    if (kind == "command") {
        JsonObject entry = commands.add<JsonObject>();
        entry["command"] = message.startsWith("CMD:") ? message.substring(4) : message;
        entry["count"] = pattern["count"];
    } else if (kind == "status") {
        JsonObject entry = statuses.add<JsonObject>();
        entry["status"] = message.substring(7);
        entry["count"] = pattern["count"];
    } else if (kind == "sensor") {
        JsonObject entry = sensors.add<JsonObject>();
        entry["sample"] = message;
        entry["count"] = pattern["count"];
    } else if (kind == "capability") {
        JsonObject entry = capabilities.add<JsonObject>();
        entry["list"] = message.substring(13);
        entry["count"] = pattern["count"];
    } else if (kind == "response") {
        JsonObject entry = responses.add<JsonObject>();
        entry["response"] = message;
        entry["count"] = pattern["count"];
    } else if (kind == "unknown" || kind == "binary") {
        JsonObject entry = unknownFormats.add<JsonObject>();
        entry["format"] = message;
        entry["count"] = pattern["count"];
    }

    SdManager::writeJSON(kDevicePatternsPath, document);
}

size_t DeviceLearning::patternCountForDevice(const String& deviceId, const String& pattern, const String& kind) const {
    JsonDocument document;
    if (!SdManager::readJSON(kDevicePatternsPath, document)) {
        return 0;
    }

    JsonArray devices = document["devices"].as<JsonArray>();
    if (devices.isNull()) {
        return 0;
    }

    for (JsonVariant value : devices) {
        JsonObject device = value.as<JsonObject>();
        if (String(device["device_id"] | "") != deviceId) {
            continue;
        }

        JsonArray rawMessages = device["raw_messages"].as<JsonArray>();
        if (rawMessages.isNull()) {
            return 0;
        }

        size_t matches = 0;
        for (JsonVariant rawValue : rawMessages) {
            JsonObject entry = rawValue.as<JsonObject>();
            if ((String(entry["message"] | "") == pattern) && (String(entry["kind"] | "") == kind)) {
                ++matches;
            }
        }
        return matches;
    }

    return 0;
}

String DeviceLearning::classifyMessage(const String& message) const {
    if (message.startsWith("BINARY:") || message.startsWith("BINARY_RAW:")) {
        return "binary";
    }
    if (message.startsWith("DEVICE_ID:")) {
        return "identity";
    }
    if (message.startsWith("CAPABILITIES:") || message.startsWith("CAPS:") || message.startsWith("FEATURES:")) {
        return "capability";
    }
    if (message.startsWith("CMD:")) {
        return "command";
    }
    if (message.startsWith("STATUS:")) {
        return "status";
    }
    if (message == "PING") {
        return "status";
    }
    if (message.startsWith("RESP:")) {
        return "response";
    }
    if (isLikelySensorPattern(message)) {
        return "sensor";
    }
    if (isLikelyCommandToken(message)) {
        return "command";
    }
    if (message.indexOf(':') >= 0) {
        return "response";
    }
    return "unknown";
}

bool DeviceLearning::isLikelyCommandToken(const String& message) const {
    if (message.length() < 3 || message.length() > 48) {
        return false;
    }
    if (message.indexOf(' ') >= 0 || message.indexOf(':') >= 0) {
        return false;
    }
    bool hasUnderscore = false;
    for (size_t i = 0; i < static_cast<size_t>(message.length()); ++i) {
        const char c = message.charAt(i);
        if (c == '_') {
            hasUnderscore = true;
            continue;
        }
        if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9')) {
            return false;
        }
    }
    return hasUnderscore;
}

bool DeviceLearning::isLikelySensorPattern(const String& message) const {
    const int separator = message.indexOf(':');
    if (separator <= 0 || separator >= message.length() - 1) {
        return false;
    }
    const String value = message.substring(separator + 1);
    bool hasDigit = false;
    for (size_t i = 0; i < static_cast<size_t>(value.length()); ++i) {
        const char c = value.charAt(i);
        if (c >= '0' && c <= '9') {
            hasDigit = true;
            continue;
        }
        if (c == '.' || c == '-' || c == '+') {
            continue;
        }
        return false;
    }
    return hasDigit;
}

}  // namespace Flic
