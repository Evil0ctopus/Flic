#include "animation_engine.h"

#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <SD.h>

#include <vector>

namespace Flic {
namespace {
constexpr const char* kAnimationRoot = "/ai/animations";
constexpr uint16_t kMinimumFps = 10;
constexpr uint16_t kMaximumFps = 20;

struct Pixel {
    int16_t x;
    int16_t y;
    uint32_t color;
};

struct Frame {
    uint16_t durationMs;
    std::vector<Pixel> pixels;
};

struct Animation {
    String name;
    uint16_t fps;
    std::vector<Frame> frames;
};

bool parseHexColor(const char* colorText, uint32_t& color) {
    if (colorText == nullptr || colorText[0] != '#') {
        return false;
    }

    if (strlen(colorText) != 7) {
        return false;
    }

    const uint32_t value = strtoul(colorText + 1, nullptr, 16);

    color = value;
    return true;
}

bool loadAnimationDocument(const String& path, Animation& animation) {
    File file = SD.open(path);
    if (!file) {
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, file);
    file.close();
    if (error) {
        Serial.printf("Flic: invalid animation JSON (%s): %s\n", path.c_str(), error.c_str());
        return false;
    }

    animation.name = document["name"] | "";
    animation.fps = document["fps"] | 0;
    if (animation.name.isEmpty() || animation.fps < kMinimumFps || animation.fps > kMaximumFps) {
        Serial.printf("Flic: animation rejected (%s): invalid header\n", path.c_str());
        return false;
    }

    JsonArray frames = document["frames"].as<JsonArray>();
    if (frames.isNull() || frames.size() == 0) {
        Serial.printf("Flic: animation rejected (%s): no frames\n", path.c_str());
        return false;
    }

    animation.frames.clear();
    for (JsonObject frameObject : frames) {
        Frame frame;
        frame.durationMs = frameObject["duration"] | 0;
        if (frame.durationMs == 0) {
            Serial.printf("Flic: animation rejected (%s): frame duration invalid\n", path.c_str());
            return false;
        }

        JsonArray pixels = frameObject["pixels"].as<JsonArray>();
        if (pixels.isNull()) {
            Serial.printf("Flic: animation rejected (%s): frame pixels missing\n", path.c_str());
            return false;
        }

        for (JsonObject pixelObject : pixels) {
            Pixel pixel;
            pixel.x = pixelObject["x"] | 0;
            pixel.y = pixelObject["y"] | 0;
            const char* colorText = pixelObject["color"] | nullptr;
            if (!parseHexColor(colorText, pixel.color)) {
                Serial.printf("Flic: animation rejected (%s): invalid color\n", path.c_str());
                return false;
            }
            frame.pixels.push_back(pixel);
        }

        animation.frames.push_back(frame);
    }

    return !animation.frames.empty();
}

}  // namespace

bool AnimationEngine::begin() {
    return SdManager::isMounted() && SD.exists(kAnimationRoot);
}

bool AnimationEngine::hasRealAnimations() const {
    File root = SD.open(kAnimationRoot);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const String fileName = entry.name();
            if (fileName.endsWith(".json") && !fileName.endsWith("placeholder.json")) {
                return true;
            }
        }
        entry = root.openNextFile();
    }

    return false;
}

bool AnimationEngine::isPlaying() const {
    return isPlaying_;
}

bool AnimationEngine::playFirstAnimation() {
    String filePath;
    if (!loadFirstAnimationFromDisk(filePath)) {
        return false;
    }

    return renderAnimationFile(filePath);
}

bool AnimationEngine::playAnimation(const char* fileName) {
    if (fileName == nullptr || fileName[0] == '\0') {
        return false;
    }

    String path = String(kAnimationRoot) + "/" + fileName;
    if (!SD.exists(path)) {
        return false;
    }

    return renderAnimationFile(path);
}

bool AnimationEngine::generateFirstAnimationIfNeeded() {
    if (hasRealAnimations()) {
        return true;
    }

    Serial.println("Flic: no real animations found; run ai/scripts/generate_first_animation.py on the host.");
    return false;
}

bool AnimationEngine::loadFirstAnimationFromDisk(String& filePath) {
    File root = SD.open(kAnimationRoot);
    if (!root || !root.isDirectory()) {
        Serial.println("Flic: animation directory missing.");
        return false;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String candidate = entry.name();
            if (candidate.endsWith(".json") && !candidate.endsWith("placeholder.json")) {
                filePath = String(kAnimationRoot) + "/" + candidate;
                return true;
            }
        }
        entry = root.openNextFile();
    }

    return false;
}

bool AnimationEngine::renderAnimationFile(const String& filePath) {
    Animation animation;
    if (!loadAnimationDocument(filePath, animation)) {
        return false;
    }

    auto& display = M5.Display;
    const uint32_t frameInterval = 1000U / animation.fps;
    isPlaying_ = true;
    for (const Frame& frame : animation.frames) {
        display.startWrite();
        display.fillScreen(TFT_BLACK);
        for (const Pixel& pixel : frame.pixels) {
            display.drawPixel(pixel.x, pixel.y, pixel.color);
        }
        display.endWrite();

        const uint32_t delayMs = frame.durationMs > 0 ? frame.durationMs : frameInterval;
        delay(delayMs);
    }
    isPlaying_ = false;

    return true;
}

}  // namespace Flic
