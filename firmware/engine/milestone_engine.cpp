#include "milestone_engine.h"

#include "animation_engine.h"
#include "communication_engine.h"
#include "emotion_engine.h"
#include "memory_manager.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kMilestonesPath = "/Flic/memory/milestones.json";
constexpr const char* kLegacyMilestonesPath = "/Flic/memory/milestone_state.json";
constexpr uint16_t kLevelStep = 3;
}

bool MilestoneEngine::begin(MemoryManager* memoryManager,
                           AnimationEngine* animationEngine,
                           EmotionEngine* emotionEngine,
                           CommunicationEngine* communicationEngine) {
    memoryManager_ = memoryManager;
    animationEngine_ = animationEngine;
    emotionEngine_ = emotionEngine;
    communicationEngine_ = communicationEngine;
    wroteIntro_ = false;
    unlockedCache_.clear();
    ensureStateFiles();

    JsonDocument cached;
    if (SdManager::readJSON(kMilestonesPath, cached) && cached.is<JsonObject>()) {
        cacheUnlockedFromDocument(cached);
    }

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

    if (memoryManager_->countEventsOfType("learn_voice") >= 1) {
        unlock("first_voice_exchange", "interaction_voice");
    }

    if (memoryManager_->countEventsOfType("new_pattern") >= 1) {
        unlock("pattern_detected", "learning_pattern");
    }

    if (memoryManager_->countEventsOfType("command_sent") >= 3) {
        unlock("trusted_command_channel", "approved_command_streak");
    }

    if (memoryManager_->eventCount() >= 40) {
        unlock("expressive_growth", "event_volume_plus");
    }
}

bool MilestoneEngine::unlock(const String& id, const String& reason) {
    if (id.length() == 0 || hasUnlocked(id)) {
        return false;
    }

    unlockedCache_.push_back(id);

    JsonDocument document;
    SdManager::readJSON(kMilestonesPath, document);
    if (!document.is<JsonObject>()) {
        document.clear();
        document.to<JsonObject>();
    }
    JsonArray milestones = document["unlocked"].to<JsonArray>();
    JsonObject entry = milestones.add<JsonObject>();
    entry["id"] = id;
    entry["reason"] = reason;
    entry["timestamp"] = millis();
    document["_schema"] = "flic.milestones.v1";
    document["_updated_at"] = millis();

    applyUnlockEffects(document, id, reason);
    refreshProgression(document);

    // Keep backward compatibility with legacy milestone_state.json.
    mirrorLegacyState(milestones);

    SdManager::writeJSON(kMilestonesPath, document);

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("milestone", id);
    }
    return true;
}

void MilestoneEngine::save() {
    JsonDocument document;
    if (!SdManager::readJSON(kMilestonesPath, document)) {
        JsonArray unlocked = document["unlocked"].to<JsonArray>();
        (void)unlocked;
    } else if (!document.is<JsonObject>()) {
        document.clear();
        document.to<JsonObject>();
    }

    document["_schema"] = "flic.milestones.v1";
    document["_updated_at"] = millis();
    refreshProgression(document);
    SdManager::writeJSON(kMilestonesPath, document);
}

bool MilestoneEngine::hasUnlocked(const String& id) {
    for (const String& cachedId : unlockedCache_) {
        if (cachedId == id) {
            return true;
        }
    }

    JsonDocument document;
    if (!SdManager::readJSON(kMilestonesPath, document)) {
        return false;
    }
    if (!document.is<JsonObject>()) {
        return false;
    }
    JsonArray milestones = document["unlocked"].as<JsonArray>();
    if (milestones.isNull()) {
        return false;
    }
    for (JsonVariant value : milestones) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["id"] | "") == id) {
            unlockedCache_.push_back(id);
            return true;
        }
    }
    return false;
}

