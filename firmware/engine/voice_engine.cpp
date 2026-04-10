#include "voice_engine.h"

#include "face_engine.h"
#include "audio_input.h"
#include "audio_output.h"
#include "creature_grammar.h"

#include <vector>

namespace Flic {

namespace {
String normalizeEmotion(const String& emotion) {
    String value = emotion;
    value.trim();
    value.toLowerCase();
    if (value.length() == 0) {
        value = "neutral";
    }
    return value;
}

bool isSpikeEmotion(const String& emotion) {
    return emotion == "mischievous" || emotion == "curious" || emotion == "surprised" || emotion == "excited";
}
}

bool VoiceEngine::begin(AudioInput* input, AudioOutput* output, FaceEngine* faceEngine) {
    input_ = input;
    output_ = output;
    faceEngine_ = faceEngine;
    voiceEmotionState_ = "neutral";
    voiceChaos_ = 0.0f;
    updateAccumulator_ = 0.0f;
    lastMicroVocalMs_ = 0;
    if (input_ != nullptr) {
        input_->begin();
    }
    if (output_ != nullptr) {
        output_->begin();
    }
    return true;
}

void VoiceEngine::update() {
    if (input_ != nullptr) {
        input_->update();
    }
    updateVoiceEngine(0.0f);
}

void VoiceEngine::updateVoiceEngine(float dtSeconds) {
    if (output_ == nullptr) {
        return;
    }

    if (dtSeconds > 0.0f) {
        updateAccumulator_ += dtSeconds;
    }

    const unsigned long nowMs = millis();
    const bool spike = isSpikeEmotion(voiceEmotionState_);

    if (!spike) {
        voiceChaos_ *= 0.96f;
    }

    const unsigned long intervalMs = spike ? 1800UL : 4200UL;
    if ((nowMs - lastMicroVocalMs_) >= intervalMs) {
        lastMicroVocalMs_ = nowMs;
        if (spike) {
            const char* sound = (voiceEmotionState_ == "surprised") ? "eep" : ((voiceEmotionState_ == "mischievous") ? "hah" : "hmm");
            playCreatureSound(sound);
        } else if (voiceEmotionState_ == "idle" || voiceEmotionState_ == "neutral" || voiceEmotionState_ == "sleepy") {
            playCreatureSound("hmm");
        }
    }
}

bool VoiceEngine::popVoiceCommand(String& commandOut) {
    return input_ != nullptr && input_->popCommand(commandOut);
}

bool VoiceEngine::popSoundEvent(String& eventOut) {
    return input_ != nullptr && input_->popSoundEvent(eventOut);
}

void VoiceEngine::speak(const String& msg, const String& emotion) {
    if (output_ == nullptr) {
        return;
    }
    setVoiceEmotionState(emotion);
    Serial.printf("[VoiceTrace] trigger speak emotion=%s len=%u\n",
                  voiceEmotionState_.c_str(), static_cast<unsigned>(msg.length()));
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion(voiceEmotionState_);
        faceEngine_->setSpeakingAmplitude(0.75f);
        faceEngine_->play("speaking");
    }

    speakTextCreature(msg.c_str());
}

void VoiceEngine::speakTextCreature(const char* text) {
    if (output_ == nullptr || text == nullptr) {
        return;
    }

    Serial.printf("[VoiceTrace] creature input=\"%s\"\n", text);

    const String creatureText = generateCreatureSpeech(text);
    Serial.printf("[VoiceTrace] grammar output=\"%s\"\n", creatureText.c_str());
    output_->setCreatureEmotion(voiceEmotionState_);
    output_->speakCreatureTTS(creatureText.c_str(), voiceEmotionState_.c_str(), "en");
}

String VoiceEngine::generateCreatureSpeech(const char* input) const {
    return buildCreatureSpeech(input, voiceEmotionState_, voiceChaos_);
}

bool VoiceEngine::playCreatureSound(const char* name) {
    if (output_ == nullptr || name == nullptr) {
        return false;
    }
    if (output_->playCreatureSound(name)) {
        return true;
    }
    output_->playCreatureSound(String(name));
    return true;
}

void VoiceEngine::setVoiceEmotionState(const String& emotion) {
    voiceEmotionState_ = normalizeEmotion(emotion);
    if (isSpikeEmotion(voiceEmotionState_)) {
        voiceChaos_ = 0.65f;
    } else if (voiceEmotionState_ == "happy") {
        voiceChaos_ = 0.25f;
    } else if (voiceEmotionState_ == "sleepy") {
        voiceChaos_ = 0.02f;
    } else {
        voiceChaos_ = 0.10f;
    }

    if (output_ != nullptr) {
        output_->setCreatureEmotion(voiceEmotionState_);
    }
}

String VoiceEngine::voiceEmotionState() const {
    return voiceEmotionState_;
}

}  // namespace Flic
