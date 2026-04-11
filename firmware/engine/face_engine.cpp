#include "face_engine.h"

#include "face_settings_manager.h"
#include "../subsystems/sd_manager.h"

#include "emotion_blend_engine.h"
#include "micro_expression_engine.h"
#include "personality_state_machine.h"

#include <M5Unified.h>
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <algorithm>

namespace Flic {
// Verifies the presence and frame count of a required animation set, logs results
bool FaceEngine::verifyAnimationSet(const char* animationName) const {
    Serial.printf("[LOAD] Animation verify: %s...\n", animationName);
    const AnimationDefinition* def = definitionFor(animationName);
    if (!def) {
        Serial.printf("[LOAD] Animation verify: definition NOT FOUND: %s\n", animationName);
        return false;
    }
    FrameSequence seq = resolveSequence(activeStyle_, animationName);
    if (seq.frames.empty()) {
        Serial.printf("[LOAD] Animation verify: frames MISSING: %s\n", animationName);
        return false;
    }
    Serial.printf("[LOAD] Animation verify: %s: %u frames (expected %u)\n", animationName, (unsigned)seq.frames.size(), (unsigned)def->expectedFrames);
    if (seq.frames.size() < def->expectedFrames) {
        Serial.printf("[LOAD] Animation verify: WARNING: %s has fewer frames than expected!\n", animationName);
        return false;
    }
    Serial.printf("[LOAD] Animation verify: OK: %s\n", animationName);
    return true;
}
} // namespace Flic
#include "face_engine.h"

#include "face_settings_manager.h"
#include "../subsystems/sd_manager.h"

#include "emotion_blend_engine.h"
#include "micro_expression_engine.h"
#include "personality_state_machine.h"

#include <M5Unified.h>
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

#ifndef VECTOR_ONLY_FACE
#define VECTOR_ONLY_FACE 0
#endif

const Flic::FaceEngine::AnimationDefinition kPrimeDefinitions[] = {
    {"idle", 12, 150, Flic::FaceEngine::TimingCurve::Linear, 1.00f, 10.0f, false, false, false},
};

namespace Flic {
namespace {
constexpr const char* kFaceRoot = "/Flic/animations/face";
constexpr const char* kMoodStatePath = "/Flic/memory/mood_state.json";
constexpr const char* kPersonalityMemoryPath = "/Flic/memory/personality_memory.json";
constexpr const char* kPersonalityProfilePath = "/Flic/config/personality_profile.json";
constexpr const char* kFaceMetadataPrimaryPath = "/Flic/animations/face/animation_metadata.json";
constexpr const char* kFaceMetadataSecondaryPath = "/src/face/animation_metadata.json";
constexpr const char* kDefaultStyle = "default";
constexpr const char* kDefaultAnimation = "idle";
constexpr const char* kRequiredAnimations[] = {
    "idle"
};
constexpr uint32_t kRenderTaskStackWords = 8192;
constexpr uint32_t kRenderTaskPriority = 2;
constexpr uint32_t kPlaybackQueueLength = 6;
constexpr uint32_t kIdlePulsePeriodMs = 9000;
constexpr uint32_t kBlinkPeriodMs = 3600;
constexpr uint32_t kSchedulerFrameMs = 16;
constexpr uint32_t kTelemetryLogPeriodMs = 5000;
constexpr uint32_t kMissingFramesLogIntervalMs = 2000;
constexpr bool kEnableGlowOverlay = false;
constexpr bool kEnableVectorBootAnimation = true;
constexpr size_t kInlineFrameMaxBytes = 48 * 1024;
constexpr size_t kStreamFrameMaxBytes = 2 * 1024 * 1024;
constexpr uint16_t kExpectedFrameWidth = 240;
constexpr uint16_t kExpectedFrameHeight = 240;
constexpr const char* kRequiredEmotionFolders[] = {
    "neutral",
    "curious",
    "focused",
    "tired",
    "excited",
    "confused",
    "calm",
    "happy",
    "sleepy",
    "surprised",
    "speaking",
    "listening",
    "idle",
};

struct PlaybackRequest {
    char style[32];
    char animation[32];
    uint8_t priority = 0;
    uint32_t queuedAtMs = 0;
};

QueueHandle_t gPlaybackQueue = nullptr;
TaskHandle_t gPlaybackTask = nullptr;
volatile bool gFaceBusy = false;
volatile bool gInterruptRequested = false;
unsigned long gLastMissingFramesLogMs = 0;

template <typename T, typename = void>
struct HasIsBusy : std::false_type {};

template <typename T>
struct HasIsBusy<T, std::void_t<decltype(std::declval<T&>().isBusy())>> : std::true_type {};

template <typename T, typename = void>
struct HasDisplayBusy : std::false_type {};

template <typename T>
struct HasDisplayBusy<T, std::void_t<decltype(std::declval<T&>().displayBusy())>> : std::true_type {};

template <typename T>
typename std::enable_if<HasIsBusy<T>::value, void>::type waitForDisplayReadyImpl(T& display) {
    while (display.isBusy()) {
        delayMicroseconds(50);
    }
}

template <typename T>
typename std::enable_if<!HasIsBusy<T>::value && HasDisplayBusy<T>::value, void>::type waitForDisplayReadyImpl(T& display) {
    while (display.displayBusy()) {
        delayMicroseconds(50);
    }
}

template <typename T>
typename std::enable_if<!HasIsBusy<T>::value && !HasDisplayBusy<T>::value, void>::type waitForDisplayReadyImpl(T&) {
    delayMicroseconds(500);
}

template <typename T, typename = void>
struct HasPushPixels : std::false_type {};

template <typename T>
struct HasPushPixels<T, std::void_t<decltype(std::declval<T&>().pushPixels((const uint16_t*)nullptr, size_t(0)))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPushImage : std::false_type {};

template <typename T>
struct HasPushImage<T, std::void_t<decltype(std::declval<T&>().pushImage(int32_t(0), int32_t(0), int32_t(0), int32_t(0),
                                                                           (const uint16_t*)nullptr))>>
    : std::true_type {};

template <typename T>
typename std::enable_if<HasPushPixels<T>::value, void>::type pushSolidWindow(T& display,
                                                                              int x,
                                                                              int y,
                                                                              int width,
                                                                              int height,
                                                                              uint16_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    static std::vector<uint16_t> lineBuffer;
    if (lineBuffer.size() < static_cast<size_t>(width)) {
        lineBuffer.resize(width);
    }
    std::fill(lineBuffer.begin(), lineBuffer.begin() + width, color);

    display.setAddrWindow(x, y, width, height);
    for (int row = 0; row < height; ++row) {
        display.pushPixels(lineBuffer.data(), static_cast<size_t>(width));
    }
}

template <typename T>
typename std::enable_if<!HasPushPixels<T>::value && HasPushImage<T>::value, void>::type pushSolidWindow(T& display,
                                                                                                          int x,
                                                                                                          int y,
                                                                                                          int width,
                                                                                                          int height,
                                                                                                          uint16_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    static std::vector<uint16_t> blockBuffer;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (blockBuffer.size() < pixelCount) {
        blockBuffer.resize(pixelCount);
    }
    std::fill(blockBuffer.begin(), blockBuffer.begin() + pixelCount, color);
    display.pushImage(x, y, width, height, blockBuffer.data());
}

template <typename T>
typename std::enable_if<!HasPushPixels<T>::value && !HasPushImage<T>::value, void>::type pushSolidWindow(T&,
                                                                                                           int,
                                                                                                           int,
                                                                                                           int,
                                                                                                           int,
                                                                                                           uint16_t) {
}

template <typename T>
void waitForDisplayReady(T& display) {
    waitForDisplayReadyImpl(display);
}

template <typename T>
void clearFrameBackground(T& display, int x, int y, int width, int height) {
    display.fillRect(x, y, width, height, TFT_BLACK);
}

bool endsWithPng(const String& value) {
#if VECTOR_ONLY_FACE
    (void)value;
    return false;
#else
    return value.endsWith(".png");
#endif
}

uint32_t hashString(const String& value) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < static_cast<size_t>(value.length()); ++i) {
        hash ^= static_cast<uint8_t>(value.charAt(i));
        hash *= 16777619u;
    }
    return hash;
}

bool isFrameNameValid(const String& value) {
    const int slash = value.lastIndexOf('/');
    String name = slash >= 0 ? value.substring(slash + 1) : value;
    name.toLowerCase();
    if (!name.startsWith("frame_") || !name.endsWith(".png")) {
        return false;
    }
    const String stem = name.substring(6, name.length() - 4);
    if (stem.length() != 3) {
        return false;
    }
    for (size_t i = 0; i < static_cast<size_t>(stem.length()); ++i) {
        if (!isDigit(stem.charAt(i))) {
            return false;
        }
    }
    return true;
}

bool readPngHeader(const String& path, uint32_t& width, uint32_t& height, uint8_t& colorType) {
#if VECTOR_ONLY_FACE
    (void)path;
    width = 0;
    height = 0;
    colorType = 0;
    return false;
#else
    if (!SdManager::isMounted()) {
        return false;
    }

    width = 0;
    height = 0;
    colorType = 0;

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    uint8_t header[33] = {};
    const size_t n = file.read(header, sizeof(header));
    file.close();
    if (n < sizeof(header)) {
        return false;
    }

    static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (uint8_t i = 0; i < 8; ++i) {
        if (header[i] != kSig[i]) {
            return false;
        }
    }

    if (!(header[12] == 'I' && header[13] == 'H' && header[14] == 'D' && header[15] == 'R')) {
        return false;
    }

    width = (static_cast<uint32_t>(header[16]) << 24) | (static_cast<uint32_t>(header[17]) << 16) |
            (static_cast<uint32_t>(header[18]) << 8) | static_cast<uint32_t>(header[19]);
    height = (static_cast<uint32_t>(header[20]) << 24) | (static_cast<uint32_t>(header[21]) << 16) |
             (static_cast<uint32_t>(header[22]) << 8) | static_cast<uint32_t>(header[23]);
    colorType = header[25];
    return true;
#endif
}

int decodePngStatusCode(const String& path, uint32_t& width, uint32_t& height, uint8_t& colorType) {
#if VECTOR_ONLY_FACE
    (void)path;
    width = 0;
    height = 0;
    colorType = 0;
    return -100;
#else
    if (!SdManager::isMounted()) {
        return -1;
    }

    width = 0;
    height = 0;
    colorType = 0;

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return -2;
    }

    uint8_t header[33] = {};
    const size_t n = file.read(header, sizeof(header));
    file.close();
    if (n < sizeof(header)) {
        return -3;
    }

    static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (uint8_t i = 0; i < 8; ++i) {
        if (header[i] != kSig[i]) {
            return -4;
        }
    }

    if (!(header[12] == 'I' && header[13] == 'H' && header[14] == 'D' && header[15] == 'R')) {
        return -5;
    }

    width = (static_cast<uint32_t>(header[16]) << 24) | (static_cast<uint32_t>(header[17]) << 16) |
            (static_cast<uint32_t>(header[18]) << 8) | static_cast<uint32_t>(header[19]);
    height = (static_cast<uint32_t>(header[20]) << 24) | (static_cast<uint32_t>(header[21]) << 16) |
             (static_cast<uint32_t>(header[22]) << 8) | static_cast<uint32_t>(header[23]);
    colorType = header[25];

    if (width != kExpectedFrameWidth || height != kExpectedFrameHeight) {
        return -6;
    }

    return 0;
#endif
}

int frameNumberFromName(const String& value) {
    const int slash = value.lastIndexOf('/');
    String name = slash >= 0 ? value.substring(slash + 1) : value;
    const int dot = name.lastIndexOf('.');
    if (dot > 0) {
        name = name.substring(0, dot);
    }

    int end = static_cast<int>(name.length()) - 1;
    while (end >= 0 && !isDigit(name.charAt(end))) {
        --end;
    }
    if (end < 0) {
        return 0;
    }
    int begin = end;
    while (begin > 0 && isDigit(name.charAt(begin - 1))) {
        --begin;
    }
    return name.substring(begin, end + 1).toInt();
}

uint16_t scaleColor565(uint16_t color, float intensity) {
    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }

    uint8_t r = static_cast<uint8_t>((color >> 11) & 0x1F);
    uint8_t g = static_cast<uint8_t>((color >> 5) & 0x3F);
    uint8_t b = static_cast<uint8_t>(color & 0x1F);
    r = static_cast<uint8_t>(r * intensity);
    g = static_cast<uint8_t>(g * intensity);
    b = static_cast<uint8_t>(b * intensity);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

String jsonArrayFromStrings(const std::vector<String>& values) {
    String payload = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            payload += ',';
        }
        payload += '"';
        payload += values[i];
        payload += '"';
    }
    payload += ']';
    return payload;
}

String defaultMetadataJson() {
    return String("{") +
           "\"idle\":{\"frames\":12,\"frame_time_ms\":150,\"easing\":\"Linear\",\"glow_variation_percent\":10}" +
           "}";
}

}  // namespace

