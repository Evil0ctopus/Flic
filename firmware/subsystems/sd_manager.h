#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {
namespace SdManager {

void configureBus();
bool mount();
bool isMounted();
bool fileExists(const char* path);
String listFiles(const char* path);
bool readJSON(const char* path, JsonDocument& document);
bool writeJSON(const char* path, const JsonDocument& document);

}  // namespace SdManager
}  // namespace Flic
