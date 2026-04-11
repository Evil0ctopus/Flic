#pragma once

#include <Arduino.h>

namespace Flic {

class SdVoicePackLoader {
public:
    bool begin(const String& packRoot = "/Flic/voice_packs/stitch_creature");
    bool loaded() const;
    String status() const;
    String packRoot() const;
    bool resolveLineClip(const String& text, String& wavPathOut) const;

private:
    String hashLine(const String& text) const;

    String packRoot_ = "/Flic/voice_packs/stitch_creature";
    bool loaded_ = false;
};

}  // namespace Flic