void FaceEngine::playbackTask(void* parameter) {
    auto* owner = static_cast<FaceEngine*>(parameter);
    PlaybackRequest request{};
    for (;;) {
        if (gPlaybackQueue == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(kSchedulerFrameMs));
            continue;
        }
        if (xQueueReceive(gPlaybackQueue, &request, pdMS_TO_TICKS(kSchedulerFrameMs)) != pdTRUE) {
            continue;
        }

        gInterruptRequested = false;
        gFaceBusy = true;
        const String style = String(request.style);
        const String animation = String(request.animation);
        const unsigned long startMs = millis();

        FaceEngine::FrameSequence sequence = owner != nullptr ? owner->resolveSequence(style, animation)
                                                              : FaceEngine::FrameSequence{};
        if (sequence.frames.empty() && owner != nullptr) {
            owner->loadAnimationSet(style);
            sequence = owner->resolveSequence(style, animation);
        }
        if (sequence.frames.empty() && owner != nullptr) {
            sequence = owner->resolveFallbackSequence(kDefaultAnimation);
        }

        if (sequence.frames.empty()) {
            const unsigned long nowMs = millis();
            if ((nowMs - gLastMissingFramesLogMs) >= kMissingFramesLogIntervalMs) {
                Serial.printf("[PLAY] Missing frames for %s/%s\n", style.c_str(), animation.c_str());
                gLastMissingFramesLogMs = nowMs;
            }
            if (owner != nullptr) {
                owner->finishPlayback();
            }
            gFaceBusy = false;
            continue;
        }

        const size_t frameCount = sequence.frames.size();
        if (owner != nullptr && frameCount > 0) {
            owner->runTransitionTo(sequence.frames.front());
        }

        unsigned long lastTickMs = millis();
        for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            if (gInterruptRequested && request.priority == 0) {
                break;
            }

            const float amplitude = owner != nullptr ? owner->speakingAmplitude_ : 0.0f;
            const uint16_t delayMs = owner != nullptr ? owner->frameDelayFor(sequence, frameIndex, amplitude)
                                                      : sequence.frameDelayMs;
            const unsigned long frameStartMs = millis();

            if (owner != nullptr) {
                const unsigned long nowMs = millis();
                const float dtSeconds = static_cast<float>(nowMs - lastTickMs) / 1000.0f;
                lastTickMs = nowMs;

                const EmotionBlendEngine::Snapshot blendSnapshot = owner->updateBlend(nowMs);
                const String renderEmotion = owner->renderEmotionForBlend(nowMs, static_cast<uint32_t>(frameIndex), blendSnapshot);
                owner->updateEmotion(nowMs);
                owner->updatePersonality(nowMs, blendSnapshot, renderEmotion);
                const PersonalityProfile profile = owner->personalityStateMachine_.profile();
                const MicroExpressionFrame microFrame = owner->updateMicroExpressions(nowMs,
                                                                                       dtSeconds,
                                                                                       blendSnapshot,
                                                                                       renderEmotion,
                                                                                       profile);

                FaceEngine::FrameSequence renderSequence = owner->resolveSequence(style, renderEmotion);
                if (renderSequence.frames.empty()) {
                    renderSequence = owner->resolveFallbackSequence(kDefaultAnimation);
                }

                if (!renderSequence.frames.empty()) {
                    const size_t renderIndex = owner->renderFrameIndexFor(renderSequence,
                                                                          nowMs,
                                                                          static_cast<uint32_t>(frameIndex),
                                                                          blendSnapshot);
                    FaceEngine::FrameSequence sourceSequence;
                    const FaceEngine::FrameSequence* sourceSequencePtr = nullptr;
                    size_t sourceIndex = 0;
                    if (blendSnapshot.active) {
                        String sourceEmotion = blendSnapshot.fromEmotion;
                        if (sourceEmotion.length() == 0) {
                            sourceEmotion = renderEmotion;
                        }
                        sourceSequence = owner->resolveSequence(style, sourceEmotion);
                        if (sourceSequence.frames.empty()) {
                            sourceSequence = owner->resolveFallbackSequence(kDefaultAnimation);
                        }
                        if (!sourceSequence.frames.empty()) {
                            sourceIndex = owner->renderFrameIndexFor(sourceSequence,
                                                                     nowMs,
                                                                     static_cast<uint32_t>(frameIndex),
                                                                     blendSnapshot);
                            sourceSequencePtr = &sourceSequence;
                        }
                    }

                    const FaceEngine::FrameAsset& renderFrameAsset = renderSequence.frames[renderIndex % renderSequence.frames.size()];
                    (void)renderFrameAsset;

                    const unsigned long drawStartUs = micros();
                    owner->drawFrame(renderSequence,
                                     renderIndex,
                                     microFrame,
                                     profile,
                                     blendSnapshot,
                                     0,
                                     true,
                                     sourceSequencePtr,
                                     sourceIndex);
                    const uint32_t drawUs = static_cast<uint32_t>(micros() - drawStartUs);
                    owner->noteFrameTelemetry(blendSnapshot.active,
                                              drawUs,
                                              delayMs,
                                              drawUs > (static_cast<uint32_t>(delayMs) * 1000UL));
                }

                owner->emitTelemetryIfDue(nowMs);
            }

            while ((millis() - frameStartMs) < delayMs) {
                if (gInterruptRequested && request.priority == 0) {
                    break;
                }

                const unsigned long nowMs = millis();
                lastTickMs = nowMs;
                if (owner != nullptr) {
                    owner->updateEmotion(nowMs);
                    owner->emitTelemetryIfDue(nowMs);
                }

                vTaskDelay(pdMS_TO_TICKS(kSchedulerFrameMs));
            }

            if (gInterruptRequested && request.priority == 0) {
                break;
            }
        }

        if (owner != nullptr) {
            owner->finishPlayback();
        }

        (void)startMs;
        gFaceBusy = false;
    }
}

void FaceEngine::renderBootAnimation() {
    auto& display = M5.Display;
    const int centerX = display.width() / 2;
    const int centerY = display.height() / 2;
    const int eyeOffsetX = 42;
    const int eyeY = centerY - 18;
    const int bootDurationMs = 15000;
    const int fadeStartMs = 13000;

    auto smoothstep = [](float x) {
        if (x < 0.0f) {
            x = 0.0f;
        }
        if (x > 1.0f) {
            x = 1.0f;
        }
        return x * x * (3.0f - 2.0f * x);
    };

    auto sineInOut = [](float x) {
        if (x < 0.0f) {
            x = 0.0f;
        }
        if (x > 1.0f) {
            x = 1.0f;
        }
        return 0.5f - 0.5f * std::cos(x * 3.14159265f);
    };

    const unsigned long startMs = millis();
    unsigned long nextFrameMs = startMs;

    while (true) {
        const unsigned long nowMs = millis();
        const unsigned long elapsedMs = nowMs - startMs;
        if (elapsedMs >= static_cast<unsigned long>(bootDurationMs)) {
            break;
        }

        const float t = static_cast<float>(elapsedMs) / static_cast<float>(bootDurationMs);
        const float fadeT = elapsedMs <= static_cast<unsigned long>(fadeStartMs)
                                ? 0.0f
                                : static_cast<float>(elapsedMs - fadeStartMs) /
                                      static_cast<float>(bootDurationMs - fadeStartMs);
        const float fadeOut = 1.0f - smoothstep(fadeT);

        const float pulseCycleMs = 1700.0f;
        const float pulsePhase = std::fmod(static_cast<float>(elapsedMs), pulseCycleMs) / pulseCycleMs;
        const float pulseSmooth = smoothstep(pulsePhase);
        const float pulseSin = sineInOut(pulsePhase);
        const float pulseStrength = (0.35f + 0.65f * (0.5f * pulseSmooth + 0.5f * pulseSin)) * fadeOut;

        const float cyanShift = smoothstep(t);
        const float baseR = 255.0f * (1.0f - cyanShift);
        const float baseG = 255.0f;
        const float baseB = 255.0f;

        waitForDisplayReady(display);
        display.startWrite();
        display.fillScreen(TFT_BLACK);

        // Strong neon pulse core.
        const uint16_t coreColor = display.color565(static_cast<uint8_t>(baseR * pulseStrength),
                                                    static_cast<uint8_t>(baseG * pulseStrength),
                                                    static_cast<uint8_t>(baseB * pulseStrength));
        const int coreRadius = 10 + static_cast<int>(14.0f * pulseSin);
        display.fillCircle(centerX, centerY, coreRadius, coreColor);

        // Expanding white->cyan gradient rings.
        for (int ring = 0; ring < 5; ++ring) {
            const float ringOffset = static_cast<float>(ring) * 0.18f;
            float localPhase = pulsePhase + ringOffset;
            if (localPhase > 1.0f) {
                localPhase -= 1.0f;
            }

            const float ringEase = smoothstep(localPhase);
            const int ringRadius = 24 + static_cast<int>(ringEase * 126.0f);
            const float ringEnergy = (1.0f - ringEase) * (1.0f - ringEase) * pulseStrength;
            const float ringCyanMix = cyanShift * (0.45f + 0.55f * ringEase);

            const uint8_t ringR = static_cast<uint8_t>(255.0f * (1.0f - ringCyanMix) * ringEnergy);
            const uint8_t ringG = static_cast<uint8_t>(255.0f * ringEnergy);
            const uint8_t ringB = static_cast<uint8_t>(255.0f * ringEnergy);
            const uint16_t ringColor = display.color565(ringR, ringG, ringB);

            display.drawCircle(centerX, centerY, ringRadius, ringColor);
            display.drawCircle(centerX, centerY, ringRadius + 1, ringColor);
        }

        // Eye reveal starts at 6 seconds.
        if (elapsedMs >= 6000) {
            const float reveal = smoothstep(static_cast<float>(elapsedMs - 6000) / 4000.0f) * fadeOut;
            const uint16_t eyeOutline = display.color565(static_cast<uint8_t>(220.0f * reveal),
                                                         static_cast<uint8_t>(255.0f * reveal),
                                                         static_cast<uint8_t>(255.0f * reveal));

            if (elapsedMs < 10000) {
                const int arcR0 = 9;
                const int arcR1 = 12;
                display.drawArc(centerX - eyeOffsetX, eyeY, arcR0, arcR1, 215.0f, 332.0f, eyeOutline);
                display.drawArc(centerX + eyeOffsetX, eyeY, arcR0, arcR1, 208.0f, 325.0f, eyeOutline);
            } else {
                // Stabilized eyes + one cute blink after 10 seconds.
                float blink = 0.0f;
                const unsigned long blinkStart = 10850;
                const unsigned long blinkMid = 11000;
                const unsigned long blinkEnd = 11150;
                if (elapsedMs >= blinkStart && elapsedMs < blinkMid) {
                    blink = static_cast<float>(elapsedMs - blinkStart) / static_cast<float>(blinkMid - blinkStart);
                } else if (elapsedMs >= blinkMid && elapsedMs < blinkEnd) {
                    blink = 1.0f - static_cast<float>(elapsedMs - blinkMid) / static_cast<float>(blinkEnd - blinkMid);
                }

                const int eyeRadius = 11;
                const int lid = static_cast<int>(blink * 10.0f);
                if (lid >= 8) {
                    display.drawFastHLine(centerX - eyeOffsetX - eyeRadius, eyeY, eyeRadius * 2, eyeOutline);
                    display.drawFastHLine(centerX + eyeOffsetX - eyeRadius, eyeY, eyeRadius * 2, eyeOutline);
                } else {
                    display.drawCircle(centerX - eyeOffsetX, eyeY, eyeRadius, eyeOutline);
                    display.drawCircle(centerX + eyeOffsetX, eyeY, eyeRadius, eyeOutline);
                    if (lid > 0) {
                        display.fillRect(centerX - eyeOffsetX - eyeRadius - 1, eyeY - eyeRadius - 1,
                                         eyeRadius * 2 + 3, lid, TFT_BLACK);
                        display.fillRect(centerX + eyeOffsetX - eyeRadius - 1, eyeY - eyeRadius - 1,
                                         eyeRadius * 2 + 3, lid, TFT_BLACK);
                    }
                }
            }
        }

        display.endWrite();

        // Frame pacing only.
        nextFrameMs += 16;
        const unsigned long frameNow = millis();
        if (nextFrameMs > frameNow) {
            const unsigned long sleepMs = nextFrameMs - frameNow;
            vTaskDelay(pdMS_TO_TICKS(sleepMs > 1 ? sleepMs : 1));
        } else {
            nextFrameMs = frameNow;
        }
    }

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.endWrite();
}

bool FaceEngine::begin() {
    requiredAnimations_.clear();
    metadataDefinitions_.clear();
    for (const AnimationDefinition& definition : kPrimeDefinitions) {
        metadataDefinitions_.push_back(definition);
    }
    for (const char* animation : kRequiredAnimations) {
        requiredAnimations_.push_back(String(animation));
    }

    ensureFaceDirectories();
    loadAnimationMetadata();
    scanIndex();
    transitionEngine_.setFrameRate(60.0f);
    renderFrameDelayMs_ = transitionEngine_.frameDelayMs();
    emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Crossfade);
    microExpressionEngine_.reset();
    microExpressionEngine_.setEnabled(true);
    telemetryWindowStartMs_ = millis();
    telemetryLastLogMs_ = telemetryWindowStartMs_;
    telemetryFrames_ = 0;
    telemetryBlendFrames_ = 0;
    telemetryOverBudgetFrames_ = 0;
    telemetryBlendFallbacks_ = 0;
    telemetryDrawUsTotal_ = 0;
    telemetryBlendDrawUsTotal_ = 0;
    microExpressionEngine_.setIntensity(microExpressionIntensity_);
    personalityStateMachine_.reset();
    personalityStateMachine_.enableContextRules(contextRulesEnabled_);
    adaptiveExpressionEngine_.reset();
    adaptiveModifiers_ = AdaptiveExpressionModifiers{};

    loadPersonalityProfile();
    moodModel_.begin(kMoodStatePath);
    moodModel_.setDecayRate(personalityProfile_.moodDecayRate);
    moodModel_.enableAdaptation(moodAdaptationEnabled_);
    if (!loadMood()) {
        moodModel_.setMood(personalityProfile_.baselineMood);
    }

    personalityMemory_.begin(kPersonalityMemoryPath);
    personalityMemory_.setLastKnownMood(getMood());

    if (!setStyle(activeStyle_)) {
        activeStyle_ = kDefaultStyle;
    }

    // Run startup boot sequence before asynchronous playback begins.
    if (kEnableVectorBootAnimation) {
        renderBootAnimation();
    } else {
        auto& display = M5.Display;
        waitForDisplayReady(display);
        display.startWrite();
        display.fillScreen(TFT_BLACK);
        display.endWrite();
    }

    if (gPlaybackQueue == nullptr) {
        gPlaybackQueue = xQueueCreate(kPlaybackQueueLength, sizeof(PlaybackRequest));
    }
    if (gPlaybackTask == nullptr && gPlaybackQueue != nullptr) {
        xTaskCreatePinnedToCore(FaceEngine::playbackTask,
                                "face_render",
                                kRenderTaskStackWords,
                                this,
                                kRenderTaskPriority,
                                &gPlaybackTask,
                                1);
    }

    loadAnimation(kDefaultAnimation);
    playAnimation(kDefaultAnimation);

    lastIdlePulseMs_ = millis();
    return gPlaybackQueue != nullptr && gPlaybackTask != nullptr;
}

void FaceEngine::update(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        deltaSeconds = 0.01f;
    }

    timeAccumulator_ += deltaSeconds;
    microExpressionEngine_.setEnabled(microExpressionsEnabled_);
    microExpressionEngine_.setIntensity(microExpressionIntensity_ * adaptiveModifiers_.microExpressionScale);
    personalityStateMachine_.enableContextRules(contextRulesEnabled_);
    personalityStateMachine_.update(millis(), activeEmotion_, emotionBlendEngine_.isActive(millis()));
    updatePersonality(deltaSeconds);
    if (speakingAmplitude_ > 0.0f) {
        speakingAmplitude_ -= deltaSeconds * 0.9f;
        if (speakingAmplitude_ < 0.0f) {
            speakingAmplitude_ = 0.0f;
        }
    }

    const unsigned long now = millis();
    if (shouldIdleTrigger()) {
        if ((now - lastIdlePulseMs_) >= kIdlePulsePeriodMs) {
            idlePulse();
            lastIdlePulseMs_ = now;
        }
    }
}

bool FaceEngine::loadAnimationSet(const String& styleName) {
    Serial.printf("[LOAD] Loading animation set: %s\n", styleName.c_str());
    if (styles_.empty()) {
        styles_.push_back(kDefaultStyle);
    }
    return setStyle(styleName.length() > 0 ? styleName : String(kDefaultStyle));
}

bool FaceEngine::loadAnimation(const String& animationName) {
    Serial.printf("[LOAD] Loading animation: %s\n", animationName.c_str());
    const String normalized = normalizeName(animationName);
    if (normalized.length() == 0) {
        Serial.println("[LOAD] Invalid animation name");
        Serial.printf("[LOAD] Frame count detected: 0\n");
        return false;
    }

    FrameSequence sequence;
    sequence.style = activeStyle_.length() > 0 ? activeStyle_ : String(kDefaultStyle);
    sequence.animation = normalized;
    FrameAsset vectorFrame;
    vectorFrame.path = "vector://" + sequence.animation;
    sequence.frames.push_back(vectorFrame);
    const AnimationDefinition* definition = definitionFor(sequence.animation);
    if (definition != nullptr) {
        sequence.frameDelayMs = definition->frameTimeMs;
        sequence.curve = definition->curve;
        sequence.glowMultiplier = definition->glowMultiplier;
        sequence.glowVariationPercent = definition->glowVariationPercent;
        sequence.slowPulse = definition->slowPulse;
        sequence.dynamicSpeaking = definition->dynamicSpeaking;
        sequence.glowSpike = definition->glowSpike;
    }
    Serial.printf("[LOAD] Frame count detected: %u\n", static_cast<unsigned>(sequence.frames.size()));
    return true;
}

