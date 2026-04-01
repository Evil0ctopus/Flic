#include "imu_engine.h"

#include <M5Unified.h>
#include <math.h>

namespace Flic {

bool ImuEngine::begin() {
    lastSampleMs_ = millis();
    hasEvent_ = false;
    return true;
}

void ImuEngine::update() {
    const unsigned long now = millis();
    if (now - lastSampleMs_ < 120) {
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

    if (gyroMag > 2.5f) {
        hasEvent_ = true;
        event_ = "shake";
        detail_ = String(gyroMag, 2);
    } else if (accelMag > 1.25f) {
        hasEvent_ = true;
        event_ = "pickup";
        detail_ = String(accelMag, 2);
    } else if (accelMag < 0.15f) {
        hasEvent_ = true;
        event_ = "stillness";
        detail_ = String(accelMag, 2);
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
