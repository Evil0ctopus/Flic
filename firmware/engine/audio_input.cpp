#include "audio_input.h"

#include <M5Unified.h>

namespace Flic {

bool AudioInput::begin() {
    wakeWord_ = false;
    hasCommand_ = false;
    hasSoundEvent_ = false;
    samplePending_ = false;
    lastSampleMs_ = millis();
    lastWakeMs_ = 0;
    lastSoundEventMs_ = 0;
    baselineEnergy_ = 0;
    return true;
}

void AudioInput::update() {
    constexpr unsigned long kSampleIntervalMs = 50;
    const unsigned long now = millis();
    if (!M5.Mic.isEnabled()) {
        return;
    }

    if (samplePending_) {
        if (M5.Mic.isRecording()) {
            return;
        }

        samplePending_ = false;

        uint32_t energy = 0;
        for (size_t i = 0; i < kMicSamples; ++i) {
            energy += static_cast<uint32_t>(abs(micSamples_[i]));
        }
        const uint32_t average = energy / static_cast<uint32_t>(kMicSamples);

        if (baselineEnergy_ == 0) {
            baselineEnergy_ = average;
        } else {
            baselineEnergy_ = (baselineEnergy_ * 9U + average) / 10U;
        }

        const uint32_t delta = average > baselineEnergy_ ? (average - baselineEnergy_) : 0U;
        const bool loudEvent = average > 2600U || delta > 1700U;
        const bool wakeEvent = average > 4200U || delta > 2800U;

        if (loudEvent && (now - lastSoundEventMs_) > 500) {
            hasSoundEvent_ = true;
            lastSoundEvent_ = delta > 2800U ? "impact_sound" : "loud_sound";
            lastSoundEventMs_ = now;
        }

        if (wakeEvent && (now - lastWakeMs_) > 900) {
            wakeWord_ = true;
            hasCommand_ = true;
            lastCommand_ = "wake";
            lastWakeMs_ = now;
        } else if (delta > 1200U && !hasCommand_) {
            hasCommand_ = true;
            lastCommand_ = "listen";
        }
        return;
    }

    if (now - lastSampleMs_ < kSampleIntervalMs) {
        return;
    }
    lastSampleMs_ = now;

    if (M5.Mic.isRecording()) {
        return;
    }

    if (M5.Mic.record(micSamples_, kMicSamples)) {
        samplePending_ = true;
    }
}

bool AudioInput::hasWakeWord() const {
    return wakeWord_;
}

bool AudioInput::popCommand(String& commandOut) {
    if (!hasCommand_) {
        return false;
    }
    commandOut = lastCommand_;
    hasCommand_ = false;
    wakeWord_ = false;
    return true;
}

bool AudioInput::popSoundEvent(String& eventOut) {
    if (!hasSoundEvent_) {
        return false;
    }
    eventOut = lastSoundEvent_;
    hasSoundEvent_ = false;
    return true;
}

}  // namespace Flic
