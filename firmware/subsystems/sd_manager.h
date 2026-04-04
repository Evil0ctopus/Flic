#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>


#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {
namespace SdManager {

void configureBus();
bool mount();
void verify();
void printBootSummary();
void runSelfRepair();
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
const char* mountPoint();
const char* storageBackend();
const char* lastMountError();
uint32_t lastInitFrequencyHz();
uint8_t lastSpiMode();
uint32_t lastAttemptCount();
uint16_t csPreDelayUs();
uint16_t csPostDelayUs();
int lastCmd0Status();
int lastCmd8Status();

}  // namespace SdManager
}  // namespace Flic
