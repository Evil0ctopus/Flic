#include "personality_ui.h"

#include "../engine/face_engine.h"
#include "../engine/emotion_engine.h"
#include "../subsystems/light_engine.h"

#include <M5Unified.h>

namespace Flic {

namespace {
constexpr bool kDisableDisplayRendering = false;

float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}
}  // namespace

bool PersonalityUI::begin(EmotionEngine* emotionEngine, LightEngine* lightEngine, FaceEngine* faceEngine) {
    emotionEngine_ = emotionEngine;
    lightEngine_ = lightEngine;
    faceEngine_ = faceEngine;
    lastBlinkMs_ = millis();
    blinkIntervalMs_ = 2400;
    blinkHoldMs_ = 140;
    lastEmotion_ = "calm";
    return true;
}

void PersonalityUI::setPersonality(float energy, float curiosity, float patience) {
    energy_ = clamp01(energy);
    curiosity_ = clamp01(curiosity);
    patience_ = clamp01(patience);
}

void PersonalityUI::update(bool animationPlaying) {
    if (emotionEngine_ == nullptr || animationPlaying) {
        return;
    }

    const unsigned long now = millis();
    const String emotion = emotionEngine_->getEmotion();
    const bool previousBlink = blink_;
    bool expressionChanged = false;
    if (emotion != lastEmotion_) {
        lastEmotion_ = emotion;
        updateBlinkTiming(emotion);
        blink_ = false;
        lastBlinkMs_ = now;
        expressionChanged = true;
    }

    const unsigned long elapsed = now - lastBlinkMs_;
    if (!blink_ && elapsed >= blinkIntervalMs_) {
        blink_ = true;
        lastBlinkMs_ = now;
        expressionChanged = true;
    } else if (blink_ && elapsed >= blinkHoldMs_) {
        blink_ = false;
        lastBlinkMs_ = now;
        expressionChanged = true;
    }

    if (!expressionChanged && hasRenderedFace_ && emotion == lastRenderedEmotion_ && previousBlink == blink_ &&
        lastRenderedBlink_ == blink_) {
        return;
    }

    renderFace(emotion);
}

void PersonalityUI::showExpression(const String& expression) {
    if (faceEngine_ != nullptr) {
        if (expression == "idle_breathing") {
            faceEngine_->idlePulse();
        } else {
            faceEngine_->play(expression);
        }
        return;
    }

    if (kDisableDisplayRendering) {
        return;
    }

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
    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion(emotion);
        return;
    }

    if (kDisableDisplayRendering) {
        if (lightEngine_ != nullptr) {
            lightEngine_->emotionColor(emotion);
        }
        return;
    }

    auto& display = M5.Display;
    const int width = display.width();
    const int height = display.height();
    const int centerX = width / 2;
    const int centerY = height / 2;

    uint16_t eyeColor = TFT_WHITE;
    uint16_t mouthColor = TFT_WHITE;
    int eyeOffset = 20;
    int eyeHeight = blink_ ? 3 : 12;
    int eyeRadius = 9;
    int mouthWidth = 44;
    int mouthCurve = 8;

    if (emotion == "curious") {
        eyeOffset = 24;
        mouthCurve = 4;
        if (!blink_) {
            eyeHeight = 13;
        }
    } else if (emotion == "sleepy") {
        eyeHeight = 3;
        mouthCurve = 2;
        mouthWidth = 36;
    } else if (emotion == "surprised") {
        eyeOffset = 22;
        eyeHeight = 16;
        mouthWidth = 24;
    } else if (emotion == "happy") {
        mouthCurve = 14;
        eyeColor = TFT_GREEN;
        mouthColor = TFT_GREEN;
        eyeOffset = 21;
    } else if (emotion == "calm") {
        eyeOffset = 20;
        mouthCurve = 8;
    }

    display.startWrite();
    const int faceBoxX = centerX - 105;
    const int faceBoxY = centerY - 68;
    const int faceBoxW = 210;
    const int faceBoxH = 136;
    display.fillRect(faceBoxX, faceBoxY, faceBoxW, faceBoxH, TFT_BLACK);
    display.fillCircle(centerX - eyeOffset, centerY - 18, eyeRadius, eyeColor);
    display.fillCircle(centerX + eyeOffset, centerY - 18, eyeRadius, eyeColor);
    if (eyeHeight <= 3) {
        display.drawFastHLine(centerX - eyeOffset - eyeRadius, centerY - 18, eyeRadius * 2, eyeColor);
        display.drawFastHLine(centerX + eyeOffset - eyeRadius, centerY - 18, eyeRadius * 2, eyeColor);
    } else {
        display.fillRoundRect(centerX - eyeOffset - eyeRadius, centerY - 24, eyeRadius * 2, eyeHeight, 5, eyeColor);
        display.fillRoundRect(centerX + eyeOffset - eyeRadius, centerY - 24, eyeRadius * 2, eyeHeight, 5, eyeColor);
    }

    if (emotion == "surprised") {
        display.drawCircle(centerX, centerY + 16, 12, mouthColor);
    } else {
        display.drawLine(centerX - mouthWidth / 2, centerY + 18, centerX, centerY + 18 + mouthCurve / 2, mouthColor);
        display.drawLine(centerX, centerY + 18 + mouthCurve / 2, centerX + mouthWidth / 2, centerY + 18, mouthColor);
    }
    display.endWrite();

    hasRenderedFace_ = true;
    lastRenderedEmotion_ = emotion;
    lastRenderedBlink_ = blink_;

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(emotion);
    }
}