bool FaceEngine::playAnimation(const String& animationName) {
    Serial.printf("[PLAY] Queue animation: %s\n", animationName.c_str());
    return play(animationName);
}

bool FaceEngine::setStyle(const String& styleName) {
    Serial.printf("[LOAD] Set style: %s\n", styleName.c_str());
    const String normalized = normalizeName(styleName);
    for (const String& style : styles_) {
        if (style == normalized) {
            activeStyle_ = style;
            dirty_ = true;
            return true;
        }
    }
    activeStyle_ = kDefaultStyle;
    dirty_ = true;
    return false;
}

bool FaceEngine::play(const String& animationName) {
    const String normalized = normalizeName(animationName);
    if (normalized.length() == 0) {
        return false;
    }

    String target = normalized;
    if (!isEmotionAvailable(target)) {
        target = kDefaultAnimation;
    }

    emotionBlendEngine_.stop();
    microExpressionEngine_.reset();
    registerInteraction(millis());
    if (!triggerPlayback(activeStyle_, target)) {
        return false;
    }

    activeAnimation_ = target;
    activeEmotion_ = target;
    transitionToIdlePending_ = false;
    playing_ = true;
    return true;
}

void FaceEngine::stop() {
    if (gPlaybackQueue != nullptr) {
        xQueueReset(gPlaybackQueue);
    }
    gInterruptRequested = true;
    emotionBlendEngine_.stop();
    microExpressionEngine_.reset();
    playing_ = false;
    transitionToIdlePending_ = false;
}

bool FaceEngine::setEasing(const String& animationName, const String& easingType) {
    const String normalizedAnimation = normalizeName(animationName);
    if (normalizedAnimation.length() == 0) {
        return false;
    }

    const TimingCurve curve = timingCurveFromName(easingType);
    bool changed = false;
    for (FrameSequence& sequence : index_) {
        if (sequence.animation == normalizedAnimation) {
            sequence.curve = curve;
            changed = true;
        }
    }

    for (AnimationDefinition& definition : metadataDefinitions_) {
        if (normalizedAnimation == definition.name) {
            definition.curve = curve;
            changed = true;
        }
    }

    return changed;
}

bool FaceEngine::setGlowProfile(const String& animationName, const FaceGlowProfile& params) {
    const String normalizedAnimation = normalizeName(animationName);
    if (normalizedAnimation.length() == 0) {
        return false;
    }

    bool changed = false;
    for (FrameSequence& sequence : index_) {
        if (sequence.animation == normalizedAnimation) {
            sequence.glowMultiplier = params.glowMultiplier < 0.05f ? 0.05f : params.glowMultiplier;
            sequence.glowVariationPercent = params.glowVariationPercent < 0.0f ? 0.0f : params.glowVariationPercent;
            sequence.slowPulse = params.slowPulse;
            sequence.dynamicSpeaking = params.amplitudePulse;
            sequence.glowSpike = params.glowSpike;
            changed = true;
        }
    }

    for (AnimationDefinition& definition : metadataDefinitions_) {
        if (normalizedAnimation == definition.name) {
            definition.glowMultiplier = params.glowMultiplier < 0.05f ? 0.05f : params.glowMultiplier;
            definition.glowVariationPercent = params.glowVariationPercent < 0.0f ? 0.0f : params.glowVariationPercent;
            definition.slowPulse = params.slowPulse;
            definition.dynamicSpeaking = params.amplitudePulse;
            definition.glowSpike = params.glowSpike;
            changed = true;
        }
    }

    return changed;
}

bool FaceEngine::setEmotion(const String& emotionName) {
    return setEmotionInternal(emotionName, true);
}

bool FaceEngine::clearEmotion() {
    transitionToIdlePending_ = false;
    activeEmotion_ = kDefaultAnimation;
    activeAnimation_ = kDefaultAnimation;
    microExpressionEngine_.reset();
    const bool queued = triggerPlayback(activeStyle_, kDefaultAnimation, true);
    playing_ = queued;
    return queued;
}

bool FaceEngine::setEmotionInternal(const String& emotionName, bool explicitCommand) {
    const String normalized = normalizeName(emotionName);
    if (normalized.length() == 0) {
        Serial.println("[LOAD] Invalid animation name");
        return false;
    }

    const String target = resolveAnimationForEmotion(normalized);

    emotionBlendEngine_.stop();
    const unsigned long nowMs = millis();
    if (explicitCommand) {
        registerInteraction(nowMs);
    }
    if (!triggerPlayback(activeStyle_, target, true)) {
        moodModel_.noteError();
        return false;
    }
    if (explicitCommand) {
        explicitEmotionHoldUntilMs_ = nowMs + 15000UL;
    }
    if (lastEmotionSwitch_.length() > 0 && lastEmotionSwitch_ != target) {
        if ((nowMs - lastPersonalityUpdateMs_) <= 3000UL) {
            if (rapidSwitchCount_ < 255U) {
                ++rapidSwitchCount_;
            }
        } else {
            rapidSwitchCount_ = 1;
        }
        if (rapidSwitchCount_ >= 3U) {
            moodModel_.noteRapidEmotionSwitch();
            rapidSwitchCount_ = 0;
        }
    }
    lastEmotionSwitch_ = target;
    lastPersonalityUpdateMs_ = nowMs;

    activeEmotion_ = normalized;
    activeAnimation_ = target;
    personalityMemory_.recordEmotion(normalized, nowMs);
    personalityMemory_.setLastKnownMood(getMood());
    moodModel_.noteTaskSuccess();
    transitionToIdlePending_ = (target != String(kDefaultAnimation));
    playing_ = true;
    microExpressionEngine_.reset();
    if (target == "curious") {
        setPersonalityState("curious");
    } else if (target == "happy") {
        setPersonalityState("excited");
    } else if (target == "sleepy") {
        setPersonalityState("tired");
    } else if (target == "surprised") {
        setPersonalityState("confused");
    } else {
        setPersonalityState("neutral");
    }
    return true;
}

bool FaceEngine::setEmotionBlend(const String& fromEmotion, const String& toEmotion, uint32_t durationMs) {
    String normalizedFrom = normalizeName(fromEmotion);
    String normalizedTo = normalizeName(toEmotion);
    if (normalizedFrom.length() == 0) {
        normalizedFrom = activeEmotion_;
    }
    if (normalizedTo.length() == 0) {
        normalizedTo = kDefaultAnimation;
    }

    if (normalizedFrom.length() == 0) {
        normalizedFrom = kDefaultAnimation;
    }
    if (normalizedTo.length() == 0) {
        normalizedTo = kDefaultAnimation;
    }

    const String fromAnimation = resolveAnimationForEmotion(normalizedFrom);
    const String toAnimation = resolveAnimationForEmotion(normalizedTo);

    const unsigned long nowMs = millis();
    registerInteraction(nowMs);
    if (!triggerPlayback(activeStyle_, fromAnimation, true)) {
        moodModel_.noteError();
        return false;
    }

    if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Crossfade) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Crossfade);
    } else if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Morph) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Morph);
    } else if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Dissolve) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Dissolve);
    } else {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Crossfade);
    }
    emotionBlendEngine_.start(fromAnimation, toAnimation, durationMs == 0 ? renderFrameDelayMs_ * 8UL : durationMs, nowMs);
    activeEmotion_ = normalizedFrom;
    activeAnimation_ = fromAnimation;
    transitionToIdlePending_ = false;
    playing_ = true;
    microExpressionEngine_.reset();
    personalityStateMachine_.noteEmotionChange(nowMs, normalizedTo);
    personalityMemory_.recordEmotion(normalizedTo, nowMs);
    explicitEmotionHoldUntilMs_ = nowMs + 15000UL;
    moodModel_.noteTaskSuccess();
    return true;
}

bool FaceEngine::isBlending() const {
    return emotionBlendEngine_.isActive(millis());
}

void FaceEngine::enableMicroExpressions(bool enabled) {
    microExpressionsEnabled_ = enabled;
    microExpressionEngine_.setEnabled(enabled);
}

void FaceEngine::setMicroExpressionIntensity(float level) {
    if (level < 0.0f) {
        level = 0.0f;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    microExpressionIntensity_ = level;
    microExpressionEngine_.setIntensity(level);
}

bool FaceEngine::setPersonalityState(const String& stateName) {
    return setPersonalityStateFromSystem(stateName);
}

bool FaceEngine::setPersonalityStateFromSystem(const String& stateName) {
    const bool ok = personalityStateMachine_.setStateFromString(stateName);
    if (ok) {
        personalityStateMachine_.enableContextRules(contextRulesEnabled_);
    }
    return ok;
}

String FaceEngine::getPersonalityState() const {
    return personalityStateMachine_.stateName();
}

void FaceEngine::enableContextRules(bool enabled) {
    contextRulesEnabled_ = enabled;
    personalityStateMachine_.enableContextRules(enabled);
}

String FaceEngine::getCurrentEmotion() const {
    return activeEmotion_;
}

bool FaceEngine::setMood(const String& mood) {
    if (!moodModel_.setMood(mood)) {
        return false;
    }
    personalityMemory_.setLastKnownMood(getMood());
    return saveMood();
}

String FaceEngine::getMood() const {
    return moodModel_.getMood();
}

void FaceEngine::adjustMood(float delta) {
    moodModel_.adjustMood(delta);
    personalityMemory_.setLastKnownMood(getMood());
}

bool FaceEngine::saveMood() {
    return moodModel_.saveMood();
}

bool FaceEngine::loadMood() {
    const bool loaded = moodModel_.loadMood();
    personalityMemory_.setLastKnownMood(getMood());
    return loaded;
}

void FaceEngine::enableMoodAdaptation(bool enabled) {
    moodAdaptationEnabled_ = enabled;
    moodModel_.enableAdaptation(enabled);
}

void FaceEngine::enableAutoEmotion(bool enabled) {
    autoEmotionEnabled_ = enabled;
}

void FaceEngine::recordEmotion(const String& emotion) {
    personalityMemory_.recordEmotion(emotion, millis());
}

std::vector<String> FaceEngine::getEmotionHistory() const {
    return personalityMemory_.getEmotionHistory();
}

void FaceEngine::clearEmotionHistory() {
    personalityMemory_.clearEmotionHistory();
    personalityMemory_.save();
}

bool FaceEngine::savePersonalityProfile() {
    Preferences preferences;
    if (!preferences.begin("flic_profile", false)) {
        return false;
    }
    JsonDocument document;
    document["baselineMood"] = personalityProfile_.baselineMood;
    document["microExpressionIntensity"] = personalityProfile_.microExpressionIntensity;
    document["transitionStyle"] = personalityProfile_.transitionStyle;
    document["interactionSensitivity"] = personalityProfile_.interactionSensitivity;
    document["moodDecayRate"] = personalityProfile_.moodDecayRate;

    String payload;
    serializeJson(document, payload);
    return preferences.putString("state", payload) > 0;
}

bool FaceEngine::loadPersonalityProfile() {
    Preferences preferences;
    if (!preferences.begin("flic_profile", false)) {
        return false;
    }

    const String payload = preferences.getString("state", "");
    if (payload.length() == 0) {
        return false;
    }
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error || !document.is<JsonObject>()) {
        return false;
    }

    if (document["baselineMood"].is<const char*>()) {
        personalityProfile_.baselineMood = String(document["baselineMood"].as<const char*>());
    }
    if (document["microExpressionIntensity"].is<float>() || document["microExpressionIntensity"].is<double>()) {
        personalityProfile_.microExpressionIntensity = document["microExpressionIntensity"].as<float>();
    }
    if (document["transitionStyle"].is<const char*>()) {
        personalityProfile_.transitionStyle = String(document["transitionStyle"].as<const char*>());
    }
    if (document["interactionSensitivity"].is<float>() || document["interactionSensitivity"].is<double>()) {
        personalityProfile_.interactionSensitivity = document["interactionSensitivity"].as<float>();
    }
    if (document["moodDecayRate"].is<float>() || document["moodDecayRate"].is<double>()) {
        personalityProfile_.moodDecayRate = document["moodDecayRate"].as<float>();
    }

    setMicroExpressionIntensity(personalityProfile_.microExpressionIntensity);
    setTransitionMode(personalityProfile_.transitionStyle);
    moodModel_.setDecayRate(personalityProfile_.moodDecayRate);
    moodModel_.setMood(personalityProfile_.baselineMood);
    return true;
}

void FaceEngine::resetPersonalityProfile() {
    personalityProfile_ = PersonalityProfileConfig{};
    moodModel_.setDecayRate(personalityProfile_.moodDecayRate);
    moodModel_.setMood(personalityProfile_.baselineMood);
    setMicroExpressionIntensity(personalityProfile_.microExpressionIntensity);
    setTransitionMode(personalityProfile_.transitionStyle);
    savePersonalityProfile();
    saveMood();
}

PersonalityProfileConfig FaceEngine::personalityProfile() const {
    return personalityProfile_;
}

void FaceEngine::updateMood(float deltaSeconds) {
    moodModel_.setDecayRate(personalityProfile_.moodDecayRate);
    moodModel_.enableAdaptation(moodAdaptationEnabled_);
    moodModel_.update(deltaSeconds);

    const unsigned long nowMs = millis();
    if ((nowMs - lastIdleMoodAdjustMs_) >= 10000UL && (nowMs - lastInteractionMs_) >= 15000UL) {
        moodModel_.noteLongIdle();
        lastIdleMoodAdjustMs_ = nowMs;
    }

    if ((nowMs - lastMoodPersistMs_) >= 15000UL) {
        saveMood();
        lastMoodPersistMs_ = nowMs;
    }
}

void FaceEngine::updateMemory(float) {
    const unsigned long nowMs = millis();
    personalityMemory_.update(nowMs, activeEmotion_);
    personalityMemory_.setLastKnownMood(getMood());
    if ((nowMs - lastMemoryPersistMs_) >= 20000UL) {
        personalityMemory_.save();
        lastMemoryPersistMs_ = nowMs;
    }
}

void FaceEngine::updateAdaptiveExpressions(float deltaSeconds) {
    adaptiveModifiers_ = adaptiveExpressionEngine_.update(getMood(), moodModel_.currentTraits(), personalityMemory_, deltaSeconds);
}

void FaceEngine::updateAutoEmotion(float) {
    if (!autoEmotionEnabled_) {
        return;
    }
    const unsigned long nowMs = millis();
    if (nowMs < explicitEmotionHoldUntilMs_ || emotionBlendEngine_.isActive(nowMs)) {
        return;
    }
    if ((nowMs - lastAutoEmotionMs_) < 7000UL) {
        return;
    }

    const String target = selectAutoEmotion(nowMs);
    if (target.length() > 0 && target != activeEmotion_) {
        setEmotionInternal(target, false);
        lastAutoEmotionMs_ = nowMs;
    }
}

void FaceEngine::drawFrame() {
    emitTelemetryIfDue(millis());
}

bool FaceEngine::setTransitionMode(const String& mode) {
    if (!transitionEngine_.setModeFromString(mode)) {
        return false;
    }
    if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Crossfade) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Crossfade);
    } else if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Morph) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Morph);
    } else if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Dissolve) {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Dissolve);
    } else {
        emotionBlendEngine_.setMode(EmotionBlendEngine::Mode::Crossfade);
    }
    renderFrameDelayMs_ = transitionEngine_.frameDelayMs();
    return true;
}

