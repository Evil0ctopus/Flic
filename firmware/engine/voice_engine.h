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
    void updateVoiceEngine(float dtSeconds);
    bool popVoiceCommand(String& commandOut);
    bool popSoundEvent(String& eventOut);
    void speak(const String& msg, const String& emotion);
    void speakTextCreature(const char* text);
    String generateCreatureSpeech(const char* input) const;
    bool playCreatureSound(const char* name);
    void setVoiceEmotionState(const String& emotion);
    String voiceEmotionState() const;

private:
    AudioInput* input_ = nullptr;
    AudioOutput* output_ = nullptr;
    FaceEngine* faceEngine_ = nullptr;
    String voiceEmotionState_ = "neutral";
    float voiceChaos_ = 0.0f;
    float updateAccumulator_ = 0.0f;
    unsigned long lastMicroVocalMs_ = 0;
};

}  // namespace Flic
