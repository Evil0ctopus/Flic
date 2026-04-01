#pragma once

#include <Arduino.h>

namespace Flic {

class TouchInput;

class TouchEngine {
public:
    bool begin(TouchInput* touchInput);
    bool poll(String& gestureOut, String& meaningOut);

private:
    TouchInput* touchInput_ = nullptr;
};

}  // namespace Flic
