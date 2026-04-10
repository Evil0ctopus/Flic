#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <vector>

#include "config.h"
#include "config_webui.h"
#include "engine/emotion_engine.h"
#include "engine/device_learning.h"
#include "engine/communication_engine.h"
#include "engine/touch_input.h"
#include "engine/touch_engine.h"
#include "engine/voice_engine.h"
#include "engine/asr_engine.h"
#include "engine/webui_engine.h"
#include "engine/audio_input.h"
#include "engine/audio_engine.h"
#include "engine/audio_output.h"
#include "engine/camera_engine.h"
#include "engine/environment_light_engine.h"
#include "engine/imu_engine.h"
#include "engine/learning_engine.h"
#include "engine/face_engine.h"
#include "engine/face_settings_manager.h"
#include "engine/milestone_engine.h"
#include "engine/animation_engine.h"
#include "engine/idle_behavior.h"
#include "engine/memory_manager.h"
#include "engine/proposal_system.h"
#include "engine/personality_engine.h"
#include "engine/settings_manager.h"
#include "subsystems/light_engine.h"
#include "subsystems/sd_manager.h"
#include "subsystems/usb_engine.h"
#include "diagnostics/debug_log.h"
#include "diagnostics/sd_diagnostics.h"
#include "diagnostics/webui_event_hook.h"
#include "ui/personality_ui.h"
#include "ui/text_bubbles.h"

