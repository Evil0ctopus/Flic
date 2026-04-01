#pragma once

#include <Arduino.h>

namespace Flic {

class ImuEngine {
public:
    bool begin();
    void update();
    bool popEvent(String& eventOut, String& detailOut);

private:
    unsigned long lastSampleMs_ = 0;
    bool hasEvent_ = false;
    String event_;
    String detail_;
};

}  // namespace Flic
