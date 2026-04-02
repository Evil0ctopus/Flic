#include "light_engine.h"

#include "../diagnostics/webui_event_hook.h"
#include "../config.h"
#include "sd_manager.h"

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <M5Unified.h>

namespace Flic {
namespace {
constexpr const char* kPersonalitySeedPath = "/ai/memory/personality_seed.json";
Adafruit_NeoPixel* gExternalStrip = nullptr;

struct EmotionLedColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

EmotionLedColor defaultEmotionColor(const String& emotion) {
    if (emotion == "curious") {
        return {255, 200, 0};
    }
    if (emotion == "happy") {
        return {0, 255, 80};
    }
    if (emotion == "surprised") {
        return {255, 255, 255};
    }
    if (emotion == "sleepy") {
        return {80, 0, 80};
    }
    return {0, 0, 40};
}

uint8_t clampBrightness(uint8_t level) {
    return level > 100 ? 100 : level;
}

uint8_t toLedLevel(uint8_t percent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U) / 100U);
}

float emotionBrightnessScale(const String& emotion) {
    if (emotion == "happy") {
        return 1.2f;
    }
    if (emotion == "curious") {
        return 1.05f;
    }
    if (emotion == "sleepy") {
        return 0.65f;
    }
    if (emotion == "surprised") {
        return 1.4f;
    }
    return 0.9f;
}

void scaleColor(uint8_t& r, uint8_t& g, uint8_t& b, float scale) {
    auto scaleChannel = [scale](uint8_t value) -> uint8_t {
        const int scaled = static_cast<int>(static_cast<float>(value) * scale);
        if (scaled < 0) {
            return 0;
        }
        if (scaled > 255) {
            return 255;
        }
        return static_cast<uint8_t>(scaled);
    };

    r = scaleChannel(r);
    g = scaleChannel(g);
    b = scaleChannel(b);
}

uint8_t scaleByPercent(uint8_t value, uint8_t percent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * percent) / 100U);
}

}  // namespace

bool LightEngine::begin() {
    available_ = M5.Led.isEnabled();
    externalAvailable_ = false;

    if (!available_ && Flic::kExternalRgbLedPin >= 0 && Flic::kExternalRgbLedCount > 0) {
        if (gExternalStrip == nullptr) {
            gExternalStrip = new Adafruit_NeoPixel(Flic::kExternalRgbLedCount,
                                                   static_cast<int16_t>(Flic::kExternalRgbLedPin),
                                                   NEO_GRB + NEO_KHZ800);
        }
        if (gExternalStrip != nullptr) {
            gExternalStrip->begin();
            gExternalStrip->show();
            externalAvailable_ = true;
            Serial.println(String("Flic: external RGB strip enabled on pin ") + Flic::kExternalRgbLedPin);
        }
    }

    if (!available_ && !externalAvailable_) {
        Serial.println("Flic: RGB LED backend not detected; using power LED fallback.");
    }

    setBrightness(20);
    setColor(0, 0, 40);
    return available_;
}

void LightEngine::update() {
    if (!externalAvailable_ || gExternalStrip == nullptr) {
        return;
    }

    const unsigned long now = millis();
    if ((now - lastEffectMs_) < effectSpeedMs_) {
        return;
    }
    lastEffectMs_ = now;
    effectOffset_ = static_cast<uint8_t>((effectOffset_ + 1) % Flic::kExternalRgbLedCount);
    effectPhase_ = static_cast<uint8_t>(effectPhase_ + 7);
    renderExternalAnimated();
}

void LightEngine::setColor(uint8_t r, uint8_t g, uint8_t b) {
    currentR_ = r;
    currentG_ = g;
    currentB_ = b;

    if (available_) {
        M5.Led.setAllColor(r, g, b);
        M5.Led.display();
    } else if (externalAvailable_ && gExternalStrip != nullptr) {
        renderExternalAnimated();
    } else {
        applyFallbackColor();
    }

    WebUiEventHook::emit("light", String("{\"kind\":\"color\",\"r\":") + r + ",\"g\":" + g + ",\"b\":" + b + "}");
}

