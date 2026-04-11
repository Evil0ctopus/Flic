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
    void setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context);
    void speakPiper(const String& msg,
                   const String& emotion = "neutral",
                   const String& lang = "en",
                   float speedScale = 1.0f,
                   float pitchScale = 1.0f);
    bool playWavFromSd(const String& path);
    void speakTTS(const String& msg, const String& emotion = "neutral", const String& lang = "en");
    void playEmotionTone(const String& emotion);
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
    NeuralTtsEngine neuralTts_;
};

}  // namespace Flic
