#include "sd_manager.h"


#include <Arduino.h>
#include <ArduinoJson.h>

#include <SD.h>
#include <SPI.h>

#include <vector>

namespace Flic {
bool g_sdMounted = false;

namespace {
constexpr const char* kSdMountPoint = "/sdcard";
constexpr const char* kSdBackendName = "SD_SPI";
constexpr int kSpiSckPin  = 36; // M5Stack CoreS3 official SD SCK
constexpr int kSpiMisoPin = 35; // M5Stack CoreS3 official SD MISO
constexpr int kSpiMosiPin = 37; // M5Stack CoreS3 official SD MOSI
constexpr int kSpiCsPin   = 4;  // M5Stack CoreS3 official SD CS
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
constexpr uint16_t kSdCsPreDelayUs = 0;
constexpr uint16_t kSdCsPostDelayUs = 0;
constexpr uint8_t kSdSpiMode = SPI_MODE0;

struct MountProfile {
    uint32_t frequency;
    const char* label;
    constexpr MountProfile(uint32_t freq, const char* lbl) : frequency(freq), label(lbl) {}
};
constexpr MountProfile kMountProfiles[] = {
    {400000, "400kHz"},
    {1000000, "1MHz"},
    {4000000, "4MHz"},
    {10000000, "10MHz"},
    {16000000, "16MHz"},
    {25000000, "25MHz"},
};

uint32_t gLastInitFrequencyHz = 0;
uint32_t gLastAttemptCount = 0;
int gLastCmd0Status = -2;
int gLastCmd8Status = -2;

bool tryMountProfile(const MountProfile& profile) {
    gLastInitFrequencyHz = profile.frequency;
    gLastCmd0Status = -2;
    gLastCmd8Status = -2;
    if (gSdSessionActive) {
        Serial.println("[SD] Ending previous SD session...");
        SD.end();
        gSdSessionActive = false;
        delay(8);
    }
    pinMode(kSpiCsPin, OUTPUT);
    digitalWrite(kSpiCsPin, HIGH);
    delayMicroseconds(kSdCsPreDelayUs);
    SPI.begin(kSpiSckPin, kSpiMisoPin, kSpiMosiPin, kSpiCsPin);
    for (int i = 0; i < 16; ++i) {
        SPI.beginTransaction(SPISettings(400000, MSBFIRST, kSdSpiMode));
        digitalWrite(kSpiCsPin, LOW);
        for (int j = 0; j < 8; ++j) {
            SPI.transfer(0xFF);
        }
        digitalWrite(kSpiCsPin, HIGH);
        SPI.endTransaction();
    }
    delayMicroseconds(kSdCsPostDelayUs);
    Serial.printf("[SD] SD_SPI mount attempt (%s) at %s...\n", profile.label, kSdMountPoint);
    if (!SD.begin(kSpiCsPin, SPI, profile.frequency)) {
        Serial.printf("[SD] ERROR: SD_SPI begin failed (%s)\n", profile.label);
        gLastCmd0Status = -1;
        gLastCmd8Status = -1;
        return false;
    }
    gSdSessionActive = true;
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.printf("[SD] ERROR: SD_SPI card type NONE (%s)\n", profile.label);
        gLastCmd0Status = -3;
        gLastCmd8Status = -3;
        SD.end();
        gSdSessionActive = false;
        return false;
    }
    gLastCmd0Status = 0;
    gLastCmd8Status = 0;
    uint64_t cardBytes = SD.cardSize();
    unsigned long cardMb = static_cast<unsigned long>(cardBytes / (1024ULL * 1024ULL));
    Serial.printf("[SD] SD_SPI detected type=%u size=%luMB (%s)\n", static_cast<unsigned int>(cardType), cardMb, profile.label);
    return true;
}

bool ensureMounted() {
    return g_sdMounted;
}
}  // namespace

