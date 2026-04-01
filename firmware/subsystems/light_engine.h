#pragma once

#include <Arduino.h>

namespace Flic {

class LightEngine {
public:
    bool begin();
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setBrightness(uint8_t level);  // 0-100
    void pulse(uint8_t r, uint8_t g, uint8_t b, int speed);
    void flash(uint8_t r, uint8_t g, uint8_t b, int count);
    void flashDeviceConnected();
    void pulseDeviceIdentified();
    void pulseLearning();
    void flashCommandApproved();
    void flashCommandRejected();
    void emotionColor(const String& emotion);

private:
    bool available_ = false;
    uint8_t brightness_ = 20;
    uint8_t currentR_ = 0;
    uint8_t currentG_ = 0;
    uint8_t currentB_ = 0;
};

}  // namespace Flic
