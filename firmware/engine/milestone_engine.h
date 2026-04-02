#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace Flic {

class MemoryManager;
class AnimationEngine;
class EmotionEngine;
class CommunicationEngine;

class MilestoneEngine {
public:
    bool begin(MemoryManager* memoryManager,
               AnimationEngine* animationEngine = nullptr,
               EmotionEngine* emotionEngine = nullptr,
               CommunicationEngine* communicationEngine = nullptr);
    void update();
    bool unlock(const String& id, const String& reason);

private:
    void ensureStateFiles();
    void refreshProgression(JsonDocument& document) const;
    void mirrorLegacyState(const JsonArray& unlocked);
    void applyUnlockEffects(JsonDocument& document, const String& id, const String& reason);
    void appendUnique(JsonArray array, const String& value);
    size_t unlockedCount(const JsonArray& milestones) const;
    void save();
    bool hasUnlocked(const String& id);
    void cacheUnlockedFromDocument(const JsonDocument& document);

    MemoryManager* memoryManager_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    EmotionEngine* emotionEngine_ = nullptr;
    CommunicationEngine* communicationEngine_ = nullptr;
    bool wroteIntro_ = false;
    std::vector<String> unlockedCache_;
};

}  // namespace Flic
