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

    Serial.println("Flic: SD card mounted.");
    gMounted = true;
    return true;
}

bool isMounted() {
    return gMounted;
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

    SD.remove(path);
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }

    const size_t bytesWritten = serializeJsonPretty(document, file);
    file.close();
    return bytesWritten > 0;
}

}  // namespace SdManager
}  // namespace Flic
