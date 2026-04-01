#pragma once

#include <Arduino.h>

namespace Flic {

class TouchInput {
public:
    bool begin();
    bool pollEvent(String& gestureOut, String& meaningOut);

private:
    String mapMeaning(const String& gesture) const;

    unsigned long lastGestureMs_ = 0;
};

}  // namespace Flic
