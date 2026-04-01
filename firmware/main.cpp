#include <Arduino.h>
#include <M5Unified.h>

#include "config.h"
#include "engine/emotion_engine.h"
#include "engine/device_learning.h"
#include "engine/communication_engine.h"
#include "engine/touch_input.h"
#include "engine/touch_engine.h"
#include "engine/voice_engine.h"
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
#include "ui/personality_ui.h"
#include "ui/boot_animation.h"
#include "ui/text_bubbles.h"

namespace {
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
Flic::CameraEngine cameraEngine;
Flic::ImuEngine imuEngine;
Flic::EnvironmentLightEngine environmentLightEngine;
constexpr const char* kAnimationName = "flic_first_animation.json";
constexpr const char* kFirstAnimationFlagPath = "/ai/memory/first_animation_created.flag";
constexpr const char* kUsbCdcLabel = "usb_cdc";
constexpr const char* kUsbHello = "FLIC_HELLO";
constexpr const char* kDeviceIdPrefix = "DEVICE_ID:";
constexpr size_t kDeviceIdPrefixLength = 10;
constexpr const char* kCapabilitiesPrefix = "CAPABILITIES:";
constexpr size_t kCapabilitiesPrefixLength = 13;
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
    emotionEngine.begin(&lightEngine, &memoryManager);
    usbEngine.begin(Flic::kDefaultUsbBaud);

    const bool hadRealAnimations = animationEngine.begin() && animationEngine.hasRealAnimations();
    if (!hadRealAnimations) {
        animationEngine.generateFirstAnimationIfNeeded();
    }

    proposalSystem.begin(&memoryManager, &emotionEngine);
    personalityUi.begin(&emotionEngine, &lightEngine);
    deviceLearning.begin(&memoryManager, &proposalSystem);
    learningEngine.begin(&memoryManager, &deviceLearning);
    milestoneEngine.begin(&memoryManager);
    textBubbles.begin();
    communicationEngine.begin(&lightEngine, &personalityUi, &animationEngine, &memoryManager, &textBubbles, &voiceEngine);
}

void initializeInteractionEngines() {
    touchEngine.begin(&touchInput);
    voiceEngine.begin(&audioInput, &audioOutput);
    cameraEngine.begin();
    imuEngine.begin();
    environmentLightEngine.begin();
}

void initializeRuntimeServices() {
    if (Flic::SdManager::fileExists(kFirstAnimationFlagPath)) {
        lightEngine.flash(0, 255, 0, 1);
    }

    idleBehavior.begin(&lightEngine, &emotionEngine, &personalityUi);
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

void updateRuntimeEngines() {
    emotionEngine.updateEmotion(0.016f);
    communicationEngine.update();
    milestoneEngine.update();
    voiceEngine.update();
    cameraEngine.update();
    imuEngine.update();
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
    learningEngine.observeTouch(touchGesture);
    communicationEngine.handleTouchMeaning(touchMeaning);
}

void handleVoiceInput() {
    String voiceCommand;
    if (voiceEngine.popVoiceCommand(voiceCommand)) {
        learningEngine.observeVoice(voiceCommand);
        communicationEngine.notify(String("Voice: ") + voiceCommand, "curious");
    }

    String soundEvent;
    if (voiceEngine.popSoundEvent(soundEvent)) {
        learningEngine.observeMotion(soundEvent);
        communicationEngine.speakAnimation("surprise");
    }
}

void handleCameraEvents() {
    String cameraEvent;
    String cameraDetail;
    if (!cameraEngine.popEvent(cameraEvent, cameraDetail)) {
        return;
    }

    recordDetailedEvent(kEventCamera, cameraEvent, cameraDetail);
    if (cameraEvent == "motion") {
        communicationEngine.notify("Motion detected", "curious");
    }
}

void handleImuEvents() {
    String imuEvent;
    String imuDetail;
    if (!imuEngine.popEvent(imuEvent, imuDetail)) {
        return;
    }

    recordDetailedEvent(kEventImu, imuEvent, imuDetail);
    handleImuNotification(imuEvent);
}

void handleEnvironmentLightEvents() {
    String lightEvent;
    String lightDetail;
    if (!environmentLightEngine.popEvent(lightEvent, lightDetail)) {
        return;
    }

    recordDetailedEvent(kEventLight, lightEvent, lightDetail);
    if (lightEvent == kHandWaveEvent) {
        communicationEngine.speakAnimation("blink");
    }
}

void handleUsbMessage(const String& message) {
    if (message.startsWith(kDeviceIdPrefix)) {
        const String deviceId = message.substring(kDeviceIdPrefixLength);
        memoryManager.recordEvent(kEventDeviceIdentified, deviceId);
        personalityUi.showDeviceIdentified(deviceId);
        emotionEngine.nudgeEmotion("curious", 0.35f);
        deviceLearning.processMessage(deviceId, message);
        return;
    }

    if (message.startsWith(kCapabilitiesPrefix)) {
        const String deviceId = usbEngine.connectedDeviceId();
        memoryManager.recordEvent(kEventDeviceCapabilities, message.substring(kCapabilitiesPrefixLength));
        deviceLearning.processMessage(deviceId, message);
        return;
    }

    const String deviceId = usbEngine.connectedDeviceId();
    learningEngine.observeUsb(deviceId, message);

    const String inferredEmotion = communicationEngine.inferEmotionFromText(message);
    communicationEngine.notify(message, inferredEmotion);
    memoryManager.recordEvent(kEventUsbText, message);

    if (deviceLearning.learnedSomething()) {
        memoryManager.recordEvent(kEventNewPattern, deviceLearning.lastPattern());
        personalityUi.showLearningEvent(deviceLearning.lastLearningNote());
        emotionEngine.nudgeEmotion("curious", 0.3f);
        deviceLearning.clearLearningSignal();
    }
}

void handleUsbEvents() {
    const bool usbConnected = usbEngine.deviceConnected();
    if (usbEngine.connectionJustEstablished()) {
        memoryManager.recordEvent(kEventDeviceConnected, kUsbCdcLabel);
        personalityUi.showDeviceConnected("USB CDC device");
        usbEngine.sendMessage(kUsbHello);
    }

    if (!usbConnected) {
        return;
    }

    while (true) {
        const String message = usbEngine.readMessage();
        if (message.length() == 0) {
            break;
        }
        handleUsbMessage(message);
    }

    String approvedCommand;
    if (!proposalSystem.consumeApprovedUsbCommand(usbEngine.connectedDeviceId(), approvedCommand)) {
        return;
    }

    if (usbEngine.sendApprovedCommand(approvedCommand)) {
        communicationEngine.speakText(String("TX: ") + approvedCommand);
        memoryManager.recordEvent(kEventCommandSent, approvedCommand);
        personalityUi.showCommandApproved(approvedCommand);
    } else {
        communicationEngine.speakText(String("TX blocked: ") + approvedCommand);
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
}

void loop() {
    M5.update();

    updateRuntimeEngines();
    handleTouchInput();
    handleVoiceInput();
    handleCameraEvents();
    handleImuEvents();
    handleEnvironmentLightEvents();
    handleUsbEvents();

    idleBehavior.update(animationEngine.isPlaying());
    proposalSystem.update(animationEngine.isPlaying());
    delay(Flic::kFrameDelayMs);
}