namespace SdManager {

void configureBus() {
    // SPI-only SD bus for CoreS3. No SD_MMC.
}

bool mount() {
    if (g_sdMounted) {
        Serial.println("[SD] SD card already mounted.");
        return true;
    }
    if (gMountAttempted) {
        const unsigned long now = millis();
        if ((now - gLastMountAttemptMs) < kMountRetryIntervalMs) {
            Serial.println("[SD] Mount retry interval not met, skipping.");
            return false;
        }
        Serial.println("[SD] Retrying SD mount after prior failure...");
    }
    gMountAttempted = true;
    gLastMountAttemptMs = millis();
    Serial.printf("[SD] Mounting SD card with %s at %s...\n", kSdBackendName, kSdMountPoint);
    bool mounted = false;
    uint32_t attemptCount = 0;
    for (const MountProfile& profile : kMountProfiles) {
        ++attemptCount;
        gLastAttemptCount = attemptCount;
        if (tryMountProfile(profile)) {
            mounted = true;
            Serial.printf("[SD] SD mounted successfully via %s (%s).\n", kSdBackendName, profile.label);
            break;
        }
    }
    if (!mounted) {
        g_sdMounted = false;
        gLastMountError = "init_failed_all_frequencies";
        Serial.printf("[SD] ERROR: SD mount attempts exhausted (%lu tries).\n", static_cast<unsigned long>(attemptCount));
        Serial.println("[SD] SD MOUNT FAILED - running in NO-SD fallback mode.");
        return false;
    }
    g_sdMounted = true;
    gLastMountError = "none";
    Serial.printf("[SD] SD mounted successfully at %s.\n", kSdMountPoint);
    SD.mkdir(kFlicRootDir);
    SD.mkdir(kFlicVoicesDir);
    SD.mkdir(kFlicMemoryDir);
    SD.mkdir(kFlicConfigDir);
    SD.mkdir(kFlicLogsDir);
    SD.mkdir(kFlicAnimationsDir);
    SD.mkdir(kFlicSoundsDir);
    SD.mkdir(kFlicFaceDir);
    SD.mkdir(kAiRootDir);
    SD.mkdir(kAiMemoryDir);
    SD.mkdir(kAiAnimationsDir);
    Serial.println("[SD] Running SD self-repair...");
    runSelfRepair();
    return true;
}

void verify() {
    Serial.println("Flic: SD verification: START");
    if (!isMounted()) {
        Serial.println("Flic: SD verification: NOT MOUNTED");
        return;
    }
    // Check all critical directories
    const char* dirs[] = {
        kFlicRootDir, kFlicVoicesDir, kFlicMemoryDir, kFlicConfigDir, kFlicLogsDir, kFlicAnimationsDir, kFlicSoundsDir, kFlicFaceDir, kAiRootDir, kAiMemoryDir, kAiAnimationsDir
    };
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        if (SD.exists(dirs[i])) {
            Serial.printf("Flic: SD verify: dir OK: %s\n", dirs[i]);
        } else {
            Serial.printf("Flic: SD verify: dir MISSING: %s\n", dirs[i]);
        }
    }
    // Check a few critical files
    const char* files[] = {
        "/Flic/config/config.json",
        "/Flic/memory/memory_index.json"
    };
    for (size_t i = 0; i < sizeof(files)/sizeof(files[0]); ++i) {
        if (SD.exists(files[i])) {
            Serial.printf("Flic: SD verify: file OK: %s\n", files[i]);
        } else {
            Serial.printf("Flic: SD verify: file MISSING: %s\n", files[i]);
        }
    }
    Serial.println("Flic: SD verification: END");
}

