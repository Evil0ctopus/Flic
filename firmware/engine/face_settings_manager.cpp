#include "face_settings_manager.h"

#include "face_engine.h"
#include "../subsystems/sd_manager.h"

namespace Flic {
namespace {
constexpr const char* kFaceSettingsPath = "/Flic/config/face.json";
}

bool FaceSettingsManager::begin(FaceEngine* faceEngine) {
    faceEngine_ = faceEngine;
    SdManager::ensureDirectory(SdManager::configDir());
    FaceSettings settings;
    if (!load(settings)) {
        settings_ = FaceSettings{};
        save(settings_);
        return false;
    }
    return apply(settings);
}

bool FaceSettingsManager::load(FaceSettings& settings) {
    JsonDocument document;
    if (!readDocument(document)) {
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    if (root.isNull()) {
        return false;
    }

    settings.activeStyle = root["active_style"] | settings.activeStyle;
    settings.blinkSpeed = root["blink_speed"] | settings.blinkSpeed;
    settings.idleEnabled = root["idle_enabled"] | settings.idleEnabled;
    settings.glowIntensity = root["glow_intensity"] | settings.glowIntensity;
    settings.eyeColor = root["eye_color"] | settings.eyeColor;
    settings.personalityIntensity = root["personality_intensity"] | settings.personalityIntensity;
    if (root["emotion_animation_map"].is<JsonObject>()) {
        String mapPayload;
        serializeJson(root["emotion_animation_map"], mapPayload);
        settings.emotionAnimationMapJson = mapPayload;
    } else {
        settings.emotionAnimationMapJson = root["emotion_animation_map"] | settings.emotionAnimationMapJson;
    }
    settings.aiCanModify = root["ai_can_modify"] | settings.aiCanModify;
    settings.aiCanCreate = root["ai_can_create"] | settings.aiCanCreate;

    settings_ = settings;
    return true;
}

bool FaceSettingsManager::save(const FaceSettings& settings) const {
    return writeDocument(settings);
}

bool FaceSettingsManager::apply(const FaceSettings& settings) {
    settings_ = settings;
    if (faceEngine_ != nullptr) {
        faceEngine_->applySettings(settings_);
    }
    return save(settings_);
}

const FaceSettings& FaceSettingsManager::current() const {
    return settings_;
}

const char* FaceSettingsManager::settingsPath() const {
    return kFaceSettingsPath;
}

bool FaceSettingsManager::readDocument(JsonDocument& document) const {
    document.clear();
    if (!SdManager::isMounted()) {
        return false;
    }
    if (SdManager::readJSON(kFaceSettingsPath, document)) {
        return true;
    }

    document["active_style"] = "default";
    document["blink_speed"] = 1.0f;
    document["idle_enabled"] = true;
    document["glow_intensity"] = 0.8f;
    document["eye_color"] = "#AEE6FF";
    document["personality_intensity"] = "balanced";
    document["emotion_animation_map"] = serialized("{}");
    document["ai_can_modify"] = false;
    document["ai_can_create"] = false;
    return true;
}

bool FaceSettingsManager::writeDocument(const FaceSettings& settings) const {
    if (!SdManager::isMounted()) {
        return false;
    }

    JsonDocument document;
    document["_schema"] = "flic.face_settings.v1";
    document["active_style"] = settings.activeStyle;
    document["blink_speed"] = settings.blinkSpeed;
    document["idle_enabled"] = settings.idleEnabled;
    document["glow_intensity"] = settings.glowIntensity;
    document["eye_color"] = settings.eyeColor;
    document["personality_intensity"] = settings.personalityIntensity;
    String mapPayload = settings.emotionAnimationMapJson;
    mapPayload.trim();
    if (mapPayload.length() == 0) {
        mapPayload = "{}";
    }
    JsonDocument mapDocument;
    const DeserializationError mapError = deserializeJson(mapDocument, mapPayload);
    if (!mapError && mapDocument.is<JsonObject>()) {
        document["emotion_animation_map"] = mapDocument.as<JsonObject>();
    } else {
        document["emotion_animation_map"] = serialized("{}");
    }
    document["ai_can_modify"] = settings.aiCanModify;
    document["ai_can_create"] = settings.aiCanCreate;
    document["updated_at"] = millis();
    return SdManager::writeJSON(kFaceSettingsPath, document);
}

}  // namespace Flic
