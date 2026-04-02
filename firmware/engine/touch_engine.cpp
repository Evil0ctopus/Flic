#include "touch_engine.h"

#include "touch_input.h"
#include "../diagnostics/webui_event_hook.h"
#include <M5Unified.h>

namespace Flic {

bool TouchEngine::begin(TouchInput* touchInput) {
    touchInput_ = touchInput;
    lastFallbackMs_ = millis();
    if (touchInput_ != nullptr) {
        touchInput_->begin();
    }
    return true;
}

bool TouchEngine::poll(String& gestureOut, String& meaningOut) {
    if (touchInput_ != nullptr && touchInput_->pollEvent(gestureOut, meaningOut)) {
        WebUiEventHook::emit("touch",
                             String("{\"gesture\":\"") + gestureOut + "\",\"meaning\":\"" + meaningOut + "\"}");
        return true;
    }

    // CoreS3 Lite fallback: physical button as acknowledge trigger when touch is unavailable.
    if (!M5.Touch.isEnabled()) {
        const unsigned long now = millis();
        if (M5.BtnA.wasClicked() && (now - lastFallbackMs_) > 120) {
            lastFallbackMs_ = now;
            gestureOut = "button_a";
            meaningOut = "acknowledge";
            WebUiEventHook::emit("touch", "{\"gesture\":\"button_a\",\"meaning\":\"acknowledge\"}");
            return true;
        }
    }

    return false;
}

}  // namespace Flic
