#pragma once

#include <Arduino.h>

#include <vector>

namespace Flic {

class NeuralTtsEngine {
public:
    using AmplitudeEnvelopeFn = void (*)(float amplitude, void* context);

    bool begin();
    bool speak(const String& text, const String& emotion = "neutral", const String& lang = "en");
    bool processQueuedSpeech(const String& text, const String& emotion, const String& lang);
    std::vector<String> listVoices() const;
    bool setActiveVoice(const String& modelFileName);
    String activeVoice() const;
    void setVoiceParams(float speed, float pitch, float clarity);
    float speed() const;
    float pitch() const;
    float clarity() const;
    bool isBusy() const;
    void setAmplitudeEnvelopeHandler(AmplitudeEnvelopeFn handler, void* context);

private:
    String normalizeVoiceName(const String& modelFileName) const;
    bool attemptNeuralInference(const String& text, const String& emotion, const String& lang) const;
    void emitAmplitudeEnvelope(float amplitude) const;

    String activeVoiceModel_;
    float speed_ = 1.0f;
    float pitch_ = 1.0f;
    float clarity_ = 1.0f;
    AmplitudeEnvelopeFn amplitudeEnvelope_ = nullptr;
    void* amplitudeEnvelopeContext_ = nullptr;
};

}  // namespace Flic
