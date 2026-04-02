#include "face_engine.h"

#include "face_settings_manager.h"
#include "../subsystems/sd_manager.h"

#include <M5Unified.h>
#include <SD.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>

const Flic::FaceEngine::AnimationDefinition kPrimeDefinitions[] = {
    {"blink", 6, 80, Flic::FaceEngine::TimingCurve::EaseInOut, 0.80f, 0.0f, false, false, false},
    {"idle", 12, 150, Flic::FaceEngine::TimingCurve::Linear, 1.00f, 10.0f, false, false, false},
    {"listening", 8, 120, Flic::FaceEngine::TimingCurve::EaseOut, 1.25f, 0.0f, false, false, false},
    {"thinking", 10, 140, Flic::FaceEngine::TimingCurve::EaseIn, 1.05f, 0.0f, true, false, false},
    {"speaking", 12, 120, Flic::FaceEngine::TimingCurve::Linear, 1.00f, 0.0f, false, true, false},
    {"happy", 10, 110, Flic::FaceEngine::TimingCurve::EaseOut, 1.35f, 0.0f, false, false, false},
    {"sad", 10, 160, Flic::FaceEngine::TimingCurve::EaseIn, 0.70f, 0.0f, false, false, false},
    {"surprise", 8, 90, Flic::FaceEngine::TimingCurve::EaseOut, 1.45f, 0.0f, false, false, true},
};

namespace Flic {
namespace {
constexpr const char* kFaceRoot = "/Flic/animations/face";
constexpr const char* kFaceMetadataPrimaryPath = "/Flic/animations/face/animation_metadata.json";
constexpr const char* kFaceMetadataSecondaryPath = "/src/face/animation_metadata.json";
constexpr const char* kDefaultStyle = "default";
constexpr const char* kDefaultAnimation = "idle";
constexpr const char* kRequiredAnimations[] = {
    "blink", "idle", "listening", "thinking", "speaking", "happy", "sad", "surprise"
};
constexpr uint32_t kRenderTaskStackWords = 8192;
constexpr uint32_t kRenderTaskPriority = 2;
constexpr uint32_t kPlaybackQueueLength = 6;
constexpr uint32_t kIdlePulsePeriodMs = 9000;
constexpr uint32_t kBlinkPeriodMs = 3600;
constexpr size_t kInlineFrameMaxBytes = 48 * 1024;
constexpr size_t kStreamFrameMaxBytes = 2 * 1024 * 1024;

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

bool endsWithPng(const String& value) {
    return value.endsWith(".png");
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
           "\"blink\":{\"frames\":6,\"frame_time_ms\":80,\"easing\":\"EaseInOut\",\"glow_dim_percent\":20}," +
           "\"idle\":{\"frames\":12,\"frame_time_ms\":150,\"easing\":\"Linear\",\"glow_variation_percent\":10}," +
           "\"listening\":{\"frames\":8,\"frame_time_ms\":120,\"easing\":\"EaseOut\",\"glow_brighten_percent\":25}," +
           "\"thinking\":{\"frames\":10,\"frame_time_ms\":140,\"easing\":\"EaseIn\",\"glow_pulse\":\"slow\"}," +
           "\"speaking\":{\"frames\":12,\"frame_time_ms\":\"dynamic\",\"easing\":\"Linear\",\"glow_pulse\":\"amplitude\"}," +
           "\"happy\":{\"frames\":10,\"frame_time_ms\":110,\"easing\":\"EaseOut\",\"glow_brighten_percent\":35}," +
           "\"sad\":{\"frames\":10,\"frame_time_ms\":160,\"easing\":\"EaseIn\",\"glow_dim_percent\":30}," +
           "\"surprise\":{\"frames\":8,\"frame_time_ms\":90,\"easing\":\"EaseOut\",\"glow_spike\":true}" +
           "}";
}

}  // namespace

void FaceEngine::playbackTask(void* parameter) {
    auto* owner = static_cast<FaceEngine*>(parameter);
    PlaybackRequest request{};
    for (;;) {
        if (gPlaybackQueue == nullptr || xQueueReceive(gPlaybackQueue, &request, portMAX_DELAY) != pdTRUE) {
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
            sequence = owner->resolveFallbackSequence(kDefaultAnimation);
        }

        if (sequence.frames.empty()) {
            Serial.printf("[Face] no frames found for %s/%s\n", style.c_str(), animation.c_str());
            if (owner != nullptr) {
                owner->finishPlayback();
            }
            gFaceBusy = false;
            continue;
        }

        const size_t frameCount = sequence.frames.size();
        for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            if (gInterruptRequested && request.priority == 0) {
                break;
            }

            const float amplitude = owner != nullptr ? owner->speakingAmplitude_ : 0.0f;
            const uint16_t delayMs = owner != nullptr ? owner->frameDelayFor(sequence, frameIndex, amplitude)
                                                      : sequence.frameDelayMs;
            if (owner != nullptr) {
                const float normalized = frameCount <= 1 ? 0.0f : static_cast<float>(frameIndex) / static_cast<float>(frameCount - 1);
                const float eased = owner->evaluateCurve(sequence.curve, normalized);
                const size_t mappedIndex = frameCount <= 1
                                               ? 0
                                               : static_cast<size_t>(eased * static_cast<float>(frameCount - 1));
                const size_t safeIndex = mappedIndex >= frameCount ? (frameCount - 1) : mappedIndex;
                const int eyeBounce = sequence.dynamicSpeaking
                                          ? static_cast<int>(std::round(std::sin(owner->timeAccumulator_ * 12.0f) * amplitude * 2.0f))
                                          : 0;
                owner->renderFrame(sequence.frames[safeIndex], eyeBounce);
                owner->renderGlowOverlay(sequence, normalized, amplitude);
            }

            const unsigned long frameStart = millis();
            for (;;) {
                if ((millis() - frameStart) >= delayMs) {
                    break;
                }
                if (gInterruptRequested && request.priority == 0) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(8));
            }

            if (gInterruptRequested && request.priority == 0) {
                break;
            }
        }

