#include "camera_engine.h"

#include <M5Unified.h>

namespace Flic {

bool CameraEngine::begin() {
    lastProbeMs_ = millis();
    lastEventMs_ = 0;
    hasEvent_ = false;
    lastTouchActive_ = false;
    // CoreS3 Lite profile has no onboard camera driver in this firmware; keep safe proxy mode.
    cameraAvailable_ = false;
    return true;
}

void CameraEngine::update() {
    const unsigned long now = millis();
    if (now - lastProbeMs_ < 160) {
        return;
    }
    lastProbeMs_ = now;

    // Camera features are optional on CoreS3 Lite.
    // Use edge-triggered sensor proxy only when activity starts.
    const bool touchActive = M5.Touch.getCount() > 0;
    if (touchActive && !lastTouchActive_ && (now - lastEventMs_) > 1100) {
        hasEvent_ = true;
        event_ = "motion";
        detail_ = cameraAvailable_ ? "camera_activity_start" : "proxy_touch_activity_start";
        lastEventMs_ = now;
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
