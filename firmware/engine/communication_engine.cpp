#include "communication_engine.h"

#include "animation_engine.h"
#include "face_engine.h"
#include "emotion_engine.h"
#include "memory_manager.h"
#include "voice_engine.h"
#include "../subsystems/light_engine.h"
#include "../ui/personality_ui.h"
#include "../ui/text_bubbles.h"

#include <M5Unified.h>

namespace Flic {

namespace {
constexpr bool kDisableVoiceAudioOutput = false;  // ENABLED: Real TTS voice synthesis
constexpr unsigned long kVoiceWarmupMs = 5000;
constexpr size_t kMaxVoiceMessageLen = 120;
constexpr unsigned long kVoiceSpeakCooldownMs = 3000;
constexpr unsigned long kNotifyAnimationCooldownMs = 2600;
constexpr unsigned long kNotifyBubbleCooldownMs = 1800;

String normalizeEmotion(const String& emotion) {
    if (emotion.length() == 0) {
        return "calm";
    }

    if (emotion == "warning" || emotion == "surprise") {
        return "surprised";
    }

    if (emotion == "excited") {
        return "happy";
    }

    return emotion;
}

}  // namespace

bool CommunicationEngine::begin(LightEngine* lightEngine,
                                PersonalityUI* personalityUi,
                                AnimationEngine* animationEngine,
                                EmotionEngine* emotionEngine,
                                MemoryManager* memoryManager,
                                TextBubbles* textBubbles,
                                VoiceEngine* voiceEngine,
                                FaceEngine* faceEngine) {
    lightEngine_ = lightEngine;
    personalityUi_ = personalityUi;
    animationEngine_ = animationEngine;
    emotionEngine_ = emotionEngine;
    memoryManager_ = memoryManager;
    textBubbles_ = textBubbles;
    voiceEngine_ = voiceEngine;
    faceEngine_ = faceEngine;
    activeEmotion_ = "calm";
    lastNotifyMs_ = 0;
    lastNotifyMessage_ = "";
    lastNotifyEmotion_ = "";
    lastNotifyAnimationMs_ = 0;
    lastNotifyAnimation_ = "";
    return true;
}

void CommunicationEngine::update() {
    if (textBubbles_ != nullptr) {
        textBubbles_->update();
    }
}

void CommunicationEngine::speakText(const String& msg) {
    if (msg.length() == 0) {
        return;
    }

    if (textBubbles_ != nullptr) {
        BubbleSize size = BubbleSize::Medium;
        if (msg.length() <= 20) {
            size = BubbleSize::Small;
        } else if (msg.length() >= 64) {
            size = BubbleSize::Large;
        }
        textBubbles_->showMessage(msg, size, activeEmotion_);
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("speak_text", msg);
    }
}

void CommunicationEngine::speakVoice(const String& msg) {
    if (msg.length() == 0) {
        return;
    }

    const bool voiceWarm = millis() >= kVoiceWarmupMs;
    const bool messageTooLong = msg.length() > kMaxVoiceMessageLen;
    static unsigned long lastVoiceSpeakMs = 0;
    const unsigned long nowMs = millis();
    const bool cooldownElapsed = (nowMs - lastVoiceSpeakMs) >= kVoiceSpeakCooldownMs;

    if (!kDisableVoiceAudioOutput && voiceEngine_ != nullptr && voiceWarm && !messageTooLong && cooldownElapsed) {
        if (faceEngine_ != nullptr) {
            faceEngine_->setSpeakingAmplitude(0.80f);
            faceEngine_->play("speaking");
        }
        voiceEngine_->speak(msg, activeEmotion_);
        lastVoiceSpeakMs = nowMs;
    }
    speakText(msg);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("speak_voice", msg);
    }
}

