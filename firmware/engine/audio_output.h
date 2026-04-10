#pragma once

#include <Arduino.h>

#include <vector>

#include "neural_tts_engine.h"

namespace Flic {

class AudioOutput {
public:
    using AmplitudeEnvelopeFn = NeuralTtsEngine::AmplitudeEnvelopeFn;

    bool begin();
    void setVolume(uint8_t volume);
    void setVoiceStyle(const String& style);
    String voiceStyleName() const;
    std::vector<String> listVoices() const;
    bool setActiveVoiceModel(const String& modelFileName);
    String activeVoiceModel() const;
    void setVoiceTuning(float speed, float pitch, float clarity);
    float voiceSpeed() const;
    float voicePitch() const;
    float voiceClarity() const;
    void setFallbackVoiceEnabled(bool enabled);
    bool fallbackVoiceEnabled() const;
    void setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context);
    void speakTTS(const String& msg, const String& emotion = "neutral", const String& lang = "en");
    void speakCreatureTTS(const char* text, const char* emotion = "neutral", const char* lang = "en");
    void playEmotionTone(const String& emotion);
    void playCreatureSound(const String& sound);
    bool playCreatureSound(const char* soundName);
    void processCreatureDSP(int16_t* samples, size_t count);
    void setCreatureEmotion(const String& emotion);
    String creatureEmotion() const;
    void update();  // For streaming audio playback
    bool isSpeaking() const;

private:
    enum class VoiceStyle : uint8_t {
        Natural,
        Clear,
        Bright,
        Deep,
        Warm,
    };

    uint8_t volume_ = 180;
    VoiceStyle voiceStyle_ = VoiceStyle::Natural;
    String creatureEmotion_ = "neutral";
    float dspPitchSemitones_ = 0.0f;
    float dspFormantShift_ = 1.0f;
    float dspDistortionDrive_ = 1.0f;
    float dspEqBoost_ = 1.0f;
    float dspCompressorThreshold_ = 0.65f;
    float dspCompressorRatio_ = 2.4f;
    float dspLowPassCutoffHz_ = 9000.0f;
    float dspChaosLevel_ = 0.0f;
    float dspLowPassState_ = 0.0f;
    NeuralTtsEngine neuralTts_;
};

}  // namespace Flic
