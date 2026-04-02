#include "animation_engine.h"

#include "../diagnostics/webui_event_hook.h"
#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <SD.h>

#include <vector>

namespace Flic {
namespace {
constexpr const char* kAnimationRoot = "/Flic/animations";
constexpr uint16_t kMinimumFps = 10;
constexpr uint16_t kMaximumFps = 20;
constexpr float kMinimumPlaybackSpeed = 0.25f;
constexpr float kMaximumPlaybackSpeed = 4.0f;
constexpr uint32_t kAnimationQueueWaitMs = 0;
constexpr uint32_t kLongAnimationWarnMs = 1000;
constexpr uint32_t kAnimationTaskStackWords = 7168;
constexpr uint32_t kAnimationTaskPriority = 1;

struct AnimationRequest {
    char path[96];
    uint32_t queuedAtMs = 0;
};

QueueHandle_t gAnimationQueue = nullptr;
TaskHandle_t gAnimationTask = nullptr;
AnimationEngine* gAnimationEngine = nullptr;
volatile bool gAnimationBusy = false;

bool endsWithJson(const String& value) {
    return value.endsWith(".json");
}

String toLowerCopy(String value) {
    value.toLowerCase();
    return value;
}

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
    gAnimationEngine = this;
    if (gAnimationQueue == nullptr) {
        gAnimationQueue = xQueueCreate(2, sizeof(AnimationRequest));
    }
    if (gAnimationTask == nullptr && gAnimationQueue != nullptr) {
        auto animationTask = [](void* parameter) {
            auto* owner = static_cast<AnimationEngine*>(parameter);
            AnimationRequest request{};
            for (;;) {
                if (gAnimationQueue != nullptr && xQueueReceive(gAnimationQueue, &request, portMAX_DELAY) == pdTRUE) {
                    if (owner != nullptr) {
                        Serial.printf("Flic: animation dequeued %s after %lu ms\n",
                                      request.path, static_cast<unsigned long>(millis() - request.queuedAtMs));
                        Animation animation;
                        const String filePath = String(request.path);
                        if (!loadAnimationDocument(filePath, animation)) {
                            continue;
                        }

                        auto& display = M5.Display;
                        const float speed = owner->playbackSpeed() < kMinimumPlaybackSpeed ? kMinimumPlaybackSpeed : owner->playbackSpeed();
                        const uint32_t frameInterval = static_cast<uint32_t>((1000.0f / animation.fps) / speed);
                        const unsigned long startMs = millis();
                        gAnimationBusy = true;
                        Serial.printf("Flic: animation start %s (%u frames, %u fps)\n",
                                      filePath.c_str(), static_cast<unsigned>(animation.frames.size()), static_cast<unsigned>(animation.fps));
                        WebUiEventHook::emit("animation", String("{\"kind\":\"start\",\"name\":\"") + animation.name +
                                                             "\",\"fps\":" + animation.fps + ",\"frames\":" +
                                                             static_cast<uint32_t>(animation.frames.size()) + "}");
                        for (const Frame& frame : animation.frames) {
                            display.startWrite();
                            display.fillScreen(TFT_BLACK);
                            for (const Pixel& pixel : frame.pixels) {
                                display.drawPixel(pixel.x, pixel.y, pixel.color);
                            }
                            display.endWrite();

                            const uint32_t delayMs = frame.durationMs > 0 ? static_cast<uint32_t>(frame.durationMs / speed) : frameInterval;
                            vTaskDelay(pdMS_TO_TICKS(delayMs == 0 ? 1 : delayMs));
                        }
                        gAnimationBusy = false;
                        const unsigned long elapsedMs = millis() - startMs;
                        Serial.printf("Flic: animation end %s (%lu ms)\n", filePath.c_str(), static_cast<unsigned long>(elapsedMs));
                        if (elapsedMs > kLongAnimationWarnMs) {
                            Serial.printf("Flic: animation ran long (%lu ms)\n", static_cast<unsigned long>(elapsedMs));
                        }
                        WebUiEventHook::emit("animation", String("{\"kind\":\"end\",\"name\":\"") + animation.name + "\"}");
                    }
                }
            }
        };
        xTaskCreatePinnedToCore(animationTask, "anim_task", kAnimationTaskStackWords, this, kAnimationTaskPriority, &gAnimationTask, 1);
    }

    if (gAnimationQueue == nullptr || gAnimationTask == nullptr) {
        Serial.println("Flic: animation worker unavailable; playback may be fallback synchronous");
    }

