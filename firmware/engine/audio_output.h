#pragma once

#include <Arduino.h>

namespace Flic {

class AudioOutput {
public:
    bool begin();
    void setVolume(uint8_t volume);
    void setVoiceStyle(const String& style);
    String voiceStyleName() const;
    void speakTTS(const String& msg, const String& emotion = "neutral", const String& lang = "en");
    void playEmotionTone(const String& emotion);
    void playCreatureSound(const String& sound);
    void update();  // For streaming audio playback
    bool isSpeaking() const { return isSpeaking_; }

private:
    enum class VoiceStyle : uint8_t {
        Natural,
        Clear,
        Bright,
        Deep,
        Warm,
    };

    uint8_t volume_ = 180;
    bool isSpeaking_ = false;
    VoiceStyle voiceStyle_ = VoiceStyle::Natural;
};

}  // namespace Flic
