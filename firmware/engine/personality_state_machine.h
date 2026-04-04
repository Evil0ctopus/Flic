#pragma once

#include <Arduino.h>

namespace Flic {

enum class PersonalityState : uint8_t {
    Neutral = 0,
    Curious = 1,
    Focused = 2,
    Tired = 3,
    Excited = 4,
    Confused = 5,
};

struct PersonalityProfile {
    float blinkRateScale = 1.0f;
    float pupilScale = 1.0f;
    float microExpressionScale = 0.5f;
    float transitionSpeedScale = 1.0f;
    String idleAnimation = "idle";
};

class PersonalityStateMachine {
public:
    void reset();
    void setState(PersonalityState state);
    PersonalityState state() const;
    bool setStateFromString(const String& stateName);
    String stateName() const;
    void enableContextRules(bool enabled);
    bool contextRulesEnabled() const;
    void noteInteraction(unsigned long nowMs);
    void noteEmotionChange(unsigned long nowMs, const String& emotion);
    void update(unsigned long nowMs, const String& currentEmotion, bool blendingActive);
    const PersonalityProfile& profile() const;

private:
    static String normalizeName(const String& value);
    static PersonalityState stateFromName(const String& stateName);
    static String stateToName(PersonalityState state);
    void refreshProfile();

    PersonalityState state_ = PersonalityState::Neutral;
    bool contextRulesEnabled_ = true;
    unsigned long lastInteractionMs_ = 0;
    unsigned long lastEmotionChangeMs_ = 0;
    unsigned long rapidWindowStartMs_ = 0;
    uint8_t rapidEmotionChangeCount_ = 0;
    PersonalityProfile profile_;
};

}  // namespace Flic