namespace {
struct RuntimeSettings {
    uint8_t brightness = 45;
    uint8_t volume = 180;
    String voiceStyle = "natural";
    float personalityEnergy = 0.5f;
    float personalityCuriosity = 0.5f;
    float personalityPatience = 0.5f;
    float emotionBias = 0.0f;
    float animationSpeed = 1.0f;
    bool debugEnabled = false;
    bool traceEnabled = false;
    bool voiceInputEnabled = true;
    bool autonomyEnabled = true;
    bool webHeartbeatEnabled = true;
    bool imuEventsEnabled = true;
    bool usbEventsEnabled = true;
    String voiceModel = "";
    float voiceSpeed = 1.0f;
    float voicePitch = 1.0f;
    float voiceClarity = 1.0f;
    bool fallbackVoiceEnabled = true;
};

Flic::AnimationEngine animationEngine;
Flic::LightEngine lightEngine;
Flic::MemoryManager memoryManager;
Flic::EmotionEngine emotionEngine;
Flic::ProposalSystem proposalSystem;
Flic::PersonalityUI personalityUi;
Flic::IdleBehavior idleBehavior;
Flic::UsbEngine usbEngine;
Flic::DeviceLearning deviceLearning;
Flic::LearningEngine learningEngine;
Flic::FaceEngine faceEngine;
Flic::FaceSettingsManager faceSettingsManager;
Flic::MilestoneEngine milestoneEngine;
Flic::TextBubbles textBubbles;
Flic::CommunicationEngine communicationEngine;
Flic::TouchInput touchInput;
Flic::TouchEngine touchEngine;
Flic::AudioInput audioInput;
Flic::AudioOutput audioOutput;
Flic::VoiceEngine voiceEngine;
Flic::AsrEngine asrEngine;
Flic::WebUiEngine webUiEngine;
Flic::CameraEngine cameraEngine;
Flic::ImuEngine imuEngine;
Flic::EnvironmentLightEngine environmentLightEngine;
Flic::SettingsManager settingsManager;
RuntimeSettings runtimeSettings;
bool gUseSdFallbackFace = false;
unsigned long gLastFallbackFaceDrawMs = 0;
constexpr const char* kAnimationName = "flic_first_animation.json";
constexpr const char* kFirstAnimationFlagPath = "/Flic/memory/first_animation_created.flag";
constexpr const char* kUsbCdcLabel = "usb_cdc";
constexpr const char* kDeviceIdPrefix = "DEVICE_ID:";
constexpr size_t kDeviceIdPrefixLength = 10;
constexpr const char* kCapabilitiesPrefix = "CAPABILITIES:";
constexpr size_t kCapabilitiesPrefixLength = 13;
constexpr const char* kCapabilitiesShortPrefix = "CAPS:";
constexpr size_t kCapabilitiesShortPrefixLength = 5;
constexpr const char* kFeaturesPrefix = "FEATURES:";
constexpr size_t kFeaturesPrefixLength = 9;
constexpr const char* kEventTouch = "touch";
constexpr const char* kEventCamera = "camera";
constexpr const char* kEventImu = "imu";
constexpr const char* kEventLight = "light";
constexpr const char* kEventDeviceConnected = "device_connected";
constexpr const char* kEventDeviceIdentified = "device_identified";
constexpr const char* kEventDeviceCapabilities = "device_capabilities";
constexpr const char* kEventUsbText = "usb_text";
constexpr const char* kEventNewPattern = "new_pattern";
constexpr const char* kEventCommandSent = "command_sent";
constexpr const char* kEventCommandRejected = "command_rejected";
constexpr const char* kHandWaveEvent = "hand_wave";
constexpr uint8_t kMaxUsbMessagesPerLoop = 6;
constexpr unsigned long kMotionNotifyCooldownMs = 2200;
constexpr unsigned long kImuNotifyCooldownMs = 2200;
constexpr unsigned long kWebHeartbeatMs = 1000;
constexpr bool kEmergencyMinimalLoop = false;
constexpr bool kDisableVoiceRuntime = false;
constexpr bool kDisableVoiceInputHandling = false;
constexpr bool kPreferMicStabilityOverSpeaker = false;
constexpr bool kEnableVoiceLiteMode = true;
constexpr bool kVoiceLiteSuppressSoundEvents = true;
constexpr unsigned long kVoiceLiteUpdateIntervalMs = 120;
constexpr unsigned long kVoiceLiteInputPollIntervalMs = 120;
constexpr unsigned long kVoiceLiteFeedbackCooldownMs = 700;
constexpr bool kVoiceLiteAutoSentenceReplies = true;
constexpr bool kDisableImuEngine = false;
constexpr bool kEnableUsbRuntime = true;
constexpr bool kEnableMilestoneRuntime = true;
constexpr bool kSafeBootMode = false;
constexpr bool kShowWebUiTextBubbles = false;
constexpr bool kRunBootExpressionDemo = false;
constexpr bool kRunCreatureVoiceSelfTestOnBoot = true;
constexpr bool kRunCreatureVoiceSelfTestInLoop = true;
constexpr bool kRunSdDeepVerifyOnBoot = false;

unsigned long lastLoopMs = 0;
unsigned long lastMotionNotifyMs = 0;
unsigned long lastImuNotifyMs = 0;
unsigned long lastWebHeartbeatMs = 0;
unsigned long lastVoiceUpdateMs = 0;
unsigned long lastVoiceInputPollMs = 0;
unsigned long lastVoiceFeedbackMs = 0;

String lastImuEventName;
String lastImuEventDetail;
String lastLightEventName;
String lastLightEventDetail;
String lastTouchGesture;
String lastTouchMeaning;
String lastCameraEventName;
String lastCameraEventDetail;

struct LearnedTopic {
    String word;
    uint16_t score = 0;
};

constexpr unsigned long kAutonomySpeakCooldownMs = 18000;
constexpr unsigned long kAutonomyNeedTickMs = 2000;
constexpr unsigned long kAutonomyLonelyAfterMs = 30000;
constexpr unsigned long kAutonomyBoredAfterMs = 40000;
unsigned long lastAutonomySpeakMs = 0;
unsigned long lastAutonomyNeedTickMs = 0;
unsigned long lastTouchMs = 0;
unsigned long lastVoiceMs = 0;
unsigned long lastMotionMs = 0;
unsigned long bootStartMs = 0;
unsigned long postInitVoiceTestUntilMs = 0;
unsigned long lastWebUiHintBubbleMs = 0;
LearnedTopic learnedTopics[3];
float needConnection = 0.25f;
float needPlay = 0.20f;
float needCalm = 0.12f;
float needLight = 0.10f;
constexpr bool kTestMinimalBoot = false;

String composeWebUiHelpMessage() {
    if (!webUiEngine.isReady()) {
        return "WebUI unavailable\nCheck WiFi credentials";
    }

    if (webUiEngine.apMode()) {
        String msg = "AP Mode\nSSID: Flic-Setup\nPW: flic-dev-only\nURL: http://";
        msg += webUiEngine.localIp().toString();
        msg += ":";
        msg += String(Flic::kWebUiHttpPort);
        return msg;
    }

    String msg = "WebUI: http://";
    msg += webUiEngine.localIp().toString();
    msg += ":";
    msg += String(Flic::kWebUiHttpPort);
    msg += "\nSetup AP: Flic-Setup";
    msg += "\nPW: flic-dev-only";
    msg += "\nAP URL: http://192.168.4.1";
    return msg;
}

String composeVoiceModelsJsonArray() {
    const std::vector<String> voices = audioOutput.listVoices();
    String payload = "[";
    for (size_t i = 0; i < voices.size(); ++i) {
        if (i > 0) {
            payload += ",";
        }
        payload += "\"";
        payload += voices[i];
        payload += "\"";
    }
    payload += "]";
    return payload;
}

String composeFaceSettingsJson() {
    String payload = "{\"ok\":true,\"settings\":";
    payload += faceEngine.settingsJson();
    payload += ",\"styles\":";
    payload += faceEngine.stylesJson();
    payload += "}";
    return payload;
}

String composeFaceStylesJson() {
    return faceEngine.stylesJson();
}

String composeFaceAnimationsJson(const String& style) {
    return faceEngine.animationsJson(style);
}

String composeFaceAnimationsCatalogJson() {
    return faceEngine.animationsCatalogJson();
}

String composeFaceValidateJson() {
    return faceEngine.validateAnimationSetJson();
}

String composeFaceTelemetryJson() {
    return faceEngine.telemetryJson();
}

String composeFaceSnapshotPath() {
    return faceEngine.currentFramePath();
}

String composeWebUiStatus() {
    String payload = "{\"ok\":true,\"type\":\"status\",\"uptime_ms\":";
    payload += String(millis());
    payload += ",\"emotion\":\"";
    payload += emotionEngine.getEmotion();
    payload += "\",\"animation_playing\":";
    payload += animationEngine.isPlaying() ? "true" : "false";
    payload += ",\"events\":";
    payload += String(memoryManager.eventCount());
    payload += ",\"settings\":{\"brightness\":";
    payload += String(runtimeSettings.brightness);
    payload += ",\"volume\":";
    payload += String(runtimeSettings.volume);
    payload += ",\"voice_style\":\"";
    payload += runtimeSettings.voiceStyle;
    payload += "\",\"voice_model\":\"";
    payload += runtimeSettings.voiceModel;
    payload += "\",\"voice_speed\":";
    payload += String(runtimeSettings.voiceSpeed, 2);
    payload += ",\"voice_pitch\":";
    payload += String(runtimeSettings.voicePitch, 2);
    payload += ",\"voice_clarity\":";
    payload += String(runtimeSettings.voiceClarity, 2);
    payload += ",\"fallback_voice\":";
    payload += runtimeSettings.fallbackVoiceEnabled ? "true" : "false";
    payload += ",\"available_voices\":";
    payload += composeVoiceModelsJsonArray();
    payload += ",\"personality\":{\"energy\":";
    payload += String(runtimeSettings.personalityEnergy, 2);
    payload += ",\"curiosity\":";
    payload += String(runtimeSettings.personalityCuriosity, 2);
    payload += ",\"patience\":";
    payload += String(runtimeSettings.personalityPatience, 2);
    payload += "},\"emotion_bias\":";
    payload += String(runtimeSettings.emotionBias, 2);
    payload += ",\"animation_speed\":";
    payload += String(runtimeSettings.animationSpeed, 2);
    payload += ",\"debug\":{\"enabled\":";
    payload += runtimeSettings.debugEnabled ? "true" : "false";
    payload += ",\"trace\":";
    payload += runtimeSettings.traceEnabled ? "true" : "false";
    payload += ",\"level\":";
    payload += String(Flic::Debug::runtimeLogLevel());
    payload += "}},\"runtime\":{\"voice_input\":";
    payload += runtimeSettings.voiceInputEnabled ? "true" : "false";
    payload += ",\"autonomy\":";
    payload += runtimeSettings.autonomyEnabled ? "true" : "false";
    payload += ",\"web_heartbeat\":";
    payload += runtimeSettings.webHeartbeatEnabled ? "true" : "false";
    payload += ",\"imu_events\":";
    payload += runtimeSettings.imuEventsEnabled ? "true" : "false";
    payload += ",\"usb_events\":";
    payload += runtimeSettings.usbEventsEnabled ? "true" : "false";
    payload += "}}";
    payload += "}";
    return payload;
}

String composeWebUiSensors() {
    String payload = "{\"ok\":true,\"type\":\"sensors\",\"imu\":{\"event\":\"";
    payload += lastImuEventName;
    payload += "\",\"detail\":\"";
    payload += lastImuEventDetail;
    payload += "\"},\"light\":{\"event\":\"";
    payload += lastLightEventName;
    payload += "\",\"detail\":\"";
    payload += lastLightEventDetail;
    payload += "\"},\"touch\":{\"gesture\":\"";
    payload += lastTouchGesture;
    payload += "\",\"meaning\":\"";
    payload += lastTouchMeaning;
    payload += "\"},\"camera\":{\"event\":\"";
    payload += lastCameraEventName;
    payload += "\",\"detail\":\"";
    payload += lastCameraEventDetail;
    payload += "\"}}";
    return payload;
}

String composeWebUiEngines() {
    const bool usbReady = usbEngine.connectedDeviceId().length() > 0;
    String payload = "{\"ok\":true,\"type\":\"engines\",\"touch\":true,\"imu\":true,\"light\":true,\"camera\":true,\"audio\":true,\"usb\":";
    payload += usbReady ? "true" : "false";
    payload += ",\"communication\":true,\"emotion\":true,\"animation\":true,\"milestone\":true,\"learning\":true}";
    return payload;
}

void webUiLog(const String& level, const String& message) {
    String payload = "{\"level\":\"";
    payload += level;
    payload += "\",\"message\":\"";
    payload += message;
    payload += "\"}";
    webUiEngine.sendEvent("system_log", payload);
}

void sendWebUiHookEvent(const String& type, const String& payload) {
    webUiEngine.sendEvent(type, payload);
}

void onTtsAmplitudeEnvelope(float amplitude, void* context) {
    (void)context;
    faceEngine.setSpeakingAmplitude(amplitude);
}

void applyRuntimeSettings() {
    if (runtimeSettings.brightness < 10) {
        runtimeSettings.brightness = 45;
    }
    lightEngine.setBrightness(runtimeSettings.brightness);
    if (runtimeSettings.volume < 25) {
        runtimeSettings.volume = 120;
    }
    audioOutput.setVolume(runtimeSettings.volume);
    audioOutput.setVoiceStyle(runtimeSettings.voiceStyle);
    audioOutput.setVoiceTuning(runtimeSettings.voiceSpeed, runtimeSettings.voicePitch, runtimeSettings.voiceClarity);
    audioOutput.setFallbackVoiceEnabled(runtimeSettings.fallbackVoiceEnabled);
    if (runtimeSettings.voiceModel.length() > 0) {
        audioOutput.setActiveVoiceModel(runtimeSettings.voiceModel);
    }
    personalityUi.setPersonality(runtimeSettings.personalityEnergy, runtimeSettings.personalityCuriosity,
                                 runtimeSettings.personalityPatience);
    emotionEngine.setEmotionBias(runtimeSettings.emotionBias);
    animationEngine.setPlaybackSpeed(runtimeSettings.animationSpeed);
    faceEngine.enableContextRules(true);
    faceEngine.enableMicroExpressions(true);
    if (runtimeSettings.personalityEnergy > 0.72f) {
        faceEngine.setPersonalityState("excited");
    } else if (runtimeSettings.personalityCuriosity > 0.72f) {
        faceEngine.setPersonalityState("curious");
    } else if (runtimeSettings.personalityPatience > 0.72f) {
        faceEngine.setPersonalityState("focused");
    } else {
        faceEngine.setPersonalityState("neutral");
    }
    faceEngine.setMicroExpressionIntensity((runtimeSettings.personalityEnergy * 0.35f) + (runtimeSettings.personalityCuriosity * 0.25f) + 0.2f);

    if (runtimeSettings.traceEnabled) {
        Flic::Debug::setRuntimeLogLevel(5);
    } else if (runtimeSettings.debugEnabled) {
        Flic::Debug::setRuntimeLogLevel(4);
    } else {
        Flic::Debug::setRuntimeLogLevel(3);
    }
}

bool readNumericRange(JsonVariantConst value, float minimumValue, float maximumValue, float& output);

bool handleWebUiFacePreview(const String& requestJson, String& responseJson);
bool handleWebUiFaceSetStyle(const String& requestJson, String& responseJson);
bool handleWebUiFaceSetAnimation(const String& requestJson, String& responseJson);
bool handleWebUiFacePlay(const String& requestJson, String& responseJson);
bool handleWebUiFaceSetEmotion(const String& requestJson, String& responseJson);
bool handleWebUiFaceReload(const String& requestJson, String& responseJson);
bool handleWebUiFaceTelemetry(const String& requestJson, String& responseJson);

bool handleWebUiFaceSettings(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = String("{\"ok\":false,\"error\":\"invalid_face_settings_json\"") + (error ? String(",\"message\":\"") + error.c_str() + "\"" : "") + "}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    JsonVariant nested = root["settings"];
    if (nested.is<JsonObject>()) {
        root = nested.as<JsonObject>();
    }

    Flic::FaceSettings next = faceSettingsManager.current();
    bool changed = false;

    if (!root["active_style"].isNull()) {
        if (!root["active_style"].is<const char*>()) {
            responseJson = "{\"ok\":false,\"error\":\"active_style_must_be_string\"}";
            return false;
        }
        next.activeStyle = String(root["active_style"].as<const char*>());
        next.activeStyle.trim();
        if (next.activeStyle.length() == 0) {
            next.activeStyle = "default";
        }
        if (!faceEngine.setStyle(next.activeStyle)) {
            next.activeStyle = "default";
        }
        changed = true;
    }

    if (!root["blink_speed"].isNull()) {
        float value = 0.0f;
        if (!readNumericRange(root["blink_speed"], 0.25f, 4.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"blink_speed_must_be_number_0_25_4_0\"}";
            return false;
        }
        next.blinkSpeed = value;
        changed = true;
    }

    if (!root["idle_enabled"].isNull()) {
        if (!root["idle_enabled"].is<bool>()) {
            responseJson = "{\"ok\":false,\"error\":\"idle_enabled_must_be_boolean\"}";
            return false;
        }
        next.idleEnabled = root["idle_enabled"].as<bool>();
        changed = true;
    }

    if (!root["glow_intensity"].isNull()) {
        float value = 0.0f;
        if (!readNumericRange(root["glow_intensity"], 0.0f, 1.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"glow_intensity_must_be_number_0_1\"}";
            return false;
        }
        next.glowIntensity = value;
        changed = true;
    }

    if (!root["eye_color"].isNull()) {
        if (!root["eye_color"].is<const char*>()) {
            responseJson = "{\"ok\":false,\"error\":\"eye_color_must_be_string\"}";
            return false;
        }
        next.eyeColor = String(root["eye_color"].as<const char*>());
        next.eyeColor.trim();
        changed = true;
    }

    if (!root["personality_intensity"].isNull()) {
        if (!root["personality_intensity"].is<const char*>()) {
            responseJson = "{\"ok\":false,\"error\":\"personality_intensity_must_be_string\"}";
            return false;
        }
        next.personalityIntensity = String(root["personality_intensity"].as<const char*>());
        next.personalityIntensity.trim();
        next.personalityIntensity.toLowerCase();
        if (!(next.personalityIntensity == "subtle" || next.personalityIntensity == "balanced" || next.personalityIntensity == "dramatic")) {
            responseJson = "{\"ok\":false,\"error\":\"personality_intensity_must_be_subtle_balanced_or_dramatic\"}";
            return false;
        }
        changed = true;
    }

    if (!root["emotion_animation_map"].isNull()) {
        if (root["emotion_animation_map"].is<JsonObject>()) {
            String mappingPayload;
            serializeJson(root["emotion_animation_map"], mappingPayload);
            next.emotionAnimationMapJson = mappingPayload;
            changed = true;
        } else if (root["emotion_animation_map"].is<const char*>()) {
            next.emotionAnimationMapJson = String(root["emotion_animation_map"].as<const char*>());
            next.emotionAnimationMapJson.trim();
            changed = true;
        } else {
            responseJson = "{\"ok\":false,\"error\":\"emotion_animation_map_must_be_object_or_json_string\"}";
            return false;
        }
    }

    if (!root["ai_can_modify"].isNull()) {
        if (!root["ai_can_modify"].is<bool>()) {
            responseJson = "{\"ok\":false,\"error\":\"ai_can_modify_must_be_boolean\"}";
            return false;
        }
        next.aiCanModify = root["ai_can_modify"].as<bool>();
        changed = true;
    }

    if (!root["ai_can_create"].isNull()) {
        if (!root["ai_can_create"].is<bool>()) {
            responseJson = "{\"ok\":false,\"error\":\"ai_can_create_must_be_boolean\"}";
            return false;
        }
        next.aiCanCreate = root["ai_can_create"].as<bool>();
        changed = true;
    }

    if (!changed) {
        responseJson = "{\"ok\":false,\"error\":\"no_valid_face_settings_provided\"}";
        return false;
    }

    faceSettingsManager.apply(next);
    responseJson = "{\"ok\":true,\"applied\":true,\"persisted\":true}";
    return true;
}

bool handleWebUiFacePreview(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_face_preview_json\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    String style = root["style"] | faceEngine.activeStyle();
    String animation = root["animation"] | "idle";
    style.trim();
    animation.trim();
    if (style.length() == 0) {
        style = "default";
    }
    if (animation.length() == 0) {
        animation = "idle";
    }

    faceEngine.loadAnimationSet(style);
    faceEngine.setStyle(style);
    const bool played = faceEngine.playAnimation(animation);
    if (!played) {
        faceEngine.playAnimation("idle");
        responseJson = "{\"ok\":false,\"fallback\":\"idle\"}";
        return false;
    }

    responseJson = "{\"ok\":true,\"preview\":true}";
    return true;
}

bool handleWebUiFaceSetStyle(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_face_set_style_json\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    String style = root["style"] | "default";
    style.trim();
    if (style.length() == 0) {
        responseJson = "{\"ok\":false,\"error\":\"missing_style\"}";
        return false;
    }

    if (!faceEngine.setStyle(style)) {
        responseJson = "{\"ok\":false,\"error\":\"style_not_found\"}";
        return false;
    }

    Flic::FaceSettings settings = faceSettingsManager.current();
    settings.activeStyle = style;
    faceSettingsManager.apply(settings);
    responseJson = "{\"ok\":true,\"persisted\":true}";
    return true;
}

bool handleWebUiFaceSetAnimation(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_face_set_animation_json\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    String style = root["style"] | faceEngine.activeStyle();
    String animation = root["animation"] | "idle";
    style.trim();
    animation.trim();

    if (style.length() > 0) {
        faceEngine.loadAnimationSet(style);
        faceEngine.setStyle(style);
    }

    if (!faceEngine.play(animation)) {
        responseJson = "{\"ok\":false,\"error\":\"animation_not_found\",\"fallback\":\"idle\"}";
        faceEngine.play("idle");
        return false;
    }

    responseJson = "{\"ok\":true,\"applied\":true}";
    return true;
}

bool handleWebUiFacePlay(const String& requestJson, String& responseJson) {
    return handleWebUiFaceSetAnimation(requestJson, responseJson);
}

bool handleWebUiFaceSetEmotion(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_face_set_emotion_json\"}";
        return false;
    }

    const String emotion = String(document["emotion"] | "neutral");
    if (emotion.length() == 0) {
        responseJson = "{\"ok\":false,\"error\":\"missing_emotion\"}";
        return false;
    }

    emotionEngine.setEmotion(emotion);
    faceEngine.setEmotion(emotion);
    if (emotion == "curious") {
        faceEngine.setPersonalityState("curious");
    } else if (emotion == "happy") {
        faceEngine.setPersonalityState("excited");
    } else if (emotion == "sleepy") {
        faceEngine.setPersonalityState("tired");
    } else if (emotion == "surprised") {
        faceEngine.setPersonalityState("confused");
    } else if (emotion == "focused") {
        faceEngine.setPersonalityState("focused");
    } else {
        faceEngine.setPersonalityState("neutral");
    }
    responseJson = "{\"ok\":true,\"emotion\":\"" + emotion + "\"}";
    return true;
}

bool handleWebUiFaceReload(const String& requestJson, String& responseJson) {
    (void)requestJson;
    const bool ok = faceEngine.reloadActiveStyle();
    responseJson = String("{\"ok\":") + (ok ? "true" : "false") + "}";
    return ok;
}

bool handleWebUiFaceTelemetry(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = String("{\"ok\":false,\"error\":\"invalid_face_telemetry_json\",\"message\":\"") +
                       error.c_str() + "\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    JsonVariant thresholdsVariant = root["thresholds"];
    if (thresholdsVariant.is<JsonObject>()) {
        root = thresholdsVariant.as<JsonObject>();
    }

    Flic::FaceTelemetryThresholds thresholds = faceEngine.telemetryThresholds();
    bool changed = false;
    float value = 0.0f;

    if (!root["fps_warn"].isNull()) {
        if (!readNumericRange(root["fps_warn"], 1.0f, 120.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"fps_warn_must_be_number_1_120\"}";
            return false;
        }
        thresholds.fpsWarn = value;
        changed = true;
    }
    if (!root["fps_bad"].isNull()) {
        if (!readNumericRange(root["fps_bad"], 1.0f, 120.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"fps_bad_must_be_number_1_120\"}";
            return false;
        }
        thresholds.fpsBad = value;
        changed = true;
    }
    if (!root["draw_warn_ms"].isNull()) {
        if (!readNumericRange(root["draw_warn_ms"], 1.0f, 200.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"draw_warn_ms_must_be_number_1_200\"}";
            return false;
        }
        thresholds.drawWarnMs = value;
        changed = true;
    }
    if (!root["draw_bad_ms"].isNull()) {
        if (!readNumericRange(root["draw_bad_ms"], 1.0f, 200.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"draw_bad_ms_must_be_number_1_200\"}";
            return false;
        }
        thresholds.drawBadMs = value;
        changed = true;
    }
    if (!root["blend_draw_warn_ms"].isNull()) {
        if (!readNumericRange(root["blend_draw_warn_ms"], 1.0f, 250.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"blend_draw_warn_ms_must_be_number_1_250\"}";
            return false;
        }
        thresholds.blendDrawWarnMs = value;
        changed = true;
    }
    if (!root["blend_draw_bad_ms"].isNull()) {
        if (!readNumericRange(root["blend_draw_bad_ms"], 1.0f, 250.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"blend_draw_bad_ms_must_be_number_1_250\"}";
            return false;
        }
        thresholds.blendDrawBadMs = value;
        changed = true;
    }
    if (!root["over_budget_warn_pct"].isNull()) {
        if (!readNumericRange(root["over_budget_warn_pct"], 0.0f, 100.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"over_budget_warn_pct_must_be_number_0_100\"}";
            return false;
        }
        thresholds.overBudgetWarnPct = value;
        changed = true;
    }
    if (!root["over_budget_bad_pct"].isNull()) {
        if (!readNumericRange(root["over_budget_bad_pct"], 0.0f, 100.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"over_budget_bad_pct_must_be_number_0_100\"}";
            return false;
        }
        thresholds.overBudgetBadPct = value;
        changed = true;
    }
    if (!root["fallback_warn_count"].isNull()) {
        if (!readNumericRange(root["fallback_warn_count"], 0.0f, 1000.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"fallback_warn_count_must_be_number_0_1000\"}";
            return false;
        }
        thresholds.fallbackWarnCount = value;
        changed = true;
    }
    if (!root["fallback_bad_count"].isNull()) {
        if (!readNumericRange(root["fallback_bad_count"], 0.0f, 1000.0f, value)) {
            responseJson = "{\"ok\":false,\"error\":\"fallback_bad_count_must_be_number_0_1000\"}";
            return false;
        }
        thresholds.fallbackBadCount = value;
        changed = true;
    }

    if (!changed) {
        responseJson = "{\"ok\":false,\"error\":\"no_valid_telemetry_thresholds_provided\"}";
        return false;
    }

    if (thresholds.fpsBad > thresholds.fpsWarn ||
        thresholds.drawBadMs < thresholds.drawWarnMs ||
        thresholds.blendDrawBadMs < thresholds.blendDrawWarnMs ||
        thresholds.overBudgetBadPct < thresholds.overBudgetWarnPct ||
        thresholds.fallbackBadCount < thresholds.fallbackWarnCount) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_threshold_ordering\"}";
        return false;
    }

    faceEngine.setTelemetryThresholds(thresholds);
    responseJson = faceEngine.telemetryThresholdsJson();
    return true;
}

float clampNeed(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

bool isIgnoredTopicWord(const String& word) {
    static constexpr const char* kIgnored[] = {
        "this", "that", "with", "from", "just", "what", "when", "where", "which", "want", "have",
        "your", "about", "would", "there", "could", "should", "like", "more", "tell", "please",
        "hello", "flic", "listen", "wake", "think", "thinking", "okay", "yeah", "them", "they", "then"
    };
    for (const char* ignored : kIgnored) {
        if (word == ignored) {
            return true;
        }
    }
    return false;
}

void sortLearnedTopics() {
    for (uint8_t i = 0; i < 3; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < 3; ++j) {
            if (learnedTopics[j].score > learnedTopics[i].score) {
                LearnedTopic swap = learnedTopics[i];
                learnedTopics[i] = learnedTopics[j];
                learnedTopics[j] = swap;
            }
        }
    }
}

void learnTopicWord(const String& word, uint8_t gain = 1) {
    if (word.length() < 4 || isIgnoredTopicWord(word)) {
        return;
    }

    for (LearnedTopic& topic : learnedTopics) {
        if (topic.word == word) {
            uint32_t increased = static_cast<uint32_t>(topic.score) + gain;
            topic.score = static_cast<uint16_t>(increased > 9999 ? 9999 : increased);
            sortLearnedTopics();
            return;
        }
    }

    uint8_t slot = 0;
    for (uint8_t i = 1; i < 3; ++i) {
        if (learnedTopics[i].score < learnedTopics[slot].score) {
            slot = i;
        }
    }

    if (learnedTopics[slot].score <= gain || learnedTopics[slot].word.length() == 0) {
        learnedTopics[slot].word = word;
        learnedTopics[slot].score = static_cast<uint16_t>(gain + 1);
        sortLearnedTopics();
    }
}

void learnFromUserSentence(const String& text) {
    String normalized = text;
    normalized.toLowerCase();

    for (int i = 0; i < normalized.length(); ++i) {
        if (!isAlphaNumeric(normalized[i])) {
            normalized.setCharAt(i, ' ');
        }
    }

    int start = 0;
    while (start < normalized.length()) {
        while (start < normalized.length() && normalized[start] == ' ') {
            ++start;
        }
        if (start >= normalized.length()) {
            break;
        }
        int end = start;
        while (end < normalized.length() && normalized[end] != ' ') {
            ++end;
        }
        learnTopicWord(normalized.substring(start, end));
        start = end + 1;
    }

    if (normalized.indexOf("play") >= 0 || normalized.indexOf("game") >= 0 || normalized.indexOf("move") >= 0) {
        needPlay = clampNeed(needPlay + 0.08f);
    }
    if (normalized.indexOf("quiet") >= 0 || normalized.indexOf("calm") >= 0 || normalized.indexOf("rest") >= 0) {
        needCalm = clampNeed(needCalm + 0.08f);
    }
    if (normalized.indexOf("light") >= 0 || normalized.indexOf("dark") >= 0 || normalized.indexOf("bright") >= 0) {
        needLight = clampNeed(needLight + 0.10f);
    }

    lastVoiceMs = millis();
    needConnection = clampNeed(needConnection - 0.18f);
}

void updateAutonomyNeeds() {
    const unsigned long now = millis();
    if ((now - lastAutonomyNeedTickMs) < kAutonomyNeedTickMs) {
        return;
    }
    lastAutonomyNeedTickMs = now;

    if ((now - lastTouchMs) > kAutonomyLonelyAfterMs && (now - lastVoiceMs) > kAutonomyLonelyAfterMs) {
        needConnection = clampNeed(needConnection + 0.05f);
    }
    if ((now - lastMotionMs) > kAutonomyBoredAfterMs) {
        needPlay = clampNeed(needPlay + 0.04f);
    }

    const String emotion = emotionEngine.getEmotion();
    if (emotion == "sleepy") {
        needCalm = clampNeed(needCalm + 0.04f);
    } else if (emotion == "curious") {
        needPlay = clampNeed(needPlay + 0.02f);
    }

    needConnection = clampNeed(needConnection * 0.995f);
    needPlay = clampNeed(needPlay * 0.996f);
    needCalm = clampNeed(needCalm * 0.996f);
    needLight = clampNeed(needLight * 0.995f);
}

bool composeAutonomySentence(String& sentence, String& sentenceEmotion) {
    float highest = needConnection;
    uint8_t mode = 0;
    if (needPlay > highest) {
        highest = needPlay;
        mode = 1;
    }
    if (needCalm > highest) {
        highest = needCalm;
        mode = 2;
    }
    if (needLight > highest) {
        highest = needLight;
        mode = 3;
    }

    if (highest < 0.45f) {
        return false;
    }

    const String topTopic = learnedTopics[0].word;
    const uint8_t variant = static_cast<uint8_t>(millis() % 3);

    if (mode == 0) {
        sentenceEmotion = "curious";
        if (topTopic.length() > 0 && learnedTopics[0].score > 2) {
            if (variant == 0) {
                sentence = String("I want to hear more about ") + topTopic + ".";
            } else if (variant == 1) {
                sentence = String("Can we talk about ") + topTopic + "?";
            } else {
                sentence = String("I keep thinking about ") + topTopic + ". Tell me more?";
            }
        } else {
            sentence = (variant == 0) ? "I want your attention. Tap me?" : "Can we chat for a moment?";
        }
        needConnection = clampNeed(needConnection - 0.35f);
        return true;
    }

    if (mode == 1) {
        sentenceEmotion = "happy";
        sentence = (variant == 0) ? "I want to play. Wave your hand at me?" : "Can you move me a little? I want action.";
        needPlay = clampNeed(needPlay - 0.35f);
        return true;
    }

    if (mode == 2) {
        sentenceEmotion = "sleepy";
        sentence = (variant == 0) ? "I want a calm minute. Can we stay still?" : "I want quiet right now. Let's pause.";
        needCalm = clampNeed(needCalm - 0.32f);
        return true;
    }

    sentenceEmotion = "calm";
    sentence = (variant == 0) ? "I want a little more light around me." : "It's dim for me. Can we make it brighter?";
    needLight = clampNeed(needLight - 0.30f);
    return true;
}

void maybeSpeakAutonomously() {
    updateAutonomyNeeds();

    const unsigned long now = millis();
    if ((now - lastAutonomySpeakMs) < kAutonomySpeakCooldownMs) {
        return;
    }
    if ((now - lastVoiceFeedbackMs) < 2000) {
        return;
    }

    String sentence;
    String sentenceEmotion;
    if (!composeAutonomySentence(sentence, sentenceEmotion)) {
        return;
    }

    communicationEngine.notify(sentence, sentenceEmotion);
    memoryManager.recordEvent("self_talk", sentence);
    lastAutonomySpeakMs = now;
}

bool readNumericRange(JsonVariantConst value, float minimumValue, float maximumValue, float& output) {
    if (!value.is<int>() && !value.is<float>()) {
        return false;
    }

    const float raw = value.as<float>();
    if (raw < minimumValue || raw > maximumValue) {
        return false;
    }

    output = raw;
    return true;
}

void persistRuntimeSettings() {
    JsonDocument document;
    document["_schema"] = "flic.settings.v1";
    document["updated_at"] = millis();

    JsonObject settings = document["settings"].to<JsonObject>();
    settings["brightness"] = runtimeSettings.brightness;
    settings["volume"] = runtimeSettings.volume;
    settings["voice_style"] = runtimeSettings.voiceStyle;
    settings["animation_speed"] = runtimeSettings.animationSpeed * 100.0f;
    settings["emotion_bias"] = runtimeSettings.emotionBias * 100.0f;

    JsonObject voice = settings["voice"].to<JsonObject>();
    voice["model"] = runtimeSettings.voiceModel;
    voice["speed"] = runtimeSettings.voiceSpeed;
    voice["pitch"] = runtimeSettings.voicePitch;
    voice["clarity"] = runtimeSettings.voiceClarity;
    voice["fallback"] = runtimeSettings.fallbackVoiceEnabled;

    JsonObject personality = settings["personality"].to<JsonObject>();
    personality["energy"] = runtimeSettings.personalityEnergy * 100.0f;
    personality["curiosity"] = runtimeSettings.personalityCuriosity * 100.0f;
    personality["patience"] = runtimeSettings.personalityPatience * 100.0f;

    JsonObject debug = settings["debug"].to<JsonObject>();
    debug["enabled"] = runtimeSettings.debugEnabled;
    debug["trace"] = runtimeSettings.traceEnabled;
    debug["level"] = Flic::Debug::runtimeLogLevel();

    JsonObject runtime = settings["runtime"].to<JsonObject>();
    runtime["voice_input"] = runtimeSettings.voiceInputEnabled;
    runtime["autonomy"] = runtimeSettings.autonomyEnabled;
    runtime["web_heartbeat"] = runtimeSettings.webHeartbeatEnabled;
    runtime["imu_events"] = runtimeSettings.imuEventsEnabled;
    runtime["usb_events"] = runtimeSettings.usbEventsEnabled;

    settingsManager.save(document);
}

void loadRuntimeSettings() {
    JsonDocument document;
    if (!settingsManager.load(document)) {
        return;
    }

    JsonObject root = document.as<JsonObject>();
    if (root.isNull()) {
        return;
    }

    JsonObject settings = root["settings"].as<JsonObject>();
    if (settings.isNull()) {
        settings = root;
    }

    float numericValue = 0.0f;
    if (readNumericRange(settings["brightness"], 0.0f, 100.0f, numericValue)) {
        runtimeSettings.brightness = static_cast<uint8_t>(numericValue + 0.5f);
    }
    if (readNumericRange(settings["volume"], 0.0f, 255.0f, numericValue)) {
        runtimeSettings.volume = static_cast<uint8_t>(numericValue + 0.5f);
    }

    const char* voiceStyle = settings["voice_style"] | nullptr;
    if (voiceStyle != nullptr) {
        runtimeSettings.voiceStyle = String(voiceStyle);
    }

    if (readNumericRange(settings["animation_speed"], 25.0f, 400.0f, numericValue)) {
        runtimeSettings.animationSpeed = numericValue / 100.0f;
    }
    if (readNumericRange(settings["emotion_bias"], -100.0f, 100.0f, numericValue)) {
        runtimeSettings.emotionBias = numericValue / 100.0f;
    }

    JsonObject personality = settings["personality"].as<JsonObject>();
    if (!personality.isNull()) {
        if (readNumericRange(personality["energy"], 0.0f, 100.0f, numericValue)) {
            runtimeSettings.personalityEnergy = numericValue / 100.0f;
        }
        if (readNumericRange(personality["curiosity"], 0.0f, 100.0f, numericValue)) {
            runtimeSettings.personalityCuriosity = numericValue / 100.0f;
        }
        if (readNumericRange(personality["patience"], 0.0f, 100.0f, numericValue)) {
            runtimeSettings.personalityPatience = numericValue / 100.0f;
        }
    }

    JsonObject debug = settings["debug"].as<JsonObject>();
    if (!debug.isNull()) {
        runtimeSettings.debugEnabled = debug["enabled"] | runtimeSettings.debugEnabled;
        runtimeSettings.traceEnabled = debug["trace"] | runtimeSettings.traceEnabled;
    }

    JsonObject runtime = settings["runtime"].as<JsonObject>();
    if (!runtime.isNull()) {
        runtimeSettings.voiceInputEnabled = runtime["voice_input"] | runtimeSettings.voiceInputEnabled;
        runtimeSettings.autonomyEnabled = runtime["autonomy"] | runtimeSettings.autonomyEnabled;
        runtimeSettings.webHeartbeatEnabled = runtime["web_heartbeat"] | runtimeSettings.webHeartbeatEnabled;
        runtimeSettings.imuEventsEnabled = runtime["imu_events"] | runtimeSettings.imuEventsEnabled;
        runtimeSettings.usbEventsEnabled = runtime["usb_events"] | runtimeSettings.usbEventsEnabled;
    }

    JsonObject voice = settings["voice"].as<JsonObject>();
    if (!voice.isNull()) {
        const char* model = voice["model"] | nullptr;
        if (model != nullptr) {
            runtimeSettings.voiceModel = String(model);
        }
        if (readNumericRange(voice["speed"], 0.5f, 2.0f, numericValue)) {
            runtimeSettings.voiceSpeed = numericValue;
        }
        if (readNumericRange(voice["pitch"], 0.5f, 2.0f, numericValue)) {
            runtimeSettings.voicePitch = numericValue;
        }
        if (readNumericRange(voice["clarity"], 0.5f, 2.0f, numericValue)) {
            runtimeSettings.voiceClarity = numericValue;
        }
        runtimeSettings.fallbackVoiceEnabled = voice["fallback"] | runtimeSettings.fallbackVoiceEnabled;
    }
}

bool handleWebUiSettings(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error) {
        responseJson = String("{\"ok\":false,\"error\":\"invalid_json\",\"message\":\"") + error.c_str() + "\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    if (root.isNull()) {
        responseJson = "{\"ok\":false,\"error\":\"settings_must_be_object\"}";
        return false;
    }

    RuntimeSettings nextSettings = runtimeSettings;
    bool changed = false;

    JsonVariant settings = root["settings"];
    if (!settings.isNull()) {
        if (!settings.is<JsonObjectConst>()) {
            responseJson = "{\"ok\":false,\"error\":\"settings_must_be_object\"}";
            return false;
        }
        root = settings.as<JsonObject>();
    }

    if (!root["brightness"].isNull()) {
        float brightnessValue = 0.0f;
        if (!readNumericRange(root["brightness"], 0.0f, 100.0f, brightnessValue)) {
            responseJson = "{\"ok\":false,\"error\":\"brightness_must_be_number_0_100\"}";
            return false;
        }
        nextSettings.brightness = static_cast<uint8_t>(brightnessValue + 0.5f);
        changed = true;
    }

    if (!root["volume"].isNull()) {
        float volumeValue = 0.0f;
        if (!readNumericRange(root["volume"], 0.0f, 255.0f, volumeValue)) {
            responseJson = "{\"ok\":false,\"error\":\"volume_must_be_number_0_255\"}";
            return false;
        }
        nextSettings.volume = static_cast<uint8_t>(volumeValue + 0.5f);
        changed = true;
    }

    if (!root["voice_style"].isNull()) {
        if (!root["voice_style"].is<const char*>()) {
            responseJson = "{\"ok\":false,\"error\":\"voice_style_must_be_string\"}";
            return false;
        }

        String voiceStyle = root["voice_style"].as<const char*>();
        voiceStyle.trim();
        voiceStyle.toLowerCase();
        if (voiceStyle != "natural" && voiceStyle != "clear" && voiceStyle != "bright" &&
            voiceStyle != "deep" && voiceStyle != "warm") {
            responseJson = "{\"ok\":false,\"error\":\"voice_style_must_be_natural_clear_bright_deep_or_warm\"}";
            return false;
        }
        nextSettings.voiceStyle = voiceStyle;
        changed = true;
    }

    if (!root["voice_model"].isNull()) {
        if (!root["voice_model"].is<const char*>()) {
            responseJson = "{\"ok\":false,\"error\":\"voice_model_must_be_string\"}";
            return false;
        }
        nextSettings.voiceModel = String(root["voice_model"].as<const char*>());
        nextSettings.voiceModel.trim();
        if (nextSettings.voiceModel.length() > 0 && !audioOutput.setActiveVoiceModel(nextSettings.voiceModel)) {
            responseJson = "{\"ok\":false,\"error\":\"voice_model_not_found_on_sd\"}";
            return false;
        }
        changed = true;
    }

    if (!root["voice_speed"].isNull()) {
        float voiceSpeed = 0.0f;
        if (!readNumericRange(root["voice_speed"], 0.5f, 2.0f, voiceSpeed)) {
            responseJson = "{\"ok\":false,\"error\":\"voice_speed_must_be_number_0_5_2_0\"}";
            return false;
        }
        nextSettings.voiceSpeed = voiceSpeed;
        changed = true;
    }

    if (!root["voice_pitch"].isNull()) {
        float voicePitch = 0.0f;
        if (!readNumericRange(root["voice_pitch"], 0.5f, 2.0f, voicePitch)) {
            responseJson = "{\"ok\":false,\"error\":\"voice_pitch_must_be_number_0_5_2_0\"}";
            return false;
        }
        nextSettings.voicePitch = voicePitch;
        changed = true;
    }

    if (!root["voice_clarity"].isNull()) {
        float voiceClarity = 0.0f;
        if (!readNumericRange(root["voice_clarity"], 0.5f, 2.0f, voiceClarity)) {
            responseJson = "{\"ok\":false,\"error\":\"voice_clarity_must_be_number_0_5_2_0\"}";
            return false;
        }
        nextSettings.voiceClarity = voiceClarity;
        changed = true;
    }

    if (!root["fallback_voice"].isNull()) {
        if (!root["fallback_voice"].is<bool>()) {
            responseJson = "{\"ok\":false,\"error\":\"fallback_voice_must_be_boolean\"}";
            return false;
        }
        nextSettings.fallbackVoiceEnabled = root["fallback_voice"].as<bool>();
        changed = true;
    }

    JsonVariant voice = root["voice"];
    if (!voice.isNull()) {
        if (!voice.is<JsonObjectConst>()) {
            responseJson = "{\"ok\":false,\"error\":\"voice_must_be_object\"}";
            return false;
        }
        JsonObject voiceObject = voice.as<JsonObject>();
        if (!voiceObject["model"].isNull()) {
            if (!voiceObject["model"].is<const char*>()) {
                responseJson = "{\"ok\":false,\"error\":\"voice_model_must_be_string\"}";
                return false;
            }
            nextSettings.voiceModel = String(voiceObject["model"].as<const char*>());
            nextSettings.voiceModel.trim();
            if (nextSettings.voiceModel.length() > 0 && !audioOutput.setActiveVoiceModel(nextSettings.voiceModel)) {
                responseJson = "{\"ok\":false,\"error\":\"voice_model_not_found_on_sd\"}";
                return false;
            }
            changed = true;
        }
        if (!voiceObject["speed"].isNull()) {
            float value = 0.0f;
            if (!readNumericRange(voiceObject["speed"], 0.5f, 2.0f, value)) {
                responseJson = "{\"ok\":false,\"error\":\"voice_speed_must_be_number_0_5_2_0\"}";
                return false;
            }
            nextSettings.voiceSpeed = value;
            changed = true;
        }
        if (!voiceObject["pitch"].isNull()) {
            float value = 0.0f;
            if (!readNumericRange(voiceObject["pitch"], 0.5f, 2.0f, value)) {
                responseJson = "{\"ok\":false,\"error\":\"voice_pitch_must_be_number_0_5_2_0\"}";
                return false;
            }
            nextSettings.voicePitch = value;
            changed = true;
        }
        if (!voiceObject["clarity"].isNull()) {
            float value = 0.0f;
            if (!readNumericRange(voiceObject["clarity"], 0.5f, 2.0f, value)) {
                responseJson = "{\"ok\":false,\"error\":\"voice_clarity_must_be_number_0_5_2_0\"}";
                return false;
            }
            nextSettings.voiceClarity = value;
            changed = true;
        }
        if (!voiceObject["fallback"].isNull()) {
            if (!voiceObject["fallback"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"fallback_voice_must_be_boolean\"}";
                return false;
            }
            nextSettings.fallbackVoiceEnabled = voiceObject["fallback"].as<bool>();
            changed = true;
        }
    }

    if (!root["animation_speed"].isNull()) {
        float animationSpeedPercent = 0.0f;
        if (!readNumericRange(root["animation_speed"], 25.0f, 400.0f, animationSpeedPercent)) {
            responseJson = "{\"ok\":false,\"error\":\"animation_speed_must_be_number_25_400\"}";
            return false;
        }
        nextSettings.animationSpeed = animationSpeedPercent / 100.0f;
        changed = true;
    }

    if (!root["emotion_bias"].isNull()) {
        float emotionBiasPercent = 0.0f;
        if (!readNumericRange(root["emotion_bias"], -100.0f, 100.0f, emotionBiasPercent)) {
            responseJson = "{\"ok\":false,\"error\":\"emotion_bias_must_be_number_-100_100\"}";
            return false;
        }
        nextSettings.emotionBias = emotionBiasPercent / 100.0f;
        changed = true;
    }

    JsonVariant personality = root["personality"];
    if (!personality.isNull()) {
        if (!personality.is<JsonObjectConst>()) {
            responseJson = "{\"ok\":false,\"error\":\"personality_must_be_object\"}";
            return false;
        }

        JsonObject personalityObject = personality.as<JsonObject>();
        if (!personalityObject["energy"].isNull()) {
            float energyPercent = 0.0f;
            if (!readNumericRange(personalityObject["energy"], 0.0f, 100.0f, energyPercent)) {
                responseJson = "{\"ok\":false,\"error\":\"personality_energy_must_be_number_0_100\"}";
                return false;
            }
            nextSettings.personalityEnergy = energyPercent / 100.0f;
            changed = true;
        }
        if (!personalityObject["curiosity"].isNull()) {
            float curiosityPercent = 0.0f;
            if (!readNumericRange(personalityObject["curiosity"], 0.0f, 100.0f, curiosityPercent)) {
                responseJson = "{\"ok\":false,\"error\":\"personality_curiosity_must_be_number_0_100\"}";
                return false;
            }
            nextSettings.personalityCuriosity = curiosityPercent / 100.0f;
            changed = true;
        }
        if (!personalityObject["patience"].isNull()) {
            float patiencePercent = 0.0f;
            if (!readNumericRange(personalityObject["patience"], 0.0f, 100.0f, patiencePercent)) {
                responseJson = "{\"ok\":false,\"error\":\"personality_patience_must_be_number_0_100\"}";
                return false;
            }
            nextSettings.personalityPatience = patiencePercent / 100.0f;
            changed = true;
        }
    }

    JsonVariant debug = root["debug"];
    if (!debug.isNull()) {
        if (!debug.is<JsonObjectConst>()) {
            responseJson = "{\"ok\":false,\"error\":\"debug_must_be_object\"}";
            return false;
        }

        JsonObject debugObject = debug.as<JsonObject>();
        if (!debugObject["trace"].isNull()) {
            if (!debugObject["trace"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"debug_trace_must_be_boolean\"}";
                return false;
            }
            nextSettings.traceEnabled = debugObject["trace"].as<bool>();
            changed = true;
        }
        if (!debugObject["enabled"].isNull()) {
            if (!debugObject["enabled"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"debug_enabled_must_be_boolean\"}";
                return false;
            }
            nextSettings.debugEnabled = debugObject["enabled"].as<bool>();
            changed = true;
        }
        if (!debugObject["level"].isNull()) {
            float levelValue = 0.0f;
            if (!readNumericRange(debugObject["level"], 0.0f, 5.0f, levelValue)) {
                responseJson = "{\"ok\":false,\"error\":\"debug_level_must_be_number_0_5\"}";
                return false;
            }
            const int level = static_cast<int>(levelValue + 0.5f);
            Flic::Debug::setRuntimeLogLevel(static_cast<uint8_t>(level));
            nextSettings.debugEnabled = level >= 4;
            nextSettings.traceEnabled = level >= 5;
            changed = true;
        }
    }

    JsonVariant runtime = root["runtime"];
    if (!runtime.isNull()) {
        if (!runtime.is<JsonObjectConst>()) {
            responseJson = "{\"ok\":false,\"error\":\"runtime_must_be_object\"}";
            return false;
        }

        JsonObject runtimeObject = runtime.as<JsonObject>();
        if (!runtimeObject["voice_input"].isNull()) {
            if (!runtimeObject["voice_input"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"runtime_voice_input_must_be_boolean\"}";
                return false;
            }
            nextSettings.voiceInputEnabled = runtimeObject["voice_input"].as<bool>();
            changed = true;
        }
        if (!runtimeObject["autonomy"].isNull()) {
            if (!runtimeObject["autonomy"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"runtime_autonomy_must_be_boolean\"}";
                return false;
            }
            nextSettings.autonomyEnabled = runtimeObject["autonomy"].as<bool>();
            changed = true;
        }
        if (!runtimeObject["web_heartbeat"].isNull()) {
            if (!runtimeObject["web_heartbeat"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"runtime_web_heartbeat_must_be_boolean\"}";
                return false;
            }
            nextSettings.webHeartbeatEnabled = runtimeObject["web_heartbeat"].as<bool>();
            changed = true;
        }
        if (!runtimeObject["imu_events"].isNull()) {
            if (!runtimeObject["imu_events"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"runtime_imu_events_must_be_boolean\"}";
                return false;
            }
            nextSettings.imuEventsEnabled = runtimeObject["imu_events"].as<bool>();
            changed = true;
        }
        if (!runtimeObject["usb_events"].isNull()) {
            if (!runtimeObject["usb_events"].is<bool>()) {
                responseJson = "{\"ok\":false,\"error\":\"runtime_usb_events_must_be_boolean\"}";
                return false;
            }
            nextSettings.usbEventsEnabled = runtimeObject["usb_events"].as<bool>();
            changed = true;
        }
    }

    if (!changed) {
        responseJson = "{\"ok\":false,\"error\":\"no_valid_settings_provided\"}";
        return false;
    }

    runtimeSettings = nextSettings;
    applyRuntimeSettings();
    persistRuntimeSettings();
    responseJson = "{\"ok\":true,\"applied\":true,\"persisted\":true}";
    return true;
}

bool handleWebUiCommand(const String& requestJson, String& responseJson) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, requestJson);
    if (error || !document.is<JsonObject>()) {
        responseJson = "{\"ok\":false,\"error\":\"invalid_command_json\"}";
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    String text = root["text"] | root["transcript"] | root["message"] | "";
    text.trim();
    if (text.length() == 0) {
        responseJson = "{\"ok\":false,\"error\":\"missing_text\"}";
        return false;
    }

    String lower = text;
    lower.toLowerCase();

    String reply;
    String emotion = "calm";
    if (lower.indexOf("preview face") >= 0 || lower.indexOf("face preview") >= 0) {
        String previewStyle = faceSettingsManager.current().activeStyle;
        if (lower.indexOf("soft_glow") >= 0) {
            previewStyle = "soft_glow";
        } else if (lower.indexOf("minimal") >= 0) {
            previewStyle = "minimal";
        } else if (lower.indexOf("custom") >= 0) {
            previewStyle = "custom";
        }
        faceEngine.setStyle(previewStyle);
        faceEngine.play("idle");
        reply = String("Previewing face style: ") + previewStyle;
        emotion = "curious";
    } else if (lower.indexOf("preview voice") >= 0 || lower.indexOf("voice preview") >= 0) {
        reply = "This is a preview of the selected voice pack.";
        emotion = "happy";
    } else if (lower.indexOf("voice style") >= 0 || lower.indexOf("voice") >= 0) {
        if (lower.indexOf("clear") >= 0) {
            runtimeSettings.voiceStyle = "clear";
            applyRuntimeSettings();
            reply = "Voice style set to clear.";
            emotion = "happy";
        } else if (lower.indexOf("bright") >= 0) {
            runtimeSettings.voiceStyle = "bright";
            applyRuntimeSettings();
            reply = "Voice style set to bright.";
            emotion = "happy";
        } else if (lower.indexOf("deep") >= 0) {
            runtimeSettings.voiceStyle = "deep";
            applyRuntimeSettings();
            reply = "Voice style set to deep.";
            emotion = "calm";
        } else if (lower.indexOf("warm") >= 0) {
            runtimeSettings.voiceStyle = "warm";
            applyRuntimeSettings();
            reply = "Voice style set to warm.";
            emotion = "calm";
        } else if (lower.indexOf("natural") >= 0) {
            runtimeSettings.voiceStyle = "natural";
            applyRuntimeSettings();
            reply = "Voice style set to natural.";
            emotion = "happy";
        } else {
            reply = "Say voice style natural, clear, bright, deep, or warm.";
            emotion = "curious";
        }
    } else if (lower.indexOf("hello") >= 0 || lower.indexOf("hi") >= 0) {
        reply = "Hi! I'm listening.";
        emotion = "happy";
    } else if (lower.indexOf("name") >= 0) {
        reply = "I'm Flic.";
        emotion = "happy";
    } else if (lower.indexOf("what can you do") >= 0 || lower.indexOf("capabilities") >= 0) {
        reply = "I can use my own device mic, react with emotions, and show WebUI control.";
        emotion = "curious";
    } else if (lower.indexOf("show webui info") >= 0 || lower.indexOf("webui") >= 0 || lower.indexOf("ip") >= 0) {
        reply = composeWebUiHelpMessage();
        emotion = "curious";
    } else if (lower.indexOf("set mood ") >= 0) {
        String mood = text.substring(lower.indexOf("set mood ") + 9);
        mood.trim();
        mood.toLowerCase();
        if (faceEngine.setMood(mood)) {
            reply = String("Mood set to ") + mood;
            emotion = mood == "stressed" ? "confused" : (mood == "tired" ? "sleepy" : "calm");
        } else {
            reply = String("Unable to set mood: ") + mood;
            emotion = "confused";
        }
    } else if (lower == "get mood") {
        reply = String("Current mood: ") + faceEngine.getMood();
        emotion = "curious";
    } else if (lower.indexOf("mood adaptation on") >= 0) {
        faceEngine.enableMoodAdaptation(true);
        reply = "Mood adaptation enabled.";
        emotion = "happy";
    } else if (lower.indexOf("mood adaptation off") >= 0) {
        faceEngine.enableMoodAdaptation(false);
        reply = "Mood adaptation disabled.";
        emotion = "calm";
    } else if (lower.indexOf("auto emotion on") >= 0) {
        faceEngine.enableAutoEmotion(true);
        reply = "Auto-emotion enabled.";
        emotion = "happy";
    } else if (lower.indexOf("auto emotion off") >= 0) {
        faceEngine.enableAutoEmotion(false);
        reply = "Auto-emotion disabled.";
        emotion = "calm";
    } else if (lower.indexOf("save personality profile") >= 0) {
        const bool ok = faceEngine.savePersonalityProfile();
        reply = ok ? "Personality profile saved." : "Failed to save personality profile.";
        emotion = ok ? "happy" : "confused";
    } else if (lower.indexOf("load personality profile") >= 0) {
        const bool ok = faceEngine.loadPersonalityProfile();
        reply = ok ? "Personality profile loaded." : "Failed to load personality profile.";
        emotion = ok ? "happy" : "confused";
    } else if (lower.indexOf("reset personality profile") >= 0) {
        faceEngine.resetPersonalityProfile();
        reply = "Personality profile reset.";
        emotion = "calm";
    } else {
        reply = text;
        emotion = communicationEngine.inferEmotionFromText(text);
    }

    communicationEngine.notify(reply, emotion);
    memoryManager.recordEvent("web_command", text);
    JsonDocument response;
    response["ok"] = true;
    response["accepted"] = true;
    response["executed"] = true;
    response["reply"] = reply;
    response["emotion"] = emotion;
    serializeJson(response, responseJson);
    return true;
}

String composeWebUiState() {
    String payload = "{\"type\":\"state\",\"uptime_ms\":";
    payload += String(millis());
    payload += ",\"emotion\":\"";
    payload += emotionEngine.getEmotion();
    payload += "\",\"animation_playing\":";
    payload += animationEngine.isPlaying() ? "true" : "false";
    payload += ",\"events\":";
    payload += String(memoryManager.eventCount());
    payload += "}";
    return payload;
}

void renderBuiltInFallbackFace() {
    auto& display = M5.Display;
    const int w = display.width();
    const int h = display.height();
    const int cy = h / 2;
    const int lx = (w / 2) - 42;
    const int rx = (w / 2) + 42;

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.fillCircle(lx, cy - 10, 16, 0xB71C);
    display.fillCircle(rx, cy - 10, 16, 0xB71C);
    display.fillCircle(lx, cy - 10, 5, TFT_BLACK);
    display.fillCircle(rx, cy - 10, 5, TFT_BLACK);
    display.drawLine((w / 2) - 28, cy + 24, (w / 2) + 28, cy + 24, 0x7BEF);
    display.setTextSize(1);
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.setCursor((w / 2) - 26, cy + 40);
    display.print("SD ERROR");
    display.endWrite();
}

void initializeCoreS3() {
    auto config = M5.config();
    config.clear_display = true;
    config.output_power = true;
    config.internal_imu = true;
    config.internal_rtc = true;
    config.internal_spk = !kPreferMicStabilityOverSpeaker;
    config.internal_mic = kPreferMicStabilityOverSpeaker;

    M5.begin(config);
    M5.Display.setBrightness(128);
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(180);
        M5.Speaker.tone(740.0f, 80);
        delay(20);
        M5.Speaker.tone(980.0f, 80);
    }
}

void initializeLightingAndBoot() {
    lightEngine.begin();
    lightEngine.setBrightness(45);
    lightEngine.emotionColor("calm");
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.endWrite();
}

void initializeStorage() {
    Flic::SdManager::configureBus();
    Flic::SdManager::mount();
    Flic::SdDiagnostics::logSdStatus();
    if (kRunSdDeepVerifyOnBoot) {
        Flic::SdManager::verify();
    }
    if (!Flic::SdManager::isMounted()) {
        gUseSdFallbackFace = true;
        Serial.println("Flic: NO-SD mode active - using built-in faces and boot indicator.");
        Serial.println("Flic: continuing without SD-backed animations.");
        return;
    }
    gUseSdFallbackFace = false;
    settingsManager.begin();
}

void initializeWiFi() {
    Serial.println("Flic: Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(Flic::kWiFiSSID, Flic::kWiFiPassword);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < Flic::kWiFiConnectTimeoutMs) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println(String("Flic: WiFi connected! IP: ") + WiFi.localIP().toString());
    } else {
        Serial.println();
        Serial.println("Flic: WiFi connection timeout. Continuing without WiFi.");
    }
}

void initializeCoreEngines() {
    memoryManager.begin();
    faceEngine.begin();
    faceSettingsManager.begin(&faceEngine);
    emotionEngine.begin(&lightEngine, &memoryManager, &animationEngine, &faceEngine);
    if (kEnableUsbRuntime) {
        usbEngine.begin(Flic::kDefaultUsbBaud);
    }

    const bool hadRealAnimations = animationEngine.begin() && animationEngine.hasRealAnimations();
    const bool hasFaceIdle = faceEngine.isEmotionAvailable("idle");
    if (!hadRealAnimations && !hasFaceIdle) {
        animationEngine.generateFirstAnimationIfNeeded();
    }

    // Animation verification for required sets
    Serial.println("Flic: Verifying required face animations...");
    faceEngine.verifyAnimationSet("idle_breathing");
    faceEngine.verifyAnimationSet("emotion_calm");
    faceEngine.verifyAnimationSet("emotion_curious");
    faceEngine.verifyAnimationSet("emotion_happy");
    faceEngine.verifyAnimationSet("emotion_sleepy");
    faceEngine.verifyAnimationSet("emotion_surprised");

    proposalSystem.begin(&memoryManager, &emotionEngine);
    personalityUi.begin(&emotionEngine, &lightEngine, &faceEngine);
    deviceLearning.begin(&memoryManager, &proposalSystem);
    learningEngine.begin(&memoryManager, &deviceLearning, &faceEngine);
    textBubbles.begin();
    communicationEngine.begin(&lightEngine, &personalityUi, &animationEngine, &emotionEngine, &memoryManager, &textBubbles,
                              &voiceEngine, &faceEngine);
    if (kEnableMilestoneRuntime) {
        milestoneEngine.begin(&memoryManager, &animationEngine, &emotionEngine, &communicationEngine);
    }
}

void initializeInteractionEngines() {
    touchEngine.begin(&touchInput);
    voiceEngine.begin(&audioInput, &audioOutput, &faceEngine);
    audioOutput.setAmplitudeEnvelopeHandler(onTtsAmplitudeEnvelope, nullptr);
    asrEngine.begin();
    cameraEngine.begin();
    if (!kDisableImuEngine) {
        imuEngine.begin();
    }
    environmentLightEngine.begin();
}

void runBootExpressionDemoIfEnabled() {
    if (!kRunBootExpressionDemo) {
        return;
    }

    const char* sequence[] = {
        "idle_breathing",
        "emotion_calm",
        "emotion_curious",
        "emotion_happy",
        "emotion_sleepy",
        "emotion_surprised",
        "idle_breathing",
    };

    Serial.println("Flic: boot expression demo start");
    for (const char* preset : sequence) {
        if (!animationEngine.playPreset(String(preset))) {
            Serial.printf("Flic: demo preset failed: %s\n", preset);
            delay(250);
            continue;
        }

        // Give the animation queue time to start playback.
        delay(120);

        // Hold until playback finishes or timeout (keeps setup moving if SD content is missing).
        const unsigned long timeoutStart = millis();
        while (animationEngine.isPlaying()) {
            if ((millis() - timeoutStart) > 2400UL) {
                break;
            }
            delay(20);
        }

        // Brief beat between expressions for readability.
        delay(120);
    }
    Serial.println("Flic: boot expression demo end");
}

void handleAsrTranscripts() {
    String transcript;
    String source;
    if (!asrEngine.popTranscript(transcript, source)) {
        return;
    }

    String lower = transcript;
    lower.toLowerCase();
    learnFromUserSentence(lower);

    String reply;
    String replyEmotion = "calm";
    if (lower.indexOf("hello") >= 0 || lower.indexOf("hi") >= 0) {
        reply = "Hi! I'm listening.";
        replyEmotion = "happy";
    } else if (lower.indexOf("how are you") >= 0) {
        reply = "I'm doing well and ready to help.";
        replyEmotion = "calm";
    } else if (lower.indexOf("name") >= 0) {
        reply = "I'm Flic.";
        replyEmotion = "happy";
    } else if (lower.indexOf("what can you do") >= 0) {
        reply = "I can react, emote, and respond with text bubbles.";
        replyEmotion = "curious";
    } else {
        reply = String("I heard: ") + transcript;
        replyEmotion = communicationEngine.inferEmotionFromText(transcript);
    }

    communicationEngine.notify(reply, replyEmotion);
    memoryManager.recordEvent("voice_text", transcript);
    memoryManager.recordEvent("voice_source", source);
}

void initializeRuntimeServices() {
    if (Flic::SdManager::fileExists(kFirstAnimationFlagPath)) {
        lightEngine.flash(0, 255, 0, 1);
    }

    lastLoopMs = millis();
    lastMotionNotifyMs = 0;
    lastImuNotifyMs = 0;
    lastWebHeartbeatMs = 0;
    lastVoiceUpdateMs = 0;
    lastVoiceInputPollMs = 0;
    lastVoiceFeedbackMs = 0;
    lastAutonomySpeakMs = 0;
    lastAutonomyNeedTickMs = 0;
    const unsigned long nowMs = millis();
    lastTouchMs = nowMs;
    lastVoiceMs = nowMs;
    lastMotionMs = nowMs;

    idleBehavior.begin(&lightEngine, &emotionEngine, &personalityUi);

    if (Flic::kWebUiSsid[0] != '\0') {
        webUiEngine.setDataProvider(composeWebUiState);
        webUiEngine.setStatusProvider(composeWebUiStatus);
        webUiEngine.setSensorsProvider(composeWebUiSensors);
        webUiEngine.setEnginesProvider(composeWebUiEngines);
        webUiEngine.setSettingsHandler(handleWebUiSettings);
        webUiEngine.setCommandHandler(handleWebUiCommand);
        webUiEngine.setFaceSettingsProvider(composeFaceSettingsJson);
        webUiEngine.setFaceSettingsHandler(handleWebUiFaceSettings);
        webUiEngine.setFaceStylesProvider(composeFaceStylesJson);
        webUiEngine.setFaceAnimationsCatalogProvider(composeFaceAnimationsCatalogJson);
        webUiEngine.setFaceAnimationsProvider(composeFaceAnimationsJson);
        webUiEngine.setFacePreviewHandler(handleWebUiFacePreview);
        webUiEngine.setFaceSetStyleHandler(handleWebUiFaceSetStyle);
        webUiEngine.setFaceSetAnimationHandler(handleWebUiFaceSetAnimation);
        webUiEngine.setFacePlayHandler(handleWebUiFacePlay);
        webUiEngine.setFaceSetEmotionHandler(handleWebUiFaceSetEmotion);
        webUiEngine.setFaceReloadHandler(handleWebUiFaceReload);
        webUiEngine.setFaceValidateProvider(composeFaceValidateJson);
        webUiEngine.setFaceTelemetryProvider(composeFaceTelemetryJson);
        webUiEngine.setFaceTelemetryHandler(handleWebUiFaceTelemetry);
        webUiEngine.setFaceSnapshotPathProvider(composeFaceSnapshotPath);
        if (webUiEngine.begin(Flic::kWebUiSsid, Flic::kWebUiPassword, Flic::kWebUiHttpPort, Flic::kWebUiWsPort)) {
            Flic::WebUiEventHook::setSender(sendWebUiHookEvent);
            String webUiInfo = String("WebUI: http://") + webUiEngine.localIp().toString() + ":" + String(Flic::kWebUiHttpPort);
            if (webUiEngine.apMode()) {
                Serial.println("Flic: WebUI AP fallback mode enabled.");
                Serial.println("Flic: AP SSID=Flic-Setup password=flic-dev-only");
                Serial.println(webUiInfo);
                if (kShowWebUiTextBubbles) {
                    textBubbles.showMessage("WebUI AP mode\nSSID: Flic-Setup\nPW: flic-dev-only\n" + webUiInfo, Flic::BubbleSize::Large, "curious");
                }
            } else {
                Serial.println(String("Flic: WebUI ready at http://") + webUiEngine.localIp().toString());
                if (kShowWebUiTextBubbles) {
                    textBubbles.showMessage(webUiInfo, Flic::BubbleSize::Medium, "happy");
                }
            }
            webUiLog("info", "WebUI connected");
            applyRuntimeSettings();
        } else {
            Serial.println("Flic: WebUI disabled (Wi-Fi unavailable or connect timeout).");
            if (kShowWebUiTextBubbles) {
                textBubbles.showMessage("WebUI unavailable\nCheck WiFi credentials", Flic::BubbleSize::Medium, "warning");
            }
            Flic::WebUiEventHook::setSender(nullptr);
        }
    } else {
        Serial.println("Flic: WebUI disabled (SSID not configured).");
        if (kShowWebUiTextBubbles) {
            textBubbles.showMessage("WebUI disabled\nSSID not configured", Flic::BubbleSize::Medium, "warning");
        }
        Flic::WebUiEventHook::setSender(nullptr);
    }
}

void runFirstAnimationSequence() {
    if (!animationEngine.playAnimation(kAnimationName)) {
        return;
    }

    Serial.println("Flic: first animation played.");
    memoryManager.recordEvent("animation", kAnimationName);
    lightEngine.emotionColor("happy");
    lightEngine.flash(0, 255, 80, 2);
    emotionEngine.setEmotion("happy");
    milestoneEngine.unlock("first_animation", "animation_created");
}

void showEmergencyScreenIfEnabled() {
    if (!kEmergencyMinimalLoop) {
        return;
    }

    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(12, 24);
    M5.Display.print("SAFE MODE");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(12, 56);
    M5.Display.print("Loop minimized for recovery");
    M5.Display.setCursor(12, 72);
    M5.Display.print("Serial/flash path remains active");
    M5.Display.endWrite();

    Serial.println("Flic: emergency minimal loop enabled.");
}

void updateRuntimeEngines(float dtSeconds) {
    emotionEngine.updateEmotion(dtSeconds);
    faceEngine.updateMood(dtSeconds);
    faceEngine.updateMemory(dtSeconds);
    faceEngine.updateAdaptiveExpressions(dtSeconds);
    faceEngine.updateAutoEmotion(dtSeconds);
    faceEngine.update(dtSeconds);
    faceEngine.drawFrame();
    audioOutput.update();
    communicationEngine.update();
    if (kEnableMilestoneRuntime) {
        milestoneEngine.update();
    }
    if (!kDisableVoiceRuntime) {
        const unsigned long nowMs = millis();
        if (!kEnableVoiceLiteMode || (nowMs - lastVoiceUpdateMs) >= kVoiceLiteUpdateIntervalMs) {
            voiceEngine.update();
            voiceEngine.updateVoiceEngine(dtSeconds);
            lastVoiceUpdateMs = nowMs;
        }
    }
    webUiEngine.update();
    cameraEngine.update();
    if (!kDisableImuEngine) {
        imuEngine.update();
    }
    environmentLightEngine.update();
}

void recordDetailedEvent(const String& type, const String& name, const String& detail) {
    memoryManager.recordEvent(type, name + ":" + detail);
}

void handleImuNotification(const String& imuEvent) {
    if (imuEvent == "shake") {
        communicationEngine.notify("Whoa, dizzy!", "warning");
    } else if (imuEvent == "pickup") {
        communicationEngine.notify("I'm awake.", "happy");
    } else if (imuEvent == "stillness") {
        communicationEngine.notify("Quiet mode.", "sleepy");
    }
}

void handleTouchInput() {
    String touchGesture;
    String touchMeaning;
    if (!touchEngine.poll(touchGesture, touchMeaning)) {
        return;
    }

    memoryManager.recordEvent(kEventTouch, touchGesture + ":" + touchMeaning);
    lastTouchGesture = touchGesture;
    lastTouchMeaning = touchMeaning;
    learningEngine.observeTouch(touchGesture);
    emotionEngine.observeTouch(touchGesture, touchMeaning);
    communicationEngine.handleTouchMeaning(touchMeaning);
    lastTouchMs = millis();
    needConnection = clampNeed(needConnection - 0.20f);
    needPlay = clampNeed(needPlay - 0.10f);
}

void handleVoiceInput() {
    String voiceCommand;
    if (voiceEngine.popVoiceCommand(voiceCommand)) {
        faceEngine.play("listening");
        String normalizedVoice = voiceCommand;
        normalizedVoice.trim();
        normalizedVoice.toLowerCase();

        // Handle WebUI info requests
        if (normalizedVoice.indexOf("webui") >= 0 || normalizedVoice.indexOf("web ui") >= 0 || 
            normalizedVoice.indexOf("ip") >= 0 || normalizedVoice.indexOf("address") >= 0 ||
            normalizedVoice.indexOf("connect") >= 0 || normalizedVoice.indexOf("access") >= 0) {

            const String ipInfo = composeWebUiHelpMessage();
            textBubbles.showMessage(ipInfo, Flic::BubbleSize::Large, "curious");
            emotionEngine.nudgeEmotion("happy", 0.15f);
            communicationEngine.speakText("Here is my web interface information: " + ipInfo);
            lastVoiceFeedbackMs = millis();
            return;
        }

        if (normalizedVoice == "wake" || normalizedVoice == "listen") {
            const unsigned long nowMs = millis();
            if ((nowMs - lastVoiceFeedbackMs) >= kVoiceLiteFeedbackCooldownMs) {
                if (kVoiceLiteAutoSentenceReplies) {
                    static uint8_t replyIndex = 0;
                    const char* replies[] = {
                        "I heard you.",
                        "I'm listening.",
                        "Tell me more.",
                        "I am here with you.",
                        "Okay, go on.",
                    };
                    constexpr uint8_t kReplyCount = sizeof(replies) / sizeof(replies[0]);
                    textBubbles.showMessage(replies[replyIndex], Flic::BubbleSize::Medium, "calm");
                    replyIndex = static_cast<uint8_t>((replyIndex + 1) % kReplyCount);
                    emotionEngine.nudgeEmotion("curious", 0.12f);
                } else {
                    textBubbles.showMessage("I heard you.", Flic::BubbleSize::Small, "calm");
                    emotionEngine.nudgeEmotion("calm", 0.10f);
                }
                lastVoiceFeedbackMs = nowMs;
            }
            lastVoiceMs = nowMs;
            needConnection = clampNeed(needConnection - 0.10f);
            return;
        }

        if (normalizedVoice.length() > 0) {
            learningEngine.observeVoice(voiceCommand);
            emotionEngine.observeVoice(voiceCommand);
            learnFromUserSentence(voiceCommand);
            const String inferredEmotion = communicationEngine.inferEmotionFromText(voiceCommand);
            communicationEngine.notify(String("Voice: ") + voiceCommand, inferredEmotion);
        }
    }

    String soundEvent;
    if (voiceEngine.popSoundEvent(soundEvent)) {
        if (kEnableVoiceLiteMode && kVoiceLiteSuppressSoundEvents) {
            return;
        }
        learningEngine.observeMotion(soundEvent);
        emotionEngine.observeMotion("audio", "loud_sound", soundEvent);
        communicationEngine.notify(String("Sound: ") + soundEvent, "warning");
        lastMotionMs = millis();
    }
}

void handleCameraEvents() {
    String cameraEvent;
    String cameraDetail;
    if (!cameraEngine.popEvent(cameraEvent, cameraDetail)) {
        return;
    }

    recordDetailedEvent(kEventCamera, cameraEvent, cameraDetail);
    lastCameraEventName = cameraEvent;
    lastCameraEventDetail = cameraDetail;
    emotionEngine.observeMotion("camera", cameraEvent, cameraDetail);
    const unsigned long now = millis();
    if (cameraEvent == "motion" && (now - lastMotionNotifyMs) >= kMotionNotifyCooldownMs) {
        communicationEngine.notify("Motion detected", "curious");
        lastMotionNotifyMs = now;
    }
}

void handleImuEvents() {
    String imuEvent;
    String imuDetail;
    if (!imuEngine.popEvent(imuEvent, imuDetail)) {
        return;
    }

    recordDetailedEvent(kEventImu, imuEvent, imuDetail);
    lastImuEventName = imuEvent;
    lastImuEventDetail = imuDetail;
    emotionEngine.observeMotion("imu", imuEvent, imuDetail);
    lastMotionMs = millis();
    needPlay = clampNeed(needPlay - 0.10f);
    const unsigned long now = millis();
    if ((now - lastImuNotifyMs) >= kImuNotifyCooldownMs) {
        handleImuNotification(imuEvent);
        lastImuNotifyMs = now;
    }
}

void handleEnvironmentLightEvents() {
    String lightEvent;
    String lightDetail;
    if (!environmentLightEngine.popEvent(lightEvent, lightDetail)) {
        return;
    }

    recordDetailedEvent(kEventLight, lightEvent, lightDetail);
    lastLightEventName = lightEvent;
    lastLightEventDetail = lightDetail;
    emotionEngine.observeMotion("light", lightEvent, lightDetail);
    if (lightEvent == "dark") {
        needLight = clampNeed(needLight + 0.22f);
    } else if (lightEvent == "bright") {
        needLight = clampNeed(needLight - 0.18f);
    }
    if (lightEvent == kHandWaveEvent) {
        communicationEngine.notify("Hand wave detected", "curious");
        needPlay = clampNeed(needPlay - 0.15f);
        lastMotionMs = millis();
    }
}

void handleUsbMessage(const String& message) {
    if (message.length() == 0) {
        return;
    }

    auto publishLearningSignal = []() {
        if (!deviceLearning.learnedSomething()) {
            return;
        }
        memoryManager.recordEvent(kEventNewPattern, deviceLearning.lastPattern());
        personalityUi.showLearningEvent(deviceLearning.lastLearningNote());
        emotionEngine.nudgeEmotion("curious", 0.3f);
        deviceLearning.clearLearningSignal();
    };

    if (message.startsWith(kDeviceIdPrefix)) {
        const String deviceId = message.substring(kDeviceIdPrefixLength);
        memoryManager.recordEvent(kEventDeviceIdentified, deviceId);
        personalityUi.showDeviceIdentified(deviceId);
        emotionEngine.nudgeEmotion("curious", 0.35f);
        emotionEngine.observeUsb(message);
        learningEngine.observeUsb(deviceId, message);
        publishLearningSignal();
        return;
    }

    if (message.startsWith(kCapabilitiesPrefix) || message.startsWith(kCapabilitiesShortPrefix) ||
        message.startsWith(kFeaturesPrefix)) {
        const String deviceId = usbEngine.connectedDeviceId();
        if (deviceId.length() == 0) {
            return;
        }
        String capabilities = message;
        if (message.startsWith(kCapabilitiesPrefix)) {
            capabilities = message.substring(kCapabilitiesPrefixLength);
        } else if (message.startsWith(kCapabilitiesShortPrefix)) {
            capabilities = message.substring(kCapabilitiesShortPrefixLength);
        } else if (message.startsWith(kFeaturesPrefix)) {
            capabilities = message.substring(kFeaturesPrefixLength);
        }
        capabilities.trim();
        memoryManager.recordEvent(kEventDeviceCapabilities, capabilities);
        emotionEngine.observeUsb(message);
        learningEngine.observeUsb(deviceId, message);
        publishLearningSignal();
        return;
    }

    const String deviceId = usbEngine.connectedDeviceId();
    if (deviceId.length() == 0) {
        return;
    }

    learningEngine.observeUsb(deviceId, message);
    emotionEngine.observeUsb(message);

    const String inferredEmotion = communicationEngine.inferEmotionFromText(message);
    communicationEngine.notify(message, inferredEmotion);
    memoryManager.recordEvent(kEventUsbText, message);
    publishLearningSignal();
}

void handleUsbEvents() {
    const bool usbConnected = usbEngine.deviceConnected();
    if (usbEngine.connectionJustEstablished()) {
        memoryManager.recordEvent(kEventDeviceConnected, kUsbCdcLabel);
        personalityUi.showDeviceConnected("USB CDC device");
    }

    if (!usbConnected) {
        return;
    }

    uint8_t processed = 0;
    while (processed < kMaxUsbMessagesPerLoop) {
        const String message = usbEngine.readMessage();
        if (message.length() == 0) {
            break;
        }
        handleUsbMessage(message);
        ++processed;
    }

    String approvedCommand;
    if (!proposalSystem.consumeApprovedUsbCommand(usbEngine.connectedDeviceId(), approvedCommand)) {
        return;
    }

    if (usbEngine.sendApprovedCommand(approvedCommand)) {
        communicationEngine.notify(String("TX: ") + approvedCommand, "happy");
        memoryManager.recordEvent(kEventCommandSent, approvedCommand);
        personalityUi.showCommandApproved(approvedCommand);
    } else {
        communicationEngine.notify(String("TX blocked: ") + approvedCommand, "warning");
        memoryManager.recordEvent(kEventCommandRejected, approvedCommand);
        personalityUi.showCommandRejected(approvedCommand);
    }
}
}

void setup() {
    Serial.begin(115200);
    delay(50);

    if (kTestMinimalBoot) {
        M5.begin();
        M5.Display.setBrightness(128);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(12, 24);
        M5.Display.print("MINIMAL BOOT");
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(12, 64);
        M5.Display.print("Core init only. No engines.");
        M5.Display.setCursor(12, 80);
        M5.Display.print("Checking for resets...");
        Serial.println("[BOOT] Minimal boot mode active - no engine initialization");
        return;
    }

    Serial.println("\n========== Flic Boot: BEGIN ==========");
    initializeCoreS3();
    bootStartMs = millis();

    Serial.println("[BOOT] Mounting SD card...");
    initializeStorage();
    Flic::SdManager::printBootSummary();
    Serial.println("[BOOT] SD mount and verification complete.");

    initializeLightingAndBoot();
    if (gUseSdFallbackFace
#if defined(VECTOR_ONLY_FACE) && VECTOR_ONLY_FACE
        && false
#endif
    ) {
        Serial.println("[BOOT] SD fallback face enabled.");
        renderBuiltInFallbackFace();
        gLastFallbackFaceDrawMs = millis();
    }

    Serial.println("[BOOT] Initializing core engines...");
    initializeCoreEngines();
    initializeInteractionEngines();
    if (kRunCreatureVoiceSelfTestOnBoot) {
        voiceEngine.setVoiceEmotionState("curious");
        voiceEngine.speakTextCreature("hmm hi friend tiny creature voice online");
    }
    loadRuntimeSettings();
    applyRuntimeSettings();
    runBootExpressionDemoIfEnabled();

    if (!kSafeBootMode) {
        Serial.println("[BOOT] Connecting to WiFi and starting runtime services...");
        initializeWiFi();
        initializeRuntimeServices();
        const bool hasFaceIdle = faceEngine.loadAnimation("idle");
        if (!hasFaceIdle && !gUseSdFallbackFace) {
            Serial.println("[BOOT] No idle face animation found, running first animation sequence...");
            runFirstAnimationSequence();
            delay(900);
        }
        if (kShowWebUiTextBubbles) {
            const String startupWebUi = composeWebUiHelpMessage();
            textBubbles.showMessage(startupWebUi, Flic::BubbleSize::Large, "curious");
        }
    }
    showEmergencyScreenIfEnabled();

    postInitVoiceTestUntilMs = millis() + 120000UL;
    Serial.println("[BOOT] Runtime voice self-test window armed (120s)");
    Serial.println("========== Flic Boot: END ==========");
    Serial.println("[BOOT] System initialization complete.\n");
}

void loop() {
    M5.update();

    const unsigned long now = millis();
    float dtSeconds = static_cast<float>(now - lastLoopMs) / 1000.0f;
    if (dtSeconds < 0.001f) {
        dtSeconds = 0.001f;
    } else if (dtSeconds > 0.100f) {
        dtSeconds = 0.100f;
    }
    lastLoopMs = now;

    if (kEmergencyMinimalLoop) {
        static unsigned long lastSafePulseMs = 0;
        const unsigned long nowMs = millis();
        if ((nowMs - lastSafePulseMs) >= 1500) {
            lastSafePulseMs = nowMs;
            lightEngine.flash(255, 200, 0, 1);
        }
        delay(Flic::kFrameDelayMs);
        return;
    }

    updateRuntimeEngines(dtSeconds);

    if (kRunCreatureVoiceSelfTestInLoop && millis() < postInitVoiceTestUntilMs) {
        static unsigned long lastVoiceSelfTestMs = 0;
        const unsigned long nowMs = millis();
        if ((nowMs - lastVoiceSelfTestMs) >= 3000UL) {
            lastVoiceSelfTestMs = nowMs;
            Serial.println("[VoiceTrace] runtime self-test firing");
            M5.Speaker.setVolume(180);
            // Direct talker-mode tones (independent of voice engine) for hard audio validation.
            M5.Speaker.tone(420.0f, 40);
            delay(12);
            M5.Speaker.tone(640.0f, 40);
            delay(12);
            M5.Speaker.tone(520.0f, 36);
            delay(12);
            M5.Speaker.tone(760.0f, 44);
            delay(12);
            M5.Speaker.tone(580.0f, 32);
            voiceEngine.setVoiceEmotionState("curious");
            voiceEngine.speakTextCreature("gremlin voice check runtime");
        }
    }

    if (gUseSdFallbackFace
#if defined(VECTOR_ONLY_FACE) && VECTOR_ONLY_FACE
        && false
#endif
    ) {
        const unsigned long nowMs = millis();
        if ((nowMs - gLastFallbackFaceDrawMs) >= 1500UL) {
            renderBuiltInFallbackFace();
            gLastFallbackFaceDrawMs = nowMs;
        }
    }
    asrEngine.update();
    handleTouchInput();
    if (!kDisableVoiceInputHandling && runtimeSettings.voiceInputEnabled) {
        const unsigned long nowMs = millis();
        if (!kEnableVoiceLiteMode || (nowMs - lastVoiceInputPollMs) >= kVoiceLiteInputPollIntervalMs) {
            handleVoiceInput();
            lastVoiceInputPollMs = nowMs;
        }
    }
    handleAsrTranscripts();
    handleCameraEvents();
    if (!kDisableImuEngine && runtimeSettings.imuEventsEnabled) {
        handleImuEvents();
    }
    handleEnvironmentLightEvents();
    if (kEnableUsbRuntime && runtimeSettings.usbEventsEnabled) {
        handleUsbEvents();
    }
    if (runtimeSettings.autonomyEnabled) {
        maybeSpeakAutonomously();
    }

    const unsigned long nowMs = millis();
    if (runtimeSettings.webHeartbeatEnabled && (nowMs - lastWebHeartbeatMs) >= kWebHeartbeatMs) {
        lastWebHeartbeatMs = nowMs;
        webUiEngine.publishHeartbeat(nowMs, emotionEngine.getEmotion(), animationEngine.isPlaying());
    }

    if (kShowWebUiTextBubbles && !kSafeBootMode && webUiEngine.isReady() && (nowMs - bootStartMs) < 180000UL && (nowMs - lastWebUiHintBubbleMs) >= 25000UL) {
        lastWebUiHintBubbleMs = nowMs;
        textBubbles.showMessage(composeWebUiHelpMessage(), Flic::BubbleSize::Large, "curious");
    }

    idleBehavior.update(animationEngine.isPlaying());
    proposalSystem.update(animationEngine.isPlaying());
    lightEngine.update();
    delay(Flic::kFrameDelayMs);
}