bool FaceEngine::setFrameRate(float fps) {
    transitionEngine_.setFrameRate(fps);
    renderFrameDelayMs_ = transitionEngine_.frameDelayMs();
    return true;
}

bool FaceEngine::reloadEmotionAssets() {
    return reloadActiveStyle();
}

bool FaceEngine::isEmotionAvailable(const String& emotionName) const {
    const String normalizedEmotion = normalizeName(emotionName);
    if (normalizedEmotion.length() == 0) {
        return false;
    }

    for (const FrameSequence& sequence : index_) {
        if (sequence.style == activeStyle_ && sequence.animation == normalizedEmotion && !sequence.frames.empty()) {
            return true;
        }
    }

    for (const FrameSequence& sequence : index_) {
        if (sequence.style == kDefaultStyle && sequence.animation == normalizedEmotion && !sequence.frames.empty()) {
            return true;
        }
    }

    return false;
}

bool FaceEngine::idlePulse() {
    if (!idleEnabled_) {
        return false;
    }
    return playAnimation("idle");
}

bool FaceEngine::setFrameRate(const String& animationName, float fps) {
    if (fps <= 0.0f) {
        return false;
    }

    const String normalized = normalizeName(animationName);
    const uint16_t delayMs = static_cast<uint16_t>(1000.0f / fps);
    bool changed = false;
    for (FrameSequence& sequence : index_) {
        if (sequence.animation == normalized) {
            sequence.frameDelayMs = delayMs < 25 ? 25 : delayMs;
            changed = true;
        }
    }
    return changed;
}

void FaceEngine::setGlowModulation(const FaceGlowModulation& params) {
    glowModulation_.innerGlowRadiusPx = params.innerGlowRadiusPx < 2.0f ? 2.0f : params.innerGlowRadiusPx;
    glowModulation_.outerGlowRadiusPx = params.outerGlowRadiusPx < glowModulation_.innerGlowRadiusPx
                                            ? (glowModulation_.innerGlowRadiusPx + 1.0f)
                                            : params.outerGlowRadiusPx;
    glowModulation_.gradientStart = sanitizeHexColor(params.gradientStart, "#FFFFFF");
    glowModulation_.gradientMid = sanitizeHexColor(params.gradientMid, "#AEE6FF");
    glowModulation_.gradientEnd = sanitizeHexColor(params.gradientEnd, "#C8B5FF");
    glowModulation_.minOpacity = params.minOpacity < 0.1f ? 0.1f : (params.minOpacity > 1.0f ? 1.0f : params.minOpacity);
    glowModulation_.maxOpacity = params.maxOpacity < glowModulation_.minOpacity
                                     ? glowModulation_.minOpacity
                                     : (params.maxOpacity > 1.0f ? 1.0f : params.maxOpacity);
}

void FaceEngine::applyGlowModulation(const FaceGlowModulation& params) {
    setGlowModulation(params);
}

void FaceEngine::setSpeakingAmplitude(float amplitude) {
    if (amplitude < 0.0f) {
        amplitude = 0.0f;
    }
    if (amplitude > 1.0f) {
        amplitude = 1.0f;
    }
    speakingAmplitude_ = amplitude;
}

void FaceEngine::applySettings(const FaceSettings& settings) {
    setStyle(settings.activeStyle);
    blinkSpeed_ = settings.blinkSpeed < 0.25f ? 0.25f : (settings.blinkSpeed > 4.0f ? 4.0f : settings.blinkSpeed);
    idleEnabled_ = settings.idleEnabled;
    glowIntensity_ = settings.glowIntensity < 0.0f ? 0.0f : (settings.glowIntensity > 1.0f ? 1.0f : settings.glowIntensity);
    eyeColor_ = sanitizeEyeColor(settings.eyeColor);
    personalityIntensityPreset_ = normalizeName(settings.personalityIntensity);
    if (personalityIntensityPreset_.length() == 0) {
        personalityIntensityPreset_ = "balanced";
    }
    if (personalityIntensityPreset_ == "subtle") {
        personalityExpressivenessScale_ = 0.72f;
        personalityCadenceScale_ = 1.22f;
    } else if (personalityIntensityPreset_ == "dramatic") {
        personalityExpressivenessScale_ = 1.35f;
        personalityCadenceScale_ = 0.74f;
    } else {
        personalityIntensityPreset_ = "balanced";
        personalityExpressivenessScale_ = 1.0f;
        personalityCadenceScale_ = 1.0f;
    }
    setEmotionAnimationOverridesFromJson(settings.emotionAnimationMapJson);
    aiCanModify_ = settings.aiCanModify;
    aiCanCreate_ = settings.aiCanCreate;
    if (settings.blinkSpeed < 0.85f) {
        setPersonalityState("tired");
    } else if (settings.blinkSpeed > 1.45f || settings.glowIntensity > 0.9f) {
        setPersonalityState("excited");
    } else if (settings.idleEnabled) {
        setPersonalityState("neutral");
    }
    dirty_ = true;
}

String FaceEngine::activeStyle() const {
    return activeStyle_;
}

String FaceEngine::currentAnimation() const {
    return activeAnimation_;
}

String FaceEngine::currentEmotion() const {
    return activeEmotion_;
}

std::vector<String> FaceEngine::listStyles() const {
    return styles_;
}

std::vector<String> FaceEngine::listAnimations(const String& styleName) const {
    std::vector<String> animations;
    const String normalizedStyle = normalizeName(styleName);
    for (const FrameSequence& sequence : index_) {
        if (sequence.style == normalizedStyle) {
            bool exists = false;
            for (const String& animation : animations) {
                if (animation == sequence.animation) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                animations.push_back(sequence.animation);
            }
        }
    }
    return animations;
}

String FaceEngine::stylesJson() const {
    return jsonArrayFromStrings(listStyles());
}

String FaceEngine::animationsJson(const String& styleName) const {
    return jsonArrayFromStrings(listAnimations(styleName));
}

String FaceEngine::animationsCatalogJson() const {
    String payload = "{\"ok\":true,\"current_animation\":\"";
    payload += activeAnimation_;
    payload += "\",\"current_emotion\":\"";
    payload += activeEmotion_;
    payload += "\",\"glow\":";
    payload += glowStateJson();
    payload += ",\"styles\":[";
    for (size_t styleIndex = 0; styleIndex < styles_.size(); ++styleIndex) {
        if (styleIndex > 0) {
            payload += ",";
        }

        const String& styleName = styles_[styleIndex];
        payload += "{\"name\":\"";
        payload += styleName;
        payload += "\",\"display\":\"";
        payload += styleDisplayName(styleName);
        payload += "\",\"animations\":[";

        bool first = true;
        for (const FrameSequence& sequence : index_) {
            if (sequence.style != styleName) {
                continue;
            }
            if (!first) {
                payload += ",";
            }
            first = false;

            payload += "{\"name\":\"";
            payload += sequence.animation;
            const AnimationDefinition* definition = definitionFor(sequence.animation);
            const uint16_t expectedFrames = definition != nullptr ? definition->expectedFrames : 0;
            const int missingFrames = expectedFrames > sequence.frames.size()
                                          ? static_cast<int>(expectedFrames - sequence.frames.size())
                                          : 0;
            payload += "\",\"frames\":";
            payload += String(sequence.frames.size());
            payload += ",\"expected_frames\":";
            payload += String(expectedFrames);
            payload += ",\"missing_frames\":";
            payload += String(missingFrames);
            payload += ",\"frame_time_ms\":";
            payload += String(sequence.frameDelayMs);
            payload += ",\"curve\":\"";
            payload += timingCurveName(sequence.curve);
            payload += "\",\"dynamic\":";
            payload += sequence.dynamicSpeaking ? "true" : "false";
            payload += ",\"glow_multiplier\":";
            payload += String(sequence.glowMultiplier, 2);
            payload += "}";
        }
        payload += "]}";
    }
    payload += "]}";
    return payload;
}

String FaceEngine::settingsJson() const {
    String payload = "{";
    payload += "\"active_style\":\"" + activeStyle_ + "\",";
    payload += "\"blink_speed\":" + String(blinkSpeed_, 2) + ",";
    payload += "\"idle_enabled\":" + String(idleEnabled_ ? "true" : "false") + ",";
    payload += "\"glow_intensity\":" + String(glowIntensity_, 2) + ",";
    payload += "\"eye_color\":\"" + eyeColor_ + "\",";
    payload += "\"personality_intensity\":\"" + personalityIntensityPreset_ + "\",";
    payload += "\"emotion_animation_map\":" + emotionAnimationOverridesJson() + ",";
    payload += "\"ai_can_modify\":" + String(aiCanModify_ ? "true" : "false") + ",";
    payload += "\"ai_can_create\":" + String(aiCanCreate_ ? "true" : "false") + ",";
    payload += "\"glow\":{";
    payload += "\"inner_glow_radius\":" + String(glowModulation_.innerGlowRadiusPx, 1) + ",";
    payload += "\"outer_glow_radius\":" + String(glowModulation_.outerGlowRadiusPx, 1) + ",";
    payload += "\"gradient\":[\"" + glowModulation_.gradientStart + "\",\"" + glowModulation_.gradientMid + "\",\"" + glowModulation_.gradientEnd + "\"],";
    payload += "\"opacity_min\":" + String(glowModulation_.minOpacity, 2) + ",";
    payload += "\"opacity_max\":" + String(glowModulation_.maxOpacity, 2);
    payload += "}}";
    return payload;
}

String FaceEngine::glowStateJson() const {
    String payload = "{\"inner\":";
    payload += String(glowModulation_.innerGlowRadiusPx, 1);
    payload += ",\"outer\":";
    payload += String(glowModulation_.outerGlowRadiusPx, 1);
    payload += ",\"opacity_min\":";
    payload += String(glowModulation_.minOpacity, 2);
    payload += ",\"opacity_max\":";
    payload += String(glowModulation_.maxOpacity, 2);
    payload += ",\"intensity\":";
    payload += String(glowIntensity_, 2);
    payload += "}";
    return payload;
}

String FaceEngine::telemetryJson() const {
    const unsigned long nowMs = millis();
    const unsigned long windowStartMs = telemetryWindowStartMs_ == 0 ? nowMs : telemetryWindowStartMs_;
    const unsigned long windowMs = nowMs >= windowStartMs ? (nowMs - windowStartMs) : 0;
    const float fps = windowMs > 0
                          ? (static_cast<float>(telemetryFrames_) * 1000.0f) / static_cast<float>(windowMs)
                          : 0.0f;
    const float avgDrawMs = telemetryFrames_ > 0
                                ? static_cast<float>(telemetryDrawUsTotal_) / (static_cast<float>(telemetryFrames_) * 1000.0f)
                                : 0.0f;
    const float avgBlendDrawMs = telemetryBlendFrames_ > 0
                                     ? static_cast<float>(telemetryBlendDrawUsTotal_) /
                                           (static_cast<float>(telemetryBlendFrames_) * 1000.0f)
                                     : 0.0f;

    String payload = "{\"ok\":true,\"type\":\"face_telemetry\",\"window_ms\":";
    payload += String(windowMs);
    payload += ",\"fps\":";
    payload += String(fps, 2);
    payload += ",\"frames\":";
    payload += String(telemetryFrames_);
    payload += ",\"blend_frames\":";
    payload += String(telemetryBlendFrames_);
    payload += ",\"avg_draw_ms\":";
    payload += String(avgDrawMs, 2);
    payload += ",\"avg_blend_draw_ms\":";
    payload += String(avgBlendDrawMs, 2);
    payload += ",\"over_budget_frames\":";
    payload += String(telemetryOverBudgetFrames_);
    payload += ",\"blend_fallbacks\":";
    payload += String(telemetryBlendFallbacks_);
    payload += ",\"thresholds\":{";
    payload += "\"fps_warn\":" + String(telemetryThresholds_.fpsWarn, 2);
    payload += ",\"fps_bad\":" + String(telemetryThresholds_.fpsBad, 2);
    payload += ",\"draw_warn_ms\":" + String(telemetryThresholds_.drawWarnMs, 2);
    payload += ",\"draw_bad_ms\":" + String(telemetryThresholds_.drawBadMs, 2);
    payload += ",\"blend_draw_warn_ms\":" + String(telemetryThresholds_.blendDrawWarnMs, 2);
    payload += ",\"blend_draw_bad_ms\":" + String(telemetryThresholds_.blendDrawBadMs, 2);
    payload += ",\"over_budget_warn_pct\":" + String(telemetryThresholds_.overBudgetWarnPct, 2);
    payload += ",\"over_budget_bad_pct\":" + String(telemetryThresholds_.overBudgetBadPct, 2);
    payload += ",\"fallback_warn_count\":" + String(telemetryThresholds_.fallbackWarnCount, 2);
    payload += ",\"fallback_bad_count\":" + String(telemetryThresholds_.fallbackBadCount, 2);
    payload += "}";
    payload += ",\"active_emotion\":\"";
    payload += activeEmotion_;
    payload += "\",\"personality\":\"";
    payload += personalityStateMachine_.stateName();
    payload += "\",\"mood\":\"";
    payload += getMood();
    payload += "\",\"mood_adaptation\":";
    payload += moodAdaptationEnabled_ ? "true" : "false";
    payload += ",\"auto_emotion\":";
    payload += autoEmotionEnabled_ ? "true" : "false";
    payload += ",\"is_blending\":";
    payload += isBlending() ? "true" : "false";
    payload += "}";
    return payload;
}

String FaceEngine::telemetryThresholdsJson() const {
    String payload = "{\"ok\":true,\"type\":\"face_telemetry_thresholds\",\"thresholds\":{";
    payload += "\"fps_warn\":" + String(telemetryThresholds_.fpsWarn, 2);
    payload += ",\"fps_bad\":" + String(telemetryThresholds_.fpsBad, 2);
    payload += ",\"draw_warn_ms\":" + String(telemetryThresholds_.drawWarnMs, 2);
    payload += ",\"draw_bad_ms\":" + String(telemetryThresholds_.drawBadMs, 2);
    payload += ",\"blend_draw_warn_ms\":" + String(telemetryThresholds_.blendDrawWarnMs, 2);
    payload += ",\"blend_draw_bad_ms\":" + String(telemetryThresholds_.blendDrawBadMs, 2);
    payload += ",\"over_budget_warn_pct\":" + String(telemetryThresholds_.overBudgetWarnPct, 2);
    payload += ",\"over_budget_bad_pct\":" + String(telemetryThresholds_.overBudgetBadPct, 2);
    payload += ",\"fallback_warn_count\":" + String(telemetryThresholds_.fallbackWarnCount, 2);
    payload += ",\"fallback_bad_count\":" + String(telemetryThresholds_.fallbackBadCount, 2);
    payload += "}}";
    return payload;
}

FaceTelemetryThresholds FaceEngine::telemetryThresholds() const {
    return telemetryThresholds_;
}

void FaceEngine::setTelemetryThresholds(const FaceTelemetryThresholds& thresholds) {
    telemetryThresholds_ = thresholds;
}

String FaceEngine::currentFramePath() const {
    return lastRenderedFramePath_;
}

bool FaceEngine::isActive() const {
    return playing_ || gFaceBusy;
}

bool FaceEngine::reloadActiveStyle() {
    return loadAnimationSet(activeStyle_);
}

