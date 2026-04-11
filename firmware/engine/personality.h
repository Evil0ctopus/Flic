#pragma once

#include <Arduino.h>

namespace Flic {

struct PersonalitySpeechResult {
    String text;
    float speedScale = 1.10f;
    float pitchScale = 1.10f;
};

class StitchPersonalityLayer {
public:
    void begin();
    void setEnabled(bool enabled);
    bool enabled() const;
    void update(unsigned long nowMs);
    PersonalitySpeechResult transformSpeech(const String& message, const String& emotion);

private:
    bool enabled_ = true;
    unsigned long lastUpdateMs_ = 0;
    uint32_t utteranceCount_ = 0;
};

}  // namespace Flic
