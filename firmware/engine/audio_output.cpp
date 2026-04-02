#include "audio_output.h"

#include <M5Unified.h>
#include <ESP8266SAM.h>
#include <AudioOutput.h>

namespace Flic {

namespace {
constexpr uint8_t kSpeechOutputVolume = 120;

uint8_t clampVolume(uint8_t volume) {
    return volume;
}

class AudioOutputM5Speaker : public ::AudioOutput {
public:
    explicit AudioOutputM5Speaker(m5::Speaker_Class* speaker, uint8_t virtualChannel = 0)
        : speaker_(speaker), virtualChannel_(virtualChannel) {}

    bool begin() override {
        bufferIndex_ = 0;
        triIndex_ = 0;
        if (speaker_ != nullptr) {
            speaker_->setVolume(kSpeechOutputVolume);
        }
        return true;
    }

    bool ConsumeSample(int16_t sample[2]) override {
        if ((bufferIndex_ + 1) >= triBufSize_) {
            flush();
        }

        if ((bufferIndex_ + 1) < triBufSize_) {
            triBuffer_[triIndex_][bufferIndex_] = sample[0];
            triBuffer_[triIndex_][bufferIndex_ + 1] = sample[1];
            bufferIndex_ += 2;
            return true;
        }

        return false;
    }

    void flush() override {
        if (speaker_ != nullptr && bufferIndex_ > 0) {
            yield();
            speaker_->playRaw(triBuffer_[triIndex_], bufferIndex_, hertz, true, 1, virtualChannel_);
            triIndex_ = triIndex_ < 2 ? triIndex_ + 1 : 0;
            bufferIndex_ = 0;
            yield();
        }
    }

    bool stop() override {
        flush();
        if (speaker_ != nullptr) {
            speaker_->stop(virtualChannel_);
        }
        return true;
    }

private:
    static constexpr size_t triBufSize_ = 2048;
    static int16_t triBuffer_[3][triBufSize_];
    m5::Speaker_Class* speaker_ = nullptr;
    uint8_t virtualChannel_ = 0;
    size_t bufferIndex_ = 0;
    size_t triIndex_ = 0;
};

int16_t AudioOutputM5Speaker::triBuffer_[3][AudioOutputM5Speaker::triBufSize_] = {};

String sanitizeSpeechText(const String& input) {
    String cleaned;
    cleaned.reserve(input.length());
    for (size_t index = 0; index < static_cast<size_t>(input.length()); ++index) {
        char c = input.charAt(index);
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (c == '"' || c == '\'' || c == '[' || c == ']' || c == '{' || c == '}' || c == '<' || c == '>') {
            continue;
        }
        cleaned += c;
    }
    cleaned.trim();
    if (cleaned.length() > 180) {
        cleaned = cleaned.substring(0, 180);
    }
    return cleaned;
}

}  // namespace

bool AudioOutput::begin() {
    if (!M5.Speaker.isEnabled()) {
        return false;
    }

    M5.Speaker.setVolume(clampVolume(volume_));
    return true;
}

void AudioOutput::setVolume(uint8_t volume) {
    volume_ = clampVolume(volume);
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(volume_);
    }
}

void AudioOutput::setVoiceStyle(const String& style) {
    String normalized = style;
    normalized.trim();
    normalized.toLowerCase();

    if (normalized == "clear") {
        voiceStyle_ = VoiceStyle::Clear;
    } else if (normalized == "bright") {
        voiceStyle_ = VoiceStyle::Bright;
    } else if (normalized == "deep") {
        voiceStyle_ = VoiceStyle::Deep;
    } else if (normalized == "warm") {
        voiceStyle_ = VoiceStyle::Warm;
    } else {
        voiceStyle_ = VoiceStyle::Natural;
    }

    Serial.printf("[AudioOutput] Voice style set: %s\n", voiceStyleName().c_str());
}

String AudioOutput::voiceStyleName() const {
    if (voiceStyle_ == VoiceStyle::Clear) {
        return "clear";
    }
    if (voiceStyle_ == VoiceStyle::Bright) {
        return "bright";
    }
    if (voiceStyle_ == VoiceStyle::Deep) {
        return "deep";
    }
    if (voiceStyle_ == VoiceStyle::Warm) {
        return "warm";
    }
    return "natural";
}

void AudioOutput::update() {
    // Non-blocking audio streaming update can be added here for advanced features
    // For now, we stream blocking during speakTTS()
}

