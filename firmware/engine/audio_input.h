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
    bool wakeWord_ = false;
    bool hasCommand_ = false;
    bool hasSoundEvent_ = false;
    String lastCommand_;
    String lastSoundEvent_;
    unsigned long lastSampleMs_ = 0;
};

}  // namespace Flic
