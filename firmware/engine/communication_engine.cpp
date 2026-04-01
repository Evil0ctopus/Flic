#include "communication_engine.h"

#include "animation_engine.h"
#include "memory_manager.h"
#include "voice_engine.h"
#include "../subsystems/light_engine.h"
#include "../ui/personality_ui.h"
#include "../ui/text_bubbles.h"

#include <M5Unified.h>

namespace Flic {

bool CommunicationEngine::begin(LightEngine* lightEngine,
                                PersonalityUI* personalityUi,
                                AnimationEngine* animationEngine,
                                MemoryManager* memoryManager,
                                TextBubbles* textBubbles,
                                VoiceEngine* voiceEngine) {
    lightEngine_ = lightEngine;
    personalityUi_ = personalityUi;
    animationEngine_ = animationEngine;
    memoryManager_ = memoryManager;
    textBubbles_ = textBubbles;
    voiceEngine_ = voiceEngine;
    activeEmotion_ = "calm";
    return true;
}

void CommunicationEngine::update() {
    if (textBubbles_ != nullptr) {
        textBubbles_->update();
    }
}

void CommunicationEngine::speakText(const String& msg) {
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
    if (voiceEngine_ != nullptr) {
        voiceEngine_->speak(msg, activeEmotion_);
    } else if (M5.Speaker.isEnabled()) {
        M5.Speaker.tone(560.0f, 90);
    }
    speakText(msg);
    if (memoryManager_ != nullptr) {
        memoryManager_->recordEvent("speak_voice", msg);
    }
}

void CommunicationEngine::speakEmotion(const String& emotion) {
    activeEmotion_ = emotion.length() == 0 ? "calm" : emotion;

    if (lightEngine_ != nullptr) {
        lightEngine_->emotionColor(activeEmotion_);
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
        return;
    }

    if (anim == "blink") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression("blink");
        }
        if (lightEngine_ != nullptr) {
            lightEngine_->setBrightness(8);
            delay(50);
            lightEngine_->setBrightness(18);
        }
        return;
    }

    if (anim == "head_tilt_left" || anim == "head_tilt_right" || anim == "idle_breathing") {
        if (personalityUi_ != nullptr) {
            personalityUi_->showExpression(anim);
        }
        return;
    }

    if (animationEngine_ != nullptr) {
        String fileName = anim;
        if (!fileName.endsWith(".json")) {
            fileName += ".json";
        }
        animationEngine_->playAnimation(fileName.c_str());
    }
}

void CommunicationEngine::speakLED(const String& pattern) {
    if (lightEngine_ == nullptr) {
        return;
    }

    if (pattern == "calm") {
        lightEngine_->pulse(0, 80, 255, 26);
    } else if (pattern == "curious") {
        lightEngine_->pulse(150, 0, 255, 18);
    } else if (pattern == "excited") {
        lightEngine_->flash(255, 220, 0, 3);
    } else if (pattern == "happy") {
        lightEngine_->flashCommandApproved();
    } else if (pattern == "warning") {
        lightEngine_->flashCommandRejected();
    } else if (pattern == "sleepy") {
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
    speakEmotion(emotion);
    speakText(msg);
    speakLED(emotion);

    if (emotion == "curious") {
        speakAnimation("thinking");
    } else if (emotion == "warning") {
        speakAnimation("surprise");
    } else if (emotion == "sleepy") {
        speakAnimation("idle_breathing");
    } else {
        speakAnimation("blink");
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
    } else if (meaning == "excitement") {
        notify("Yay!", "excited");
        speakAnimation("surprise");
    } else if (meaning == "comfort") {
        notify("I'm here.", "calm");
        vibrate("purr");
    } else if (meaning == "cancel") {
        notify("Canceled.", "warning");
    } else if (meaning == "continue") {
        notify("Continuing.", "curious");
    } else if (meaning == "attention") {
        notify("I'm paying attention.", "curious");
        speakAnimation("thinking");
    } else if (meaning == "dismiss") {
        notify("Dismissed.", "sleepy");
    } else if (meaning == "drag") {
        speakText("Dragging...");
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