void printBootSummary() {
    Serial.println("\n========== Flic Boot Summary ==========");
    Serial.printf("SD Mounted: %s\n", isMounted() ? "YES" : "NO");
    Serial.printf("SD Backend: %s\n", storageBackend());
    Serial.printf("SD Mount Point: %s\n", mountPoint());
    Serial.printf("Last Mount Error: %s\n", lastMountError());
    Serial.printf("Last Init Freq: %lu Hz\n", (unsigned long)lastInitFrequencyHz());
    Serial.printf("Last Attempt Count: %lu\n", (unsigned long)lastAttemptCount());
    Serial.printf("Last CMD0 Status: %d\n", lastCmd0Status());
    Serial.printf("Last CMD8 Status: %d\n", lastCmd8Status());
    Serial.println("Directories:");
    const char* dirs[] = {
        kFlicRootDir, kFlicVoicesDir, kFlicMemoryDir, kFlicConfigDir, kFlicLogsDir, kFlicAnimationsDir, kFlicSoundsDir, kFlicFaceDir, kAiRootDir, kAiMemoryDir, kAiAnimationsDir
    };
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        Serial.printf("  [%s] %s\n", SD.exists(dirs[i]) ? "OK" : "MISSING", dirs[i]);
    }
    Serial.println("Files:");
    const char* files[] = {
        "/Flic/config/config.json",
        "/Flic/memory/memory_index.json"
    };
    for (size_t i = 0; i < sizeof(files)/sizeof(files[0]); ++i) {
        Serial.printf("  [%s] %s\n", SD.exists(files[i]) ? "OK" : "MISSING", files[i]);
    }
    Serial.println("========================================\n");
}

void runSelfRepair() {
    if (!isMounted()) {
        return;
    }
    Serial.println("Flic: SD self-repair: START");
    struct DirEntry { const char* path; bool created; bool failed; };
    DirEntry dirs[] = {
        {kFlicRootDir, false, false},
        {kFlicVoicesDir, false, false},
        {kFlicMemoryDir, false, false},
        {kFlicConfigDir, false, false},
        {kFlicLogsDir, false, false},
        {kFlicAnimationsDir, false, false},
        {kFlicSoundsDir, false, false},
        {kFlicFaceDir, false, false},
    };
    int createdCount = 0, failedCount = 0;
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        if (SD.exists(dirs[i].path)) {
            Serial.printf("Flic: SD self-repair: dir ok: %s\n", dirs[i].path);
        } else {
            if (SD.mkdir(dirs[i].path)) {
                dirs[i].created = true;
                ++createdCount;
                Serial.printf("Flic: SD self-repair: dir created: %s\n", dirs[i].path);
            } else {
                dirs[i].failed = true;
                ++failedCount;
                Serial.printf("Flic: SD self-repair: dir FAILED: %s\n", dirs[i].path);
            }
        }
    }
    struct FileEntry { const char* path; const char* minimalJson; bool created; bool failed; };
    FileEntry files[] = {
        {"/Flic/config/config.json", "{}", false, false},
        {"/Flic/memory/memory_index.json", "{}", false, false},
    };
    int fileCreated = 0, fileFailed = 0;
    for (size_t i = 0; i < sizeof(files)/sizeof(files[0]); ++i) {
        if (SD.exists(files[i].path)) {
            Serial.printf("Flic: SD self-repair: file ok: %s\n", files[i].path);
        } else {
            File f = SD.open(files[i].path, FILE_WRITE);
            if (f) {
                f.print(files[i].minimalJson);
                f.close();
                files[i].created = true;
                ++fileCreated;
                Serial.printf("Flic: SD self-repair: file created: %s\n", files[i].path);
            } else {
                files[i].failed = true;
                ++fileFailed;
                Serial.printf("Flic: SD self-repair: file FAILED: %s\n", files[i].path);
            }
        }
    }
    Serial.printf("Flic: SD self-repair: SUMMARY: %d dirs created, %d dir failures, %d files created, %d file failures\n", createdCount, failedCount, fileCreated, fileFailed);
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

const char* mountPoint() {
    return kSdMountPoint;
}

const char* storageBackend() {
    return kSdBackendName;
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
