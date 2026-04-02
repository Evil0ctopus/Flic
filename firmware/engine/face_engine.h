#pragma once

#include <Arduino.h>

#include <vector>

namespace Flic {

struct FaceSettings;

struct FaceGlowModulation {
    float innerGlowRadiusPx = 12.0f;
    float outerGlowRadiusPx = 28.0f;
    String gradientStart = "#FFFFFF";
    String gradientMid = "#AEE6FF";
    String gradientEnd = "#C8B5FF";
    float minOpacity = 0.35f;
    float maxOpacity = 0.55f;
};

struct FaceGlowProfile {
    float glowMultiplier = 1.0f;
    float glowVariationPercent = 0.0f;
    bool slowPulse = false;
    bool amplitudePulse = false;
    bool glowSpike = false;
};

class FaceEngine {
public:
    enum class TimingCurve : uint8_t {
        Linear = 0,
        EaseIn = 1,
        EaseOut = 2,
        EaseInOut = 3,
    };

    struct AnimationDefinition {
        const char* name;
        uint16_t expectedFrames;
        uint16_t frameTimeMs;
        TimingCurve curve;
        float glowMultiplier;
        float glowVariationPercent;
        bool slowPulse;
        bool dynamicSpeaking;
        bool glowSpike;
    };

    bool begin();
    void update(float deltaSeconds);

    bool loadAnimationSet(const String& styleName);
    bool loadAnimation(const String& animationName);
    bool playAnimation(const String& animationName);
    bool setStyle(const String& styleName);
    bool play(const String& animationName);
    void stop();
    bool setEasing(const String& animationName, const String& easingType);
    bool setGlowProfile(const String& animationName, const FaceGlowProfile& params);
    bool setEmotion(const String& emotionName);
    bool idlePulse();
    bool setFrameRate(const String& animationName, float fps);
    void setGlowModulation(const FaceGlowModulation& params);
    void applyGlowModulation(const FaceGlowModulation& params);
    void setSpeakingAmplitude(float amplitude);

    void applySettings(const FaceSettings& settings);
    String activeStyle() const;
    String currentAnimation() const;
    String currentEmotion() const;

    std::vector<String> listStyles() const;
    std::vector<String> listAnimations(const String& styleName) const;

    String stylesJson() const;
    String animationsJson(const String& styleName) const;
    String animationsCatalogJson() const;
    String settingsJson() const;
    String glowStateJson() const;
    String currentFramePath() const;
    String validateAnimationSetJson() const;
    bool reloadActiveStyle();

    bool canWriteCustomAnimation() const;
    bool canModifyExistingAnimation() const;
    bool canCreateStyle() const;
    String customAnimationRoot(const String& animationName) const;

private:
    struct FrameAsset {
        String path;
        std::vector<uint8_t> bytes;
        bool cached = false;
    };

    struct FrameSequence {
        String style;
        String animation;
        std::vector<FrameAsset> frames;
        uint16_t frameDelayMs = 120;
        TimingCurve curve = TimingCurve::Linear;
        float glowMultiplier = 1.0f;
        float glowVariationPercent = 0.0f;
        bool slowPulse = false;
        bool dynamicSpeaking = false;
        bool glowSpike = false;
    };

    static void playbackTask(void* parameter);

    bool scanIndex();
    bool ensureFaceDirectories();
    bool loadAnimationMetadata();
    bool triggerPlayback(const String& styleName, const String& animationName);
    FrameSequence resolveSequence(const String& styleName, const String& animationName) const;
    FrameSequence resolveFallbackSequence(const String& animationName) const;
    String normalizeName(const String& value) const;
    String sanitizeEyeColor(const String& value) const;
    String sanitizeHexColor(const String& value, const String& fallback) const;
    bool animationIsHighPriority(const String& animationName) const;
    TimingCurve timingCurveFromName(const String& curveName) const;
    float evaluateCurve(TimingCurve curve, float t) const;
    uint16_t frameDelayFor(const FrameSequence& sequence, size_t frameIndex, float amplitude) const;
    FrameAsset loadFrameAsset(const String& framePath) const;
    String framePathFromEntry(const String& styleName, const String& animationName, const String& fileName) const;
    String styleDisplayName(const String& styleName) const;
    String timingCurveName(TimingCurve curve) const;
    void renderFrame(const FrameAsset& frame, int yOffsetPx = 0) const;
    void renderGlowOverlay(const FrameSequence& sequence, float normalizedFrame, float amplitude) const;
    void finishPlayback();
    bool shouldIdleTrigger() const;
    String stylePath(const String& styleName) const;
    const AnimationDefinition* definitionFor(const String& animationName) const;
    bool shouldReturnToIdleAfterPlayback() const;

    String activeStyle_ = "default";
    String activeEmotion_ = "calm";
    String activeAnimation_ = "idle";
    bool idleEnabled_ = true;
    float blinkSpeed_ = 1.0f;
    float glowIntensity_ = 0.8f;
    String eyeColor_ = "#AEE6FF";
    bool aiCanModify_ = false;
    bool aiCanCreate_ = false;
    float timeAccumulator_ = 0.0f;
    float speakingAmplitude_ = 0.0f;
    unsigned long lastIdlePulseMs_ = 0;
    unsigned long lastBlinkMs_ = 0;
    bool playing_ = false;
    bool dirty_ = false;
    bool transitionToIdlePending_ = false;
    FaceGlowModulation glowModulation_;
    std::vector<String> requiredAnimations_;
    std::vector<String> warnings_;
    std::vector<String> styles_;
    std::vector<FrameSequence> index_;
    std::vector<AnimationDefinition> metadataDefinitions_;
    String lastRenderedFramePath_;
};

}  // namespace Flic
