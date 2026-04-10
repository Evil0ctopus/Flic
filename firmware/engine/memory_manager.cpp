#include "memory_manager.h"

#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>

namespace Flic {
namespace {
constexpr const char* kMemoryIndexPath = "/Flic/memory/memory_index.json";
constexpr bool kDisableSdPersistence = true;
}

bool MemoryManager::begin() {
    if (!kDisableSdPersistence) {
        loadMemory();
    }
    return true;
}

void MemoryManager::recordEvent(String type, String detail) {
    if (count_ >= kMaxEvents) {
        trimToLimit();
    }

    types_[count_] = type;
    details_[count_] = detail;
    timestamps_[count_] = ++sequence_;
    ++count_;
    saveMemory();
}

void MemoryManager::saveMemory() {
    if (kDisableSdPersistence) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kMemoryIndexPath, document);
    document["_schema"] = "flic.memory_index.v1";
    document["_updated_at"] = millis();

    JsonArray interactions = document["interaction_timestamps"].to<JsonArray>();
    JsonArray emotionalHistory = document["emotional_history"].to<JsonArray>();
    JsonArray animationEvents = document["animation_events"].to<JsonArray>();
    JsonArray milestoneUnlocks = document["milestone_unlocks"].to<JsonArray>();

    interactions.clear();
    emotionalHistory.clear();
    animationEvents.clear();
    milestoneUnlocks.clear();

    for (size_t index = 0; index < count_; ++index) {
        interactions.add(timestamps_[index]);

        JsonObject entry = emotionalHistory.add<JsonObject>();
        entry["type"] = types_[index];
        entry["detail"] = details_[index];
        entry["timestamp"] = timestamps_[index];

        if (types_[index].startsWith("animation")) {
            JsonObject animationEntry = animationEvents.add<JsonObject>();
            animationEntry["type"] = types_[index];
            animationEntry["detail"] = details_[index];
            animationEntry["timestamp"] = timestamps_[index];
        }

        if (types_[index].startsWith("milestone")) {
            JsonObject milestoneEntry = milestoneUnlocks.add<JsonObject>();
            milestoneEntry["id"] = details_[index];
            milestoneEntry["timestamp"] = timestamps_[index];
        }
    }

    SdManager::writeJSON(kMemoryIndexPath, document);
}

void MemoryManager::loadMemory() {
    if (kDisableSdPersistence) {
        count_ = 0;
        sequence_ = 0;
        return;
    }

    JsonDocument document;
    if (!SdManager::readJSON(kMemoryIndexPath, document)) {
        return;
    }

    count_ = 0;
    sequence_ = 0;

    JsonArray history = document["emotional_history"].as<JsonArray>();
    if (history.isNull()) {
        JsonArray interactions = document["interaction_timestamps"].as<JsonArray>();
        if (!interactions.isNull()) {
            for (JsonVariant value : interactions) {
                if (count_ >= kMaxEvents) {
                    break;
                }
                timestamps_[count_] = value.as<uint32_t>();
                types_[count_] = "interaction";
                details_[count_] = "loaded";
                ++count_;
            }
        }
        return;
    }

    for (JsonVariant value : history) {
        if (count_ >= kMaxEvents) {
            break;
        }

        JsonObject entry = value.as<JsonObject>();
        timestamps_[count_] = entry["timestamp"] | 0;
        types_[count_] = entry["type"] | "interaction";
        details_[count_] = entry["detail"] | "";
        sequence_ = max(sequence_, timestamps_[count_]);
        ++count_;
    }
}

void MemoryManager::trimToLimit() {
    for (size_t index = 1; index < kMaxEvents; ++index) {
        types_[index - 1] = types_[index];
        details_[index - 1] = details_[index];
        timestamps_[index - 1] = timestamps_[index];
    }
    count_ = kMaxEvents - 1;
}

size_t MemoryManager::eventCount() const {
    return count_;
}

String MemoryManager::lastEventType() const {
    if (count_ == 0) {
        return String();
    }
    return types_[count_ - 1];
}

String MemoryManager::lastEventDetail() const {
    if (count_ == 0) {
        return String();
    }
    return details_[count_ - 1];
}

size_t MemoryManager::countEventsOfType(const String& type) const {
    size_t matches = 0;
    for (size_t index = 0; index < count_; ++index) {
        if (types_[index] == type) {
            ++matches;
        }
    }
    return matches;
}

}  // namespace Flic
