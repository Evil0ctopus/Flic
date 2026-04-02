#include "sd_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

#include <vector>

namespace Flic {
namespace {
constexpr int kSdClockPin = 36;
constexpr int kSdMosiPin = 35;
constexpr int kSdMisoPin = 37;
constexpr gpio_num_t kSdChipSelectPin = GPIO_NUM_4;
constexpr const char* kFlicRootDir = "/Flic";
constexpr const char* kFlicVoicesDir = "/Flic/voices";
constexpr const char* kFlicMemoryDir = "/Flic/memory";
constexpr const char* kFlicConfigDir = "/Flic/config";
constexpr const char* kFlicLogsDir = "/Flic/logs";
constexpr const char* kFlicAnimationsDir = "/Flic/animations";
constexpr const char* kFlicSoundsDir = "/Flic/sounds";
constexpr const char* kFlicFaceDir = "/Flic/animations/face";
constexpr const char* kAiRootDir = "/ai";
constexpr const char* kAiMemoryDir = "/ai/memory";
constexpr const char* kAiAnimationsDir = "/ai/animations";

bool gMounted = false;
}  // namespace

namespace SdManager {

void configureBus() {
    SPI.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, kSdChipSelectPin);
}

bool mount() {
    if (gMounted) {
        Serial.println("Flic: SD card already mounted.");
        return true;
    }

    Serial.println("Flic: mounting SD card...");
    if (!SD.begin(kSdChipSelectPin, SPI, 25000000)) {
        Serial.println("Flic: SD mount failed.");
        gMounted = false;
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        Serial.println("Flic: SD card not detected after mount.");
        SD.end();
        gMounted = false;
        return false;
    }

    SD.mkdir(kFlicRootDir);
    SD.mkdir(kFlicVoicesDir);
    SD.mkdir(kFlicMemoryDir);
    SD.mkdir(kFlicConfigDir);
    SD.mkdir(kFlicLogsDir);
    SD.mkdir(kFlicAnimationsDir);
    SD.mkdir(kFlicSoundsDir);
    SD.mkdir(kFlicFaceDir);

    // Keep legacy paths available to avoid breaking older SD contents.
    SD.mkdir(kAiRootDir);
    SD.mkdir(kAiMemoryDir);
    SD.mkdir(kAiAnimationsDir);

    Serial.println("Flic: SD card mounted.");
    gMounted = true;
    return true;
}

bool isMounted() {
    return gMounted;
}

bool ensureDirectory(const char* path) {
    if (!gMounted || path == nullptr || path[0] == '\0') {
        return false;
    }
    if (SD.exists(path)) {
        return true;
    }
    return SD.mkdir(path);
}

bool fileExists(const char* path) {
    return gMounted && SD.exists(path);
}

String listFiles(const char* path) {
    if (!gMounted) {
        return String();
    }

    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        return String();
    }

    String output;
    File entry = root.openNextFile();
    while (entry) {
        output += entry.name();
        output += entry.isDirectory() ? "/" : "";
        output += "\n";
        entry = root.openNextFile();
    }

    return output;
}

bool readJSON(const char* path, JsonDocument& document) {
    if (!gMounted || !SD.exists(path)) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    const DeserializationError error = deserializeJson(document, file);
    file.close();
    return !error;
}

bool writeJSON(const char* path, const JsonDocument& document) {
    if (!gMounted) {
        return false;
    }

    if (SD.exists(path)) {
        SD.remove(path);
    }
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }

    const size_t bytesWritten = serializeJsonPretty(document, file);
    file.close();
    return bytesWritten > 0;
}

const char* rootDir() {
    return kFlicRootDir;
}

const char* voicesDir() {
    return kFlicVoicesDir;
}

const char* memoryDir() {
    return kFlicMemoryDir;
}

const char* configDir() {
    return kFlicConfigDir;
}

const char* logsDir() {
    return kFlicLogsDir;
}

const char* animationsDir() {
    return kFlicAnimationsDir;
}

const char* soundsDir() {
    return kFlicSoundsDir;
}

}  // namespace SdManager
}  // namespace Flic
