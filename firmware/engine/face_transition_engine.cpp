#include "face_transition_engine.h"

namespace Flic {

namespace {
constexpr float kMinFps = 15.0f;
constexpr float kMaxFps = 60.0f;
}

void FaceTransitionEngine::setMode(Mode mode) {
    mode_ = mode;
}

FaceTransitionEngine::Mode FaceTransitionEngine::mode() const {
    return mode_;
}

bool FaceTransitionEngine::setModeFromString(const String& modeName) {
    String mode = modeName;
    mode.trim();
    mode.toLowerCase();

    if (mode == "crossfade") {
        mode_ = Mode::Crossfade;
        return true;
    }
    if (mode == "morph") {
        mode_ = Mode::Morph;
        return true;
    }
    if (mode == "dissolve") {
        mode_ = Mode::Dissolve;
        return true;
    }
    if (mode == "fade_to_black" || mode == "fade-to-black") {
        mode_ = Mode::FadeToBlack;
        return true;
    }
    if (mode == "direct" || mode == "direct_cut" || mode == "cut" || mode == "default") {
        mode_ = Mode::DirectCut;
        return true;
    }

    return false;
}

String FaceTransitionEngine::modeName() const {
    switch (mode_) {
        case Mode::Crossfade:
            return "crossfade";
        case Mode::Morph:
            return "morph";
        case Mode::Dissolve:
            return "dissolve";
        case Mode::FadeToBlack:
            return "fade_to_black";
        case Mode::DirectCut:
        default:
            return "direct_cut";
    }
}

void FaceTransitionEngine::setFrameRate(float fps) {
    if (fps < kMinFps) {
        fps = kMinFps;
    }
    if (fps > kMaxFps) {
        fps = kMaxFps;
    }
    frameRate_ = fps;
}

float FaceTransitionEngine::frameRate() const {
    return frameRate_;
}

uint16_t FaceTransitionEngine::frameDelayMs() const {
    const float fps = frameRate_ <= 0.0f ? 60.0f : frameRate_;
    const float delay = 1000.0f / fps;
    return delay < 1.0f ? 1 : static_cast<uint16_t>(delay);
}

}  // namespace Flic
