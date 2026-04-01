#include "milestone_engine.h"

#include "memory_manager.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kMilestonesPath = "/ai/memory/milestones.json";
}

bool MilestoneEngine::begin(MemoryManager* memoryManager) {
    memoryManager_ = memoryManager;
    wroteIntro_ = false;
    save();
    return true;
}

void MilestoneEngine::update() {
    if (!wroteIntro_) {
        unlock("system_awake", "boot_complete");
        wroteIntro_ = true;
    }

    if (memoryManager_ == nullptr) {
        return;
    }

    if (memoryManager_->countEventsOfType("touch") >= 1) {
        unlock("first_touch", "interaction_touch");
    }

    if (memoryManager_->countEventsOfType("usb_text") >= 1) {
        unlock("first_usb_message", "interaction_usb");
    }

    if (memoryManager_->countEventsOfType("device_connected") >= 1) {
        unlock("first_device_link", "usb_connect");
    }

    if (memoryManager_->countEventsOfType("command_sent") >= 1) {
        unlock("first_approved_command", "approved_command_sent");
    }

    if (memoryManager_->eventCount() >= 20) {
        unlock("active_day_one", "event_volume");
    }
}

bool MilestoneEngine::unlock(const String& id, const String& reason) {
    if (id.length() == 0 || hasUnlocked(id)) {
        return false;
    }

    JsonDocument document;
    SdManager::readJSON(kMilestonesPath, document);
    JsonArray milestones = document["unlocked"].to<JsonArray>();
    JsonObject entry = milestones.add<JsonObject>();
    entry["id"] = id;
    entry["reason"] = reason;
    entry["timestamp"] = millis();
    SdManager::writeJSON(kMilestonesPath, document);

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("milestone", id);
    }
    return true;
}

void MilestoneEngine::save() {
    JsonDocument document;
    if (!SdManager::readJSON(kMilestonesPath, document)) {
        document["unlocked"] = JsonArray();
        SdManager::writeJSON(kMilestonesPath, document);
    }
}

bool MilestoneEngine::hasUnlocked(const String& id) {
    JsonDocument document;
    if (!SdManager::readJSON(kMilestonesPath, document)) {
        return false;
    }
    JsonArray milestones = document["unlocked"].as<JsonArray>();
    if (milestones.isNull()) {
        return false;
    }
    for (JsonVariant value : milestones) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["id"] | "") == id) {
            return true;
        }
    }
    return false;
}

}  // namespace Flic
