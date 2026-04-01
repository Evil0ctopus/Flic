#include "audio_input.h"

#include <M5Unified.h>

namespace Flic {

bool AudioInput::begin() {
    wakeWord_ = false;
    hasCommand_ = false;
    hasSoundEvent_ = false;
    lastSampleMs_ = millis();
    return true;
}

void AudioInput::update() {
    const unsigned long now = millis();
    if (now - lastSampleMs_ < 150) {
        return;
    }
    lastSampleMs_ = now;

    if (!M5.Mic.isEnabled()) {
        return;
    }

    int16_t pcm[64];
    if (!M5.Mic.record(pcm, 64)) {
        return;
    }

    uint32_t energy = 0;
    for (size_t i = 0; i < 64; ++i) {
        energy += static_cast<uint32_t>(abs(pcm[i]));
    }
    const uint32_t average = energy / 64;

    if (average > 2800) {
        hasSoundEvent_ = true;
        lastSoundEvent_ = "loud_sound";
    }

    if (average > 4200) {
        wakeWord_ = true;
        hasCommand_ = true;
        lastCommand_ = "wake";
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