String FaceEngine::validateAnimationSetJson() const {
#if VECTOR_ONLY_FACE
    String payload = "{\"ok\":true,\"required\":[";
    for (size_t i = 0; i < requiredAnimations_.size(); ++i) {
        if (i > 0) {
            payload += ",";
        }
        payload += "\"";
        payload += requiredAnimations_[i];
        payload += "\"";
    }
    payload += "],\"results\":[";

    bool first = true;
    for (const String& animationName : requiredAnimations_) {
        if (!first) {
            payload += ",";
        }
        first = false;

        payload += "{\"animation\":\"";
        payload += animationName;
        payload += "\",\"pass\":true,\"frame_count\":1,\"errors\":[]}";
    }
    payload += "]}";
    return payload;
#else
    String payload = "{\"ok\":true,\"required\":[";
    for (size_t i = 0; i < requiredAnimations_.size(); ++i) {
        if (i > 0) {
            payload += ",";
        }
        payload += "\"";
        payload += requiredAnimations_[i];
        payload += "\"";
    }
    payload += "],\"results\":[";

    bool first = true;
    for (const String& animationName : requiredAnimations_) {
        FrameSequence sequence = resolveSequence(kDefaultStyle, animationName);
        if (!first) {
            payload += ",";
        }
        first = false;

        bool pass = !sequence.frames.empty();
        String errors = "[";
        bool firstErr = true;
        if (sequence.frames.empty()) {
            errors += "\"missing_folder_or_frames\"";
            firstErr = false;
        }

        int expectedFrame = 0;
        for (const FrameAsset& frame : sequence.frames) {
            const String framePath = frame.path;
            if (!isFrameNameValid(framePath)) {
                if (!firstErr) {
                    errors += ",";
                }
                errors += "\"invalid_name_pattern\"";
                firstErr = false;
                pass = false;
            }

            const int current = frameNumberFromName(framePath);
            if (current != expectedFrame) {
                if (!firstErr) {
                    errors += ",";
                }
                errors += "\"missing_frame_number\"";
                firstErr = false;
                pass = false;
            }
            ++expectedFrame;

            uint32_t width = 0;
            uint32_t height = 0;
            uint8_t colorType = 0;
            if (!readPngHeader(framePath, width, height, colorType)) {
                if (!firstErr) {
                    errors += ",";
                }
                errors += "\"corrupted_png\"";
                firstErr = false;
                pass = false;
                continue;
            }
            if (width != 240 || height != 240) {
                if (!firstErr) {
                    errors += ",";
                }
                errors += "\"wrong_dimensions\"";
                firstErr = false;
                pass = false;
            }
            if (colorType != 6) {
                if (!firstErr) {
                    errors += ",";
                }
                errors += "\"not_rgba\"";
                firstErr = false;
                pass = false;
            }
        }
        errors += "]";

        payload += "{\"animation\":\"";
        payload += animationName;
        payload += "\",\"pass\":";
        payload += pass ? "true" : "false";
        payload += ",\"frame_count\":";
        payload += String(sequence.frames.size());
        payload += ",\"errors\":";
        payload += errors;
        payload += "}";
    }
    payload += "]}";
    return payload;
#endif
}

bool FaceEngine::canWriteCustomAnimation() const {
    return aiCanCreate_;
}

bool FaceEngine::canModifyExistingAnimation() const {
    return aiCanModify_;
}

bool FaceEngine::canCreateStyle() const {
    return aiCanCreate_;
}

String FaceEngine::customAnimationRoot(const String& animationName) const {
#if VECTOR_ONLY_FACE
    return "vector://" + normalizeName(animationName);
#else
    return String(kFaceRoot) + "/custom/" + normalizeName(animationName);
#endif
}

bool FaceEngine::scanIndex() {
    styles_.clear();
    index_.clear();

    styles_.push_back(kDefaultStyle);
    FrameSequence sequence;
    sequence.style = kDefaultStyle;
    sequence.animation = kDefaultAnimation;
    FrameAsset vectorFrame;
    vectorFrame.path = "vector://" + sequence.animation;
    sequence.frames.push_back(vectorFrame);
    const AnimationDefinition* definition = definitionFor(sequence.animation);
    if (definition != nullptr) {
        sequence.frameDelayMs = definition->frameTimeMs;
        sequence.curve = definition->curve;
        sequence.glowMultiplier = definition->glowMultiplier;
        sequence.glowVariationPercent = definition->glowVariationPercent;
        sequence.slowPulse = definition->slowPulse;
        sequence.dynamicSpeaking = definition->dynamicSpeaking;
        sequence.glowSpike = definition->glowSpike;
    }
    index_.push_back(sequence);
    Serial.println("[SCAN] Vector renderer active; SD animation scan disabled");
    return true;
}

bool FaceEngine::ensureFaceDirectories() {
#if VECTOR_ONLY_FACE
    Serial.println("[SCAN] Vector-only mode: directory creation skipped");
    return true;
#else
    SdManager::ensureDirectory(kFaceRoot);
    SdManager::ensureDirectory((String(kFaceRoot) + "/default").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/soft_glow").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/minimal").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/custom").c_str());

    const char* styles[] = {"default", "soft_glow", "minimal", "custom"};
    for (const char* style : styles) {
        const String styleRoot = String(kFaceRoot) + "/" + style;
        for (const char* emotion : kRequiredEmotionFolders) {
            SdManager::ensureDirectory((styleRoot + "/" + emotion).c_str());
        }
    }

    for (const char* animation : kRequiredAnimations) {
        SdManager::ensureDirectory((String(kFaceRoot) + "/default/" + animation).c_str());
    }

    if (SdManager::isMounted() && !SD.exists(kFaceMetadataPrimaryPath)) {
        File metadataFile = SD.open(kFaceMetadataPrimaryPath, FILE_WRITE);
        if (metadataFile) {
            metadataFile.print(defaultMetadataJson());
            metadataFile.close();
            Serial.printf("[SCAN] created metadata file at %s\n", kFaceMetadataPrimaryPath);
        }
    }
    return true;
#endif
}

bool FaceEngine::loadAnimationMetadata() {
#if VECTOR_ONLY_FACE
    return true;
#else
    if (!SdManager::isMounted()) {
        return false;
    }

    JsonDocument document;
    const bool loadedPrimary = SdManager::readJSON(kFaceMetadataPrimaryPath, document);
    const bool loadedSecondary = !loadedPrimary && SdManager::readJSON(kFaceMetadataSecondaryPath, document);
    if (!loadedPrimary && !loadedSecondary) {
        return false;
    }

    if (!document.is<JsonObject>()) {
        return false;
    }

    JsonObject root = document.as<JsonObject>();
    for (JsonPair kv : root) {
        const String animationName = normalizeName(String(kv.key().c_str()));
        if (animationName.length() == 0 || !kv.value().is<JsonObject>()) {
            continue;
        }

        JsonObject metadata = kv.value().as<JsonObject>();
        AnimationDefinition* target = nullptr;
        for (AnimationDefinition& definition : metadataDefinitions_) {
            if (animationName == definition.name) {
                target = &definition;
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }

        if (!metadata["frames"].isNull() && metadata["frames"].is<uint16_t>()) {
            target->expectedFrames = metadata["frames"].as<uint16_t>();
        }

        if (!metadata["frame_time_ms"].isNull()) {
            if (metadata["frame_time_ms"].is<uint16_t>()) {
                target->frameTimeMs = metadata["frame_time_ms"].as<uint16_t>();
                target->dynamicSpeaking = false;
            } else if (metadata["frame_time_ms"].is<const char*>()) {
                const String frameTime = normalizeName(String(metadata["frame_time_ms"].as<const char*>()));
                if (frameTime == "dynamic") {
                    target->dynamicSpeaking = true;
                }
            }
        }

        if (!metadata["easing"].isNull() && metadata["easing"].is<const char*>()) {
            target->curve = timingCurveFromName(String(metadata["easing"].as<const char*>()));
        }

        if (!metadata["glow_dim_percent"].isNull() &&
            (metadata["glow_dim_percent"].is<float>() || metadata["glow_dim_percent"].is<int>())) {
            const float dimPercent = metadata["glow_dim_percent"].as<float>();
            target->glowMultiplier = 1.0f - (dimPercent / 100.0f);
        }

        if (!metadata["glow_brighten_percent"].isNull() &&
            (metadata["glow_brighten_percent"].is<float>() || metadata["glow_brighten_percent"].is<int>())) {
            const float brightenPercent = metadata["glow_brighten_percent"].as<float>();
            target->glowMultiplier = 1.0f + (brightenPercent / 100.0f);
        }

        if (!metadata["glow_variation_percent"].isNull() &&
            (metadata["glow_variation_percent"].is<float>() || metadata["glow_variation_percent"].is<int>())) {
            target->glowVariationPercent = metadata["glow_variation_percent"].as<float>();
        }

        if (!metadata["glow_pulse"].isNull() && metadata["glow_pulse"].is<const char*>()) {
            const String pulse = normalizeName(String(metadata["glow_pulse"].as<const char*>()));
            target->slowPulse = pulse == "slow";
            if (pulse == "amplitude") {
                target->dynamicSpeaking = true;
            }
        }

        if (!metadata["glow_spike"].isNull() && metadata["glow_spike"].is<bool>()) {
            target->glowSpike = metadata["glow_spike"].as<bool>();
        }
    }

    return true;
#endif
}

bool FaceEngine::triggerPlayback(const String& styleName, const String& animationName, bool interruptCurrent) {
    if (gPlaybackQueue == nullptr) {
        return false;
    }

    PlaybackRequest request{};
    const String normalizedStyle = normalizeName(styleName);
    const String normalizedAnimation = normalizeName(animationName);
    snprintf(request.style, sizeof(request.style), "%s", normalizedStyle.c_str());
    snprintf(request.animation, sizeof(request.animation), "%s", normalizedAnimation.c_str());
    request.priority = animationIsHighPriority(normalizedAnimation) ? 1 : 0;
    request.queuedAtMs = millis();

    if (interruptCurrent || request.priority > 0) {
        gInterruptRequested = true;
        xQueueReset(gPlaybackQueue);
    }

    if (xQueueSend(gPlaybackQueue, &request, 0) != pdTRUE) {
        Serial.println("[PLAY] playback queue full; dropping request");
        return false;
    }
    return true;
}

FaceEngine::FrameSequence FaceEngine::resolveSequence(const String& styleName, const String& animationName) const {
    const String normalizedStyle = normalizeName(styleName);
    const String normalizedAnimation = normalizeName(animationName);

    FrameSequence sequence;
    sequence.style = normalizedStyle.length() > 0 ? normalizedStyle : String(kDefaultStyle);
    sequence.animation = normalizedAnimation.length() > 0 ? normalizedAnimation : String(kDefaultAnimation);
    FrameAsset vectorFrame;
    vectorFrame.path = "vector://" + sequence.animation;
    sequence.frames.push_back(vectorFrame);

    const AnimationDefinition* definition = definitionFor(sequence.animation);
    if (definition != nullptr) {
        sequence.frameDelayMs = definition->frameTimeMs;
        sequence.curve = definition->curve;
        sequence.glowMultiplier = definition->glowMultiplier;
        sequence.glowVariationPercent = definition->glowVariationPercent;
        sequence.slowPulse = definition->slowPulse;
        sequence.dynamicSpeaking = definition->dynamicSpeaking;
        sequence.glowSpike = definition->glowSpike;
    }

    return sequence;
}

FaceEngine::FrameSequence FaceEngine::resolveFallbackSequence(const String& animationName) const {
    const String normalizedAnimation = normalizeName(animationName);
    FrameSequence sequence;
    sequence.style = kDefaultStyle;
    sequence.animation = normalizedAnimation.length() > 0 ? normalizedAnimation : String(kDefaultAnimation);
    FrameAsset vectorFrame;
    vectorFrame.path = "vector://" + sequence.animation;
    sequence.frames.push_back(vectorFrame);

    const AnimationDefinition* definition = definitionFor(sequence.animation);
    if (definition != nullptr) {
        sequence.frameDelayMs = definition->frameTimeMs;
        sequence.curve = definition->curve;
        sequence.glowMultiplier = definition->glowMultiplier;
        sequence.glowVariationPercent = definition->glowVariationPercent;
        sequence.slowPulse = definition->slowPulse;
        sequence.dynamicSpeaking = definition->dynamicSpeaking;
        sequence.glowSpike = definition->glowSpike;
    }

    return sequence;
}

String FaceEngine::normalizeName(const String& value) const {
    String name = value;
    name.trim();
    name.toLowerCase();
    const int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) {
        name = name.substring(lastSlash + 1);
    }
    const int lastDot = name.lastIndexOf('.');
    if (lastDot > 0) {
        name = name.substring(0, lastDot);
    }
    return name;
}

String FaceEngine::sanitizeEyeColor(const String& value) const {
    return sanitizeHexColor(value, "#AEE6FF");
}

String FaceEngine::sanitizeHexColor(const String& value, const String& fallback) const {
    if (value.length() == 7 && value.charAt(0) == '#') {
        bool valid = true;
        for (int index = 1; index < 7; ++index) {
            const char c = value.charAt(index);
            const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!isHex) {
                valid = false;
                break;
            }
        }
        if (valid) {
            return value;
        }
    }
    return fallback;
}

String FaceEngine::resolveAnimationForEmotion(const String& emotionName) const {
    String normalizedEmotion = normalizeName(emotionName);
    if (normalizedEmotion.length() == 0) {
        normalizedEmotion = kDefaultAnimation;
    }

    for (const EmotionAnimationOverride& entry : emotionAnimationOverrides_) {
        if (entry.emotion == normalizedEmotion && entry.animation.length() > 0) {
            if (isEmotionAvailable(entry.animation)) {
                return entry.animation;
            }
            break;
        }
    }

    if (isEmotionAvailable(normalizedEmotion)) {
        return normalizedEmotion;
    }
    return String(kDefaultAnimation);
}

void FaceEngine::setEmotionAnimationOverridesFromJson(const String& jsonPayload) {
    emotionAnimationOverrides_.clear();

    String trimmed = jsonPayload;
    trimmed.trim();
    if (trimmed.length() == 0) {
        return;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, trimmed);
    if (error || !document.is<JsonObject>()) {
        return;
    }

    JsonObject root = document.as<JsonObject>();
    for (JsonPair kv : root) {
        const String emotion = normalizeName(String(kv.key().c_str()));
        if (emotion.length() == 0) {
            continue;
        }
        if (!kv.value().is<const char*>()) {
            continue;
        }
        const String animation = normalizeName(String(kv.value().as<const char*>()));
        if (animation.length() == 0) {
            continue;
        }
        emotionAnimationOverrides_.push_back({emotion, animation});
    }
}

String FaceEngine::emotionAnimationOverridesJson() const {
    String payload = "{";
    bool first = true;
    for (const EmotionAnimationOverride& entry : emotionAnimationOverrides_) {
        if (entry.emotion.length() == 0 || entry.animation.length() == 0) {
            continue;
        }
        if (!first) {
            payload += ",";
        }
        first = false;
        payload += "\"";
        payload += entry.emotion;
        payload += "\":\"";
        payload += entry.animation;
        payload += "\"";
    }
    payload += "}";
    return payload;
}

bool FaceEngine::animationIsHighPriority(const String& animationName) const {
    const String normalized = normalizeName(animationName);
    return normalized == "speaking" || normalized == "listening" || normalized == "surprise";
}

