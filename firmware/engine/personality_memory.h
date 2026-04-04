#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <vector>

namespace Flic {

class PersonalityMemory {
public:
    struct TransitionStat {
        String from;
        String to;
        uint16_t count = 0;
    };

    bool begin(const String& storagePath);
    bool save() const;
    bool load();

    void update(unsigned long nowMs, const String& currentEmotion);
    void recordEmotion(const String& emotion, unsigned long nowMs);
    void noteInteraction(unsigned long nowMs);

    std::vector<String> getEmotionHistory() const;
    void clearEmotionHistory();

    float transitionVolatility() const;
    uint32_t timeInEmotionMs(const String& emotion) const;
    const std::vector<TransitionStat>& transitions() const;

    void setLastKnownMood(const String& mood);
    String lastKnownMood() const;

private:
    static String normalizeEmotion(const String& emotion);
    static int historyIndexFromTotal(uint32_t totalCount, uint8_t offsetFromOldest);

    void pushHistory(const String& emotion);
    void noteTransition(const String& from, const String& to);

    static constexpr uint8_t kHistoryCapacity = 20;

    String storagePath_;
    mutable Preferences preferences_;
    bool preferencesOpen_ = false;
    String history_[kHistoryCapacity];
    uint8_t historyCount_ = 0;
    uint32_t totalRecorded_ = 0;

    String currentEmotion_ = "calm";
    String previousEmotion_ = "";
    unsigned long lastEmotionStartMs_ = 0;
    unsigned long lastInteractionMs_ = 0;
    String lastKnownMood_ = "calm";

    struct EmotionTime {
        String emotion;
        uint32_t totalMs = 0;
    };
    std::vector<EmotionTime> emotionTimes_;
    std::vector<TransitionStat> transitionStats_;
};

}  // namespace Flic
