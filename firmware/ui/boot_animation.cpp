#include "boot_animation.h"

#include "../subsystems/light_engine.h"

#include <M5Unified.h>

namespace Flic {

void showBootAnimation(LightEngine& lightEngine) {
    auto& display = M5.Display;
    const int centerX = display.width() / 2;
    const int centerY = display.height() / 2;
    lightEngine.setColor(0, 0, 40);
    lightEngine.setBrightness(10);

    for (int radius = 20; radius <= 44; radius += 8) {
        display.startWrite();
        display.fillScreen(TFT_BLACK);
        display.fillCircle(centerX, centerY, radius + 10, 0x0841);
        display.setTextColor(TFT_WHITE, 0x0841);
        display.setTextDatum(middle_center);
        display.drawCircle(centerX, centerY, radius, TFT_DARKGREY);
        display.drawCircle(centerX, centerY, radius - 6, TFT_CYAN);
        display.drawString("Flic waking up", centerX, centerY - 6);
        display.setTextSize(1);
        display.drawString("booting...", centerX, centerY + 20);
        display.endWrite();
        lightEngine.pulse(0, 0, 255, 45);
    }
}

}  // namespace Flic
