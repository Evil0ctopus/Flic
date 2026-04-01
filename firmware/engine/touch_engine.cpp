#include "touch_engine.h"

#include "touch_input.h"

namespace Flic {

bool TouchEngine::begin(TouchInput* touchInput) {
    touchInput_ = touchInput;
    if (touchInput_ != nullptr) {
        touchInput_->begin();
    }
    return true;
}

bool TouchEngine::poll(String& gestureOut, String& meaningOut) {
    return touchInput_ != nullptr && touchInput_->pollEvent(gestureOut, meaningOut);
}

}  // namespace Flic
