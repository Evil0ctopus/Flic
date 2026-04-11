#pragma once

#include <Arduino.h>

#include "personality.h"

namespace Flic {

class AudioInput;
class AudioOutput;
class FaceEngine;
class SdVoicePackLoader;
class VoicePackManager;

class VoiceEngine {
public:
    bool begin(AudioInput* input, AudioOutput* output, FaceEngine* faceEngine = nullptr);
    void attachAudioInput(AudioInput* input, bool beginNow = true);
    void update();
    void updateVoiceEngine(float dtSeconds);
    bool popVoiceCommand(String& commandOut);
    bool popSoundEvent(String& eventOut);
    void speak(const String& msg, const String& emotion);
    void setVoiceProfile(const String& profileName);
    String voiceProfile() const;
    void setPersonalitySpeechEnabled(bool enabled);
    bool personalitySpeechEnabled() const;
    void setVoiceEmotionState(const String& emotion);
    String voiceEmotionState() const;
    void setSdVoicePackLoader(SdVoicePackLoader* loader);
    void setVoicePackManager(VoicePackManager* manager);
    void setPiperAvailable(bool available);
    bool piperAvailable() const;
    void setAudioAvailable(bool available);
    bool audioAvailable() const;

private:
    AudioInput* input_ = nullptr;
    AudioOutput* output_ = nullptr;
    FaceEngine* faceEngine_ = nullptr;
    SdVoicePackLoader* voicePackLoader_ = nullptr;
    VoicePackManager* voicePackManager_ = nullptr;
    StitchPersonalityLayer personalityLayer_;
    String voiceEmotionState_ = "neutral";
    float updateAccumulator_ = 0.0f;
    unsigned long lastExplicitSpeechMs_ = 0;
    String voiceProfile_ = "modern_default";
    bool personalitySpeechEnabled_ = true;

    String applyPersonalityTransform(const String& message, const String& emotion);
};

}  // namespace Flic
