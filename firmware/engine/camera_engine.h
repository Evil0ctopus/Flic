#pragma once

#include <Arduino.h>

namespace Flic {

class CameraEngine {
public:
    bool begin();
    void update();
    bool popEvent(String& eventOut, String& detailOut);

private:
    unsigned long lastProbeMs_ = 0;
    bool hasEvent_ = false;
    bool lastTouchActive_ = false;
    String event_;
    String detail_;
};

}  // namespace Flic
