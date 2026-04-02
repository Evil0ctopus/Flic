#pragma once

#include <Arduino.h>

namespace Flic {

class AudioInput;
class AudioOutput;
class FaceEngine;

class VoiceEngine {
public:
    bool begin(AudioInput* input, AudioOutput* output, FaceEngine* faceEngine = nullptr);
    void update();
    bool popVoiceCommand(String& commandOut);
    bool popSoundEvent(String& eventOut);
    void speak(const String& msg, const String& emotion);

private:
    AudioInput* input_ = nullptr;
    AudioOutput* output_ = nullptr;
    FaceEngine* faceEngine_ = nullptr;
};

}  // namespace Flic
