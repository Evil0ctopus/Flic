#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace Flic {

enum class MoodKind : uint8_t {
    Calm = 0,
    Happy = 1,
    Stressed = 2,
    Tired = 3,
    Curious = 4,
    Bored = 5,
    Count = 6,
};

struct MoodTraits {
    float blinkRateScale = 1.0f;
    float pupilBias = 0.0f;
    float microExpressionScale = 1.0f;
    float transitionSpeedScale = 1.0f;
    float happyWeight = 1.0f;
    float curiousWeight = 1.0f;
    float stressedWeight = 1.0f;
    float tiredWeight = 1.0f;
    float boredWeight = 1.0f;
};

class MoodModel {
public:
    bool begin(const String& storagePath);
    bool saveMood() const;
    bool loadMood();

    bool setMood(const String& moodName);
    String getMood() const;
    void adjustMood(float delta);
    void update(float deltaSeconds);

    void setDecayRate(float decayRate);
    float decayRate() const;
    void enableAdaptation(bool enabled);
    bool adaptationEnabled() const;

    void noteUserInteraction();
    void noteLongIdle();
    void noteRapidEmotionSwitch();
    void noteTaskSuccess();
    void noteError();

    MoodTraits currentTraits() const;

private:
    static float clamp01(float value);
    static MoodKind moodFromName(const String& moodName);
    static String moodToName(MoodKind mood);
    static const MoodTraits& traitsFor(MoodKind mood);

    void normalizeLevels();
    void applyMoodDelta(MoodKind mood, float amount);
    float levelFor(MoodKind mood) const;

    String storagePath_;
    mutable Preferences preferences_;
    bool preferencesOpen_ = false;
    float levels_[static_cast<uint8_t>(MoodKind::Count)] = {0.45f, 0.11f, 0.11f, 0.11f, 0.11f, 0.11f};
    float decayRate_ = 0.01f;
    bool adaptationEnabled_ = true;
};

}  // namespace Flic
