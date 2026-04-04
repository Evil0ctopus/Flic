#include "boot_animation.h"

#include "../diagnostics/sd_diagnostics.h"
#include "../subsystems/light_engine.h"
#include "../subsystems/sd_manager.h"

#include <M5Unified.h>
#include <SD.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Flic {
namespace {

constexpr const char* kBootRoot = "/Flic/boot";
constexpr uint16_t kBootFrameDelayMs = 16;
constexpr bool kBootFrameIndexOverlay = true;

bool readPngDimensions(const uint8_t* data, size_t size, int& width, int& height) {
    if (data == nullptr || size < 24) {
        return false;
    }

    static constexpr uint8_t kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) {
        if (data[i] != kPngSig[i]) {
            return false;
        }
    }

    width = (static_cast<int>(data[16]) << 24) | (static_cast<int>(data[17]) << 16) |
            (static_cast<int>(data[18]) << 8) | static_cast<int>(data[19]);
    height = (static_cast<int>(data[20]) << 24) | (static_cast<int>(data[21]) << 16) |
             (static_cast<int>(data[22]) << 8) | static_cast<int>(data[23]);

    return width > 0 && height > 0;
}

String baseNameFromPath(const String& path) {
    int slash = path.lastIndexOf('/');
    if (slash < 0) {
        slash = path.lastIndexOf('\\');
    }
    if (slash < 0) {
        return path;
    }
    return path.substring(slash + 1);
}

bool isBootFrameFile(const String& name) {
    const String fileName = baseNameFromPath(name);
    return fileName.startsWith("frame_") && fileName.endsWith(".png");
}

std::vector<String> listBootFrames() {
    std::vector<String> frames;
    if (!SD.exists(kBootRoot)) {
        Serial.printf("Flic: boot frame folder missing: %s\n", kBootRoot);
        return frames;
    }

    File root = SD.open(kBootRoot);
    if (!root || !root.isDirectory()) {
        return frames;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const String name = String(entry.name());
            if (isBootFrameFile(name)) {
                frames.push_back(baseNameFromPath(name));
            }
        }
        entry = root.openNextFile();
    }

    std::sort(frames.begin(), frames.end());
    Serial.printf("Flic: discovered %u boot frame(s) in %s\n", static_cast<unsigned>(frames.size()), kBootRoot);
    return frames;
}

bool drawBootFrame(const String& framePath) {
    File file = SD.open(framePath, FILE_READ);
    if (!file || file.isDirectory()) {
        return false;
    }

    const size_t size = static_cast<size_t>(file.size());
    if (size == 0) {
        file.close();
        return false;
    }

    std::vector<uint8_t> buffer(size);
    const size_t read = file.read(buffer.data(), size);
    file.close();
    if (read != size) {
        return false;
    }

    int srcWidth = 0;
    int srcHeight = 0;
    if (!readPngDimensions(buffer.data(), buffer.size(), srcWidth, srcHeight)) {
        srcWidth = 240;
        srcHeight = 240;
    }

    auto& display = M5.Display;
    const int dstWidth = display.width();
    const int dstHeight = display.height();

    LGFX_Sprite sprite(&display);
    sprite.setColorDepth(16);
    sprite.setPsram(true);
    if (sprite.createSprite(srcWidth, srcHeight) == nullptr) {
        const int x = dstWidth > srcWidth ? (dstWidth - srcWidth) / 2 : 0;
        const int y = dstHeight > srcHeight ? (dstHeight - srcHeight) / 2 : 0;
        display.startWrite();
        display.drawPng(buffer.data(), buffer.size(), x, y);
        display.endWrite();
        return true;
    }

    sprite.fillSprite(TFT_BLACK);
    sprite.drawPng(buffer.data(), buffer.size(), 0, 0);

    const uint16_t* src = static_cast<const uint16_t*>(sprite.getBuffer());
    if (src == nullptr) {
        sprite.deleteSprite();
        return false;
    }

    std::vector<uint16_t> scanline(static_cast<size_t>(dstWidth));
    display.startWrite();
    for (int y = 0; y < dstHeight; ++y) {
        const int srcY = (y * srcHeight) / dstHeight;
        const uint16_t* srcRow = src + (srcY * srcWidth);
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX = (x * srcWidth) / dstWidth;
            scanline[static_cast<size_t>(x)] = srcRow[srcX];
        }
        display.pushImage(0, y, dstWidth, 1, scanline.data());
    }
    display.endWrite();
    sprite.deleteSprite();
    return true;
}

bool playBootFramesFromSd(LightEngine& lightEngine) {
    if (!SdManager::isMounted()) {
        Serial.println("Flic: SD not mounted, skipping boot animation load.");
        return false;
    }

    const std::vector<String> frames = listBootFrames();
    if (frames.empty()) {
        return false;
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        const String path = String(kBootRoot) + "/" + frames[i];
        SdDiagnostics::logBootAnimationFrame(frames[i]);
        if (!drawBootFrame(path)) {
            Serial.printf("Flic: failed boot frame %s\n", path.c_str());
            return false;
        }
        if (kBootFrameIndexOverlay) {
            auto& display = M5.Display;
            display.startWrite();
            display.fillRect(0, 0, 70, 20, TFT_BLACK);
            display.setTextSize(1);
            display.setTextColor(TFT_YELLOW, TFT_BLACK);
            display.setCursor(4, 6);
            display.printf("F%03u", static_cast<unsigned>(i));
            display.endWrite();
        }
        if ((i % 8U) == 0U) {
            lightEngine.pulse(0, 160, 255, 35);
        }
        delay(kBootFrameDelayMs);
    }

    Serial.printf("Flic: boot animation played from SD (%u frames)\n", static_cast<unsigned>(frames.size()));
    return true;
}

}  // namespace

void showBootAnimation(LightEngine& lightEngine) {
    if (playBootFramesFromSd(lightEngine)) {
        return;
    }

    const bool sdError = !SdManager::isMounted();

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
        display.drawString(sdError ? "SD ERROR" : "Flic waking up", centerX, centerY - 6);
        display.setTextSize(1);
        display.drawString(sdError ? "using fallback" : "booting...", centerX, centerY + 20);
        display.endWrite();
        lightEngine.pulse(0, 0, 255, 45);
        delay(120);
    }
}

}  // namespace Flic
