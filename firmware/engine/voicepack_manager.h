#pragma once

#include <Arduino.h>

#include <string>

namespace Flic {

class AudioOutput;

class VoicePackManager {
public:
    void BindOutput(AudioOutput* output);
    bool Init();
    bool Exists(const char* key);
    bool Play(const char* key);
    std::string ResolveKey(const std::string& transformedText);
    bool loaded() const;

private:
    String normalizeKey(const String& input) const;
    String keyToPath(const String& key) const;

    AudioOutput* output_ = nullptr;
    bool loaded_ = false;
};

}  // namespace Flic
