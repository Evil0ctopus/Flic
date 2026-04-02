#pragma once

#include <Arduino.h>

namespace Flic {

class LightEngine {
public:
    bool begin();
    void update();
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void expressUtterance(const String& msg, const String& emotion);
    void setBrightness(uint8_t level);  // 0-100
    void setEmotionBrightness(uint8_t level);  // 0-100
    void pulse(uint8_t r, uint8_t g, uint8_t b, int speed);
    void flash(uint8_t r, uint8_t g, uint8_t b, int count);
    void flashDeviceConnected();
    void pulseDeviceIdentified();
    void pulseLearning();
    void flashCommandApproved();
    void flashCommandRejected();
    void emotionColor(const String& emotion);

private:
    enum class EffectStyle : uint8_t {
        CalmBreath,
        CuriousComet,
        HappyWave,
        SurprisedSpark,
        SleepyDrift,
    };

    bool available_ = false;
    bool externalAvailable_ = false;
    uint8_t userBrightness_ = 20;
    uint8_t emotionBrightness_ = 20;
    uint8_t currentR_ = 0;
    uint8_t currentG_ = 0;
    uint8_t currentB_ = 0;
    uint8_t effectOffset_ = 0;
    uint8_t effectPhase_ = 0;
    uint8_t effectSpeedMs_ = 35;
    unsigned long lastEffectMs_ = 0;
    unsigned long accentUntilMs_ = 0;
    uint8_t accentBoostPct_ = 0;
    EffectStyle effectStyle_ = EffectStyle::CalmBreath;

    void applyBrightness(uint8_t level);
    void refreshBrightness();
    void applyFallbackColor();
    void renderExternalAnimated();
    void configureEffectForEmotion(const String& emotion);
};

}  // namespace Flic
