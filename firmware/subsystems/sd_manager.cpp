#include "sd_manager.h"
#include "cores3_sd_pins.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

#include <vector>

namespace Flic {
bool g_sdMounted = false;

namespace {
constexpr int kSdClockPin = CoreS3SdPins::kSck;
constexpr int kSdMosiPin = CoreS3SdPins::kMosi;
constexpr int kSdMisoPin = CoreS3SdPins::kMiso;
constexpr int kSdChipSelectPin = CoreS3SdPins::kCs;
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

bool gSdSessionActive = false;
bool gMountAttempted = false;
const char* gLastMountError = "not_attempted";
unsigned long gLastMountAttemptMs = 0;
constexpr unsigned long kMountRetryIntervalMs = 5000;
constexpr uint16_t kSdCsPreDelayUs = 20;
constexpr uint16_t kSdCsPostDelayUs = 20;
constexpr uint8_t kSdSpiMode = SPI_MODE0;
constexpr uint32_t kSdInitClockHz = 400000;
constexpr uint8_t kSdInitPulseCycles = 80;

uint32_t gLastInitFrequencyHz = 0;
uint32_t gLastAttemptCount = 0;
int gLastCmd0Status = -2;
int gLastCmd8Status = -2;

constexpr uint32_t kSdMountFrequencies[] = {
    kSdInitClockHz,
    1000000,
    4000000,
    10000000,
    16000000,
    25000000,
};

void primeCardWithIdleClocks() {
    pinMode(static_cast<int>(kSdChipSelectPin), OUTPUT);
    digitalWrite(static_cast<int>(kSdChipSelectPin), HIGH);
    pinMode(kSdClockPin, OUTPUT);
    pinMode(kSdMosiPin, OUTPUT);
    digitalWrite(kSdMosiPin, HIGH);

    for (uint8_t i = 0; i < kSdInitPulseCycles; ++i) {
        digitalWrite(kSdClockPin, LOW);
        delayMicroseconds(1);
        digitalWrite(kSdClockPin, HIGH);
        delayMicroseconds(1);
    }
    delayMicroseconds(kSdCsPreDelayUs);
}

bool tryMountAtFrequency(uint32_t hz) {
    gLastInitFrequencyHz = hz;
    gLastCmd0Status = -2;
    gLastCmd8Status = -2;

    if (gSdSessionActive) {
        SD.end();
        gSdSessionActive = false;
        delay(8);
    }

    primeCardWithIdleClocks();
    SPI.end();
    delay(2);
    SPI.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, kSdChipSelectPin);
    SPI.setDataMode(kSdSpiMode);
    SPI.setFrequency(hz);

    digitalWrite(static_cast<int>(kSdChipSelectPin), HIGH);
    delayMicroseconds(kSdCsPreDelayUs);

    Serial.printf("Flic: SD mount attempt at %lu Hz...\n", static_cast<unsigned long>(hz));
    if (!SD.begin(kSdChipSelectPin, SPI, hz)) {
        Serial.printf("Flic: SD begin failed at %lu Hz\n", static_cast<unsigned long>(hz));
        gLastCmd0Status = -1;
        gLastCmd8Status = -1;
        delayMicroseconds(kSdCsPostDelayUs);
        return false;
    }
    gSdSessionActive = true;
    if (SD.cardType() == CARD_NONE) {
        Serial.printf("Flic: SD card type NONE at %lu Hz\n", static_cast<unsigned long>(hz));
        gLastCmd0Status = -3;
        gLastCmd8Status = -3;
        SD.end();
        gSdSessionActive = false;
        delayMicroseconds(kSdCsPostDelayUs);
        return false;
    }
    gLastCmd0Status = 0;
    gLastCmd8Status = 0;
    const uint8_t cardType = SD.cardType();
    const uint64_t cardBytes = SD.cardSize();
    const unsigned long cardMb = static_cast<unsigned long>(cardBytes / (1024ULL * 1024ULL));
    Serial.printf("Flic: SD detected type=%u size=%luMB at %lu Hz\n",
                  static_cast<unsigned int>(cardType),
                  cardMb,
                  static_cast<unsigned long>(hz));
    delayMicroseconds(kSdCsPostDelayUs);
    return true;
}

bool ensureMounted() {
    return g_sdMounted;
}
}  // namespace

namespace SdManager {

void configureBus() {
    SPI.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, kSdChipSelectPin);
}

bool mount() {
    if (g_sdMounted) {
        Serial.println("Flic: SD card already mounted.");
        return true;
    }

    if (gMountAttempted) {
        const unsigned long now = millis();
        if ((now - gLastMountAttemptMs) < kMountRetryIntervalMs) {
            return false;
        }
        Serial.println("Flic: retrying SD mount after prior failure...");
    }

    gMountAttempted = true;
    gLastMountAttemptMs = millis();

    Serial.println("Flic: mounting SD card...");
    Serial.printf("Flic: SD bus pins sck=%d miso=%d mosi=%d cs=%d\n",
                  kSdClockPin,
                  kSdMisoPin,
                  kSdMosiPin,
                  static_cast<int>(kSdChipSelectPin));
    bool mounted = false;
    uint32_t attemptCount = 0;
    for (uint32_t hz : kSdMountFrequencies) {
        ++attemptCount;
        gLastAttemptCount = attemptCount;
        if (tryMountAtFrequency(hz)) {
            mounted = true;
            Serial.printf("Flic: SD mounted at %lu Hz.\n", static_cast<unsigned long>(hz));
            break;
        }
    }

    if (!mounted) {
        g_sdMounted = false;
        gLastMountError = "init_failed_all_frequencies";
        Serial.printf("Flic: SD mount attempts exhausted (%lu tries).\n", static_cast<unsigned long>(attemptCount));
        Serial.println("Flic: SD MOUNT FAILED - running in NO-SD fallback mode.");
        return false;
    }

    g_sdMounted = true;
    gLastMountError = "none";
    Serial.println("Flic: SD mounted successfully.");

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

    return true;
}

bool isMounted() {
    return g_sdMounted;
}

bool ensureDirectory(const char* path) {
    if (!ensureMounted() || path == nullptr || path[0] == '\0') {
        return false;
    }
    if (SD.exists(path)) {
        return true;
    }
    return SD.mkdir(path);
}

bool fileExists(const char* path) {
    return ensureMounted() && SD.exists(path);
}

String listFiles(const char* path) {
    if (!ensureMounted()) {
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
    if (!ensureMounted() || !SD.exists(path)) {
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
    if (!ensureMounted()) {
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

const char* lastMountError() {
    return gLastMountError;
}

uint32_t lastInitFrequencyHz() {
    return gLastInitFrequencyHz;
}

uint8_t lastSpiMode() {
    return kSdSpiMode;
}

uint32_t lastAttemptCount() {
    return gLastAttemptCount;
}

uint16_t csPreDelayUs() {
    return kSdCsPreDelayUs;
}

uint16_t csPostDelayUs() {
    return kSdCsPostDelayUs;
}

int lastCmd0Status() {
    return gLastCmd0Status;
}

int lastCmd8Status() {
    return gLastCmd8Status;
}

}  // namespace SdManager
}  // namespace Flic
