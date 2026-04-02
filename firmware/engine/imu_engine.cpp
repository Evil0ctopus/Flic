#include "imu_engine.h"

#include "../diagnostics/webui_event_hook.h"
#include <M5Unified.h>
#include <math.h>

namespace Flic {

bool ImuEngine::begin() {
    lastSampleMs_ = millis();
    lastEventMs_ = 0;
    stillnessStreak_ = 0;
    hasEvent_ = false;
    return true;
}

void ImuEngine::update() {
    const unsigned long now = millis();
    if (now - lastSampleMs_ < 80) {
        return;
    }
    lastSampleMs_ = now;

    if (!M5.Imu.isEnabled()) {
        return;
    }

    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    const float accelMag = sqrtf(ax * ax + ay * ay + az * az);
    const float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);

    const float shakeThreshold = 3.0f;
    const float pickupThreshold = 1.35f;
    const float stillThreshold = 0.11f;
    const unsigned long eventCooldownMs = 750;

    if ((now - lastEventMs_) < eventCooldownMs) {
        if (accelMag < stillThreshold) {
            stillnessStreak_ = static_cast<uint8_t>(min<uint8_t>(stillnessStreak_ + 1, 10));
        } else {
            stillnessStreak_ = 0;
        }
        return;
    }

    if (gyroMag > shakeThreshold) {
        hasEvent_ = true;
        event_ = "shake";
        detail_ = String(gyroMag, 2);
        WebUiEventHook::emit("imu", String("{\"event\":\"shake\",\"detail\":\"") + detail_ + "\"}");
        lastEventMs_ = now;
        stillnessStreak_ = 0;
    } else if (accelMag > pickupThreshold) {
        hasEvent_ = true;
        event_ = "pickup";
        detail_ = String(accelMag, 2);
        WebUiEventHook::emit("imu", String("{\"event\":\"pickup\",\"detail\":\"") + detail_ + "\"}");
        lastEventMs_ = now;
        stillnessStreak_ = 0;
    } else if (accelMag < stillThreshold) {
        stillnessStreak_ = static_cast<uint8_t>(min<uint8_t>(stillnessStreak_ + 1, 10));
        if (stillnessStreak_ >= 4) {
            hasEvent_ = true;
            event_ = "stillness";
            detail_ = String(accelMag, 3);
            WebUiEventHook::emit("imu", String("{\"event\":\"stillness\",\"detail\":\"") + detail_ + "\"}");
            lastEventMs_ = now;
            stillnessStreak_ = 0;
        }
    } else {
        stillnessStreak_ = 0;
    }
}

bool ImuEngine::popEvent(String& eventOut, String& detailOut) {
    if (!hasEvent_) {
        return false;
    }
    eventOut = event_;
    detailOut = detail_;
    hasEvent_ = false;
    return true;
}

}  // namespace Flic
