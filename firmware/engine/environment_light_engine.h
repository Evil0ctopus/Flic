#pragma once

#include <Arduino.h>

namespace Flic {

class EnvironmentLightEngine {
public:
    bool begin();
    void update();
    bool popEvent(String& eventOut, String& detailOut);

private:
    unsigned long lastSampleMs_ = 0;
    bool eventAvailable_ = false;
    bool touchPreviouslyActive_ = false;
    String eventName_;
    String eventDetail_;
    uint8_t lastBrightnessSample_ = 0;
};

}  // namespace Flic
