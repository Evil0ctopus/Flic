#pragma once

#include <Arduino.h>

#include <vector>

#include "emotion_blend_engine.h"
#include "micro_expression_engine.h"
#include "personality_state_machine.h"
#include "face_transition_engine.h"
#include "mood_model.h"
#include "personality_memory.h"
#include "adaptive_expression_engine.h"

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

struct FaceTelemetryThresholds {
    float fpsWarn = 20.0f;
    float fpsBad = 12.0f;
    float drawWarnMs = 20.0f;
    float drawBadMs = 30.0f;
    float blendDrawWarnMs = 24.0f;
    float blendDrawBadMs = 36.0f;
    float overBudgetWarnPct = 5.0f;
    float overBudgetBadPct = 15.0f;
    float fallbackWarnCount = 1.0f;
    float fallbackBadCount = 3.0f;
};

struct PersonalityProfileConfig {
    String baselineMood = "calm";
    float microExpressionIntensity = 0.5f;
    String transitionStyle = "crossfade";
    float interactionSensitivity = 0.7f;
    float moodDecayRate = 0.01f;
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
    bool setEmotionBlend(const String& fromEmotion, const String& toEmotion, uint32_t durationMs);
    bool isBlending() const;
    void enableMicroExpressions(bool enabled);
    void setMicroExpressionIntensity(float level);
    bool setPersonalityState(const String& stateName);
    String getPersonalityState() const;
    bool setPersonalityStateFromSystem(const String& stateName);
    void enableContextRules(bool enabled);
    String getCurrentEmotion() const;
    bool setMood(const String& mood);
    String getMood() const;
    void adjustMood(float delta);
    bool saveMood();
    bool loadMood();
    void enableMoodAdaptation(bool enabled);
    void enableAutoEmotion(bool enabled);
    void recordEmotion(const String& emotion);
    std::vector<String> getEmotionHistory() const;
    void clearEmotionHistory();
    bool savePersonalityProfile();
    bool loadPersonalityProfile();
    void resetPersonalityProfile();
    PersonalityProfileConfig personalityProfile() const;
    void updateMood(float deltaSeconds);
    void updateMemory(float deltaSeconds);
    void updateAdaptiveExpressions(float deltaSeconds);
    void updateAutoEmotion(float deltaSeconds);
    void drawFrame();
    bool setTransitionMode(const String& mode);
    bool setFrameRate(float fps);
    bool reloadEmotionAssets();
    bool isEmotionAvailable(const String& emotionName) const;
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
    String telemetryJson() const;
    String telemetryThresholdsJson() const;
    FaceTelemetryThresholds telemetryThresholds() const;
    void setTelemetryThresholds(const FaceTelemetryThresholds& thresholds);
    String currentFramePath() const;
    String validateAnimationSetJson() const;
    bool reloadActiveStyle();
    bool isActive() const;

    bool canWriteCustomAnimation() const;
    bool canModifyExistingAnimation() const;
    bool canCreateStyle() const;
    String customAnimationRoot(const String& animationName) const;

    // Verifies the presence and frame count of a required animation set, logs results
    bool verifyAnimationSet(const char* animationName) const;

private:
    struct EmotionAnimationOverride {
        String emotion;
        String animation;
    };

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
    bool triggerPlayback(const String& styleName, const String& animationName, bool interruptCurrent = false);
    FrameSequence resolveSequence(const String& styleName, const String& animationName) const;
    FrameSequence resolveFallbackSequence(const String& animationName) const;
    String normalizeName(const String& value) const;
    String sanitizeEyeColor(const String& value) const;
    String sanitizeHexColor(const String& value, const String& fallback) const;
    String resolveAnimationForEmotion(const String& emotionName) const;
    void setEmotionAnimationOverridesFromJson(const String& jsonPayload);
    String emotionAnimationOverridesJson() const;
    bool animationIsHighPriority(const String& animationName) const;
    TimingCurve timingCurveFromName(const String& curveName) const;
    float evaluateCurve(TimingCurve curve, float t) const;
    uint16_t frameDelayFor(const FrameSequence& sequence, size_t frameIndex, float amplitude) const;
    FrameAsset loadFrameAsset(const String& framePath) const;
    String framePathFromEntry(const String& styleName, const String& animationName, const String& fileName) const;
    String styleDisplayName(const String& styleName) const;
    String timingCurveName(TimingCurve curve) const;
    void renderFrame(const FrameAsset& frame, int yOffsetPx = 0, bool forceRedraw = false) const;
    void runTransitionTo(const FrameAsset& nextFrame) const;
    void renderFaceWindowBlack(uint8_t level) const;
    void renderGlowOverlay(const FrameSequence& sequence, float normalizedFrame, float amplitude) const;
    void finishPlayback();
    bool shouldIdleTrigger() const;
    String stylePath(const String& styleName) const;
    const AnimationDefinition* definitionFor(const String& animationName) const;
    bool shouldReturnToIdleAfterPlayback() const;
    bool setEmotionInternal(const String& emotionName, bool explicitCommand);
    void registerInteraction(unsigned long nowMs);
    void updateEmotion(unsigned long nowMs);
    EmotionBlendEngine::Snapshot updateBlend(unsigned long nowMs) const;
    MicroExpressionFrame updateMicroExpressions(unsigned long nowMs,
                                                float deltaSeconds,
                                                const EmotionBlendEngine::Snapshot& blendSnapshot,
                                                const String& emotionName,
                                                const PersonalityProfile& personalityProfile);
    void updatePersonality(unsigned long nowMs, const EmotionBlendEngine::Snapshot& blendSnapshot, const String& emotionName);
    String renderEmotionForBlend(unsigned long nowMs, uint32_t frameIndex, const EmotionBlendEngine::Snapshot& blendSnapshot) const;
    size_t renderFrameIndexFor(const FrameSequence& sequence,
                               unsigned long nowMs,
                               uint32_t frameIndex,
                               const EmotionBlendEngine::Snapshot& blendSnapshot) const;
    void drawFrame(const FrameSequence& sequence,
                   size_t frameIndex,
                   const MicroExpressionFrame& microFrame,
                   const PersonalityProfile& personalityProfile,
                   const EmotionBlendEngine::Snapshot& blendSnapshot,
                   int yOffsetPx = 0,
                   bool forceRedraw = false,
                   const FrameSequence* blendSourceSequence = nullptr,
                   size_t blendSourceFrameIndex = 0) const;
    bool ensureBlendBuffers() const;
    bool decodeFrameToBuffer(const FrameAsset& frame, uint16_t* output, size_t pixelCount) const;
    void drawBlendedFrame(const FrameAsset& fromFrame,
                          const FrameAsset& toFrame,
                          float weight,
                          EmotionBlendEngine::Mode mode,
                          const String& dissolveSeed,
                          int yOffsetPx,
                          bool forceRedraw) const;
    void noteFrameTelemetry(bool blended, uint32_t drawUs, uint16_t frameBudgetMs, bool overBudget);
    void emitTelemetryIfDue(unsigned long nowMs);
    void renderMicroExpressions(const MicroExpressionFrame& microFrame,
                                const PersonalityProfile& personalityProfile,
                                const EmotionBlendEngine::Snapshot& blendSnapshot) const;
    int renderYOffsetFor(const MicroExpressionFrame& microFrame, const PersonalityProfile& personalityProfile) const;
    float transitionSpeedScaleForCurrentPersonality() const;
    String selectAutoEmotion(unsigned long nowMs) const;
    String profileStoragePath() const;

