#include "voice_engine.h"

#include "face_engine.h"
#include "audio_input.h"
#include "audio_output.h"
#include "sd_voice_pack_loader.h"
#include "voicepack_manager.h"

namespace Flic {

namespace {
bool gPiperAvailable = false;
bool gPiperUnavailableWarned = false;
unsigned long gLastDropSpeechLogMs = 0;
bool gAudioAvailable = false;

String normalizeEmotion(const String& emotion) {
    String value = emotion;
    value.trim();
    value.toLowerCase();
    if (value.length() == 0) {
        value = "neutral";
    }
    return value;
}

void VoiceTrace(const String& message) {
    Serial.printf("[VoiceTrace] %s\n", message.c_str());
}
}

bool VoiceEngine::begin(AudioInput* input, AudioOutput* output, FaceEngine* faceEngine) {
    input_ = input;
    output_ = output;
    faceEngine_ = faceEngine;
    voiceEmotionState_ = "neutral";
    updateAccumulator_ = 0.0f;
    lastExplicitSpeechMs_ = 0;
    personalityLayer_.begin();
    if (input_ != nullptr) {
        input_->begin();
    }
    bool outputReady = false;
    if (output_ != nullptr) {
        outputReady = output_->begin();
    }

    (void)outputReady;
    // Degraded mode for now: keep Piper architecture active, but disable playback until inference backend is ready.
    setPiperAvailable(false);

    return true;
}

void VoiceEngine::attachAudioInput(AudioInput* input, bool beginNow) {
    input_ = input;
    if (input_ != nullptr && beginNow) {
        input_->begin();
    }
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

    personalityLayer_.update(millis());
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
    VoiceTrace(String("VOICE: speak_original=") + msg);
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion(voiceEmotionState_);
        faceEngine_->setSpeakingAmplitude(0.75f);
        faceEngine_->play("speaking");
    }

    const String finalText = applyPersonalityTransform(msg, voiceEmotionState_);
    lastExplicitSpeechMs_ = millis();
    VoiceTrace(String("VOICE: speak_transformed=") + finalText);

    if (!gAudioAvailable) {
        const unsigned long nowMs = millis();
        if (gLastDropSpeechLogMs == 0 || (nowMs - gLastDropSpeechLogMs) >= 3000UL) {
            VoiceTrace("VOICE: drop_speech (backend unavailable).");
            gLastDropSpeechLogMs = nowMs;
        }
        if (output_ != nullptr) {
            output_->playEmotionTone(voiceEmotionState_);
        }
        return;
    }

    if (voicePackManager_ == nullptr) {
        VoiceTrace("VOICE: drop_speech (backend unavailable).");
        if (output_ != nullptr) {
            output_->playEmotionTone(voiceEmotionState_);
        }
        return;
    }

    const std::string resolvedKeyStd = voicePackManager_->ResolveKey(std::string(finalText.c_str()));
    const String resolvedKey = String(resolvedKeyStd.c_str());

    if (voicePackManager_->Exists(resolvedKey.c_str())) {
        if (!voicePackManager_->Play(resolvedKey.c_str())) {
            VoiceTrace(String("VOICE: missing_wav=") + resolvedKey);
        }
        return;
    }

    const std::string originalKeyStd = voicePackManager_->ResolveKey(std::string(msg.c_str()));
    const String originalKey = String(originalKeyStd.c_str());
    if (originalKey != resolvedKey && voicePackManager_->Exists(originalKey.c_str())) {
        if (!voicePackManager_->Play(originalKey.c_str())) {
            VoiceTrace(String("VOICE: missing_wav=") + originalKey);
        }
        return;
    }

    VoiceTrace(String("VOICE: missing_wav=") + resolvedKey);

    // Prefer phrase-level fallback clips so cadence remains natural when a specific key is missing.
    static const char* kPhraseFallbackKeys[] = {
        "hello_friend_voice_system_online",
        "runtime_voice_check",
        "i_can_pause_a_little_then_continue_with_a_second_sentence",
        "if_this_sounds_natural_cadence_tuning_is_working",
        "test_creature",
    };

    for (size_t i = 0; i < (sizeof(kPhraseFallbackKeys) / sizeof(kPhraseFallbackKeys[0])); ++i) {
        const char* key = kPhraseFallbackKeys[i];
        if (voicePackManager_->Exists(key)) {
            if (!voicePackManager_->Play(key)) {
                VoiceTrace(String("VOICE: missing_wav=") + String(key));
            }
            return;
        }
    }

    static const char* kCreatureNoises[] = {"eep", "heh", "ooh", "huh", "mm"};
    const size_t noiseIndex = static_cast<size_t>(millis() % 5UL);
    if (!voicePackManager_->Play(kCreatureNoises[noiseIndex])) {
        VoiceTrace(String("VOICE: missing_wav=") + String(kCreatureNoises[noiseIndex]));
    }

    // Guaranteed audible fallback for safety: never fail silently.
    if (output_ != nullptr) {
        output_->playEmotionTone(voiceEmotionState_);
    }

    // Keep Piper backend path in place for future inference rollout.
    if (gPiperAvailable) {
        output_->speakPiper(finalText, voiceEmotionState_, "en", 1.10f, 1.10f);
    }
}

void VoiceEngine::setVoiceProfile(const String& profileName) {
    voiceProfile_ = profileName;
    voiceProfile_.trim();
    if (voiceProfile_.length() == 0) {
        voiceProfile_ = "modern_default";
    }
    VoiceTrace(String("VOICE: profile=") + voiceProfile_);
}

String VoiceEngine::voiceProfile() const {
    return voiceProfile_;
}

void VoiceEngine::setPersonalitySpeechEnabled(bool enabled) {
    personalitySpeechEnabled_ = enabled;
    personalityLayer_.setEnabled(enabled);
    VoiceTrace(String("VOICE: personality=") + (personalitySpeechEnabled_ ? "enabled" : "disabled"));
}

bool VoiceEngine::personalitySpeechEnabled() const {
    return personalitySpeechEnabled_;
}

String VoiceEngine::applyPersonalityTransform(const String& message, const String& emotion) {
    if (!personalitySpeechEnabled_) {
        return message;
    }
    const PersonalitySpeechResult transformed = personalityLayer_.transformSpeech(message, emotion);
    return transformed.text;
}

void VoiceEngine::setVoiceEmotionState(const String& emotion) {
    voiceEmotionState_ = normalizeEmotion(emotion);
}

String VoiceEngine::voiceEmotionState() const {
    return voiceEmotionState_;
}

void VoiceEngine::setSdVoicePackLoader(SdVoicePackLoader* loader) {
    voicePackLoader_ = loader;
}

void VoiceEngine::setVoicePackManager(VoicePackManager* manager) {
    voicePackManager_ = manager;
}

void VoiceEngine::setPiperAvailable(bool available) {
    gPiperAvailable = available;
    if (!gPiperAvailable && !gPiperUnavailableWarned) {
        VoiceTrace("VOICE: WARNING: Piper inference unavailable (no neural voice / SD pack missing).");
        gPiperUnavailableWarned = true;
    }
}

bool VoiceEngine::piperAvailable() const {
    return gPiperAvailable;
}

void VoiceEngine::setAudioAvailable(bool available) {
    gAudioAvailable = available;
    if (!gAudioAvailable) {
        VoiceTrace("VOICE: WARNING: audio unavailable.");
    }
}

bool VoiceEngine::audioAvailable() const {
    return gAudioAvailable;
}

}  // namespace Flic
