#pragma once

#include <Arduino.h>

namespace Flic {

class AudioInput {
public:
    bool begin();
    void update();
    bool hasWakeWord() const;
    bool popCommand(String& commandOut);
    bool popSoundEvent(String& eventOut);

private:
    static constexpr size_t kMicSamples = 64;
    bool wakeWord_ = false;
    bool hasCommand_ = false;
    bool hasSoundEvent_ = false;
    bool samplePending_ = false;
    String lastCommand_;
    String lastSoundEvent_;
    unsigned long lastSampleMs_ = 0;
    unsigned long lastWakeMs_ = 0;
    unsigned long lastSoundEventMs_ = 0;
    uint32_t baselineEnergy_ = 0;
    int16_t micSamples_[kMicSamples] = {};
};

}  // namespace Flic
