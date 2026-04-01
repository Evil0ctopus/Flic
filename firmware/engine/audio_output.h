#pragma once

#include <Arduino.h>

namespace Flic {

class AudioOutput {
public:
    bool begin();
    void speakTTS(const String& msg);
    void playEmotionTone(const String& emotion);
    void playCreatureSound(const String& sound);
};

}  // namespace Flic