void CommunicationEngine::speakEmotion(const String& emotion) {
    activeEmotion_ = normalizeEmotion(emotion);

    if (emotionEngine_ != nullptr) {
        emotionEngine_->setEmotion(activeEmotion_);
    }

    if (faceEngine_ != nullptr) {
        faceEngine_->setEmotion(activeEmotion_);
        if (activeEmotion_ == "curious") {
            faceEngine_->setPersonalityState("curious");
        } else if (activeEmotion_ == "happy") {
            faceEngine_->setPersonalityState("excited");
        } else if (activeEmotion_ == "sleepy") {
            faceEngine_->setPersonalityState("tired");
        } else if (activeEmotion_ == "surprised") {
            faceEngine_->setPersonalityState("confused");
        } else {
            faceEngine_->setPersonalityState("neutral");
        }
        faceEngine_->enableMicroExpressions(true);
    }

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(activeEmotion_);
    }

    if (personalityUi_ != nullptr && activeEmotion_ == "surprised") {
        personalityUi_->showExpression("surprise");
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("speak_emotion", activeEmotion_);
    }
}

void CommunicationEngine::speakAnimation(const String& anim) {
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("speak_animation", anim);
    }

    if (anim == "surprise") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression("surprise");
        }
        if (lightEngine_ != nullptr) {
            lightEngine_->flash(255, 255, 255, 1);
        }
        if (animationEngine_ != nullptr) {
            animationEngine_->playPreset("surprise");
        }
        if (faceEngine_ != nullptr) {
            faceEngine_->play("surprise");
        }
        return;
    }

    if (anim == "thinking") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression("thinking");
        }
        if (textBubbles_ != nullptr) {
            textBubbles_->showMessage("Thinking...", BubbleSize::Small, "curious");
        }
        if (lightEngine_ != nullptr) {
            lightEngine_->pulse(120, 60, 255, 20);
        }
        if (animationEngine_ != nullptr) {
            animationEngine_->playPreset("thinking_loop");
        }
        if (faceEngine_ != nullptr) {
            faceEngine_->play("thinking");
        }
        return;
    }

    if (anim == "blink") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression("blink");
        }
        if (lightEngine_ != nullptr) {
            lightEngine_->setBrightness(8);
            lightEngine_->setBrightness(18);
        }
        if (animationEngine_ != nullptr) {
            animationEngine_->playPreset("blink");
        }
        if (faceEngine_ != nullptr) {
            faceEngine_->play("blink");
        }
        return;
    }

    if (anim == "head_tilt_left" || anim == "head_tilt_right" || anim == "idle_breathing") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression(anim);
        }
        if (animationEngine_ != nullptr) {
            if (anim == "head_tilt_left") {
                animationEngine_->playMicroGesture("tilt_left");
            } else if (anim == "head_tilt_right") {
                animationEngine_->playMicroGesture("tilt_right");
            } else {
                animationEngine_->playPreset("idle_breathing");
            }
        }
        if (faceEngine_ != nullptr) {
            faceEngine_->play(anim);
        }
        return;
    }

    if (animationEngine_ != nullptr) {
        String fileName = anim;
        if (!fileName.endsWith(".json")) {
            fileName += ".json";
        }
        animationEngine_->playAnimation(fileName.c_str());
        return;
    }

    if (faceEngine_ != nullptr) {
        faceEngine_->play(anim);
        return;
    }

    if (personalityUi_ != nullptr) {
        personalityUi_->showExpression(anim);
    }
}

void CommunicationEngine::speakLED(const String& pattern) {
    if (lightEngine_ == nullptr) {
        return;
    }

    if (pattern == "warning") {
        lightEngine_->flashCommandRejected();
        return;
    }

    if (pattern == "happy") {
        lightEngine_->flashCommandApproved();
        return;
    }

    const String emotion = normalizeEmotion(pattern);

    if (emotion == "calm") {
        lightEngine_->pulse(0, 80, 255, 26);
    } else if (emotion == "curious") {
        lightEngine_->pulse(150, 0, 255, 18);
    } else if (emotion == "happy") {
        lightEngine_->flash(255, 220, 0, 3);
    } else if (emotion == "surprised") {
        lightEngine_->flash(255, 255, 255, 2);
    } else if (emotion == "sleepy") {
        lightEngine_->pulse(0, 120, 120, 34);
    } else {
        lightEngine_->emotionColor(activeEmotion_);
    }
}

