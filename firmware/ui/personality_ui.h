#pragma once

#include <Arduino.h>

namespace Flic {

class EmotionEngine;
class LightEngine;

class PersonalityUI {
public:
    bool begin(EmotionEngine* emotionEngine, LightEngine* lightEngine);
    void setPersonality(float energy, float curiosity, float patience);
    void update(bool animationPlaying);
    void showExpression(const String& expression);
    void showDeviceConnected(const String& deviceId);
    void showDeviceIdentified(const String& deviceId);
    void showLearningEvent(const String& note);
    void showCommandApproved(const String& command);
    void showCommandRejected(const String& command);

private:
    void renderFace(const String& emotion);
    void updateBlinkTiming(const String& emotion);
    void showStatusLine(const String& title, const String& detail);

    EmotionEngine* emotionEngine_ = nullptr;
    LightEngine* lightEngine_ = nullptr;
    float energy_ = 0.5f;
    float curiosity_ = 0.5f;
    float patience_ = 0.5f;
    unsigned long lastBlinkMs_ = 0;
    unsigned long blinkIntervalMs_ = 2400;
    unsigned long blinkHoldMs_ = 140;
    bool blink_ = false;
    String lastEmotion_ = "calm";
    bool hasRenderedFace_ = false;
    bool lastRenderedBlink_ = false;
    String lastRenderedEmotion_ = "";
};

}  // namespace Flic
