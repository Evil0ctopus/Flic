#include "personality_ui.h"

#include "../engine/emotion_engine.h"
#include "../subsystems/light_engine.h"

#include <M5Unified.h>

namespace Flic {

bool PersonalityUI::begin(EmotionEngine* emotionEngine, LightEngine* lightEngine) {
    emotionEngine_ = emotionEngine;
    lightEngine_ = lightEngine;
    lastBlinkMs_ = millis();
    return true;
}

void PersonalityUI::update(bool animationPlaying) {
    if (emotionEngine_ == nullptr || animationPlaying) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastBlinkMs_ > 2400) {
        blink_ = !blink_;
        lastBlinkMs_ = now;
    }

    renderFace(emotionEngine_->getEmotion());
}

void PersonalityUI::showExpression(const String& expression) {
    auto& display = M5.Display;
    const int width = display.width();
    const int height = display.height();
    const int centerX = width / 2;
    const int centerY = height / 2;

    display.startWrite();
    display.fillScreen(TFT_BLACK);

    if (expression == "blink") {
        display.drawFastHLine(centerX - 24, centerY - 10, 18, TFT_WHITE);
        display.drawFastHLine(centerX + 6, centerY - 10, 18, TFT_WHITE);
    } else if (expression == "thinking") {
        display.fillCircle(centerX - 16, centerY - 12, 5, TFT_CYAN);
        display.fillCircle(centerX + 16, centerY - 12, 5, TFT_CYAN);
        display.drawCircle(centerX + 8, centerY + 14, 6, TFT_CYAN);
        display.drawCircle(centerX + 20, centerY + 20, 4, TFT_CYAN);
    } else if (expression == "surprise") {
        display.drawCircle(centerX - 15, centerY - 12, 8, TFT_WHITE);
        display.drawCircle(centerX + 15, centerY - 12, 8, TFT_WHITE);
        display.drawCircle(centerX, centerY + 10, 10, TFT_YELLOW);
    } else if (expression == "head_tilt_left") {
        display.fillRoundRect(centerX - 30, centerY - 20, 18, 8, 3, TFT_WHITE);
        display.fillRoundRect(centerX + 2, centerY - 14, 18, 8, 3, TFT_WHITE);
        display.drawLine(centerX - 18, centerY + 16, centerX + 14, centerY + 10, TFT_WHITE);
    } else if (expression == "head_tilt_right") {
        display.fillRoundRect(centerX - 20, centerY - 14, 18, 8, 3, TFT_WHITE);
        display.fillRoundRect(centerX + 12, centerY - 20, 18, 8, 3, TFT_WHITE);
        display.drawLine(centerX - 14, centerY + 10, centerX + 18, centerY + 16, TFT_WHITE);
    } else if (expression == "idle_breathing") {
        display.drawCircle(centerX, centerY + 6, 12, TFT_CYAN);
        display.drawCircle(centerX, centerY + 6, 18, TFT_DARKGREY);
        display.fillCircle(centerX - 14, centerY - 12, 5, TFT_WHITE);
        display.fillCircle(centerX + 14, centerY - 12, 5, TFT_WHITE);
    } else {
        display.fillCircle(centerX - 14, centerY - 12, 5, TFT_WHITE);
        display.fillCircle(centerX + 14, centerY - 12, 5, TFT_WHITE);
        display.drawLine(centerX - 12, centerY + 10, centerX + 12, centerY + 10, TFT_WHITE);
    }

    display.endWrite();
}

void PersonalityUI::showDeviceConnected(const String& deviceId) {
    showStatusLine("USB device connected", deviceId);
    if (lightEngine_ != nullptr) {
        lightEngine_->flashDeviceConnected();
    }
}

void PersonalityUI::showDeviceIdentified(const String& deviceId) {
    showStatusLine("Device identified", deviceId);
    if (lightEngine_ != nullptr) {
        lightEngine_->pulseDeviceIdentified();
    }
}

void PersonalityUI::showLearningEvent(const String& note) {
    showStatusLine("Flic learned", note);
    if (lightEngine_ != nullptr) {
        lightEngine_->pulseLearning();
    }
}

void PersonalityUI::showCommandApproved(const String& command) {
    showStatusLine("Command approved", command);
    if (lightEngine_ != nullptr) {
        lightEngine_->flashCommandApproved();
    }
}

void PersonalityUI::showCommandRejected(const String& command) {
    showStatusLine("Command rejected", command);
    if (lightEngine_ != nullptr) {
        lightEngine_->flashCommandRejected();
    }
}

void PersonalityUI::renderFace(const String& emotion) {
    auto& display = M5.Display;
    const int width = display.width();
    const int height = display.height();
    const int centerX = width / 2;
    const int centerY = height / 2;

    uint16_t eyeColor = TFT_WHITE;
    uint16_t mouthColor = TFT_WHITE;
    int eyeOffset = 12;
    int eyeHeight = blink_ ? 2 : 8;
    int mouthWidth = 26;
    int mouthCurve = 8;

    if (emotion == "curious") {
        eyeOffset = 16;
        mouthCurve = 4;
    } else if (emotion == "sleepy") {
        eyeHeight = 2;
        mouthCurve = 2;
    } else if (emotion == "surprised") {
        eyeOffset = 14;
        eyeHeight = 12;
        mouthWidth = 18;
    } else if (emotion == "happy") {
        mouthCurve = 14;
        eyeColor = TFT_GREEN;
        mouthColor = TFT_GREEN;
    }

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.fillCircle(centerX - eyeOffset, centerY - 12, 6, eyeColor);
    display.fillCircle(centerX + eyeOffset, centerY - 12, 6, eyeColor);
    if (eyeHeight <= 2) {
        display.drawFastHLine(centerX - eyeOffset - 6, centerY - 12, 12, eyeColor);
        display.drawFastHLine(centerX + eyeOffset - 6, centerY - 12, 12, eyeColor);
    } else {
        display.fillRoundRect(centerX - eyeOffset - 6, centerY - 16, 12, eyeHeight, 4, eyeColor);
        display.fillRoundRect(centerX + eyeOffset - 6, centerY - 16, 12, eyeHeight, 4, eyeColor);
    }

    if (emotion == "surprised") {
        display.drawCircle(centerX, centerY + 8, 8, mouthColor);
    } else {
        display.drawLine(centerX - mouthWidth / 2, centerY + 10, centerX, centerY + 10 + mouthCurve / 2, mouthColor);
        display.drawLine(centerX, centerY + 10 + mouthCurve / 2, centerX + mouthWidth / 2, centerY + 10, mouthColor);
    }
    display.endWrite();

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(emotion);
    }
}

void PersonalityUI::showStatusLine(const String& title, const String& detail) {
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print(title);
    display.setTextSize(1);
    display.setCursor(10, 56);
    display.print(detail);
    display.endWrite();
}

}  // namespace Flic
