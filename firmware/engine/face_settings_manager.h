#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {

class FaceEngine;

struct FaceSettings {
    String activeStyle = "default";
    float blinkSpeed = 1.0f;
    bool idleEnabled = true;
    float glowIntensity = 0.8f;
    String eyeColor = "#AEE6FF";
    bool aiCanModify = false;
    bool aiCanCreate = false;
};

class FaceSettingsManager {
public:
    bool begin(FaceEngine* faceEngine);
    bool load(FaceSettings& settings);
    bool save(const FaceSettings& settings) const;
    bool apply(const FaceSettings& settings);
    const FaceSettings& current() const;
    const char* settingsPath() const;

private:
    bool readDocument(JsonDocument& document) const;
    bool writeDocument(const FaceSettings& settings) const;
    FaceSettings settings_;
    FaceEngine* faceEngine_ = nullptr;
};

}  // namespace Flic
