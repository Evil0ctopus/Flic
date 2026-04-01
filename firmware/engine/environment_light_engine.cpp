#include "environment_light_engine.h"

#include <M5Unified.h>

namespace Flic {

bool EnvironmentLightEngine::begin() {
    lastSampleMs_ = millis();
    lastBrightnessSample_ = 128;
    eventAvailable_ = false;
    touchPreviouslyActive_ = false;
    return true;
}

void EnvironmentLightEngine::update() {
    const unsigned long now = millis();
    if (now - lastSampleMs_ < 250) {
        return;
    }
    lastSampleMs_ = now;

    const bool touchActive = M5.Touch.getCount() > 0;
    if (touchActive && !touchPreviouslyActive_) {
        eventAvailable_ = true;
        eventName_ = "hand_wave";
        eventDetail_ = "proxy_touch_edge";
    }
    touchPreviouslyActive_ = touchActive;

    const uint8_t currentBrightness = static_cast<uint8_t>(M5.Display.getBrightness());
    if (!eventAvailable_ && abs(static_cast<int>(currentBrightness) - static_cast<int>(lastBrightnessSample_)) >= 16) {
        eventAvailable_ = true;
        eventName_ = "display_light_change";
        eventDetail_ = String(currentBrightness);
    }
    lastBrightnessSample_ = currentBrightness;
}

bool EnvironmentLightEngine::popEvent(String& eventOut, String& detailOut) {
    if (!eventAvailable_) {
        return false;
    }
    eventOut = eventName_;
    detailOut = eventDetail_;
    eventAvailable_ = false;
    return true;
}

}  // namespace Flic