        if (owner != nullptr) {
            owner->finishPlayback();
        }

        const unsigned long elapsedMs = millis() - startMs;
        Serial.printf("[Face] playback done style=%s animation=%s elapsed=%lu ms\n",
                      style.c_str(), animation.c_str(), static_cast<unsigned long>(elapsedMs));
        gFaceBusy = false;
    }
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

    if (!setStyle(activeStyle_)) {
        activeStyle_ = kDefaultStyle;
    }

    idlePulse();
    return gPlaybackQueue != nullptr && gPlaybackTask != nullptr;
}

void FaceEngine::update(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        deltaSeconds = 0.01f;
    }

    timeAccumulator_ += deltaSeconds;
    if (speakingAmplitude_ > 0.0f) {
        speakingAmplitude_ -= deltaSeconds * 0.9f;
        if (speakingAmplitude_ < 0.0f) {
            speakingAmplitude_ = 0.0f;
        }
    }

    const unsigned long now = millis();
    if (shouldIdleTrigger()) {
        if ((now - lastBlinkMs_) >= static_cast<unsigned long>(kBlinkPeriodMs / (blinkSpeed_ < 0.5f ? 0.5f : blinkSpeed_))) {
            playAnimation("blink");
            lastBlinkMs_ = now;
            return;
        }
        if ((now - lastIdlePulseMs_) >= kIdlePulsePeriodMs) {
            idlePulse();
            lastIdlePulseMs_ = now;
        }
    }
}

bool FaceEngine::loadAnimationSet(const String& styleName) {
    if (!scanIndex()) {
        return false;
    }
    return setStyle(styleName);
}

bool FaceEngine::loadAnimation(const String& animationName) {
    const String normalized = normalizeName(animationName);
    if (normalized.length() == 0) {
        return false;
    }
    FrameSequence sequence = resolveSequence(activeStyle_, normalized);
    if (!sequence.frames.empty()) {
        return true;
    }
    sequence = resolveFallbackSequence(normalized);
    return !sequence.frames.empty();
}

