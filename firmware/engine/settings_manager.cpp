#include "settings_manager.h"

#include "../subsystems/sd_manager.h"

namespace Flic {
namespace {
constexpr const char* kSettingsPath = "/Flic/config/settings.json";
}

bool SettingsManager::begin() {
    if (!SdManager::isMounted()) {
        return false;
    }

    SdManager::ensureDirectory(SdManager::rootDir());
    SdManager::ensureDirectory(SdManager::configDir());

    JsonDocument existing;
    if (SdManager::readJSON(kSettingsPath, existing) && existing.is<JsonObject>()) {
        return true;
    }

    JsonDocument defaults;
    defaults["_schema"] = "flic.settings.v1";
    defaults["voice"] = JsonObject();
    defaults["runtime"] = JsonObject();
    defaults["updated_at"] = 0;
    return SdManager::writeJSON(kSettingsPath, defaults);
}

bool SettingsManager::load(JsonDocument& document) const {
    document.clear();
    return SdManager::readJSON(kSettingsPath, document);
}

bool SettingsManager::save(const JsonDocument& document) const {
    return SdManager::writeJSON(kSettingsPath, document);
}

const char* SettingsManager::settingsPath() const {
    return kSettingsPath;
}

}  // namespace Flic