FaceEngine::TimingCurve FaceEngine::timingCurveFromName(const String& curveName) const {
    String normalized = curveName;
    normalized.trim();
    normalized.toLowerCase();
    normalized.replace("_", "");

    if (normalized == "easein") {
        return TimingCurve::EaseIn;
    }
    if (normalized == "easeout") {
        return TimingCurve::EaseOut;
    }
    if (normalized == "easeinout") {
        return TimingCurve::EaseInOut;
    }
    return TimingCurve::Linear;
}

float FaceEngine::evaluateCurve(TimingCurve curve, float t) const {
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }

    if (curve == TimingCurve::EaseIn) {
        return t * t;
    }
    if (curve == TimingCurve::EaseOut) {
        const float inv = 1.0f - t;
        return 1.0f - inv * inv;
    }
    if (curve == TimingCurve::EaseInOut) {
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    }
    return t;
}

uint16_t FaceEngine::frameDelayFor(const FrameSequence& sequence, size_t frameIndex, float amplitude) const {
    const size_t frameCount = sequence.frames.size();
    const float t = frameCount <= 1 ? 0.0f : static_cast<float>(frameIndex) / static_cast<float>(frameCount - 1);
    const float curveValue = evaluateCurve(sequence.curve, t);

    float delay = static_cast<float>(sequence.frameDelayMs);
    if (sequence.curve == TimingCurve::EaseIn) {
        delay *= (1.15f - 0.35f * curveValue);
    } else if (sequence.curve == TimingCurve::EaseOut) {
        delay *= (0.85f + 0.35f * (1.0f - curveValue));
    } else if (sequence.curve == TimingCurve::EaseInOut) {
        const float wave = std::fabs(0.5f - curveValue);
        delay *= (0.88f + 0.4f * wave);
    }

    if (sequence.dynamicSpeaking) {
        const float amp = amplitude < 0.0f ? 0.0f : (amplitude > 1.0f ? 1.0f : amplitude);
        delay *= (1.25f - 0.65f * amp);
    }

    if (delay < 16.0f) {
        delay = 16.0f;
    }
#if VECTOR_ONLY_FACE
    // Keep vector motion smooth regardless of legacy metadata pacing.
    if (delay > 40.0f) {
        delay = 40.0f;
    }
#endif
    delay *= transitionSpeedScaleForCurrentPersonality();
    if (delay < 16.0f) {
        delay = 16.0f;
    }
    return static_cast<uint16_t>(delay);
}

FaceEngine::FrameAsset FaceEngine::loadFrameAsset(const String& framePath) const {
    FrameAsset asset;
    asset.path = framePath;

#if VECTOR_ONLY_FACE
    return asset;
#else

    if (!SdManager::isMounted()) {
        return asset;
    }

    File file = SD.open(framePath);
    if (!file) {
        return asset;
    }

    const size_t size = file.size();
    if (size == 0 || size > kStreamFrameMaxBytes) {
        file.close();
        return asset;
    }

    if (size <= kInlineFrameMaxBytes) {
        asset.bytes.resize(size);
        const size_t bytesRead = file.read(asset.bytes.data(), size);
        if (bytesRead == size) {
            asset.cached = true;
        } else {
            asset.bytes.clear();
        }
    }

    file.close();
    return asset;
#endif
}

String FaceEngine::framePathFromEntry(const String& styleName, const String& animationName, const String& fileName) const {
#if VECTOR_ONLY_FACE
    (void)styleName;
    (void)animationName;
    return "vector://" + normalizeName(fileName);
#else
    if (fileName.startsWith("/")) {
        return fileName;
    }
    return String(kFaceRoot) + "/" + styleName + "/" + animationName + "/" + fileName;
#endif
}

String FaceEngine::styleDisplayName(const String& styleName) const {
    String display = styleName;
    display.replace("_", " ");
    return display;
}

String FaceEngine::timingCurveName(TimingCurve curve) const {
    if (curve == TimingCurve::EaseIn) {
        return "EaseIn";
    }
    if (curve == TimingCurve::EaseOut) {
        return "EaseOut";
    }
    if (curve == TimingCurve::EaseInOut) {
        return "EaseInOut";
    }
    return "Linear";
}

void FaceEngine::renderFrame(const FrameAsset& frame, int yOffsetPx, bool forceRedraw) const {
    (void)frame;
    (void)yOffsetPx;
    (void)forceRedraw;
    MicroExpressionFrame microFrame;
    const PersonalityProfile profile = personalityStateMachine_.profile();
    renderVectorFace(microFrame, profile);
}

bool FaceEngine::ensureBlendBuffers() const {
    if (blendBuffersAvailable_ && blendFrameFrom_ != nullptr && blendFrameTo_ != nullptr) {
        return true;
    }

    const size_t pixelCount = static_cast<size_t>(kExpectedFrameWidth) * static_cast<size_t>(kExpectedFrameHeight);
    const size_t bytes = pixelCount * sizeof(uint16_t);
    auto* owner = const_cast<FaceEngine*>(this);

    if (owner->blendFrameFrom_ != nullptr) {
        heap_caps_free(owner->blendFrameFrom_);
        owner->blendFrameFrom_ = nullptr;
    }
    if (owner->blendFrameTo_ != nullptr) {
        heap_caps_free(owner->blendFrameTo_);
        owner->blendFrameTo_ = nullptr;
    }

    owner->blendFrameFrom_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    owner->blendFrameTo_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (owner->blendFrameFrom_ == nullptr || owner->blendFrameTo_ == nullptr) {
        if (owner->blendFrameFrom_ != nullptr) {
            heap_caps_free(owner->blendFrameFrom_);
            owner->blendFrameFrom_ = nullptr;
        }
        if (owner->blendFrameTo_ != nullptr) {
            heap_caps_free(owner->blendFrameTo_);
            owner->blendFrameTo_ = nullptr;
        }
        owner->blendFrameFrom_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        owner->blendFrameTo_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }

    owner->blendPixelCount_ = pixelCount;
    owner->blendBuffersAvailable_ = owner->blendFrameFrom_ != nullptr && owner->blendFrameTo_ != nullptr;
    return owner->blendBuffersAvailable_;
}

bool FaceEngine::decodeFrameToBuffer(const FrameAsset& frame, uint16_t* output, size_t pixelCount) const {
    (void)frame;
    (void)output;
    (void)pixelCount;
    return true;
}

void FaceEngine::drawBlendedFrame(const FrameAsset& fromFrame,
                                  const FrameAsset& toFrame,
                                  float weight,
                                  EmotionBlendEngine::Mode mode,
                                  const String& dissolveSeed,
                                  int yOffsetPx,
                                  bool forceRedraw) const {
    (void)fromFrame;
    (void)toFrame;
    (void)weight;
    (void)mode;
    (void)dissolveSeed;
    (void)yOffsetPx;
    (void)forceRedraw;
    MicroExpressionFrame microFrame;
    const PersonalityProfile profile = personalityStateMachine_.profile();
    renderVectorFace(microFrame, profile);
}

void FaceEngine::noteFrameTelemetry(bool blended, uint32_t drawUs, uint16_t frameBudgetMs, bool overBudget) {
    (void)frameBudgetMs;
    ++telemetryFrames_;
    telemetryDrawUsTotal_ += drawUs;
    if (blended) {
        ++telemetryBlendFrames_;
        telemetryBlendDrawUsTotal_ += drawUs;
    }
    if (overBudget) {
        ++telemetryOverBudgetFrames_;
    }
}

void FaceEngine::emitTelemetryIfDue(unsigned long nowMs) {
    if (telemetryWindowStartMs_ == 0) {
        telemetryWindowStartMs_ = nowMs;
        telemetryLastLogMs_ = nowMs;
    }
    if ((nowMs - telemetryLastLogMs_) < kTelemetryLogPeriodMs) {
        return;
    }

    const unsigned long windowMs = nowMs - telemetryWindowStartMs_;
    const float fps = windowMs > 0 ? (static_cast<float>(telemetryFrames_) * 1000.0f) / static_cast<float>(windowMs) : 0.0f;
    const float avgDrawMs = telemetryFrames_ > 0
                                ? static_cast<float>(telemetryDrawUsTotal_) / (static_cast<float>(telemetryFrames_) * 1000.0f)
                                : 0.0f;
    const float avgBlendDrawMs = telemetryBlendFrames_ > 0
                                     ? static_cast<float>(telemetryBlendDrawUsTotal_) /
                                           (static_cast<float>(telemetryBlendFrames_) * 1000.0f)
                                     : 0.0f;

    Serial.printf("[FaceTelemetry] window=%lums fps=%.2f frames=%lu blend=%lu avgDraw=%.2fms avgBlend=%.2fms overBudget=%lu blendFallbacks=%lu\n",
                  static_cast<unsigned long>(windowMs),
                  fps,
                  static_cast<unsigned long>(telemetryFrames_),
                  static_cast<unsigned long>(telemetryBlendFrames_),
                  avgDrawMs,
                  avgBlendDrawMs,
                  static_cast<unsigned long>(telemetryOverBudgetFrames_),
                  static_cast<unsigned long>(telemetryBlendFallbacks_));

    telemetryWindowStartMs_ = nowMs;
    telemetryLastLogMs_ = nowMs;
    telemetryFrames_ = 0;
    telemetryBlendFrames_ = 0;
    telemetryOverBudgetFrames_ = 0;
    telemetryBlendFallbacks_ = 0;
    telemetryDrawUsTotal_ = 0;
    telemetryBlendDrawUsTotal_ = 0;
}

void FaceEngine::runTransitionTo(const FrameAsset& nextFrame) const {
    if (nextFrame.path.length() == 0 || lastRenderedFramePath_.length() == 0 ||
        transitionEngine_.mode() == FaceTransitionEngine::Mode::DirectCut) {
        return;
    }

    FrameAsset previous;
    previous.path = lastRenderedFramePath_;

    if (transitionEngine_.mode() == FaceTransitionEngine::Mode::FadeToBlack) {
        renderFrame(previous, 0, true);
        vTaskDelay(pdMS_TO_TICKS(renderFrameDelayMs_));
        renderFaceWindowBlack(140);
        vTaskDelay(pdMS_TO_TICKS(renderFrameDelayMs_));
        renderFaceWindowBlack(255);
        vTaskDelay(pdMS_TO_TICKS(renderFrameDelayMs_));
        return;
    }

    const uint32_t seed = hashString(previous.path + "|" + nextFrame.path);
    for (uint8_t step = 0; step < 4; ++step) {
        bool showNext = (step % 2) == 1;
        if (transitionEngine_.mode() == FaceTransitionEngine::Mode::Dissolve) {
            const uint32_t mixed = seed ^ static_cast<uint32_t>(step * 1103515245u);
            showNext = (mixed & 0x1u) == 1u;
        }

        if (showNext) {
            renderFrame(nextFrame, 0, true);
        } else {
            renderFrame(previous, 0, true);
        }
        vTaskDelay(pdMS_TO_TICKS(renderFrameDelayMs_));
    }
}

void FaceEngine::renderFaceWindowBlack(uint8_t level) const {
    auto& display = M5.Display;
    const int xOffsetPx = display.width() > kExpectedFrameWidth ? (display.width() - kExpectedFrameWidth) / 2 : 0;
    const int yOffsetPx = display.height() > kExpectedFrameHeight ? (display.height() - kExpectedFrameHeight) / 2 : 0;
    waitForDisplayReady(display);
    display.startWrite();
    const uint8_t channel = static_cast<uint8_t>(255 - level);
    const uint16_t color = display.color565(channel, channel, channel);
    pushSolidWindow(display, xOffsetPx, yOffsetPx, kExpectedFrameWidth, kExpectedFrameHeight, color);
    display.endWrite();
}

void FaceEngine::renderGlowOverlay(const FrameSequence& sequence, float normalizedFrame, float amplitude) const {
    return; // disable glow overlay
    auto& display = M5.Display;
    const int centerX = display.width() / 2;
    const int centerY = display.height() / 2;

    const float curveValue = evaluateCurve(sequence.curve, normalizedFrame);
    const float variationFactor = sequence.glowVariationPercent > 0.0f
                                      ? (1.0f + (sequence.glowVariationPercent / 100.0f) *
                                                     std::sin(timeAccumulator_ * 1.4f + normalizedFrame * 2.0f))
                                      : 1.0f;
    const float idleVariation = sequence.animation == "idle"
                                    ? (0.9f + 0.2f * (0.5f + 0.5f * std::sin(timeAccumulator_ * 1.4f)))
                                    : 1.0f;
    const float thinkingPulse = sequence.slowPulse
                                    ? (0.88f + 0.2f * (0.5f + 0.5f * std::sin(timeAccumulator_ * 0.9f)))
                                    : 1.0f;
    const float speakingPulse = sequence.dynamicSpeaking ? (0.8f + 0.7f * amplitude) : 1.0f;
    const float surpriseSpike = sequence.glowSpike ? (1.0f + 0.55f * (1.0f - normalizedFrame)) : 1.0f;
    const float emotionScale = activeEmotion_ == "happy"
                                   ? 1.12f
                                   : (activeEmotion_ == "sad" ? 0.82f : (activeEmotion_ == "surprised" ? 1.20f : 1.0f));

    float strength = glowIntensity_ * sequence.glowMultiplier;
    strength *= variationFactor;
    strength *= idleVariation;
    strength *= thinkingPulse;
    strength *= speakingPulse;
    strength *= surpriseSpike;
    strength *= emotionScale;
    strength *= (0.9f + 0.2f * curveValue);
    if (strength < 0.05f) {
        return;
    }
    if (strength > 1.6f) {
        strength = 1.6f;
    }

    const float opacity = glowModulation_.minOpacity + (glowModulation_.maxOpacity - glowModulation_.minOpacity) * (strength / 1.6f);
    const uint16_t startColor = scaleColor565(display.color565(255, 255, 255), opacity);
    const uint16_t midColor = scaleColor565(display.color565(174, 230, 255), opacity * 0.82f);
    const uint16_t endColor = scaleColor565(display.color565(200, 181, 255), opacity * 0.65f);

    const int innerRadius = static_cast<int>(glowModulation_.innerGlowRadiusPx);
    const int outerRadius = static_cast<int>(glowModulation_.outerGlowRadiusPx);
    display.drawCircle(centerX, centerY, outerRadius, endColor);
    display.drawCircle(centerX, centerY, static_cast<int>((innerRadius + outerRadius) * 0.5f), midColor);
    display.drawCircle(centerX, centerY, innerRadius, startColor);
}

void FaceEngine::finishPlayback() {
    if (shouldReturnToIdleAfterPlayback()) {
        transitionToIdlePending_ = false;
        if (triggerPlayback(activeStyle_, kDefaultAnimation)) {
            activeAnimation_ = kDefaultAnimation;
            activeEmotion_ = kDefaultAnimation;
            playing_ = true;
            return;
        }
    }

    const String personalityIdle = personalityStateMachine_.profile().idleAnimation;
    String loopEmotion = activeEmotion_;
    if (!isEmotionAvailable(loopEmotion)) {
        loopEmotion = isEmotionAvailable(personalityIdle) ? personalityIdle : String(kDefaultAnimation);
    }
    if (triggerPlayback(activeStyle_, loopEmotion)) {
        activeAnimation_ = loopEmotion;
        activeEmotion_ = loopEmotion;
        playing_ = true;
        return;
    }

    playing_ = false;
}

bool FaceEngine::shouldIdleTrigger() const {
    return idleEnabled_ && !playing_;
}

String FaceEngine::stylePath(const String& styleName) const {
#if VECTOR_ONLY_FACE
    return "vector://style/" + normalizeName(styleName);
#else
    return String(kFaceRoot) + "/" + normalizeName(styleName);
#endif
}

