#pragma once

#include <Arduino.h>

namespace Flic {

class EmotionEngine;
class LightEngine;

class PersonalityUI {
public:
    bool begin(EmotionEngine* emotionEngine, LightEngine* lightEngine);
    void update(bool animationPlaying);
    void showExpression(const String& expression);
    void showDeviceConnected(const String& deviceId);
    void showDeviceIdentified(const String& deviceId);
    void showLearningEvent(const String& note);
    void showCommandApproved(const String& command);
    void showCommandRejected(const String& command);

private:
    void renderFace(const String& emotion);
    void showStatusLine(const String& title, const String& detail);

    EmotionEngine* emotionEngine_ = nullptr;
    LightEngine* lightEngine_ = nullptr;
    unsigned long lastBlinkMs_ = 0;
    bool blink_ = false;
};

}  // namespace Flic
