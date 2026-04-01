#include "light_engine.h"

#include "sd_manager.h"

#include <ArduinoJson.h>
#include <M5Unified.h>

namespace Flic {
namespace {
constexpr const char* kPersonalitySeedPath = "/ai/memory/personality_seed.json";

uint8_t clampBrightness(uint8_t level) {
    return level > 100 ? 100 : level;
}

uint8_t toLedLevel(uint8_t percent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U) / 100U);
}

}  // namespace

bool LightEngine::begin() {
    available_ = M5.Led.isEnabled();
    if (!available_) {
        Serial.println("Flic: GoBattery/LED backend not detected; using power LED fallback.");
    }

    setBrightness(20);
    setColor(0, 0, 40);
    return available_;
}

void LightEngine::setColor(uint8_t r, uint8_t g, uint8_t b) {
    currentR_ = r;
    currentG_ = g;
    currentB_ = b;

    if (available_) {
        M5.Led.setAllColor(r, g, b);
        M5.Led.display();
    }
}

void LightEngine::setBrightness(uint8_t level) {
    brightness_ = clampBrightness(level);
    const uint8_t ledLevel = toLedLevel(brightness_);

    if (available_) {
        M5.Led.setBrightness(ledLevel);
        M5.Led.display();
    }

    M5.Power.setLed(ledLevel);
}

void LightEngine::pulse(uint8_t r, uint8_t g, uint8_t b, int speed) {
    if (!available_) {
        return;
    }

    setColor(r, g, b);
    const int stepDelay = speed < 10 ? 10 : speed;

    for (uint8_t level = 8; level <= 20; level += 4) {
        setBrightness(level);
        delay(stepDelay);
    }
    for (int level = 20; level >= 8; level -= 4) {
        setBrightness(static_cast<uint8_t>(level));
        delay(stepDelay);
    }
}

void LightEngine::flash(uint8_t r, uint8_t g, uint8_t b, int count) {
    if (!available_) {
        return;
    }

    if (count < 1) {
        count = 1;
    }

    const uint8_t restoreBrightness = brightness_;
    const uint8_t restoreR = currentR_;
    const uint8_t restoreG = currentG_;
    const uint8_t restoreB = currentB_;

    for (int i = 0; i < count; ++i) {
        setColor(r, g, b);
        setBrightness(100);
        delay(80);
        setBrightness(0);
        delay(50);
    }

    setColor(restoreR, restoreG, restoreB);
    setBrightness(restoreBrightness);
}

void LightEngine::flashDeviceConnected() {
    flash(0, 255, 255, 2);
}

void LightEngine::pulseDeviceIdentified() {
    pulse(0, 80, 255, 18);
}

void LightEngine::pulseLearning() {
    pulse(255, 220, 0, 20);
}

void LightEngine::flashCommandApproved() {
    flash(0, 255, 0, 2);
}

void LightEngine::flashCommandRejected() {
    flash(255, 0, 0, 2);
}

void LightEngine::emotionColor(const String& emotion) {
    JsonDocument document;
    if (!SdManager::readJSON(kPersonalitySeedPath, document)) {
        Serial.println("Flic: personality seed unavailable for LED emotion map.");
        return;
    }

    JsonObject map = document["emotion_led_map"].as<JsonObject>();
    if (map.isNull()) {
        Serial.println("Flic: emotion_led_map missing.");
        return;
    }

    JsonObject color = map[emotion].as<JsonObject>();
    if (color.isNull()) {
        Serial.printf("Flic: emotion color missing for '%s'.\n", emotion.c_str());
        return;
    }

    const uint8_t r = color["r"] | 0;
    const uint8_t g = color["g"] | 0;
    const uint8_t b = color["b"] | 0;
    setColor(r, g, b);
}

}  // namespace Flic