void MilestoneEngine::cacheUnlockedFromDocument(const JsonDocument& document) {
    unlockedCache_.clear();

    JsonArrayConst milestones = document["unlocked"].as<JsonArrayConst>();
    if (milestones.isNull()) {
        return;
    }

    for (JsonVariantConst value : milestones) {
        JsonObjectConst entry = value.as<JsonObjectConst>();
        const String id = String(entry["id"] | "");
        if (id.length() == 0) {
            continue;
        }

        bool exists = false;
        for (const String& cachedId : unlockedCache_) {
            if (cachedId == id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            unlockedCache_.push_back(id);
        }
    }
}

void MilestoneEngine::ensureStateFiles() {
    JsonDocument document;
    const bool hasMilestones = SdManager::readJSON(kMilestonesPath, document);
    if (!document.is<JsonObject>()) {
        document.clear();
        document.to<JsonObject>();
    }
    JsonArray unlocked = document["unlocked"].to<JsonArray>();
    (void)unlocked;
    JsonObject progression = document["progression"].to<JsonObject>();
    if (progression["level"].isNull()) {
        progression["level"] = 1;
    }
    if (progression["points"].isNull()) {
        progression["points"] = 0;
    }
    if (progression["next_level_points"].isNull()) {
        progression["next_level_points"] = kLevelStep;
    }
    document["unlockable_behaviors"].to<JsonArray>();
    document["unlockable_animations"].to<JsonArray>();
    document["unlockable_emotions"].to<JsonArray>();
    document["unlockable_communication"].to<JsonArray>();
    document["compatibility"]["legacy_state_path"] = kLegacyMilestonesPath;
    document["_schema"] = "flic.milestones.v1";
    if (!hasMilestones) {
        document["_updated_at"] = 0;
    }
    SdManager::writeJSON(kMilestonesPath, document);

    JsonDocument legacy;
    const bool hasLegacy = SdManager::readJSON(kLegacyMilestonesPath, legacy);
    if (!legacy.is<JsonObject>()) {
        legacy.clear();
        legacy.to<JsonObject>();
    }
    legacy["unlocked"].to<JsonArray>();
    JsonObject legacyProgression = legacy["progression"].to<JsonObject>();
    if (legacyProgression["level"].isNull()) {
        legacyProgression["level"] = 1;
    }
    if (legacyProgression["points"].isNull()) {
        legacyProgression["points"] = 0;
    }
    if (legacyProgression["next_level_points"].isNull()) {
        legacyProgression["next_level_points"] = kLevelStep;
    }
    legacy["_schema"] = "flic.milestone_state.v1";
    if (!hasLegacy) {
        legacy["_updated_at"] = 0;
    }
    SdManager::writeJSON(kLegacyMilestonesPath, legacy);
}

void MilestoneEngine::refreshProgression(JsonDocument& document) const {
    JsonArray milestones = document["unlocked"].to<JsonArray>();
    const size_t count = unlockedCount(milestones);
    JsonObject progression = document["progression"].to<JsonObject>();
    progression["points"] = static_cast<uint32_t>(count);
    progression["level"] = static_cast<uint32_t>(1 + (count / kLevelStep));
    progression["next_level_points"] = static_cast<uint32_t>(((count / kLevelStep) + 1) * kLevelStep);
}

void MilestoneEngine::mirrorLegacyState(const JsonArray& unlocked) {
    JsonDocument legacy;
    SdManager::readJSON(kLegacyMilestonesPath, legacy);
    if (!legacy.is<JsonObject>()) {
        legacy.clear();
        legacy.to<JsonObject>();
    }
    JsonArray legacyUnlocked = legacy["unlocked"].to<JsonArray>();
    legacyUnlocked.clear();
    for (JsonVariantConst value : unlocked) {
        JsonObjectConst source = value.as<JsonObjectConst>();
        JsonObject target = legacyUnlocked.add<JsonObject>();
        target["id"] = source["id"] | "";
        target["reason"] = source["reason"] | "";
        target["timestamp"] = source["timestamp"] | 0;
    }

    const size_t count = legacyUnlocked.size();
    JsonObject progression = legacy["progression"].to<JsonObject>();
    progression["points"] = static_cast<uint32_t>(count);
    progression["level"] = static_cast<uint32_t>(1 + (count / kLevelStep));
    progression["next_level_points"] = static_cast<uint32_t>(((count / kLevelStep) + 1) * kLevelStep);
    legacy["_schema"] = "flic.milestone_state.v1";
    legacy["_updated_at"] = millis();

    SdManager::writeJSON(kLegacyMilestonesPath, legacy);
}

void MilestoneEngine::applyUnlockEffects(JsonDocument& document, const String& id, const String& reason) {
    JsonArray behaviors = document["unlockable_behaviors"].to<JsonArray>();
    JsonArray animations = document["unlockable_animations"].to<JsonArray>();
    JsonArray emotions = document["unlockable_emotions"].to<JsonArray>();
    JsonArray communication = document["unlockable_communication"].to<JsonArray>();

    if (id == "first_touch") {
        appendUnique(behaviors, "touch_affinity");
        appendUnique(animations, "micro_nod");
        appendUnique(emotions, "curious");
        appendUnique(communication, "touch_acknowledgement");
    } else if (id == "first_usb_message" || id == "first_device_link") {
        appendUnique(behaviors, "device_dialogue");
        appendUnique(animations, "thinking_loop");
        appendUnique(emotions, "curious");
        appendUnique(communication, "usb_status_summary");
    } else if (id == "first_approved_command") {
        appendUnique(behaviors, "command_confidence");
        appendUnique(animations, "happy_wiggle");
        appendUnique(emotions, "happy");
        appendUnique(communication, "command_confirmation");
    } else if (id == "first_voice_exchange") {
        appendUnique(behaviors, "voice_followup");
        appendUnique(animations, "blink");
        appendUnique(emotions, "calm");
        appendUnique(communication, "voice_reflection");
    } else if (id == "pattern_detected") {
        appendUnique(behaviors, "pattern_prediction");
        appendUnique(animations, "micro_tilt_left");
        appendUnique(emotions, "curious");
        appendUnique(communication, "pattern_insight");
    } else if (id == "trusted_command_channel") {
        appendUnique(behaviors, "trusted_automation");
        appendUnique(animations, "micro_tilt_right");
        appendUnique(emotions, "happy");
        appendUnique(communication, "proactive_suggestions");
    } else if (id == "active_day_one" || id == "expressive_growth") {
        appendUnique(behaviors, "expressive_idle");
        appendUnique(animations, "idle_breathing");
        appendUnique(emotions, "sleepy");
        appendUnique(communication, "contextual_greeting");
    } else if (id == "first_animation") {
        appendUnique(behaviors, "creative_momentum");
        appendUnique(animations, "surprise");
        appendUnique(emotions, "happy");
        appendUnique(communication, "celebration_message");
    }

    if (animationEngine_ != nullptr) {
        if (id == "first_approved_command" || id == "first_animation") {
            animationEngine_->playPreset("happy_wiggle");
        } else if (id == "pattern_detected") {
            animationEngine_->playPreset("thinking_loop");
        }
    }

    if (emotionEngine_ != nullptr) {
        if (id == "first_approved_command" || id == "trusted_command_channel" || id == "first_animation") {
            emotionEngine_->nudgeEmotion("happy", 0.65f);
        } else if (id == "first_usb_message" || id == "first_device_link" || id == "pattern_detected") {
            emotionEngine_->nudgeEmotion("curious", 0.6f);
        }
    }

    if (communicationEngine_ != nullptr) {
        communicationEngine_->notify(String("Milestone unlocked: ") + id, "happy");
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("milestone_reason", id + ":" + reason);
    }
}

void MilestoneEngine::appendUnique(JsonArray array, const String& value) {
    if (value.length() == 0) {
        return;
    }

    for (JsonVariant entry : array) {
        if (String(entry.as<const char*>() ? entry.as<const char*>() : "") == value) {
            return;
        }
    }

    array.add(value);
}

size_t MilestoneEngine::unlockedCount(const JsonArray& milestones) const {
    return milestones.size();
}

}  // namespace Flic
