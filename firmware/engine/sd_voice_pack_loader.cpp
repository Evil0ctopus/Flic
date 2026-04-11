#include "sd_voice_pack_loader.h"

#include "../subsystems/sd_manager.h"

namespace Flic {

bool SdVoicePackLoader::begin(const String& packRoot) {
    packRoot_ = packRoot;
    packRoot_.trim();
    if (packRoot_.length() == 0) {
        packRoot_ = "/Flic/voice_packs/stitch_creature";
    }

    if (!SdManager::isMounted()) {
        loaded_ = false;
        return false;
    }

    const String manifest = packRoot_ + "/manifest.json";
    const String linesDir = packRoot_ + "/lines";
    loaded_ = SdManager::fileExists(manifest.c_str()) || SdManager::fileExists(linesDir.c_str());
    return loaded_;
}

bool SdVoicePackLoader::loaded() const {
    return loaded_;
}

String SdVoicePackLoader::status() const {
    return loaded_ ? "loaded" : "missing";
}

String SdVoicePackLoader::packRoot() const {
    return packRoot_;
}

bool SdVoicePackLoader::resolveLineClip(const String& text, String& wavPathOut) const {
    if (!loaded_ || !SdManager::isMounted()) {
        return false;
    }
    const String hashed = hashLine(text);
    const String candidate = packRoot_ + "/lines/" + hashed + ".wav";
    if (!SdManager::fileExists(candidate.c_str())) {
        return false;
    }
    wavPathOut = candidate;
    return true;
}

String SdVoicePackLoader::hashLine(const String& text) const {
    uint32_t hash = 5381U;
    for (size_t i = 0; i < static_cast<size_t>(text.length()); ++i) {
        hash = ((hash << 5U) + hash) ^ static_cast<uint8_t>(text.charAt(i));
    }
    char out[9] = {0};
    snprintf(out, sizeof(out), "%08lx", static_cast<unsigned long>(hash));
    return String(out);
}

}  // namespace Flic
