#include "camera_engine.h"

#include <M5Unified.h>

namespace Flic {

bool CameraEngine::begin() {
    lastProbeMs_ = millis();
    hasEvent_ = false;
    lastTouchActive_ = false;
    return true;
}

void CameraEngine::update() {
    const unsigned long now = millis();
    if (now - lastProbeMs_ < 500) {
        return;
    }
    lastProbeMs_ = now;

    // Camera features are optional on this firmware profile.
    // Use edge-triggered proxy only when activity starts.
    const bool touchActive = M5.Touch.getCount() > 0;
    if (touchActive && !lastTouchActive_) {
        hasEvent_ = true;
        event_ = "motion";
        detail_ = "proxy_activity_start";
    }
    lastTouchActive_ = touchActive;

}

bool CameraEngine::popEvent(String& eventOut, String& detailOut) {
    if (!hasEvent_) {
        return false;
    }
    eventOut = event_;
    detailOut = detail_;
    hasEvent_ = false;
    return true;
}

}  // namespace Flic