const FaceEngine::AnimationDefinition* FaceEngine::definitionFor(const String& animationName) const {
    const String normalized = normalizeName(animationName);
    for (const AnimationDefinition& definition : metadataDefinitions_) {
        if (normalized == definition.name) {
            return &definition;
        }
    }
    return nullptr;
}

bool FaceEngine::shouldReturnToIdleAfterPlayback() const {
    if (!transitionToIdlePending_) {
        return false;
    }
    if (emotionBlendEngine_.isActive(millis())) {
        return false;
    }
    return true;
}

void FaceEngine::registerInteraction(unsigned long nowMs) {
    lastInteractionMs_ = nowMs;
    personalityStateMachine_.noteInteraction(nowMs);
    personalityMemory_.noteInteraction(nowMs);
    moodModel_.noteUserInteraction();
}

void FaceEngine::updateEmotion(unsigned long nowMs) {
    if (!emotionBlendEngine_.isActive(nowMs) && emotionBlendEngine_.isActive()) {
        emotionBlendEngine_.stop();
    }
}

EmotionBlendEngine::Snapshot FaceEngine::updateBlend(unsigned long nowMs) const {
    return emotionBlendEngine_.snapshot(nowMs);
}

MicroExpressionFrame FaceEngine::updateMicroExpressions(unsigned long nowMs,
                                                         float deltaSeconds,
                                                         const EmotionBlendEngine::Snapshot& blendSnapshot,
                                                         const String& emotionName,
                                                         const PersonalityProfile& personalityProfile) {
    microExpressionEngine_.setEnabled(microExpressionsEnabled_);
    microExpressionEngine_.setIntensity(microExpressionIntensity_ * personalityProfile.microExpressionScale *
                                        adaptiveModifiers_.microExpressionScale);
    const String blendEmotion = blendSnapshot.active ? blendSnapshot.toEmotion : emotionName;
    MicroExpressionFrame frame = microExpressionEngine_.update(nowMs, deltaSeconds, blendEmotion, personalityProfile);
    frame.eyeJitterX = static_cast<int8_t>(frame.eyeJitterX * adaptiveModifiers_.jitterScale);
    frame.eyeJitterY = static_cast<int8_t>(frame.eyeJitterY * adaptiveModifiers_.jitterScale);
    frame.eyeJitterX = static_cast<int8_t>(frame.eyeJitterX + static_cast<int8_t>(eyeDriftX_ * 0.35f));
    frame.eyeJitterY = static_cast<int8_t>(frame.eyeJitterY + static_cast<int8_t>(eyeDriftY_ * 0.35f));
    frame.driftX = static_cast<int8_t>(frame.driftX + static_cast<int8_t>(eyeDriftX_ * 0.5f));
    frame.driftY = static_cast<int8_t>(frame.driftY + static_cast<int8_t>(eyeDriftY_ * 0.5f));
    if (blinkActive_ || sleepyHalfBlinkActive_) {
        frame.blink = true;
    }
    if (eyebrowFlickActive_) {
        frame.eyelidTwitch = true;
    }
    frame.pupilScale *= adaptiveModifiers_.pupilScale;
    return frame;
}

FaceEngine::EmotionParameters FaceEngine::emotionParametersFor(const String& emotionName) const {
    struct EmotionEntry {
        const char* emotion;
        EmotionParameters parameters;
    };

    const auto makeParams = [](float eyebrowAngle,
                               float eyebrowHeight,
                               float eyeSquish,
                               float pupilJitter,
                               float mouthCurve,
                               float mouthOpen,
                               float glowIntensity,
                               float microExpressionBias) {
        EmotionParameters params;
        params.eyebrowAngle = eyebrowAngle;
        params.eyebrowHeight = eyebrowHeight;
        params.eyeSquish = eyeSquish;
        params.pupilJitter = pupilJitter;
        params.mouthCurve = mouthCurve;
        params.mouthOpen = mouthOpen;
        params.glowIntensity = glowIntensity;
        params.microExpressionBias = microExpressionBias;
        return params;
    };

    static const EmotionEntry kEmotionTable[] = {
        {"neutral", makeParams(0.0f, 0.0f, 1.0f, 0.45f, 0.0f, 0.05f, 0.35f, 0.45f)},
        {"happy", makeParams(6.0f, 2.0f, 1.0f, 0.55f, 0.9f, 0.35f, 0.60f, 0.70f)},
        {"curious", makeParams(8.0f, 4.0f, 0.95f, 0.80f, 0.20f, 0.18f, 0.62f, 0.82f)},
        {"confused", makeParams(-4.0f, 3.0f, 0.92f, 0.65f, -0.20f, 0.25f, 0.48f, 0.72f)},
        {"excited", makeParams(10.0f, 6.0f, 0.90f, 0.90f, 0.85f, 0.45f, 0.85f, 0.92f)},
        {"sleepy", makeParams(-8.0f, -3.0f, 0.68f, 0.25f, -0.45f, 0.05f, 0.24f, 0.22f)},
        {"mischievous", makeParams(7.0f, 3.0f, 0.95f, 0.88f, 0.50f, 0.22f, 0.78f, 0.88f)},
        {"focused", makeParams(4.0f, -2.0f, 0.88f, 0.35f, -0.08f, 0.10f, 0.42f, 0.36f)},
    };

    const String normalized = normalizeName(emotionName);
    for (size_t index = 0; index < (sizeof(kEmotionTable) / sizeof(kEmotionTable[0])); ++index) {
        if (normalized == kEmotionTable[index].emotion) {
            return kEmotionTable[index].parameters;
        }
    }
    return kEmotionTable[0].parameters;
}

void FaceEngine::updatePersonality(float deltaSeconds) {
    if (deltaSeconds < 0.001f) {
        deltaSeconds = 0.001f;
    }

    const unsigned long nowMs = millis();
    String targetEmotion = normalizeName(activeEmotion_);
    if (targetEmotion.length() == 0 || targetEmotion == "idle" || targetEmotion == "calm") {
        targetEmotion = "neutral";
    }
    if (targetEmotion == "surprised") {
        targetEmotion = "confused";
    }
    if (targetEmotion == "thinking") {
        targetEmotion = "focused";
    }

    if (proceduralCurrentEmotion_.length() == 0) {
        proceduralCurrentEmotion_ = "neutral";
    }
    if (proceduralTargetEmotion_.length() == 0) {
        proceduralTargetEmotion_ = proceduralCurrentEmotion_;
    }

    if (targetEmotion != proceduralTargetEmotion_) {
        if (proceduralEmotionBlend_ >= 1.0f) {
            proceduralCurrentEmotion_ = proceduralTargetEmotion_;
        }
        proceduralTargetEmotion_ = targetEmotion;
        proceduralEmotionBlend_ = 0.0f;
    }

    const float expressiveBlendSpeed = 2.6f * personalityExpressivenessScale_;
    proceduralEmotionBlend_ += deltaSeconds * expressiveBlendSpeed;
    if (proceduralEmotionBlend_ > 1.0f) {
        proceduralEmotionBlend_ = 1.0f;
        proceduralCurrentEmotion_ = proceduralTargetEmotion_;
    }

    const String mood = normalizeName(getMood());
    if (mood == "curious" || mood == "happy") {
        curiosityMode_ = CuriosityMode::Active;
    } else {
        curiosityMode_ = CuriosityMode::Soft;
    }

    if (nowMs > mischiefCooldownUntilMs_ && !mood.equals("tired") && !mood.equals("sleepy")) {
        const bool triggerEvent = random(0, 1000) < 2;
        if (triggerEvent) {
            curiosityMode_ = CuriosityMode::Mischievous;
            proceduralTargetEmotion_ = "mischievous";
            proceduralEmotionBlend_ = 0.0f;
            mischiefCooldownUntilMs_ = nowMs + 38000UL;
            glowPulseActive_ = true;
            glowPulseEndMs_ = nowMs + 600UL;
            cuteExpressionActive_ = true;
            mouthTwitchActive_ = true;
            mouthTwitchEndMs_ = nowMs + 140UL;
        }
    }

    if (nowMs >= nextIdleLookMs_) {
        eyeDriftTargetX_ = static_cast<float>(random(-8, 9));
        eyeDriftTargetY_ = static_cast<float>(random(-5, 6));
        nextIdleLookMs_ = nowMs + static_cast<unsigned long>(1200 + random(0, 2400));

        if (targetEmotion == "curious" || curiosityMode_ != CuriosityMode::Soft) {
            eyebrowFlickActive_ = true;
            eyebrowFlickEndMs_ = nowMs + static_cast<unsigned long>(100 + random(0, 51));
        }
    }

    const float driftEase = 3.2f * deltaSeconds;
    eyeDriftX_ += (eyeDriftTargetX_ - eyeDriftX_) * driftEase;
    eyeDriftY_ += (eyeDriftTargetY_ - eyeDriftY_) * driftEase;

    const float microBias = emotionParametersFor(proceduralTargetEmotion_).microExpressionBias;
    const float moodScale = (mood == "sleepy" || mood == "tired") ? 1.35f : ((mood == "happy" || mood == "curious") ? 0.75f : 1.0f);
    const unsigned long cadenceMs = static_cast<unsigned long>((700.0f + (1.0f - microBias) * 900.0f) * moodScale * personalityCadenceScale_);
    if (nowMs >= nextMicroEventMs_) {
        const int roll = random(0, 100);
        if (roll < 32) {
            blinkActive_ = true;
            fastBlinkActive_ = random(0, 100) < 40;
            blinkEndMs_ = nowMs + static_cast<unsigned long>(fastBlinkActive_ ? 80 : (140 + random(0, 90)));
            doubleBlinkPending_ = random(0, 100) < 24;
        } else if (roll < 50) {
            eyebrowFlickActive_ = true;
            eyebrowFlickEndMs_ = nowMs + static_cast<unsigned long>(100 + random(0, 51));
        } else if (roll < 64) {
            mouthTwitchActive_ = true;
            mouthTwitchEndMs_ = nowMs + static_cast<unsigned long>(90 + random(0, 71));
            mouthTwitchOffset_ = static_cast<float>(random(-18, 19)) / 100.0f;
        } else if (roll < 76) {
            headTiltActive_ = true;
            headTiltOffset_ = static_cast<float>(random(-10, 11)) / 10.0f;
            headTiltEndMs_ = nowMs + static_cast<unsigned long>(220 + random(0, 220));
        } else if (roll < 88) {
            glowPulseActive_ = true;
            glowPulseBoost_ = 0.28f + static_cast<float>(random(0, 28)) / 100.0f;
            glowPulseEndMs_ = nowMs + static_cast<unsigned long>(180 + random(0, 240));
        } else {
            cuteExpressionActive_ = true;
            sleepyHalfBlinkActive_ = (targetEmotion == "sleepy") || (mood == "sleepy" && random(0, 100) < 70);
            if (sleepyHalfBlinkActive_) {
                sleepyHalfBlinkEndMs_ = nowMs + static_cast<unsigned long>(220 + random(0, 210));
            }
        }
        nextMicroEventMs_ = nowMs + cadenceMs;
    }

    if (blinkActive_ && nowMs >= blinkEndMs_) {
        blinkActive_ = false;
        if (doubleBlinkPending_) {
            blinkActive_ = true;
            fastBlinkActive_ = true;
            blinkEndMs_ = nowMs + 95UL;
            doubleBlinkPending_ = false;
        }
    }

    if (eyebrowFlickActive_ && nowMs >= eyebrowFlickEndMs_) {
        eyebrowFlickActive_ = false;
    }
    if (mouthTwitchActive_ && nowMs >= mouthTwitchEndMs_) {
        mouthTwitchActive_ = false;
        mouthTwitchOffset_ *= 0.5f;
    }
    if (headTiltActive_ && nowMs >= headTiltEndMs_) {
        headTiltActive_ = false;
        headTiltOffset_ *= 0.65f;
    }
    if (glowPulseActive_ && nowMs >= glowPulseEndMs_) {
        glowPulseActive_ = false;
        glowPulseBoost_ *= 0.6f;
    }
    if (sleepyHalfBlinkActive_ && nowMs >= sleepyHalfBlinkEndMs_) {
        sleepyHalfBlinkActive_ = false;
    }

    if (curiosityMode_ != CuriosityMode::Mischievous) {
        cuteExpressionActive_ = (nowMs % 18000UL) < 400UL;
    }
}

void FaceEngine::applyPersonalityToFace(FaceState& state) const {
    const float expressiveScale = 1.28f * personalityExpressivenessScale_;
    state.eyebrowAngle *= expressiveScale;
    state.eyebrowHeight *= expressiveScale;
    state.mouthCurve *= expressiveScale;
    state.pupilJitter *= expressiveScale;
    state.microExpressionBias *= expressiveScale;

    if (curiosityMode_ == CuriosityMode::Active) {
        state.pupilJitter += 0.28f;
        state.eyebrowHeight += 0.9f;
        state.glowIntensity += 0.10f;
    } else if (curiosityMode_ == CuriosityMode::Mischievous) {
        state.mouthCurve += 0.35f;
        state.eyebrowAngle += 4.0f;
        state.glowIntensity += 0.22f;
    }

    if (cuteExpressionActive_) {
        state.mouthOpen += 0.08f;
        state.mouthCurve += 0.18f;
    }

    if (eyebrowFlickActive_) {
        state.eyebrowHeight += 2.5f;
    }
    if (mouthTwitchActive_) {
        state.mouthCurve += mouthTwitchOffset_;
    }
    if (headTiltActive_) {
        state.headTilt += headTiltOffset_;
    }
    if (glowPulseActive_) {
        state.glowIntensity += glowPulseBoost_;
    }
    if (sleepyHalfBlinkActive_) {
        state.halfBlink = true;
        if (state.blinkClosure < 0.55f) {
            state.blinkClosure = 0.55f;
        }
    }

    if (state.glowIntensity < 0.1f) {
        state.glowIntensity = 0.1f;
    }
    if (state.glowIntensity > 1.35f) {
        state.glowIntensity = 1.35f;
    }
}

FaceEngine::FaceState FaceEngine::composeProceduralFaceState(const MicroExpressionFrame& microFrame,
                                                             const PersonalityProfile& personalityProfile) const {
    const EmotionParameters from = emotionParametersFor(proceduralCurrentEmotion_);
    const EmotionParameters to = emotionParametersFor(proceduralTargetEmotion_);
    const float t = proceduralEmotionBlend_ < 0.0f ? 0.0f : (proceduralEmotionBlend_ > 1.0f ? 1.0f : proceduralEmotionBlend_);

    auto blend = [t](float a, float b) {
        return a + (b - a) * t;
    };

    FaceState state;
    state.eyebrowAngle = blend(from.eyebrowAngle, to.eyebrowAngle);
    state.eyebrowHeight = blend(from.eyebrowHeight, to.eyebrowHeight);
    state.eyeSquish = blend(from.eyeSquish, to.eyeSquish);
    state.pupilJitter = blend(from.pupilJitter, to.pupilJitter);
    state.mouthCurve = blend(from.mouthCurve, to.mouthCurve);
    state.mouthOpen = blend(from.mouthOpen, to.mouthOpen);
    state.glowIntensity = blend(from.glowIntensity, to.glowIntensity) * (0.85f + personalityProfile.microExpressionScale * 0.35f);
    state.microExpressionBias = blend(from.microExpressionBias, to.microExpressionBias);
    state.headTilt = 0.0f;
    state.blinkClosure = blinkActive_ ? 1.0f : 0.0f;
    state.eyeDriftX = static_cast<int>(eyeDriftX_ + microFrame.driftX);
    state.eyeDriftY = static_cast<int>(eyeDriftY_ + microFrame.driftY);
    state.halfBlink = sleepyHalfBlinkActive_;
    applyPersonalityToFace(state);
    return state;
}