void LightEngine::expressUtterance(const String& msg, const String& emotion) {
    if (msg.length() == 0) {
        return;
    }

    String lower = msg;
    lower.toLowerCase();
    const unsigned long now = millis();

    const bool asksQuestion = msg.indexOf('?') >= 0 || lower.indexOf("can ") >= 0 || lower.indexOf("could ") >= 0 ||
                              lower.indexOf("would ") >= 0;
    const bool expressesWant = lower.indexOf("i want") >= 0 || lower.indexOf("want ") >= 0 || lower.indexOf("need ") >= 0;
    const bool excitement = msg.indexOf('!') >= 0 || lower.indexOf("yay") >= 0 || lower.indexOf("great") >= 0;

    if (asksQuestion) {
        effectStyle_ = EffectStyle::SurprisedSpark;
        effectSpeedMs_ = 16;
        accentBoostPct_ = 34;
        accentUntilMs_ = now + 2600;
        return;
    }

    if (expressesWant) {
        effectStyle_ = EffectStyle::CuriousComet;
        effectSpeedMs_ = 20;
        accentBoostPct_ = 28;
        accentUntilMs_ = now + 3200;
        return;
    }

    if (excitement) {
        effectStyle_ = EffectStyle::HappyWave;
        effectSpeedMs_ = 18;
        accentBoostPct_ = 30;
        accentUntilMs_ = now + 2200;
        return;
    }

    configureEffectForEmotion(emotion);
    accentBoostPct_ = 14;
    accentUntilMs_ = now + 1200;
}

void LightEngine::setBrightness(uint8_t level) {
    userBrightness_ = clampBrightness(level);
    refreshBrightness();
    WebUiEventHook::emit("light", String("{\"kind\":\"brightness\",\"level\":") + userBrightness_ + "}");
}

void LightEngine::setEmotionBrightness(uint8_t level) {
    emotionBrightness_ = clampBrightness(level);
    refreshBrightness();
}

void LightEngine::pulse(uint8_t r, uint8_t g, uint8_t b, int speed) {
    setColor(r, g, b);
    const int stepDelay = speed < 8 ? 8 : speed;
    const uint8_t restoreBrightness = emotionBrightness_;

    for (uint8_t level = 9; level <= 19; level += 5) {
        setEmotionBrightness(level);
        delay(stepDelay);
    }
    for (int level = 19; level >= 9; level -= 5) {
        setEmotionBrightness(static_cast<uint8_t>(level));
        delay(stepDelay);
    }

    setEmotionBrightness(restoreBrightness);
}

void LightEngine::flash(uint8_t r, uint8_t g, uint8_t b, int count) {
    if (count < 1) {
        count = 1;
    }

    const uint8_t restoreR = currentR_;
    const uint8_t restoreG = currentG_;
    const uint8_t restoreB = currentB_;
    const uint8_t restoreEmotionBrightness = emotionBrightness_;

    for (int i = 0; i < count; ++i) {
        setColor(r, g, b);
        setEmotionBrightness(100);
        delay(65);
        setEmotionBrightness(0);
        delay(35);
    }

    setColor(restoreR, restoreG, restoreB);
    setEmotionBrightness(restoreEmotionBrightness);
    refreshBrightness();
}

void LightEngine::flashDeviceConnected() {
    flash(0, 255, 255, 1);
}

void LightEngine::pulseDeviceIdentified() {
    pulse(0, 80, 255, 14);
}

void LightEngine::pulseLearning() {
    pulse(255, 220, 0, 16);
}

void LightEngine::flashCommandApproved() {
    flash(0, 255, 0, 1);
}

void LightEngine::flashCommandRejected() {
    flash(255, 0, 0, 1);
}

