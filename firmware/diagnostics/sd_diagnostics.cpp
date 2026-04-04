#include "sd_diagnostics.h"

#include "../subsystems/sd_manager.h"

namespace Flic {
namespace SdDiagnostics {

namespace {
constexpr const char* kBootPath = "/Flic/boot/";
constexpr const char* kFacePath = "/Flic/animations/face/default/";
constexpr const char* kSoundPath = "/Flic/sounds/";
}

void logSdStatus() {
    const bool mounted = SdManager::isMounted();
    Serial.printf("[SD] mounted=%s\n", mounted ? "true" : "false");
    Serial.printf("[SD] boot_path=%s\n", kBootPath);
    Serial.printf("[SD] face_path=%s\n", kFacePath);
    Serial.printf("[SD] sound_path=%s\n", kSoundPath);
    Serial.printf("[SD] init_hz=%lu\n", static_cast<unsigned long>(SdManager::lastInitFrequencyHz()));
    Serial.printf("[SD] spi_mode=%u\n", static_cast<unsigned int>(SdManager::lastSpiMode()));
    Serial.printf("[SD] cs_pre_delay_us=%u\n", static_cast<unsigned int>(SdManager::csPreDelayUs()));
    Serial.printf("[SD] cs_post_delay_us=%u\n", static_cast<unsigned int>(SdManager::csPostDelayUs()));
    Serial.printf("[SD] attempt_count=%lu\n", static_cast<unsigned long>(SdManager::lastAttemptCount()));
    Serial.printf("[SD] cmd0_status=%d\n", SdManager::lastCmd0Status());
    Serial.printf("[SD] cmd8_status=%d\n", SdManager::lastCmd8Status());
    if (!mounted) {
        Serial.printf("[SD] last_mount_error=%s\n", SdManager::lastMountError());
        Serial.println("[SD] fallback mode active (built-in boot/face/sound behavior)");
    }
}

void logFaceLoad(const String& filename, const String& fullPath) {
    if (!SdManager::isMounted()) {
        return;
    }
    Serial.printf("[SD][FACE] file=%s path=%s\n", filename.c_str(), fullPath.c_str());
}

void logBootAnimationFrame(const String& filename) {
    if (!SdManager::isMounted()) {
        return;
    }
    Serial.printf("[SD][BOOT] frame=%s\n", filename.c_str());
}

void logSoundPlay(const String& filename) {
    if (!SdManager::isMounted()) {
        return;
    }
    Serial.printf("[SD][SOUND] play=%s\n", filename.c_str());
}

}  // namespace SdDiagnostics
}  // namespace Flic
