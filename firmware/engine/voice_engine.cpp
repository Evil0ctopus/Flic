#include "voice_engine.h"

#include "audio_input.h"
#include "audio_output.h"

namespace Flic {

bool VoiceEngine::begin(AudioInput* input, AudioOutput* output) {
    input_ = input;
    output_ = output;
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
    output_->speakTTS(msg);
    output_->playEmotionTone(emotion);
}

}  // namespace Flic
