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
#include "engine/asr_engine.h"
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
Flic::AsrEngine asrEngine;
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
        String msg = "AP Mode\nSSID: Flic-Setup\nPW: flic1234\nURL: http://";
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
    msg += "\nPW: flic1234";
    msg += "\nAP URL: http://192.168.4.1";
    return msg;
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
    payload += "\"";
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

void applyRuntimeSettings() {
    lightEngine.setBrightness(runtimeSettings.brightness);
    audioOutput.setVolume(runtimeSettings.volume);
    audioOutput.setVoiceStyle(runtimeSettings.voiceStyle);
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
    responseJson = "{\"ok\":true,\"applied\":true}";
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
    if (lower.indexOf("voice style") >= 0 || lower.indexOf("voice") >= 0) {
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
    if (kEnableMilestoneRuntime) {
        milestoneEngine.begin(&memoryManager, &animationEngine, &emotionEngine, &communicationEngine);
    }
}

void initializeInteractionEngines() {
    touchEngine.begin(&touchInput);
    voiceEngine.begin(&audioInput, &audioOutput);
    asrEngine.begin();
    cameraEngine.begin();
    if (!kDisableImuEngine) {
        imuEngine.begin();
    }
    environmentLightEngine.begin();
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
        if (webUiEngine.begin(Flic::kWebUiSsid, Flic::kWebUiPassword, Flic::kWebUiHttpPort, Flic::kWebUiWsPort)) {
            Flic::WebUiEventHook::setSender(sendWebUiHookEvent);
            String webUiInfo = String("WebUI: http://") + webUiEngine.localIp().toString() + ":" + String(Flic::kWebUiHttpPort);
            if (webUiEngine.apMode()) {
                Serial.println("Flic: WebUI AP fallback mode enabled.");
                Serial.println("Flic: AP SSID=Flic-Setup password=flic1234");
                Serial.println(webUiInfo);
                if (kShowWebUiTextBubbles) {
                    textBubbles.showMessage("WebUI AP mode\nSSID: Flic-Setup\nPW: flic1234\n" + webUiInfo, Flic::BubbleSize::Large, "curious");
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
    communicationEngine.update();
    if (kEnableMilestoneRuntime) {
        milestoneEngine.update();
    }
    if (!kDisableVoiceRuntime) {
        const unsigned long nowMs = millis();
        if (!kEnableVoiceLiteMode || (nowMs - lastVoiceUpdateMs) >= kVoiceLiteUpdateIntervalMs) {
            voiceEngine.update();
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

    initializeCoreS3();
    bootStartMs = millis();

    initializeLightingAndBoot();
    initializeStorage();
    initializeCoreEngines();
    initializeInteractionEngines();
    if (!kSafeBootMode) {
        initializeWiFi();
        initializeRuntimeServices();
        runFirstAnimationSequence();
        delay(900);
        if (kShowWebUiTextBubbles) {
            const String startupWebUi = composeWebUiHelpMessage();
            textBubbles.showMessage(startupWebUi, Flic::BubbleSize::Large, "curious");
        }
    }
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