    String activeStyle_ = "default";
    String activeEmotion_ = "calm";
    String activeAnimation_ = "idle";
    bool idleEnabled_ = true;
    float blinkSpeed_ = 1.0f;
    float glowIntensity_ = 0.8f;
    String eyeColor_ = "#AEE6FF";
    std::vector<EmotionAnimationOverride> emotionAnimationOverrides_;
    bool aiCanModify_ = false;
    bool aiCanCreate_ = false;
    float timeAccumulator_ = 0.0f;
    float speakingAmplitude_ = 0.0f;
    unsigned long lastIdlePulseMs_ = 0;
    unsigned long lastBlinkMs_ = 0;
    bool playing_ = false;
    bool dirty_ = false;
    bool transitionToIdlePending_ = false;
    bool microExpressionsEnabled_ = true;
    float microExpressionIntensity_ = 0.35f;
    bool contextRulesEnabled_ = true;
    FaceGlowModulation glowModulation_;
    std::vector<String> requiredAnimations_;
    std::vector<String> warnings_;
    std::vector<String> styles_;
    std::vector<FrameSequence> index_;
    std::vector<AnimationDefinition> metadataDefinitions_;
    String lastRenderedFramePath_;
    int lastRenderedFrameNumber_ = -1;
    mutable std::vector<uint8_t> streamBuffer_;
    mutable uint16_t* blendFrameFrom_ = nullptr;
    mutable uint16_t* blendFrameTo_ = nullptr;
    mutable size_t blendPixelCount_ = 0;
    mutable bool blendBuffersAvailable_ = false;
    uint32_t telemetryFrames_ = 0;
    uint32_t telemetryBlendFrames_ = 0;
    uint32_t telemetryOverBudgetFrames_ = 0;
    uint32_t telemetryBlendFallbacks_ = 0;
    uint64_t telemetryDrawUsTotal_ = 0;
    uint64_t telemetryBlendDrawUsTotal_ = 0;
    unsigned long telemetryWindowStartMs_ = 0;
    unsigned long telemetryLastLogMs_ = 0;
    FaceTelemetryThresholds telemetryThresholds_;
    PersonalityProfileConfig personalityProfile_;
    MoodModel moodModel_;
    PersonalityMemory personalityMemory_;
    AdaptiveExpressionEngine adaptiveExpressionEngine_;
    AdaptiveExpressionModifiers adaptiveModifiers_;
    bool moodAdaptationEnabled_ = true;
    bool autoEmotionEnabled_ = true;
    unsigned long lastMoodPersistMs_ = 0;
    unsigned long lastMemoryPersistMs_ = 0;
    unsigned long lastIdleMoodAdjustMs_ = 0;
    unsigned long lastAutoEmotionMs_ = 0;
    unsigned long explicitEmotionHoldUntilMs_ = 0;
    unsigned long lastInteractionMs_ = 0;
    unsigned long lastPersonalityUpdateMs_ = 0;
    uint8_t rapidSwitchCount_ = 0;
    String lastEmotionSwitch_;
    FaceTransitionEngine transitionEngine_;
    EmotionBlendEngine emotionBlendEngine_;
    MicroExpressionEngine microExpressionEngine_;
    PersonalityStateMachine personalityStateMachine_;
    uint16_t renderFrameDelayMs_ = 16;
};

}  // namespace Flic