void PersonalityUI::updateBlinkTiming(const String& emotion) {
    const float quietness = 1.0f + patience_ * 0.35f;
    const float quickness = 1.0f - ((energy_ * 0.55f) + (curiosity_ * 0.35f));

    blinkIntervalMs_ = static_cast<unsigned long>(2400.0f * quietness * (quickness < 0.45f ? 0.45f : quickness));
    blinkHoldMs_ = static_cast<unsigned long>(140.0f * (1.0f + patience_ * 0.25f));

    if (blinkIntervalMs_ < 900) {
        blinkIntervalMs_ = 900;
    }
    if (blinkHoldMs_ < 90) {
        blinkHoldMs_ = 90;
    }

    if (emotion == "calm") {
        blinkIntervalMs_ = 2600;
        blinkHoldMs_ = 150;
    } else if (emotion == "curious") {
        blinkIntervalMs_ = static_cast<unsigned long>(1700.0f * (1.0f - curiosity_ * 0.2f));
        blinkHoldMs_ = static_cast<unsigned long>(120.0f * (1.0f + patience_ * 0.15f));
    } else if (emotion == "happy") {
        blinkIntervalMs_ = static_cast<unsigned long>(2100.0f * (1.0f - energy_ * 0.15f));
        blinkHoldMs_ = static_cast<unsigned long>(130.0f * (1.0f + energy_ * 0.1f));
    } else if (emotion == "sleepy") {
        blinkIntervalMs_ = static_cast<unsigned long>(3600.0f * (1.0f + patience_ * 0.2f));
        blinkHoldMs_ = static_cast<unsigned long>(200.0f * (1.0f + patience_ * 0.2f));
    } else if (emotion == "surprised") {
        blinkIntervalMs_ = static_cast<unsigned long>(1200.0f * (1.0f - curiosity_ * 0.15f));
        blinkHoldMs_ = static_cast<unsigned long>(100.0f * (1.0f + energy_ * 0.1f));
    }

    if (blinkIntervalMs_ < 650) {
        blinkIntervalMs_ = 650;
    }
    if (blinkHoldMs_ < 80) {
        blinkHoldMs_ = 80;
    }
}

void PersonalityUI::showStatusLine(const String& title, const String& detail) {
    if (kDisableDisplayRendering) {
        return;
    }
    if (faceEngine_ != nullptr && faceEngine_->isActive()) {
        return;
    }

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