    return SdManager::isMounted() && SD.exists(kAnimationRoot);
}

float AnimationEngine::playbackSpeed() const {
    return playbackSpeed_;
}

void AnimationEngine::setPlaybackSpeed(float speed) {
    if (speed < kMinimumPlaybackSpeed) {
        speed = kMinimumPlaybackSpeed;
    } else if (speed > kMaximumPlaybackSpeed) {
        speed = kMaximumPlaybackSpeed;
    }
    playbackSpeed_ = speed;
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
    return gAnimationBusy;
}

bool AnimationEngine::playFirstAnimation() {
    String filePath;
    if (!loadFirstAnimationFromDisk(filePath)) {
        return false;
    }

    if (gAnimationQueue == nullptr || gAnimationEngine == nullptr) {
        return false;
    }

    AnimationRequest request{};
    snprintf(request.path, sizeof(request.path), "%s", filePath.c_str());
    request.queuedAtMs = millis();

    if (xQueueSend(gAnimationQueue, &request, kAnimationQueueWaitMs) != pdTRUE) {
        Serial.printf("Flic: animation queue full, dropped %s\n", request.path);
        return false;
    }

    Serial.printf("Flic: animation queued %s\n", request.path);
    return true;
}

bool AnimationEngine::playAnimation(const char* fileName) {
    if (fileName == nullptr || fileName[0] == '\0') {
        return false;
    }

    String path = String(kAnimationRoot) + "/" + fileName;
    if (!SD.exists(path)) {
        return false;
    }

    if (gAnimationQueue == nullptr || gAnimationEngine == nullptr) {
        return false;
    }

    AnimationRequest request{};
    snprintf(request.path, sizeof(request.path), "%s", path.c_str());
    request.queuedAtMs = millis();

    if (xQueueSend(gAnimationQueue, &request, kAnimationQueueWaitMs) != pdTRUE) {
        Serial.printf("Flic: animation queue full, dropped %s\n", request.path);
        return false;
    }

    Serial.printf("Flic: animation queued %s\n", request.path);
    return true;
}

bool AnimationEngine::playPreset(const String& preset) {
    WebUiEventHook::emit("animation", String("{\"kind\":\"preset\",\"value\":\"") + preset + "\"}");
    String key = toLowerCopy(preset);
    if (key.length() == 0) {
        return false;
    }

    if (endsWithJson(key)) {
        return playAnimation(key.c_str());
    }

    if (key == "blink") {
        return playAnimation("blink.json");
    }

    if (key == "idle" || key == "idle_breathing") {
        return playAnimation("idle_breathing.json");
    }

    if (key == "happy_wiggle" || key == "wiggle") {
        return playAnimation("happy_wiggle.json");
    }

    if (key == "sleepy_fade" || key == "fade") {
        return playAnimation("sleepy_fade.json");
    }

    if (key == "surprise" || key == "surprised") {
        return playAnimation("surprise.json");
    }

    if (key == "thinking" || key == "thinking_loop") {
        return playAnimation("thinking_loop.json");
    }

    if (key.startsWith("micro_")) {
        return playAnimation((key + ".json").c_str());
    }

    String candidate = key + ".json";
    return playAnimation(candidate.c_str());
}

bool AnimationEngine::playEmotionCue(const String& emotion) {
    WebUiEventHook::emit("animation", String("{\"kind\":\"emotion_cue\",\"value\":\"") + emotion + "\"}");
    String key = toLowerCopy(emotion);
    if (key == "calm") {
        return playPreset("idle_breathing");
    }
    if (key == "curious") {
        return playPreset("thinking_loop");
    }
    if (key == "happy") {
        return playPreset("happy_wiggle");
    }
    if (key == "sleepy") {
        return playPreset("sleepy_fade");
    }
    if (key == "surprised" || key == "warning" || key == "surprise") {
        return playPreset("surprise");
    }

    return playPreset(key);
}

bool AnimationEngine::playMicroGesture(const String& gesture) {
    WebUiEventHook::emit("animation", String("{\"kind\":\"micro\",\"value\":\"") + gesture + "\"}");
    String key = toLowerCopy(gesture);
    if (key.length() == 0) {
        return false;
    }

    if (key.startsWith("micro_")) {
        return playPreset(key);
    }

    return playPreset(String("micro_") + key);
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

}  // namespace Flic
