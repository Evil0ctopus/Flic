#include "personality_state_machine.h"

namespace Flic {

namespace {
constexpr unsigned long kExcitedWindowMs = 8000;
constexpr unsigned long kConfusedWindowMs = 6000;
constexpr unsigned long kTiredAfterMs = 30000;
constexpr unsigned long kFocusedAfterMs = 12000;
}

void PersonalityStateMachine::reset() {
    state_ = PersonalityState::Neutral;
    contextRulesEnabled_ = true;
    lastInteractionMs_ = 0;
    lastEmotionChangeMs_ = 0;
    rapidWindowStartMs_ = 0;
    rapidEmotionChangeCount_ = 0;
    voiceEmotionState_ = "neutral";
    refreshProfile();
}

void PersonalityStateMachine::setState(PersonalityState state) {
    state_ = state;
    refreshProfile();
}

PersonalityState PersonalityStateMachine::state() const {
    return state_;
}

bool PersonalityStateMachine::setStateFromString(const String& stateName) {
    const PersonalityState parsed = stateFromName(normalizeName(stateName));
    if (parsed == PersonalityState::Neutral && normalizeName(stateName) != "neutral") {
        return false;
    }
    setState(parsed);
    return true;
}

String PersonalityStateMachine::stateName() const {
    return stateToName(state_);
}

void PersonalityStateMachine::enableContextRules(bool enabled) {
    contextRulesEnabled_ = enabled;
}

bool PersonalityStateMachine::contextRulesEnabled() const {
    return contextRulesEnabled_;
}

void PersonalityStateMachine::noteInteraction(unsigned long nowMs) {
    lastInteractionMs_ = nowMs;
}

void PersonalityStateMachine::noteEmotionChange(unsigned long nowMs, const String&) {
    if (rapidWindowStartMs_ == 0 || (nowMs - rapidWindowStartMs_) > kConfusedWindowMs) {
        rapidWindowStartMs_ = nowMs;
        rapidEmotionChangeCount_ = 1;
    } else if (rapidEmotionChangeCount_ < 255U) {
        ++rapidEmotionChangeCount_;
    }
    lastEmotionChangeMs_ = nowMs;
    lastInteractionMs_ = nowMs;
}

void PersonalityStateMachine::setVoiceEmotionState(const String& emotion) {
    String normalized = normalizeName(emotion);
    if (normalized.length() == 0) {
        normalized = "neutral";
    }
    voiceEmotionState_ = normalized;
    profile_.voiceEmotionState = voiceEmotionState_;
}

String PersonalityStateMachine::voiceEmotionState() const {
    return voiceEmotionState_;
}

void PersonalityStateMachine::update(unsigned long nowMs, const String& currentEmotion, bool blendingActive) {
    setVoiceEmotionState(currentEmotion);

    if (!contextRulesEnabled_) {
        refreshProfile();
        return;
    }

    if (rapidWindowStartMs_ != 0 && (nowMs - rapidWindowStartMs_) > kConfusedWindowMs) {
        rapidWindowStartMs_ = nowMs;
        rapidEmotionChangeCount_ = 0;
    }

    if (rapidEmotionChangeCount_ >= 3U) {
        setState(PersonalityState::Confused);
        return;
    }

    if ((nowMs - lastInteractionMs_) >= kTiredAfterMs) {
        setState(PersonalityState::Tired);
        return;
    }

    if ((nowMs - lastEmotionChangeMs_) <= kExcitedWindowMs && !blendingActive) {
        if (currentEmotion == "happy" || currentEmotion == "surprised") {
            setState(PersonalityState::Excited);
            return;
        }
        if (currentEmotion == "curious" || currentEmotion == "thinking") {
            setState(PersonalityState::Curious);
            return;
        }
        if (currentEmotion == "sleepy") {
            setState(PersonalityState::Tired);
            return;
        }
        if (currentEmotion == "focused") {
            setState(PersonalityState::Focused);
            return;
        }
    }

    if ((nowMs - lastInteractionMs_) <= kFocusedAfterMs && currentEmotion == "curious") {
        setState(PersonalityState::Focused);
        return;
    }

    if (currentEmotion == "curious") {
        setState(PersonalityState::Curious);
    } else if (currentEmotion == "sleepy") {
        setState(PersonalityState::Tired);
    } else if (currentEmotion == "happy") {
        setState(PersonalityState::Excited);
    } else if (currentEmotion == "surprised") {
        setState(PersonalityState::Confused);
    } else {
        setState(PersonalityState::Neutral);
    }
}

const PersonalityProfile& PersonalityStateMachine::profile() const {
    return profile_;
}

String PersonalityStateMachine::normalizeName(const String& value) {
    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();
    return normalized;
}

PersonalityState PersonalityStateMachine::stateFromName(const String& stateName) {
    if (stateName == "curious") {
        return PersonalityState::Curious;
    }
    if (stateName == "focused") {
        return PersonalityState::Focused;
    }
    if (stateName == "tired") {
        return PersonalityState::Tired;
    }
    if (stateName == "excited") {
        return PersonalityState::Excited;
    }
    if (stateName == "confused") {
        return PersonalityState::Confused;
    }
    return PersonalityState::Neutral;
}

String PersonalityStateMachine::stateToName(PersonalityState state) {
    switch (state) {
        case PersonalityState::Curious:
            return "curious";
        case PersonalityState::Focused:
            return "focused";
        case PersonalityState::Tired:
            return "tired";
        case PersonalityState::Excited:
            return "excited";
        case PersonalityState::Confused:
            return "confused";
        case PersonalityState::Neutral:
        default:
            return "neutral";
    }
}

void PersonalityStateMachine::refreshProfile() {
    switch (state_) {
        case PersonalityState::Curious:
            profile_.blinkRateScale = 0.92f;
            profile_.pupilScale = 1.10f;
            profile_.microExpressionScale = 0.72f;
            profile_.transitionSpeedScale = 0.88f;
            profile_.idleAnimation = "thinking_loop";
            break;
        case PersonalityState::Focused:
            profile_.blinkRateScale = 1.12f;
            profile_.pupilScale = 1.02f;
            profile_.microExpressionScale = 0.46f;
            profile_.transitionSpeedScale = 0.80f;
            profile_.idleAnimation = "idle_breathing";
            break;
        case PersonalityState::Tired:
            profile_.blinkRateScale = 1.45f;
            profile_.pupilScale = 0.84f;
            profile_.microExpressionScale = 0.28f;
            profile_.transitionSpeedScale = 1.18f;
            profile_.idleAnimation = "sleepy_fade";
            break;
        case PersonalityState::Excited:
            profile_.blinkRateScale = 0.78f;
            profile_.pupilScale = 1.18f;
            profile_.microExpressionScale = 0.90f;
            profile_.transitionSpeedScale = 0.72f;
            profile_.idleAnimation = "happy_wiggle";
            break;
        case PersonalityState::Confused:
            profile_.blinkRateScale = 1.28f;
            profile_.pupilScale = 0.92f;
            profile_.microExpressionScale = 0.64f;
            profile_.transitionSpeedScale = 1.06f;
            profile_.idleAnimation = "thinking_loop";
            break;
        case PersonalityState::Neutral:
        default:
            profile_.blinkRateScale = 1.0f;
            profile_.pupilScale = 1.0f;
            profile_.microExpressionScale = 0.50f;
            profile_.transitionSpeedScale = 1.0f;
            profile_.idleAnimation = "idle";
            break;
    }

    profile_.voiceEmotionState = voiceEmotionState_;
}

}  // namespace Flic