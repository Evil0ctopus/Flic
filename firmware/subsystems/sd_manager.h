#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {
namespace SdManager {

void configureBus();
bool mount();
bool isMounted();
bool ensureDirectory(const char* path);
bool fileExists(const char* path);
String listFiles(const char* path);
bool readJSON(const char* path, JsonDocument& document);
bool writeJSON(const char* path, const JsonDocument& document);

const char* rootDir();
const char* voicesDir();
const char* memoryDir();
const char* configDir();
const char* logsDir();
const char* animationsDir();
const char* soundsDir();

}  // namespace SdManager
}  // namespace Flic