void CommunicationEngine::vibrate(const String& pattern) {
    auto buzz = [](uint8_t level, uint16_t ms) {
        M5.Power.setVibration(level);
        delay(ms);
        M5.Power.setVibration(0);
    };

    if (pattern == "tap") {
        buzz(90, 45);
    } else if (pattern == "purr") {
        buzz(55, 120);
    } else if (pattern == "alert") {
        buzz(120, 150);
    } else if (pattern == "heartbeat") {
        buzz(80, 120);
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("vibration", pattern);
    }
}

void CommunicationEngine::notify(const String& msg, const String& emotion) {
    const String normalizedEmotion = normalizeEmotion(emotion);
    const unsigned long nowMs = millis();
    const bool sameMessage = msg == lastNotifyMessage_;
    const bool sameEmotion = normalizedEmotion == lastNotifyEmotion_;
    const bool bubbleCooldownElapsed = (nowMs - lastNotifyMs_) >= kNotifyBubbleCooldownMs;

    speakEmotion(normalizedEmotion);
    if (lightEngine_ != nullptr && msg.length() > 0) {
        lightEngine_->expressUtterance(msg, normalizedEmotion);
    }
    if (msg.length() > 0 && (!sameMessage || !sameEmotion || bubbleCooldownElapsed)) {
        speakVoice(msg);
        lastNotifyMessage_ = msg;
        lastNotifyEmotion_ = normalizedEmotion;
        lastNotifyMs_ = nowMs;
    }
    speakLED(normalizedEmotion);

    String notifyAnimation = "blink";
    if (emotion == "curious") {
        notifyAnimation = "thinking";
    } else if (emotion == "warning") {
        notifyAnimation = "surprise";
    } else if (emotion == "sleepy") {
        notifyAnimation = "idle_breathing";
    }

    const bool sameAnimation = notifyAnimation == lastNotifyAnimation_;
    const bool cooldownElapsed = (nowMs - lastNotifyAnimationMs_) >= kNotifyAnimationCooldownMs;
    if ((!sameAnimation || cooldownElapsed) && (!sameEmotion || cooldownElapsed)) {
        speakAnimation(notifyAnimation);
        lastNotifyAnimation_ = notifyAnimation;
        lastNotifyAnimationMs_ = nowMs;
    }

    if (emotion == "warning") {
        vibrate("alert");
    } else if (emotion == "happy") {
        vibrate("tap");
    }

    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("notify", msg + "|" + emotion);
    }
}

void CommunicationEngine::handleTouchMeaning(const String& meaning) {
    if (meaning == "acknowledge") {
        notify("Got it.", "calm");
        speakAnimation("micro_nod");
    } else if (meaning == "excitement") {
        notify("Yay!", "surprise");
    } else if (meaning == "comfort") {
        notify("I'm here.", "calm");
        vibrate("purr");
    } else if (meaning == "cancel") {
        notify("Canceled.", "warning");
    } else if (meaning == "continue") {
        notify("Continuing.", "curious");
        speakAnimation("micro_tilt_right");
    } else if (meaning == "attention") {
        notify("I'm paying attention.", "curious");
        speakAnimation("micro_tilt_left");
    } else if (meaning == "dismiss") {
        notify("Dismissed.", "sleepy");
    } else if (meaning == "drag") {
        notify("Dragging...", "curious");
    }
}

String CommunicationEngine::inferEmotionFromText(const String& msg) const {
    String lower = msg;
    lower.toLowerCase();
    if (lower.indexOf("error") >= 0 || lower.indexOf("fail") >= 0 || lower.indexOf("warn") >= 0) {
        return "warning";
    }
    if (lower.indexOf("ok") >= 0 || lower.indexOf("done") >= 0 || lower.indexOf("success") >= 0) {
        return "happy";
    }
    if (lower.indexOf("temp") >= 0 || lower.indexOf("status") >= 0 || lower.indexOf("sensor") >= 0) {
        return "curious";
    }
    return "calm";
}

}  // namespace Flic
