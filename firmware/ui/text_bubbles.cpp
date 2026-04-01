#include "text_bubbles.h"

#include <M5Unified.h>

namespace Flic {

bool TextBubbles::begin() {
    visible_ = false;
    showStartMs_ = 0;
    showDurationMs_ = 0;
    return true;
}

void TextBubbles::showMessage(const String& msg, BubbleSize size, const String& emotion) {
    message_ = msg;
    size_ = size;
    emotion_ = emotion;
    showStartMs_ = millis();

    const unsigned long base = 900;
    const unsigned long perChar = 45;
    unsigned long sizeExtra = 0;
    if (size == BubbleSize::Medium) {
        sizeExtra = 250;
    } else if (size == BubbleSize::Large) {
        sizeExtra = 450;
    }

    showDurationMs_ = base + perChar * static_cast<unsigned long>(msg.length()) + sizeExtra;
    if (showDurationMs_ > 7000) {
        showDurationMs_ = 7000;
    }

    visible_ = true;
}

void TextBubbles::update() {
    if (!visible_) {
        return;
    }

    const unsigned long now = millis();
    const unsigned long elapsed = now - showStartMs_;
    if (elapsed >= showDurationMs_) {
        visible_ = false;
        return;
    }

    const unsigned long popInMs = 170;
    const unsigned long popOutMs = 220;
    float scale = 1.0f;
    if (elapsed < popInMs) {
        scale = static_cast<float>(elapsed) / static_cast<float>(popInMs);
        if (scale < 0.15f) {
            scale = 0.15f;
        }
    } else if (showDurationMs_ - elapsed < popOutMs) {
        scale = static_cast<float>(showDurationMs_ - elapsed) / static_cast<float>(popOutMs);
        if (scale < 0.15f) {
            scale = 0.15f;
        }
    }

    uint16_t bgColor = TFT_DARKGREY;
    uint16_t fgColor = TFT_WHITE;
    resolveTheme(emotion_, bgColor, fgColor);
    drawBubble(scale, bgColor, fgColor);
}

bool TextBubbles::isVisible() const {
    return visible_;
}

void TextBubbles::drawBubble(float scale, uint16_t bgColor, uint16_t fgColor) {
    auto& display = M5.Display;
    const int screenW = display.width();
    const int screenH = display.height();

    int textSize = 1;
    if (size_ == BubbleSize::Medium) {
        textSize = 2;
    } else if (size_ == BubbleSize::Large) {
        textSize = 3;
    }

    const int bubbleW = static_cast<int>((screenW - 20) * scale);
    const int bubbleHBase = size_ == BubbleSize::Small ? 38 : (size_ == BubbleSize::Medium ? 62 : 86);
    const int bubbleH = static_cast<int>(bubbleHBase * scale);
    const int bubbleX = (screenW - bubbleW) / 2;
    const int bubbleY = screenH - bubbleH - 10;

    display.startWrite();
    display.fillRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 12, bgColor);
    display.drawRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 12, fgColor);

    display.fillTriangle(
        bubbleX + bubbleW / 2 - 8,
        bubbleY + bubbleH,
        bubbleX + bubbleW / 2 + 8,
        bubbleY + bubbleH,
        bubbleX + bubbleW / 2,
        bubbleY + bubbleH + 10,
        bgColor
    );

    display.setTextColor(fgColor, bgColor);
    display.setTextSize(textSize);
    String clipped = message_;
    if (clipped.length() > 80) {
        clipped = clipped.substring(0, 77) + "...";
    }

    const int paddingX = 10;
    const int paddingY = 10;
    const int maxTextWidth = bubbleW - (paddingX * 2);
    const int lineHeight = 8 * textSize + 2;
    const int maxLines = (bubbleH - (paddingY * 2)) / lineHeight;

    String lines[8];
    int lineCount = 0;
    String line;
    int start = 0;
    while (start < clipped.length()) {
        int space = clipped.indexOf(' ', start);
        String word = (space < 0) ? clipped.substring(start) : clipped.substring(start, space);
        if (word.length() == 0) {
            start = (space < 0) ? clipped.length() : (space + 1);
            continue;
        }

        String candidate = line.length() == 0 ? word : (line + " " + word);
        if (display.textWidth(candidate.c_str()) <= maxTextWidth || line.length() == 0) {
            line = candidate;
        } else {
            if (lineCount < 8) {
                lines[lineCount++] = line;
            }
            line = word;
        }

        if (space < 0) {
            break;
        }
        start = space + 1;
    }
    if (line.length() > 0) {
        if (lineCount < 8) {
            lines[lineCount++] = line;
        }
    }

    if (maxLines > 0 && lineCount > maxLines) {
        lineCount = maxLines;
        String& last = lines[lineCount - 1];
        while (last.length() > 0 && display.textWidth((last + "...").c_str()) > maxTextWidth) {
            last.remove(last.length() - 1);
        }
        last += "...";
    }

    int cursorY = bubbleY + paddingY;
    for (int i = 0; i < lineCount; ++i) {
        display.setCursor(bubbleX + paddingX, cursorY);
        display.print(lines[i]);
        cursorY += lineHeight;
    }
    display.endWrite();
}

void TextBubbles::resolveTheme(const String& emotion, uint16_t& bgColor, uint16_t& fgColor) const {
    bgColor = TFT_DARKGREY;
    fgColor = TFT_WHITE;

    if (emotion == "calm") {
        bgColor = TFT_NAVY;
        fgColor = TFT_CYAN;
    } else if (emotion == "curious") {
        bgColor = 0x500A;
        fgColor = 0xD7FF;
    } else if (emotion == "happy") {
        bgColor = TFT_DARKGREEN;
        fgColor = TFT_GREENYELLOW;
    } else if (emotion == "excited") {
        bgColor = 0xB400;
        fgColor = TFT_YELLOW;
    } else if (emotion == "warning") {
        bgColor = TFT_MAROON;
        fgColor = TFT_ORANGE;
    } else if (emotion == "sleepy") {
        bgColor = 0x0410;
        fgColor = TFT_CYAN;
    }
}

}  // namespace Flic
