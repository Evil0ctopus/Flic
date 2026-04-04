#include "personality_memory.h"

#include <ArduinoJson.h>
#include <Preferences.h>

namespace Flic {

bool PersonalityMemory::begin(const String& storagePath) {
    storagePath_ = storagePath;
    preferencesOpen_ = preferences_.begin("flic_memory", false);
    return load();
}

bool PersonalityMemory::save() const {
    if (!preferencesOpen_) {
        return false;
    }

    JsonDocument document;
    document["last_known_mood"] = lastKnownMood_;
    document["last_interaction_ms"] = lastInteractionMs_;
    document["history_count"] = historyCount_;
    document["total_recorded"] = totalRecorded_;
    document["current_emotion"] = currentEmotion_;

    JsonArray history = document["history"].to<JsonArray>();
    for (uint8_t i = 0; i < historyCount_; ++i) {
        const int idx = historyIndexFromTotal(totalRecorded_, i);
        history.add(history_[idx]);
    }

    JsonArray times = document["emotion_times"].to<JsonArray>();
    for (const EmotionTime& item : emotionTimes_) {
        JsonObject row = times.add<JsonObject>();
        row["emotion"] = item.emotion;
        row["ms"] = item.totalMs;
    }

    JsonArray transitions = document["transitions"].to<JsonArray>();
    for (const TransitionStat& item : transitionStats_) {
        JsonObject row = transitions.add<JsonObject>();
        row["from"] = item.from;
        row["to"] = item.to;
        row["count"] = item.count;
    }

    String payload;
    serializeJson(document, payload);
    return preferences_.putString("state", payload) > 0;
}

bool PersonalityMemory::load() {
    if (!preferencesOpen_) {
        clearEmotionHistory();
        return false;
    }

    const String payload = preferences_.getString("state", "");
    if (payload.length() == 0) {
        clearEmotionHistory();
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error || !document.is<JsonObject>()) {
        clearEmotionHistory();
        return false;
    }

    clearEmotionHistory();
    if (document["last_known_mood"].is<const char*>()) {
        lastKnownMood_ = String(document["last_known_mood"].as<const char*>());
    }
    if (document["current_emotion"].is<const char*>()) {
        currentEmotion_ = normalizeEmotion(String(document["current_emotion"].as<const char*>()));
    }

    if (document["history"].is<JsonArray>()) {
        JsonArray arr = document["history"].as<JsonArray>();
        for (JsonVariant value : arr) {
            if (historyCount_ >= kHistoryCapacity || !value.is<const char*>()) {
                break;
            }
            pushHistory(String(value.as<const char*>()));
        }
    }

    if (document["emotion_times"].is<JsonArray>()) {
        JsonArray arr = document["emotion_times"].as<JsonArray>();
        for (JsonVariant value : arr) {
            if (!value.is<JsonObject>()) {
                continue;
            }
            JsonObject row = value.as<JsonObject>();
            if (!row["emotion"].is<const char*>()) {
                continue;
            }
            EmotionTime timeRow;
            timeRow.emotion = normalizeEmotion(String(row["emotion"].as<const char*>()));
            timeRow.totalMs = row["ms"].as<uint32_t>();
            emotionTimes_.push_back(timeRow);
        }
    }

    if (document["transitions"].is<JsonArray>()) {
        JsonArray arr = document["transitions"].as<JsonArray>();
        for (JsonVariant value : arr) {
            if (!value.is<JsonObject>()) {
                continue;
            }
            JsonObject row = value.as<JsonObject>();
            if (!row["from"].is<const char*>() || !row["to"].is<const char*>()) {
                continue;
            }
            TransitionStat stat;
            stat.from = normalizeEmotion(String(row["from"].as<const char*>()));
            stat.to = normalizeEmotion(String(row["to"].as<const char*>()));
            stat.count = row["count"].as<uint16_t>();
            transitionStats_.push_back(stat);
        }
    }

    if (document["last_interaction_ms"].is<unsigned long>()) {
        lastInteractionMs_ = document["last_interaction_ms"].as<unsigned long>();
    }
    return true;
}

void PersonalityMemory::update(unsigned long nowMs, const String& currentEmotion) {
    const String normalized = normalizeEmotion(currentEmotion);
    if (lastEmotionStartMs_ == 0) {
        lastEmotionStartMs_ = nowMs;
        currentEmotion_ = normalized;
        return;
    }

    if (normalized != currentEmotion_) {
        const uint32_t elapsed = nowMs >= lastEmotionStartMs_ ? static_cast<uint32_t>(nowMs - lastEmotionStartMs_) : 0U;
        if (elapsed > 0U) {
            bool applied = false;
            for (EmotionTime& entry : emotionTimes_) {
                if (entry.emotion == currentEmotion_) {
                    entry.totalMs += elapsed;
                    applied = true;
                    break;
                }
            }
            if (!applied) {
                EmotionTime row;
                row.emotion = currentEmotion_;
                row.totalMs = elapsed;
                emotionTimes_.push_back(row);
            }
        }
        recordEmotion(normalized, nowMs);
        currentEmotion_ = normalized;
        lastEmotionStartMs_ = nowMs;
    }
}

void PersonalityMemory::recordEmotion(const String& emotion, unsigned long nowMs) {
    const String normalized = normalizeEmotion(emotion);
    if (normalized.length() == 0) {
        return;
    }

    if (historyCount_ == 0 || history_[historyIndexFromTotal(totalRecorded_, historyCount_ - 1)] != normalized) {
        pushHistory(normalized);
    }

    if (previousEmotion_.length() > 0 && previousEmotion_ != normalized) {
        noteTransition(previousEmotion_, normalized);
    }
    previousEmotion_ = normalized;
    currentEmotion_ = normalized;
    if (lastEmotionStartMs_ == 0) {
        lastEmotionStartMs_ = nowMs;
    }
}

void PersonalityMemory::noteInteraction(unsigned long nowMs) {
    lastInteractionMs_ = nowMs;
}

std::vector<String> PersonalityMemory::getEmotionHistory() const {
    std::vector<String> output;
    output.reserve(historyCount_);
    for (uint8_t i = 0; i < historyCount_; ++i) {
        output.push_back(history_[historyIndexFromTotal(totalRecorded_, i)]);
    }
    return output;
}

void PersonalityMemory::clearEmotionHistory() {
    for (uint8_t i = 0; i < kHistoryCapacity; ++i) {
        history_[i] = "";
    }
    historyCount_ = 0;
    totalRecorded_ = 0;
    previousEmotion_ = "";
    currentEmotion_ = "calm";
    lastEmotionStartMs_ = 0;
    lastInteractionMs_ = 0;
    lastKnownMood_ = "calm";
    emotionTimes_.clear();
    transitionStats_.clear();
}

float PersonalityMemory::transitionVolatility() const {
    if (historyCount_ < 2) {
        return 0.0f;
    }
    uint8_t changes = 0;
    String prev = history_[historyIndexFromTotal(totalRecorded_, 0)];
    for (uint8_t i = 1; i < historyCount_; ++i) {
        const String current = history_[historyIndexFromTotal(totalRecorded_, i)];
        if (current != prev) {
            ++changes;
        }
        prev = current;
    }
    return static_cast<float>(changes) / static_cast<float>(historyCount_ - 1);
}

uint32_t PersonalityMemory::timeInEmotionMs(const String& emotion) const {
    const String normalized = normalizeEmotion(emotion);
    for (const EmotionTime& entry : emotionTimes_) {
        if (entry.emotion == normalized) {
            return entry.totalMs;
        }
    }
    return 0;
}

const std::vector<PersonalityMemory::TransitionStat>& PersonalityMemory::transitions() const {
    return transitionStats_;
}

void PersonalityMemory::setLastKnownMood(const String& mood) {
    lastKnownMood_ = mood;
}

String PersonalityMemory::lastKnownMood() const {
    return lastKnownMood_;
}

String PersonalityMemory::normalizeEmotion(const String& emotion) {
    String normalized = emotion;
    normalized.trim();
    normalized.toLowerCase();
    return normalized;
}

int PersonalityMemory::historyIndexFromTotal(uint32_t totalCount, uint8_t offsetFromOldest) {
    if (totalCount <= kHistoryCapacity) {
        return static_cast<int>(offsetFromOldest);
    }
    const uint32_t oldest = totalCount % kHistoryCapacity;
    return static_cast<int>((oldest + offsetFromOldest) % kHistoryCapacity);
}

void PersonalityMemory::pushHistory(const String& emotion) {
    const int idx = static_cast<int>(totalRecorded_ % kHistoryCapacity);
    history_[idx] = normalizeEmotion(emotion);
    ++totalRecorded_;
    if (historyCount_ < kHistoryCapacity) {
        ++historyCount_;
    }
}

void PersonalityMemory::noteTransition(const String& from, const String& to) {
    for (TransitionStat& stat : transitionStats_) {
        if (stat.from == from && stat.to == to) {
            if (stat.count < 65535U) {
                ++stat.count;
            }
            return;
        }
    }
    TransitionStat row;
    row.from = from;
    row.to = to;
    row.count = 1;
    transitionStats_.push_back(row);
    if (transitionStats_.size() > 64U) {
        transitionStats_.erase(transitionStats_.begin());
    }
}

}  // namespace Flic
