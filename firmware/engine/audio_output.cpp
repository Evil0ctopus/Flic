#include "audio_output.h"

#include <M5Unified.h>

namespace Flic {

bool AudioOutput::begin() {
    return M5.Speaker.isEnabled();
}

void AudioOutput::speakTTS(const String& msg) {
    (void)msg;
    if (!M5.Speaker.isEnabled()) {
        return;
    }
    M5.Speaker.tone(660.0f, 60);
}

void AudioOutput::playEmotionTone(const String& emotion) {
    if (!M5.Speaker.isEnabled()) {
        return;
    }

    if (emotion == "happy") {
        M5.Speaker.tone(880.0f, 90);
    } else if (emotion == "warning") {
        M5.Speaker.tone(220.0f, 140);
    } else if (emotion == "curious") {
        M5.Speaker.tone(520.0f, 90);
    } else {
        M5.Speaker.tone(420.0f, 80);
    }
}

void AudioOutput::playCreatureSound(const String& sound) {
    if (sound == "purr") {
        M5.Speaker.tone(180.0f, 90);
    } else {
        M5.Speaker.tone(500.0f, 70);
    }
}

}  // namespace Flic