void AudioOutput::speakTTS(const String& msg, const String& emotion, const String& lang) {
    if (!M5.Speaker.isEnabled()) {
        Serial.println("[AudioOutput] Speaker not enabled");
        return;
    }

    if (msg.length() == 0) {
        return;
    }

    Serial.printf("[AudioOutput] Speaking: %s (emotion: %s, lang: %s, style: %s)\n",
                  msg.c_str(), emotion.c_str(), lang.c_str(), voiceStyleName().c_str());

    isSpeaking_ = true;
    String speechText = sanitizeSpeechText(msg);
    if (speechText.length() == 0) {
        playEmotionTone(emotion);
        isSpeaking_ = false;
        return;
    }

    if (!speechText.endsWith(".") && !speechText.endsWith("!") && !speechText.endsWith("?")) {
        speechText += ".";
    }

    // Create AudioOutput adapter for ESP8266SAM
    AudioOutputM5Speaker audioOut(&M5.Speaker, 0);
    if (!audioOut.begin()) {
        Serial.println("[AudioOutput] Failed to initialize audio output");
        playEmotionTone(emotion);
        isSpeaking_ = false;
        return;
    }

    ESP8266SAM sam;

    uint8_t speed = 58;
    uint8_t pitch = 50;
    uint8_t mouth = 138;
    uint8_t throat = 118;

    if (voiceStyle_ == VoiceStyle::Clear) {
        sam.SetVoice(ESP8266SAM::VOICE_SAM);
        speed = 61;
        pitch = 56;
        mouth = 150;
        throat = 110;
    } else if (voiceStyle_ == VoiceStyle::Bright) {
        sam.SetVoice(ESP8266SAM::VOICE_ELF);
        speed = 62;
        pitch = 58;
        mouth = 146;
        throat = 116;
    } else if (voiceStyle_ == VoiceStyle::Deep) {
        sam.SetVoice(ESP8266SAM::VOICE_SAM);
        speed = 56;
        pitch = 46;
        mouth = 126;
        throat = 140;
    } else if (voiceStyle_ == VoiceStyle::Warm) {
        sam.SetVoice(ESP8266SAM::VOICE_OLDLADY);
        speed = 57;
        pitch = 49;
        mouth = 132;
        throat = 124;
    } else {
        sam.SetVoice(ESP8266SAM::VOICE_OLDLADY);
    }

    // Gentle emotion variation (keep style baseline)
    if (emotion == "happy") {
        speed = static_cast<uint8_t>(speed + 3);
        pitch = static_cast<uint8_t>(pitch + 3);
    } else if (emotion == "curious") {
        speed = static_cast<uint8_t>(speed + 2);
        pitch = static_cast<uint8_t>(pitch + 2);
    } else if (emotion == "warning" || emotion == "surprised") {
        speed = static_cast<uint8_t>(speed + 2);
        pitch = static_cast<uint8_t>(pitch > 3 ? pitch - 3 : pitch);
    } else if (emotion == "sleepy") {
        speed = static_cast<uint8_t>(speed > 5 ? speed - 5 : speed);
        pitch = static_cast<uint8_t>(pitch > 4 ? pitch - 4 : pitch);
    }

    sam.SetSpeed(speed);
    sam.SetPitch(pitch);
    sam.SetMouth(mouth);
    sam.SetThroat(throat);

    yield();
    const bool speechOk = sam.Say(&audioOut, speechText.c_str());
    yield();

    if (!speechOk) {
        Serial.println("[AudioOutput] SAM speech failed, falling back to tone");
        playEmotionTone(emotion);
    } else {
        Serial.println("[AudioOutput] SAM speech complete");
    }

    audioOut.stop();
    isSpeaking_ = false;
}

void AudioOutput::playEmotionTone(const String& emotion) {
    if (!M5.Speaker.isEnabled()) {
        return;
    }

    float freq = 420.0f;
    uint16_t durationMs = 80;

    if (emotion == "happy") {
        freq = 880.0f;
        durationMs = 90;
    } else if (emotion == "warning" || emotion == "surprised") {
        freq = 220.0f;
        durationMs = 140;
    } else if (emotion == "curious") {
        freq = 520.0f;
        durationMs = 90;
    } else if (emotion == "sleepy") {
        freq = 280.0f;
        durationMs = 120;
    }

    M5.Speaker.tone(freq, durationMs);
}


void AudioOutput::playCreatureSound(const String& sound) {
    if (!M5.Speaker.isEnabled()) {
        return;
    }

    if (sound == "purr") {
        M5.Speaker.tone(180.0f, 90);
    } else if (sound == "chirp") {
        M5.Speaker.tone(920.0f, 45);
    } else if (sound == "tick") {
        M5.Speaker.tone(720.0f, 30);
    } else {
        M5.Speaker.tone(500.0f, 70);
    }
}

}  // namespace Flic