void LightEngine::emotionColor(const String& emotion) {
    static String lastEmotion;
    static bool loadedMap = false;
    static EmotionLedColor calm = {0, 0, 40};
    static EmotionLedColor curious = {255, 200, 0};
    static EmotionLedColor happy = {0, 255, 80};
    static EmotionLedColor surprised = {255, 255, 255};
    static EmotionLedColor sleepy = {80, 0, 80};

    if (!loadedMap) {
        JsonDocument document;
        if (SdManager::readJSON(kPersonalitySeedPath, document)) {
            JsonObject map = document["emotion_led_map"].as<JsonObject>();
            if (!map.isNull()) {
                auto assignColor = [&map](const char* key, EmotionLedColor& target) {
                    JsonObject color = map[key].as<JsonObject>();
                    if (!color.isNull()) {
                        target.r = color["r"] | target.r;
                        target.g = color["g"] | target.g;
                        target.b = color["b"] | target.b;
                    }
                };

                assignColor("calm", calm);
                assignColor("curious", curious);
                assignColor("happy", happy);
                assignColor("surprised", surprised);
                assignColor("sleepy", sleepy);
            }
        }

        loadedMap = true;
    }

    EmotionLedColor color = defaultEmotionColor(emotion);
    if (emotion == "calm") {
        color = calm;
    } else if (emotion == "curious") {
        color = curious;
    } else if (emotion == "happy") {
        color = happy;
    } else if (emotion == "surprised") {
        color = surprised;
    } else if (emotion == "sleepy") {
        color = sleepy;
    }

    uint8_t adjustedR = color.r;
    uint8_t adjustedG = color.g;
    uint8_t adjustedB = color.b;
    scaleColor(adjustedR, adjustedG, adjustedB, emotionBrightnessScale(emotion));
    configureEffectForEmotion(emotion);
    setColor(adjustedR, adjustedG, adjustedB);
    setEmotionBrightness(static_cast<uint8_t>(emotionBrightnessScale(emotion) * 100.0f));

    if (emotion != lastEmotion) {
        lastEmotion = emotion;
        WebUiEventHook::emit("light", String("{\"kind\":\"emotion\",\"emotion\":\"") + emotion + "\"}");
    }
}

void LightEngine::applyBrightness(uint8_t level) {
    const uint8_t ledLevel = toLedLevel(level);

    if (available_) {
        M5.Led.setBrightness(ledLevel);
        M5.Led.display();
    } else if (externalAvailable_ && gExternalStrip != nullptr) {
        gExternalStrip->setBrightness(ledLevel);
        renderExternalAnimated();
        return;
    } else {
        applyFallbackColor();
        return;
    }

    M5.Power.setLed(ledLevel);
}

void LightEngine::refreshBrightness() {
    const uint8_t effectiveBrightness = userBrightness_ < emotionBrightness_ ? userBrightness_ : emotionBrightness_;
    applyBrightness(effectiveBrightness);
}

void LightEngine::applyFallbackColor() {
    const uint8_t effectiveBrightness = userBrightness_ < emotionBrightness_ ? userBrightness_ : emotionBrightness_;
    const uint16_t colorPeak = currentR_ > currentG_ ? (currentR_ > currentB_ ? currentR_ : currentB_)
                                                     : (currentG_ > currentB_ ? currentG_ : currentB_);
    const uint16_t scaled = static_cast<uint16_t>((colorPeak * static_cast<uint16_t>(toLedLevel(effectiveBrightness))) / 255U);
    M5.Power.setLed(static_cast<uint8_t>(scaled));
}

