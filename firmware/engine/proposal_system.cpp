#include "proposal_system.h"

#include "emotion_engine.h"
#include "memory_manager.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kProposalsPath = "/Flic/memory/proposals.json";
}

bool ProposalSystem::begin(MemoryManager* memoryManager, EmotionEngine* emotionEngine) {
    memoryManager_ = memoryManager;
    emotionEngine_ = emotionEngine;
    loadProposals();
    return true;
}

void ProposalSystem::update(bool animationPlaying) {
    if (animationPlaying || proposedThisBoot_ || emotionEngine_ == nullptr || memoryManager_ == nullptr) {
        return;
    }

    if (memoryManager_->eventCount() >= 3) {
        proposeGrowthIdeas();
        proposedThisBoot_ = true;
    }
}

void ProposalSystem::proposeChange(const String& proposal) {
    if (hasProposal(proposal)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);

    JsonArray proposals = document["proposals"].to<JsonArray>();
    JsonObject entry = proposals.add<JsonObject>();
    entry["proposal"] = proposal;
    entry["type"] = "general";
    entry["requires_approval"] = true;
    entry["approved"] = false;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", proposal);
    }
}

void ProposalSystem::proposeGrowthIdeas() {
    const String emotion = emotionEngine_ != nullptr ? emotionEngine_->getEmotion() : String("calm");
    if (emotion == "happy") {
        proposeChange("Add a brighter celebratory animation for milestone moments.");
    } else if (emotion == "curious") {
        proposeChange("Expand the idle behavior with a more expressive curiosity loop.");
    } else if (emotion == "sleepy") {
        proposeChange("Create a softer low-power breathing animation for quiet states.");
    } else {
        proposeChange("Add a new emotion-linked animation that helps Flic explain its state.");
    }
}

void ProposalSystem::proposeUsbDevice(const String& deviceId, const String& reason) {
    if (hasUsbProposal("usb_device", deviceId, "device", deviceId)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);
    JsonArray proposals = document["proposals"].to<JsonArray>();

    JsonObject entry = proposals.add<JsonObject>();
    entry["type"] = "usb_device";
    entry["device"] = deviceId;
    entry["reason"] = reason;
    entry["approved"] = false;
    entry["requires_approval"] = true;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", deviceId + ":usb_device");
    }
}

void ProposalSystem::proposeUsbCommand(const String& deviceId, const String& command, const String& reason) {
    if (hasUsbProposal("usb_command", deviceId, "command", command)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);
    JsonArray proposals = document["proposals"].to<JsonArray>();

    JsonObject entry = proposals.add<JsonObject>();
    entry["type"] = "usb_command";
    entry["device"] = deviceId;
    entry["command"] = command;
    entry["reason"] = reason;
    entry["approved"] = false;
    entry["sent"] = false;
    entry["requires_approval"] = true;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", deviceId + ":" + command);
    }
}

void ProposalSystem::proposeUsbResponse(const String& deviceId, const String& response, const String& reason) {
    if (hasUsbProposal("usb_response", deviceId, "response", response)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);
    JsonArray proposals = document["proposals"].to<JsonArray>();

    JsonObject entry = proposals.add<JsonObject>();
    entry["type"] = "usb_response";
    entry["device"] = deviceId;
    entry["response"] = response;
    entry["reason"] = reason;
    entry["approved"] = false;
    entry["requires_approval"] = true;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", deviceId + ":" + response);
    }
}

void ProposalSystem::proposeUsbCapability(const String& deviceId, const String& capability, const String& reason) {
    if (hasUsbProposal("usb_capability", deviceId, "capability", capability)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);
    JsonArray proposals = document["proposals"].to<JsonArray>();

    JsonObject entry = proposals.add<JsonObject>();
    entry["type"] = "usb_capability";
    entry["device"] = deviceId;
    entry["capability"] = capability;
    entry["reason"] = reason;
    entry["approved"] = false;
    entry["requires_approval"] = true;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", deviceId + ":capability:" + capability);
    }
}

