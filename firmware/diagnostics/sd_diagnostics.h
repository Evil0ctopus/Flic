#pragma once

#include <Arduino.h>

namespace Flic {
namespace SdDiagnostics {

void logSdStatus();
void logFaceLoad(const String& filename, const String& fullPath);
void logBootAnimationFrame(const String& filename);
void logSoundPlay(const String& filename);

}  // namespace SdDiagnostics
}  // namespace Flic
