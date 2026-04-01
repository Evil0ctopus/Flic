#include "touch_input.h"

#include <M5Unified.h>

namespace Flic {

bool TouchInput::begin() {
    lastGestureMs_ = 0;
    return true;
}

bool TouchInput::pollEvent(String& gestureOut, String& meaningOut) {
    gestureOut = String();
    meaningOut = String();

    if (M5.Touch.getCount() == 0) {
        return false;
    }

    const auto td = M5.Touch.getDetail(0);

    if (td.wasClicked()) {
        if (td.getClickCount() >= 2) {
            gestureOut = "double_tap";
        } else {
            gestureOut = "tap";
        }
    } else if (td.wasHold()) {
        gestureOut = "long_press";
    } else if (td.wasFlicked()) {
        const int dx = td.distanceX();
        const int dy = td.distanceY();
        if (abs(dx) > abs(dy)) {
            gestureOut = dx > 0 ? "swipe_right" : "swipe_left";
        } else {
            gestureOut = dy > 0 ? "swipe_down" : "swipe_up";
        }
    } else if (td.wasDragged() || td.isDragging()) {
        gestureOut = "drag";
    }

    if (gestureOut.length() == 0) {
        return false;
    }

    const unsigned long now = millis();
    if (now - lastGestureMs_ < 80) {
        return false;
    }
    lastGestureMs_ = now;

    meaningOut = mapMeaning(gestureOut);
    return true;
}

String TouchInput::mapMeaning(const String& gesture) const {
    if (gesture == "tap") {
        return "acknowledge";
    }
    if (gesture == "double_tap") {
        return "excitement";
    }
    if (gesture == "long_press") {
        return "comfort";
    }
    if (gesture == "swipe_left") {
        return "cancel";
    }
    if (gesture == "swipe_right") {
        return "continue";
    }
    if (gesture == "swipe_up") {
        return "attention";
    }
    if (gesture == "swipe_down") {
        return "dismiss";
    }
    if (gesture == "drag") {
        return "drag";
    }
    return "acknowledge";
}

}  // namespace Flic
