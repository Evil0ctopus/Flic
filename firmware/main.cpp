#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>

#include "config.h"
#include "config_webui.h"
#include "engine/emotion_engine.h"
#include "engine/device_learning.h"
#include "engine/communication_engine.h"
#include "engine/touch_input.h"
#include "engine/touch_engine.h"
#include "engine/voice_engine.h"
#include "engine/webui_engine.h"
#include "engine/audio_input.h"
#include "engine/audio_output.h"
#include "engine/camera_engine.h"
#include "engine/environment_light_engine.h"
#include "engine/imu_engine.h"
#include "engine/learning_engine.h"
#include "engine/milestone_engine.h"
#include "engine/animation_engine.h"
#include "engine/idle_behavior.h"
#include "engine/memory_manager.h"
#include "engine/proposal_system.h"
#include "subsystems/light_engine.h"
#include "subsystems/sd_manager.h"
#include "subsystems/usb_engine.h"
#include "diagnostics/debug_log.h"
#include "diagnostics/webui_event_hook.h"
#include "ui/personality_ui.h"
#include "ui/boot_animation.h"
#include "ui/text_bubbles.h"

namespace {
struct RuntimeSettings {
    uint8_t brightness = 20;
    uint8_t volume = 180;
    float personalityEnergy = 0.5f;
    float personalityCuriosity = 0.5f;
    float personalityPatience = 0.5f;
    float emotionBias = 0.0f;
    float animationSpeed = 1.0f;
    bool debugEnabled = false;
    bool traceEnabled = false;
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
Flic::MilestoneEngine milestoneEngine;
Flic::TextBubbles textBubbles;
Flic::CommunicationEngine communicationEngine;
Flic::TouchInput touchInput;
Flic::TouchEngine touchEngine;
Flic::AudioInput audioInput;
Flic::AudioOutput audioOutput;
Flic::VoiceEngine voiceEngine;
Flic::WebUiEngine webUiEngine;
Flic::CameraEngine cameraEngine;
Flic::ImuEngine imuEngine;
Flic::EnvironmentLightEngine environmentLightEngine;
RuntimeSettings runtimeSettings;
constexpr const char* kAnimationName = "flic_first_animation.json";
constexpr const char* kFirstAnimationFlagPath = "/ai/memory/first_animation_created.flag";
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
constexpr unsigned long kMotionNotifyCooldownMs = 1200;
constexpr unsigned long kImuNotifyCooldownMs = 900;
constexpr unsigned long kWebHeartbeatMs = 1000;
constexpr bool kEmergencyMinimalLoop = false;
constexpr bool kDisableVoiceRuntime = true;
constexpr bool kDisableVoiceInputHandling = true;
constexpr bool kDisableImuEngine = true;
constexpr bool kEnableUsbRuntime = true;

unsigned long lastLoopMs = 0;
unsigned long lastMotionNotifyMs = 0;
unsigned long lastImuNotifyMs = 0;
unsigned long lastWebHeartbeatMs = 0;

String lastImuEventName;
String lastImuEventDetail;
String lastLightEventName;
String lastLightEventDetail;
String lastTouchGesture;
String lastTouchMeaning;
String lastCameraEventName;
String lastCameraEventDetail;

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

void applyRuntimeSettings() {
    lightEngine.setBrightness(runtimeSettings.brightness);
    audioOutput.setVolume(runtimeSettings.volume);
    personalityUi.setPersonality(runtimeSettings.personalityEnergy, runtimeSettings.personalityCuriosity,
                                 runtimeSettings.personalityPatience);
    emotionEngine.setEmotionBias(runtimeSettings.emotionBias);
    animationEngine.setPlaybackSpeed(runtimeSettings.animationSpeed);

    if (runtimeSettings.traceEnabled) {
        Flic::Debug::setRuntimeLogLevel(5);
    } else if (runtimeSettings.debugEnabled) {
        Flic::Debug::setRuntimeLogLevel(4);
    } else {
        Flic::Debug::setRuntimeLogLevel(3);
    }
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

    if (!changed) {
        responseJson = "{\"ok\":false,\"error\":\"no_valid_settings_provided\"}";
        return false;
    }

    runtimeSettings = nextSettings;
    applyRuntimeSettings();
    responseJson = "{\"ok\":true,\"applied\":true}";
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

void initializeCoreS3() {
    auto config = M5.config();
    config.clear_display = true;
    config.output_power = true;
    config.internal_imu = true;
    config.internal_rtc = true;
    config.internal_spk = true;
    config.internal_mic = true;

    M5.begin(config);
    M5.Display.setBrightness(128);
}

void initializeLightingAndBoot() {
    lightEngine.begin();
    lightEngine.emotionColor("calm");
    Flic::showBootAnimation(lightEngine);
}

void initializeStorage() {
    Flic::SdManager::configureBus();
    if (!Flic::SdManager::mount()) {
        Serial.println("Flic: continuing without SD-backed animations.");
    }
}

void initializeCoreEngines() {
    memoryManager.begin();
    emotionEngine.begin(&lightEngine, &memoryManager, &animationEngine);
    if (kEnableUsbRuntime) {
        usbEngine.begin(Flic::kDefaultUsbBaud);
    }

    const bool hadRealAnimations = animationEngine.begin() && animationEngine.hasRealAnimations();
    if (!hadRealAnimations) {
        animationEngine.generateFirstAnimationIfNeeded();
    }

    proposalSystem.begin(&memoryManager, &emotionEngine);
    personalityUi.begin(&emotionEngine, &lightEngine);
    deviceLearning.begin(&memoryManager, &proposalSystem);
    learningEngine.begin(&memoryManager, &deviceLearning);
    textBubbles.begin();
    communicationEngine.begin(&lightEngine, &personalityUi, &animationEngine, &emotionEngine, &memoryManager, &textBubbles, &voiceEngine);
    // Stable baseline: milestone engine intentionally disabled for now.
    // milestoneEngine.begin(&memoryManager, &animationEngine, &emotionEngine, &communicationEngine);
}

void initializeInteractionEngines() {
    touchEngine.begin(&touchInput);
    voiceEngine.begin(&audioInput, &audioOutput);
    cameraEngine.begin();
    if (!kDisableImuEngine) {
        imuEngine.begin();
    }
    environmentLightEngine.begin();
}

void initializeRuntimeServices() {
    if (Flic::SdManager::fileExists(kFirstAnimationFlagPath)) {
        lightEngine.flash(0, 255, 0, 1);
    }

    lastLoopMs = millis();
    lastMotionNotifyMs = 0;
    lastImuNotifyMs = 0;
    lastWebHeartbeatMs = 0;

    idleBehavior.begin(&lightEngine, &emotionEngine, &personalityUi);

    if (Flic::kWebUiSsid[0] != '\0') {
        webUiEngine.setDataProvider(composeWebUiState);
        webUiEngine.setStatusProvider(composeWebUiStatus);
        webUiEngine.setSensorsProvider(composeWebUiSensors);
        webUiEngine.setEnginesProvider(composeWebUiEngines);
        webUiEngine.setSettingsHandler(handleWebUiSettings);
        if (webUiEngine.begin(Flic::kWebUiSsid, Flic::kWebUiPassword, Flic::kWebUiHttpPort, Flic::kWebUiWsPort)) {
            Flic::WebUiEventHook::setSender(sendWebUiHookEvent);
            Serial.println(String("Flic: WebUI ready at http://") + webUiEngine.localIp().toString());
            webUiLog("info", "WebUI connected");
            applyRuntimeSettings();
        } else {
            Serial.println("Flic: WebUI disabled (Wi-Fi unavailable or connect timeout).");
            Flic::WebUiEventHook::setSender(nullptr);
        }
    } else {
        Serial.println("Flic: WebUI disabled (SSID not configured).");
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
    communicationEngine.update();
    // Stable baseline: milestone engine intentionally disabled for now.
    // milestoneEngine.update();
    if (!kDisableVoiceRuntime) {
        voiceEngine.update();
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
}

void handleVoiceInput() {
    String voiceCommand;
    if (voiceEngine.popVoiceCommand(voiceCommand)) {
        String normalizedVoice = voiceCommand;
        normalizedVoice.trim();
        normalizedVoice.toLowerCase();

        if (normalizedVoice != "wake" && normalizedVoice != "listen" && normalizedVoice.length() > 0) {
            learningEngine.observeVoice(voiceCommand);
            emotionEngine.observeVoice(voiceCommand);
            const String inferredEmotion = communicationEngine.inferEmotionFromText(voiceCommand);
            communicationEngine.notify(String("Voice: ") + voiceCommand, inferredEmotion);
        }
    }

    String soundEvent;
    if (voiceEngine.popSoundEvent(soundEvent)) {
        learningEngine.observeMotion(soundEvent);
        emotionEngine.observeMotion("audio", "loud_sound", soundEvent);
        communicationEngine.notify(String("Sound: ") + soundEvent, "warning");
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
    if (lightEvent == kHandWaveEvent) {
        communicationEngine.notify("Hand wave detected", "curious");
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

    initializeCoreS3();

    initializeLightingAndBoot();
    initializeStorage();
    initializeCoreEngines();
    initializeInteractionEngines();
    initializeRuntimeServices();
    runFirstAnimationSequence();
    showEmergencyScreenIfEnabled();
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
    handleTouchInput();
    if (!kDisableVoiceInputHandling) {
        handleVoiceInput();
    }
    handleCameraEvents();
    if (!kDisableImuEngine) {
        handleImuEvents();
    }
    handleEnvironmentLightEvents();
    if (kEnableUsbRuntime) {
        handleUsbEvents();
    }

    const unsigned long nowMs = millis();
    if ((nowMs - lastWebHeartbeatMs) >= kWebHeartbeatMs) {
        lastWebHeartbeatMs = nowMs;
        webUiEngine.publishHeartbeat(nowMs, emotionEngine.getEmotion(), animationEngine.isPlaying());
    }

    idleBehavior.update(animationEngine.isPlaying());
    proposalSystem.update(animationEngine.isPlaying());
    delay(Flic::kFrameDelayMs);
}