void LightEngine::renderExternalAnimated() {
    if (!externalAvailable_ || gExternalStrip == nullptr || Flic::kExternalRgbLedCount == 0) {
        return;
    }

    const uint8_t effectiveBrightness = userBrightness_ < emotionBrightness_ ? userBrightness_ : emotionBrightness_;
    const uint8_t ledLevel = toLedLevel(effectiveBrightness);
    gExternalStrip->setBrightness(ledLevel);
    const bool accentActive = millis() < accentUntilMs_;
    const uint8_t accentBoost = accentActive ? accentBoostPct_ : 0;

    auto applyAccent = [accentBoost](uint8_t value) -> uint8_t {
        const uint16_t boosted = static_cast<uint16_t>(value) + static_cast<uint16_t>((static_cast<uint16_t>(value) * accentBoost) / 100U);
        return static_cast<uint8_t>(boosted > 255 ? 255 : boosted);
    };

    if (effectStyle_ == EffectStyle::CalmBreath || effectStyle_ == EffectStyle::SleepyDrift) {
        const uint8_t wave = (effectPhase_ < 128) ? effectPhase_ : static_cast<uint8_t>(255 - effectPhase_);
        const uint8_t minPct = (effectStyle_ == EffectStyle::SleepyDrift) ? 12 : 24;
        const uint8_t maxPct = (effectStyle_ == EffectStyle::SleepyDrift) ? 45 : 78;
        const uint8_t span = static_cast<uint8_t>(maxPct - minPct);
        const uint8_t pct = static_cast<uint8_t>(minPct + ((static_cast<uint16_t>(wave) * span) / 127U));
        for (uint16_t i = 0; i < Flic::kExternalRgbLedCount; ++i) {
            uint8_t r = scaleByPercent(currentR_, pct);
            uint8_t g = scaleByPercent(currentG_, pct);
            uint8_t b = scaleByPercent(currentB_, pct);
            if (effectStyle_ == EffectStyle::SleepyDrift && (i % 2 == ((effectOffset_ / 2) % 2))) {
                r = scaleByPercent(r, 70);
                g = scaleByPercent(g, 70);
                b = scaleByPercent(b, 85);
            }
            gExternalStrip->setPixelColor(i, gExternalStrip->Color(applyAccent(r), applyAccent(g), applyAccent(b)));
        }
        gExternalStrip->show();
        return;
    }

    if (effectStyle_ == EffectStyle::HappyWave) {
        const uint8_t lowPct = 28;
        const uint8_t highPct = 100;
        for (uint16_t i = 0; i < Flic::kExternalRgbLedCount; ++i) {
            const uint8_t phase = static_cast<uint8_t>((i * 21U) + effectPhase_);
            const uint8_t tri = (phase < 128) ? phase : static_cast<uint8_t>(255 - phase);
            const uint8_t pct = static_cast<uint8_t>(lowPct + ((static_cast<uint16_t>(tri) * (highPct - lowPct)) / 127U));
            gExternalStrip->setPixelColor(i,
                gExternalStrip->Color(applyAccent(scaleByPercent(currentR_, pct)),
                                      applyAccent(scaleByPercent(currentG_, pct)),
                                      applyAccent(scaleByPercent(currentB_, pct))));
        }
        gExternalStrip->show();
        return;
    }

    if (effectStyle_ == EffectStyle::SurprisedSpark) {
        const uint8_t flashPhase = static_cast<uint8_t>((effectPhase_ / 12U) % 3U);
        const bool flashOn = flashPhase == 0;
        for (uint16_t i = 0; i < Flic::kExternalRgbLedCount; ++i) {
            if (flashOn && (i % 2 == (effectOffset_ % 2))) {
                gExternalStrip->setPixelColor(i, gExternalStrip->Color(255, 255, 255));
            } else {
                gExternalStrip->setPixelColor(i,
                    gExternalStrip->Color(applyAccent(scaleByPercent(currentR_, 24)),
                                          applyAccent(scaleByPercent(currentG_, 24)),
                                          applyAccent(scaleByPercent(currentB_, 24))));
            }
        }
        gExternalStrip->show();
        return;
    }

    const uint8_t baseR = scaleByPercent(currentR_, 28);
    const uint8_t baseG = scaleByPercent(currentG_, 28);
    const uint8_t baseB = scaleByPercent(currentB_, 28);
    for (uint16_t i = 0; i < Flic::kExternalRgbLedCount; ++i) {
        int16_t d = static_cast<int16_t>(i) - static_cast<int16_t>(effectOffset_);
        if (d < 0) {
            d = static_cast<int16_t>(-d);
        }
        const uint8_t influence = (d == 0) ? 100 : (d == 1 ? 60 : (d == 2 ? 30 : 0));
        const uint8_t fxR = scaleByPercent(currentR_, influence);
        const uint8_t fxG = scaleByPercent(currentG_, influence);
        const uint8_t fxB = scaleByPercent(currentB_, influence);
        const uint8_t outR = fxR > baseR ? fxR : baseR;
        const uint8_t outG = fxG > baseG ? fxG : baseG;
        const uint8_t outB = fxB > baseB ? fxB : baseB;
        gExternalStrip->setPixelColor(i, gExternalStrip->Color(applyAccent(outR), applyAccent(outG), applyAccent(outB)));
    }

    gExternalStrip->show();
}

void LightEngine::configureEffectForEmotion(const String& emotion) {
    if (emotion == "happy") {
        effectStyle_ = EffectStyle::HappyWave;
        effectSpeedMs_ = 26;
        return;
    }
    if (emotion == "curious") {
        effectStyle_ = EffectStyle::CuriousComet;
        effectSpeedMs_ = 24;
        return;
    }
    if (emotion == "surprised") {
        effectStyle_ = EffectStyle::SurprisedSpark;
        effectSpeedMs_ = 18;
        return;
    }
    if (emotion == "sleepy") {
        effectStyle_ = EffectStyle::SleepyDrift;
        effectSpeedMs_ = 60;
        return;
    }

    effectStyle_ = EffectStyle::CalmBreath;
    effectSpeedMs_ = 45;
}

}  // namespace Flic
