#pragma once

#include <Arduino.h>

namespace Flic {
String buildCreatureSpeech(const char* input, const String& emotionState, float chaosLevel);
}
