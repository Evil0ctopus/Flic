#include "voicepack_manager.h"

#include "audio_output.h"
#include "../subsystems/sd_manager.h"

namespace Flic {

void VoicePackManager::BindOutput(AudioOutput* output) {
    output_ = output;
}

bool VoicePackManager::Init() {
    if (!SdManager::isMounted()) {
        loaded_ = false;
        Serial.println("[VoiceTrace] VOICE: SD pack=missing");
        return false;
    }

    // Required marker files for current SD voicepack layout.
    const bool hasNoise = SdManager::fileExists("/voicepack/eep.wav") ||
                          SdManager::fileExists("/voicepack/heh.wav") ||
                          SdManager::fileExists("/voicepack/ooh.wav") ||
                          SdManager::fileExists("/voicepack/huh.wav") ||
                          SdManager::fileExists("/voicepack/mm.wav");
    loaded_ = hasNoise || SdManager::fileExists("/voicepack/test_creature.wav");

    Serial.printf("[VoiceTrace] VOICE: SD pack=%s\n", loaded_ ? "loaded" : "missing");
    return loaded_;
}

bool VoicePackManager::Exists(const char* key) {
    if (key == nullptr || !loaded_ || !SdManager::isMounted()) {
        return false;
    }

    const String path = keyToPath(String(key));
    return SdManager::fileExists(path.c_str());
}

bool VoicePackManager::Play(const char* key) {
    if (key == nullptr || output_ == nullptr) {
        return false;
    }

    const String path = keyToPath(String(key));
    if (!SdManager::fileExists(path.c_str())) {
        return false;
    }

    return output_->playWavFromSd(path);
}

std::string VoicePackManager::ResolveKey(const std::string& transformedText) {
    const String normalized = normalizeKey(String(transformedText.c_str()));
    return std::string(normalized.c_str());
}

bool VoicePackManager::loaded() const {
    return loaded_;
}

String VoicePackManager::normalizeKey(const String& input) const {
    String key;
    key.reserve(input.length());

    for (size_t i = 0; i < static_cast<size_t>(input.length()); ++i) {
        char c = static_cast<char>(tolower(input.charAt(i)));
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            key += c;
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!key.endsWith("_")) {
                key += '_';
            }
        }
    }

    while (key.endsWith("_")) {
        key.remove(key.length() - 1);
    }

    if (key.length() == 0) {
        key = "unknown";
    }
    if (key.length() > 56) {
        key = key.substring(0, 56);
    }
    return key;
}

String VoicePackManager::keyToPath(const String& key) const {
    String normalized = key;
    normalized.trim();
    if (!normalized.endsWith(".wav")) {
        normalized += ".wav";
    }
    return String("/voicepack/") + normalized;
}

}  // namespace Flic
