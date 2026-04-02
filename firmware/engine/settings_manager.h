#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {

class SettingsManager {
public:
    bool begin();
    bool load(JsonDocument& document) const;
    bool save(const JsonDocument& document) const;
    const char* settingsPath() const;
};

}  // namespace Flic