bool FaceEngine::playAnimation(const String& animationName) {
    return play(animationName);
}

bool FaceEngine::setStyle(const String& styleName) {
    const String normalized = normalizeName(styleName);
    for (const String& style : styles_) {
        if (style == normalized) {
            activeStyle_ = style;
            dirty_ = true;
            return true;
        }
    }
    Serial.printf("[Face] style '%s' missing, using default\n", normalized.c_str());
    activeStyle_ = kDefaultStyle;
    dirty_ = true;
    return false;
}

bool FaceEngine::play(const String& animationName) {
    const String normalized = normalizeName(animationName);
    if (normalized.length() == 0) {
        return false;
    }

    if (normalized == "blink") {
        lastBlinkMs_ = millis();
    }

    if (!triggerPlayback(activeStyle_, normalized)) {
        return false;
    }

    activeAnimation_ = normalized;
    transitionToIdlePending_ = (normalized == "speaking");
    playing_ = true;
    return true;
}

void FaceEngine::stop() {
    if (gPlaybackQueue != nullptr) {
        xQueueReset(gPlaybackQueue);
    }
    gInterruptRequested = true;
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
    activeEmotion_ = normalizeName(emotionName);
    String mapped = activeEmotion_;
    if (mapped == "warning" || mapped == "surprise") {
        mapped = "surprise";
    } else if (mapped == "surprised") {
        mapped = "surprise";
    } else if (mapped == "thinking") {
        mapped = "thinking";
    } else if (mapped == "excited") {
        mapped = "idle";
        setFrameRate("idle", 10.0f);
        setSpeakingAmplitude(0.75f);
        FaceGlowProfile profile;
        profile.glowMultiplier = 1.2f;
        profile.amplitudePulse = true;
        setGlowProfile("idle", profile);
    } else if (mapped == "tired") {
        mapped = "idle";
        setFrameRate("idle", 5.0f);
        FaceGlowProfile profile;
        profile.glowMultiplier = 0.80f;
        profile.slowPulse = true;
        setGlowProfile("idle", profile);
    } else if (mapped == "neutral") {
        mapped = "idle";
    } else if (mapped == "calm") {
        mapped = "idle";
    } else if (mapped == "curious") {
        mapped = "thinking";
    } else if (mapped == "speaking") {
        mapped = "speaking";
    } else if (mapped == "listening") {
        mapped = "listening";
    } else if (mapped == "sad") {
        mapped = "sad";
    } else if (mapped == "happy") {
        mapped = "happy";
    } else if (mapped == "sleepy") {
        mapped = "idle";
    } else if (mapped.length() == 0) {
        mapped = "idle";
    }

    return play(mapped);
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
    aiCanModify_ = settings.aiCanModify;
    aiCanCreate_ = settings.aiCanCreate;
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

String FaceEngine::currentFramePath() const {
    return lastRenderedFramePath_;
}

bool FaceEngine::reloadActiveStyle() {
    return loadAnimationSet(activeStyle_);
}

String FaceEngine::validateAnimationSetJson() const {
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
    return String(kFaceRoot) + "/custom/" + normalizeName(animationName);
}

bool FaceEngine::scanIndex() {
    styles_.clear();
    index_.clear();

    if (!SdManager::isMounted()) {
        return false;
    }

    File root = SD.open(kFaceRoot);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File styleEntry = root.openNextFile();
    while (styleEntry) {
        if (!styleEntry.isDirectory()) {
            styleEntry = root.openNextFile();
            continue;
        }

        const String styleName = normalizeName(styleEntry.name());
        if (styleName.length() == 0) {
            styleEntry = root.openNextFile();
            continue;
        }

        bool styleExists = false;
        for (const String& knownStyle : styles_) {
            if (knownStyle == styleName) {
                styleExists = true;
                break;
            }
        }
        if (!styleExists) {
            styles_.push_back(styleName);
        }

        File animationEntry = styleEntry.openNextFile();
        while (animationEntry) {
            if (!animationEntry.isDirectory()) {
                animationEntry = styleEntry.openNextFile();
                continue;
            }

            FrameSequence sequence;
            sequence.style = styleName;
            sequence.animation = normalizeName(animationEntry.name());

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

            File frameEntry = animationEntry.openNextFile();
            while (frameEntry) {
                if (!frameEntry.isDirectory()) {
                    const String framePath = framePathFromEntry(styleName, sequence.animation, frameEntry.name());
                    if (endsWithPng(framePath)) {
                        FrameAsset asset = loadFrameAsset(framePath);
                        if (asset.path.length() > 0) {
                            sequence.frames.push_back(asset);
                        }
                    }
                }
                frameEntry = animationEntry.openNextFile();
            }

            std::sort(sequence.frames.begin(), sequence.frames.end(), [](const FrameAsset& left, const FrameAsset& right) {
                return frameNumberFromName(left.path) < frameNumberFromName(right.path);
            });

            if (!sequence.frames.empty()) {
                if (definition != nullptr && definition->expectedFrames > 0 &&
                    sequence.frames.size() != definition->expectedFrames) {
                    Serial.printf("[Face] warning: %s/%s expected %u frames, found %u\n",
                                  styleName.c_str(),
                                  sequence.animation.c_str(),
                                  static_cast<unsigned>(definition->expectedFrames),
                                  static_cast<unsigned>(sequence.frames.size()));
                }
                index_.push_back(sequence);
            }

            animationEntry = styleEntry.openNextFile();
        }
        styleEntry = root.openNextFile();
    }

    if (styles_.empty()) {
        styles_.push_back(kDefaultStyle);
    }

    for (const String& animationName : requiredAnimations_) {
        bool found = false;
        for (const FrameSequence& sequence : index_) {
            if (sequence.style == kDefaultStyle && sequence.animation == animationName) {
                found = true;
                break;
            }
        }
        if (!found) {
            Serial.printf("[Face] warning: missing required animation folder %s/%s/%s\n", kFaceRoot, kDefaultStyle,
                          animationName.c_str());
        }
    }

    return true;
}

bool FaceEngine::ensureFaceDirectories() {
    SdManager::ensureDirectory(kFaceRoot);
    SdManager::ensureDirectory((String(kFaceRoot) + "/default").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/soft_glow").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/minimal").c_str());
    SdManager::ensureDirectory((String(kFaceRoot) + "/custom").c_str());

    for (const char* animation : kRequiredAnimations) {
        SdManager::ensureDirectory((String(kFaceRoot) + "/default/" + animation).c_str());
    }

    if (SdManager::isMounted() && !SD.exists(kFaceMetadataPrimaryPath)) {
        File metadataFile = SD.open(kFaceMetadataPrimaryPath, FILE_WRITE);
        if (metadataFile) {
            metadataFile.print(defaultMetadataJson());
            metadataFile.close();
            Serial.printf("[Face] created metadata file at %s\n", kFaceMetadataPrimaryPath);
        }
    }
    return true;
}