void ProposalSystem::proposeUsbSensor(const String& deviceId, const String& sensorPattern, const String& reason) {
    if (hasUsbProposal("usb_sensor", deviceId, "sensor", sensorPattern)) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kProposalsPath, document);
    JsonArray proposals = document["proposals"].to<JsonArray>();

    JsonObject entry = proposals.add<JsonObject>();
    entry["type"] = "usb_sensor";
    entry["device"] = deviceId;
    entry["sensor"] = sensorPattern;
    entry["reason"] = reason;
    entry["approved"] = false;
    entry["requires_approval"] = true;
    entry["path_scope"] = "/Flic";

    SdManager::writeJSON(kProposalsPath, document);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("proposal", deviceId + ":sensor:" + sensorPattern);
    }
}

bool ProposalSystem::consumeApprovedUsbCommand(const String& deviceId, String& commandOut) {
    JsonDocument document;
    if (!SdManager::readJSON(kProposalsPath, document)) {
        return false;
    }

    JsonArray proposals = document["proposals"].as<JsonArray>();
    if (proposals.isNull()) {
        return false;
    }

    for (JsonVariant value : proposals) {
        JsonObject entry = value.as<JsonObject>();
        if (isValidUsbCommandProposal(entry, deviceId)) {
            commandOut = entry["command"] | "";
            entry["sent"] = true;
            SdManager::writeJSON(kProposalsPath, document);
            if (memoryManager_ != nullptr) {
                memoryManager_->recordEvent("command_approved", commandOut);
            }
            return true;
        }
    }

    return false;
}

bool ProposalSystem::loadProposals() {
    JsonDocument document;
    if (!SdManager::readJSON(kProposalsPath, document)) {
        return false;
    }
    return true;
}

bool ProposalSystem::hasProposal(const String& proposal) const {
    JsonDocument document;
    if (!SdManager::readJSON(kProposalsPath, document)) {
        return false;
    }

    JsonArray proposals = document["proposals"].as<JsonArray>();
    if (proposals.isNull()) {
        return false;
    }

    for (JsonVariant value : proposals) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["proposal"] | "") == proposal) {
            return true;
        }
    }

    return false;
}

bool ProposalSystem::hasUsbProposal(const String& type, const String& deviceId, const char* valueKey, const String& value) const {
    JsonDocument document;
    if (!SdManager::readJSON(kProposalsPath, document)) {
        return false;
    }

    JsonArray proposals = document["proposals"].as<JsonArray>();
    if (proposals.isNull()) {
        return false;
    }

    for (JsonVariant valueEntry : proposals) {
        JsonObject entry = valueEntry.as<JsonObject>();
        if ((String(entry["type"] | "") == type) && (String(entry["device"] | "") == deviceId) &&
            (String(entry[valueKey] | "") == value)) {
            return true;
        }
    }

    return false;
}

bool ProposalSystem::isSafeUsbCommand(const String& command) const {
    if (command.length() == 0) {
        return false;
    }

    String check = command;
    check.toUpperCase();

    if (check.indexOf("FLASH") >= 0 || check.indexOf("ERASE") >= 0 || check.indexOf("FORMAT") >= 0 ||
        check.indexOf("DELETE") >= 0 || check.indexOf("RM ") >= 0 || check.indexOf("FACTORY_RESET") >= 0) {
        return false;
    }

    return true;
}

bool ProposalSystem::isValidUsbCommandProposal(const JsonObject& entry, const String& deviceId) const {
    const String entryType = entry["type"] | "";
    const String entryDevice = entry["device"] | "";
    const String entryCommand = entry["command"] | "";
    const String pathScope = entry["path_scope"] | "";
    const bool approved = entry["approved"] | false;
    const bool sent = entry["sent"] | false;
    const bool requiresApproval = entry["requires_approval"] | false;

    if (entryType != "usb_command") {
        return false;
    }
    if (entryDevice != deviceId || entryCommand.length() == 0) {
        return false;
    }
    if (!approved || sent) {
        return false;
    }
    if (!requiresApproval) {
        return false;
    }
    if (pathScope != "/Flic") {
        return false;
    }

    return isSafeUsbCommand(entryCommand);
}

}  // namespace Flic