void FaceEngine::updatePersonality(unsigned long nowMs, const EmotionBlendEngine::Snapshot& blendSnapshot, const String& emotionName) {
    personalityStateMachine_.enableContextRules(contextRulesEnabled_);
    personalityStateMachine_.update(nowMs, emotionName, blendSnapshot.active);

    if (blendSnapshot.active) {
        return;
    }

    const String mood = getMood();
    if (mood == "tired") {
        setPersonalityStateFromSystem("tired");
    } else if (mood == "curious") {
        setPersonalityStateFromSystem("curious");
    } else if (mood == "stressed") {
        setPersonalityStateFromSystem("confused");
    } else if (mood == "happy") {
        setPersonalityStateFromSystem("excited");
    }
}

String FaceEngine::renderEmotionForBlend(unsigned long nowMs,
                                         uint32_t frameIndex,
                                         const EmotionBlendEngine::Snapshot& blendSnapshot) const {
    if (blendSnapshot.active) {
        return emotionBlendEngine_.selectEmotion(nowMs, frameIndex);
    }
    return resolveAnimationForEmotion(activeEmotion_);
}

size_t FaceEngine::renderFrameIndexFor(const FrameSequence& sequence,
                                       unsigned long nowMs,
                                       uint32_t frameIndex,
                                       const EmotionBlendEngine::Snapshot& blendSnapshot) const {
    (void)nowMs;
    if (sequence.frames.empty()) {
        return 0;
    }

    const size_t frameCount = sequence.frames.size();
    size_t safeIndex = static_cast<size_t>(frameIndex % frameCount);

    if (blendSnapshot.active && blendSnapshot.weight > 0.0f) {
        const size_t blendOffset = static_cast<size_t>(blendSnapshot.weight * static_cast<float>(frameCount - 1));
        safeIndex = (safeIndex + blendOffset) % frameCount;
    }

    return safeIndex;
}

void FaceEngine::drawFrame(const FrameSequence& sequence,
                           size_t frameIndex,
                           const MicroExpressionFrame& microFrame,
                           const PersonalityProfile& personalityProfile,
                           const EmotionBlendEngine::Snapshot& blendSnapshot,
                           int yOffsetPx,
                           bool forceRedraw,
                           const FrameSequence* blendSourceSequence,
                           size_t blendSourceFrameIndex) const {
    if (sequence.frames.empty()) {
        return;
    }

    (void)sequence;
    (void)frameIndex;
    (void)blendSnapshot;
    (void)yOffsetPx;
    (void)forceRedraw;
    (void)blendSourceSequence;
    (void)blendSourceFrameIndex;
    renderVectorFace(microFrame, personalityProfile);
}

void FaceEngine::renderVectorFace(const MicroExpressionFrame& microFrame, const PersonalityProfile&) const {
    auto& display = M5.Display;
    constexpr bool kUseFrameSprite = true;
    const int centerX = display.width() / 2;
    const int centerY = display.height() / 2;

    const String emotion = normalizeName(activeEmotion_);
    const uint16_t bgColor = display.color565(0, 0, 0);
    const uint16_t eyeColor = display.color565(245, 245, 245);
    const uint16_t accentColor = display.color565(200, 220, 255);
    const uint16_t glowWhite = display.color565(255, 255, 255);
    const uint16_t glowCyan = display.color565(120, 240, 255);

    const PersonalityProfile profile = personalityStateMachine_.profile();
    const FaceState state = composeProceduralFaceState(microFrame, profile);

    const int eyeOffsetX = 42 + static_cast<int>(state.headTilt * 0.3f);
    const int eyeY = centerY - 24 + state.eyeDriftY;
    const int eyeRadiusX = 12;
    int eyeRadiusY = static_cast<int>(12.0f * state.eyeSquish);
    if (eyeRadiusY < 5) {
        eyeRadiusY = 5;
    }
    const int jitterX = microFrame.eyeJitterX + static_cast<int>(state.pupilJitter * std::sin(timeAccumulator_ * 2.2f));
    const int jitterY = microFrame.eyeJitterY + static_cast<int>(state.pupilJitter * std::cos(timeAccumulator_ * 1.8f));
    const int faceBoxW = 224;
    const int faceBoxH = 172;
    const int faceBoxX = centerX - (faceBoxW / 2);
    const int faceBoxY = centerY - 96;
    const int browY = eyeY - 21 - static_cast<int>(state.eyebrowHeight);
    const int browTilt = static_cast<int>(state.eyebrowAngle);
    const int mouthY = centerY + 34 + static_cast<int>(state.headTilt * 0.2f);
    const bool eyesClosed = microFrame.blink || state.blinkClosure >= 0.85f;
    const bool eyesHalfClosed = state.halfBlink;

    static LGFX_Sprite frameSprite(&display);
    static bool spriteInitialized = false;
    static bool spritePrimed = false;
    static int spriteWidth = 0;
    static int spriteHeight = 0;

    const int displayWidth = display.width();
    const int displayHeight = display.height();
    const bool spriteSizeChanged = spriteWidth != displayWidth || spriteHeight != displayHeight;
    if (spriteSizeChanged) {
        if (spriteInitialized) {
            frameSprite.deleteSprite();
            spriteInitialized = false;
        }
        frameSprite.setColorDepth(16);
        frameSprite.setPsram(true);
        spriteInitialized = frameSprite.createSprite(displayWidth, displayHeight) != nullptr;
        if (spriteInitialized) {
            frameSprite.fillSprite(TFT_BLACK);
            spritePrimed = true;
        } else {
            spritePrimed = false;
        }
        spriteWidth = displayWidth;
        spriteHeight = displayHeight;
    }

    if (kUseFrameSprite && spriteInitialized) {
        if (!spritePrimed) {
            frameSprite.fillSprite(TFT_BLACK);
            spritePrimed = true;
        }
        frameSprite.fillSprite(TFT_BLACK);
        frameSprite.fillRect(faceBoxX, faceBoxY, faceBoxW, faceBoxH, bgColor);

        if (eyesClosed) {
            frameSprite.drawFastHLine(centerX - eyeOffsetX - eyeRadiusX, eyeY, eyeRadiusX * 2, eyeColor);
            frameSprite.drawFastHLine(centerX + eyeOffsetX - eyeRadiusX, eyeY, eyeRadiusX * 2, eyeColor);
        } else if (eyesHalfClosed) {
            frameSprite.fillRoundRect(centerX - eyeOffsetX - eyeRadiusX, eyeY - 2, eyeRadiusX * 2, 5, 2, eyeColor);
            frameSprite.fillRoundRect(centerX + eyeOffsetX - eyeRadiusX, eyeY - 2, eyeRadiusX * 2, 5, 2, eyeColor);
        } else {
            frameSprite.fillRoundRect(centerX - eyeOffsetX - eyeRadiusX + jitterX,
                                      eyeY - eyeRadiusY + jitterY,
                                      eyeRadiusX * 2,
                                      eyeRadiusY * 2,
                                      eyeRadiusY,
                                      eyeColor);
            frameSprite.fillRoundRect(centerX + eyeOffsetX - eyeRadiusX - jitterX,
                                      eyeY - eyeRadiusY - jitterY,
                                      eyeRadiusX * 2,
                                      eyeRadiusY * 2,
                                      eyeRadiusY,
                                      eyeColor);
        }

        frameSprite.drawLine(centerX - eyeOffsetX - 14, browY + browTilt,
                             centerX - eyeOffsetX + 14, browY - browTilt, accentColor);
        frameSprite.drawLine(centerX + eyeOffsetX - 14, browY - browTilt,
                             centerX + eyeOffsetX + 14, browY + browTilt, accentColor);

        if (state.mouthOpen > 0.35f) {
            const int openR = 7 + static_cast<int>(state.mouthOpen * 8.0f);
            frameSprite.drawCircle(centerX, mouthY, openR, accentColor);
        } else if (state.mouthCurve > 0.22f || emotion == "happy") {
            frameSprite.drawCircle(centerX, mouthY, 12, accentColor);
            frameSprite.fillRect(centerX - 14, mouthY - 12, 28, 12, bgColor);
        } else if (state.mouthCurve < -0.18f || emotion == "sleepy" || emotion == "confused") {
            frameSprite.drawCircle(centerX, mouthY + 12, 10, accentColor);
            frameSprite.fillRect(centerX - 12, mouthY + 12, 24, 12, bgColor);
        } else {
            frameSprite.drawFastHLine(centerX - 18, mouthY, 36, accentColor);
        }

        if (microFrame.eyelidTwitch) {
            frameSprite.drawPixel(centerX - eyeOffsetX + 16, eyeY - 6, accentColor);
            frameSprite.drawPixel(centerX + eyeOffsetX - 16, eyeY - 6, accentColor);
        }

        waitForDisplayReady(display);
        display.startWrite();
        frameSprite.pushSprite(0, 0);
        display.endWrite();
        return;
    }

    waitForDisplayReady(display);
    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.fillRect(faceBoxX, faceBoxY, faceBoxW, faceBoxH, bgColor);

    if (eyesClosed) {
        display.drawFastHLine(centerX - eyeOffsetX - eyeRadiusX, eyeY, eyeRadiusX * 2, eyeColor);
        display.drawFastHLine(centerX + eyeOffsetX - eyeRadiusX, eyeY, eyeRadiusX * 2, eyeColor);
    } else if (eyesHalfClosed) {
        display.fillRoundRect(centerX - eyeOffsetX - eyeRadiusX, eyeY - 2, eyeRadiusX * 2, 5, 2, eyeColor);
        display.fillRoundRect(centerX + eyeOffsetX - eyeRadiusX, eyeY - 2, eyeRadiusX * 2, 5, 2, eyeColor);
    } else {
        display.fillRoundRect(centerX - eyeOffsetX - eyeRadiusX + jitterX,
                              eyeY - eyeRadiusY + jitterY,
                              eyeRadiusX * 2,
                              eyeRadiusY * 2,
                              eyeRadiusY,
                              eyeColor);
        display.fillRoundRect(centerX + eyeOffsetX - eyeRadiusX - jitterX,
                              eyeY - eyeRadiusY - jitterY,
                              eyeRadiusX * 2,
                              eyeRadiusY * 2,
                              eyeRadiusY,
                              eyeColor);
    }

    display.drawLine(centerX - eyeOffsetX - 14, browY + browTilt,
                     centerX - eyeOffsetX + 14, browY - browTilt, accentColor);
    display.drawLine(centerX + eyeOffsetX - 14, browY - browTilt,
                     centerX + eyeOffsetX + 14, browY + browTilt, accentColor);

    if (state.mouthOpen > 0.35f) {
        const int openR = 7 + static_cast<int>(state.mouthOpen * 8.0f);
        display.drawCircle(centerX, mouthY, openR, accentColor);
    } else if (state.mouthCurve > 0.22f || emotion == "happy") {
        display.drawCircle(centerX, mouthY, 12, accentColor);
        display.fillRect(centerX - 14, mouthY - 12, 28, 12, bgColor);
    } else if (state.mouthCurve < -0.18f || emotion == "sleepy" || emotion == "confused") {
        display.drawCircle(centerX, mouthY + 12, 10, accentColor);
        display.fillRect(centerX - 12, mouthY + 12, 24, 12, bgColor);
    } else {
        display.drawFastHLine(centerX - 18, mouthY, 36, accentColor);
    }

    if (microFrame.eyelidTwitch) {
        display.drawPixel(centerX - eyeOffsetX + 16, eyeY - 6, accentColor);
        display.drawPixel(centerX + eyeOffsetX - 16, eyeY - 6, accentColor);
    }
    display.endWrite();
}

void FaceEngine::renderMicroExpressions(const MicroExpressionFrame& microFrame,
                                        const PersonalityProfile& personalityProfile,
                                        const EmotionBlendEngine::Snapshot&) const {
    return; // disable microexpressions
    if (!microExpressionsEnabled_) {
        return;
    }

    auto& display = M5.Display;
    const int centerX = display.width() / 2 + microFrame.driftX;
    const int centerY = display.height() / 2 + microFrame.driftY;
    const int eyeOffset = static_cast<int>(20 + (personalityProfile.pupilScale * 4.0f));
    const int eyeRadius = static_cast<int>(6 + personalityProfile.pupilScale * 2.0f);
    const uint16_t eyeColor = display.color565(235, 245, 255);
    const uint16_t blinkColor = display.color565(0, 0, 0);

    waitForDisplayReady(display);
    display.startWrite();
    if (microFrame.blink) {
        display.drawFastHLine(centerX - eyeOffset - eyeRadius, centerY - 18, eyeRadius * 2, blinkColor);
        display.drawFastHLine(centerX + eyeOffset - eyeRadius, centerY - 18, eyeRadius * 2, blinkColor);
    } else {
        const int pupilShiftX = microFrame.eyeJitterX;
        const int pupilShiftY = microFrame.eyeJitterY;
        display.fillCircle(centerX - eyeOffset + pupilShiftX, centerY - 18 + pupilShiftY, eyeRadius, eyeColor);
        display.fillCircle(centerX + eyeOffset - pupilShiftX, centerY - 18 - pupilShiftY, eyeRadius, eyeColor);
    }

    if (microFrame.eyelidTwitch) {
        display.drawLine(centerX - eyeOffset - 4, centerY - 22, centerX - eyeOffset + 4, centerY - 20, blinkColor);
        display.drawLine(centerX + eyeOffset - 4, centerY - 22, centerX + eyeOffset + 4, centerY - 20, blinkColor);
    }
    display.endWrite();
}

int FaceEngine::renderYOffsetFor(const MicroExpressionFrame& microFrame, const PersonalityProfile& personalityProfile) const {
    int yOffset = microFrame.driftY;
    if (personalityProfile.idleAnimation == "sleepy_fade") {
        yOffset += 1;
    } else if (personalityProfile.idleAnimation == "happy_wiggle") {
        yOffset -= 1;
    }
    yOffset += adaptiveModifiers_.yOffsetBias;
    return yOffset;
}

float FaceEngine::transitionSpeedScaleForCurrentPersonality() const {
    const PersonalityProfile& profile = personalityStateMachine_.profile();
    float value = profile.transitionSpeedScale * adaptiveModifiers_.transitionSpeedScale;
    return value < 0.5f ? 0.5f : (value > 1.5f ? 1.5f : value);
}

String FaceEngine::selectAutoEmotion(unsigned long nowMs) const {
    (void)nowMs;
    if (!(activeEmotion_ == "idle" || activeEmotion_ == "calm" || activeEmotion_ == "neutral" || activeEmotion_ == "sleepy")) {
        return String();
    }

    const String mood = getMood();
    if (mood == "curious") {
        return "listening";
    }
    if (mood == "tired") {
        return "sleepy";
    }
    if (mood == "stressed") {
        return (personalityMemory_.transitionVolatility() > 0.35f) ? "thinking" : "surprised";
    }
    if (mood == "happy") {
        return "happy";
    }
    return String();
}

String FaceEngine::profileStoragePath() const {
    return String(kPersonalityProfilePath);
}

}  // namespace Flic
