#pragma once

#include <Arduino.h>

namespace Flic {

enum class BubbleSize : uint8_t {
    Small = 0,
    Medium = 1,
    Large = 2,
};

class TextBubbles {
public:
    bool begin();
    void showMessage(const String& msg, BubbleSize size, const String& emotion);
    void update();
    bool isVisible() const;

private:
    void drawBubble(float scale, uint16_t bgColor, uint16_t fgColor);
    void resolveTheme(const String& emotion, uint16_t& bgColor, uint16_t& fgColor) const;

    String message_;
    String emotion_ = "calm";
    BubbleSize size_ = BubbleSize::Medium;
    unsigned long showStartMs_ = 0;
    unsigned long showDurationMs_ = 0;
    bool visible_ = false;
};

}  // namespace Flic