bool FaceEngine::loadAnimationMetadata() {
    if (!SdManager::isMounted()) {
        return false;
    }

    JsonDocument document;
    const bool loadedPrimary = SdManager::readJSON(kFaceMetadataPrimaryPath, document);
    const bool loadedSecondary = !loadedPrimary && SdManager::readJSON(kFaceMetadataSecondaryPath, document);
    if (!loadedPrimary && !loadedSecondary) {
        Serial.printf("[Face] metadata not found at %s or %s; using defaults\n",
                      kFaceMetadataPrimaryPath,
                      kFaceMetadataSecondaryPath);
        return false;
    }

    if (!document.is<JsonObject>()) {
        Serial.println("[Face] invalid metadata JSON root; expected object");
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

    Serial.println("[Face] loaded animation metadata blueprint");
    return true;
}

bool FaceEngine::triggerPlayback(const String& styleName, const String& animationName) {
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

    if (request.priority > 0) {
        gInterruptRequested = true;
        xQueueReset(gPlaybackQueue);
    }

    if (xQueueSend(gPlaybackQueue, &request, 0) != pdTRUE) {
        Serial.println("[Face] playback queue full; dropping request");
        return false;
    }
    return true;
}

FaceEngine::FrameSequence FaceEngine::resolveSequence(const String& styleName, const String& animationName) const {
    const String normalizedStyle = normalizeName(styleName);
    const String normalizedAnimation = normalizeName(animationName);

    for (const FrameSequence& sequence : index_) {
        if (sequence.style == normalizedStyle && sequence.animation == normalizedAnimation) {
            return sequence;
        }
    }

    if (normalizedStyle != kDefaultStyle) {
        for (const FrameSequence& sequence : index_) {
            if (sequence.style == kDefaultStyle && sequence.animation == normalizedAnimation) {
                Serial.printf("[Face] warning: style '%s' missing animation '%s'; using default style\n",
                              normalizedStyle.c_str(), normalizedAnimation.c_str());
                return sequence;
            }
        }
    }

    return resolveFallbackSequence(normalizedAnimation);
}

FaceEngine::FrameSequence FaceEngine::resolveFallbackSequence(const String& animationName) const {
    const String normalizedAnimation = normalizeName(animationName);
    for (const FrameSequence& sequence : index_) {
        if (sequence.style == kDefaultStyle && sequence.animation == normalizedAnimation) {
            return sequence;
        }
    }

    for (const FrameSequence& sequence : index_) {
        if (sequence.style == kDefaultStyle && sequence.animation == kDefaultAnimation) {
            Serial.printf("[Face] warning: '%s' missing; falling back to idle\n", normalizedAnimation.c_str());
            return sequence;
        }
    }

    return FrameSequence{};
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

    if (delay < 30.0f) {
        delay = 30.0f;
    }
    return static_cast<uint16_t>(delay);
}

FaceEngine::FrameAsset FaceEngine::loadFrameAsset(const String& framePath) const {
    FrameAsset asset;
    asset.path = framePath;

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
}

String FaceEngine::framePathFromEntry(const String& styleName, const String& animationName, const String& fileName) const {
    if (fileName.startsWith("/")) {
        return fileName;
    }
    return String(kFaceRoot) + "/" + styleName + "/" + animationName + "/" + fileName;
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

void FaceEngine::renderFrame(const FrameAsset& frame, int yOffsetPx) const {
    if (!SdManager::isMounted() || frame.path.length() == 0) {
        return;
    }

    const_cast<FaceEngine*>(this)->lastRenderedFramePath_ = frame.path;

    auto& display = M5.Display;
    display.startWrite();

    if (frame.cached && !frame.bytes.empty()) {
        display.drawPng(frame.bytes.data(), frame.bytes.size(), 0, yOffsetPx);
        display.endWrite();
        return;
    }

    File file = SD.open(frame.path);
    if (!file) {
        display.endWrite();
        return;
    }

    const size_t size = file.size();
    if (size == 0 || size > kStreamFrameMaxBytes) {
        file.close();
        display.endWrite();
        return;
    }

    std::vector<uint8_t> buffer(size);
    const size_t bytesRead = file.read(buffer.data(), size);
    file.close();
    if (bytesRead == size) {
        display.drawPng(buffer.data(), buffer.size(), 0, yOffsetPx);
    }

    display.endWrite();
}

void FaceEngine::renderGlowOverlay(const FrameSequence& sequence, float normalizedFrame, float amplitude) const {
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
    playing_ = false;
    if (shouldReturnToIdleAfterPlayback()) {
        transitionToIdlePending_ = false;
        triggerPlayback(activeStyle_, "idle");
        activeAnimation_ = "idle";
        playing_ = true;
    }
}

bool FaceEngine::shouldIdleTrigger() const {
    return idleEnabled_ && !playing_;
}

String FaceEngine::stylePath(const String& styleName) const {
    return String(kFaceRoot) + "/" + normalizeName(styleName);
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
    if (activeEmotion_ == "speaking" || activeEmotion_ == "listening" || activeEmotion_ == "thinking") {
        return false;
    }
    return true;
}

}  // namespace Flic
