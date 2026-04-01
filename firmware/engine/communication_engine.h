#pragma once

#include <Arduino.h>

namespace Flic {

class LightEngine;
class PersonalityUI;
class AnimationEngine;
class MemoryManager;
class TextBubbles;
class VoiceEngine;

class CommunicationEngine {
public:
    bool begin(LightEngine* lightEngine,
               PersonalityUI* personalityUi,
               AnimationEngine* animationEngine,
               MemoryManager* memoryManager,
               TextBubbles* textBubbles,
               VoiceEngine* voiceEngine);

    void update();
    void speakText(const String& msg);
    void speakVoice(const String& msg);
    void speakEmotion(const String& emotion);
    void speakAnimation(const String& anim);
    void speakLED(const String& pattern);
    void vibrate(const String& pattern);
    void notify(const String& msg, const String& emotion);
    void handleTouchMeaning(const String& meaning);
    String inferEmotionFromText(const String& msg) const;

private:
    LightEngine* lightEngine_ = nullptr;
    PersonalityUI* personalityUi_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    MemoryManager* memoryManager_ = nullptr;
    TextBubbles* textBubbles_ = nullptr;
    VoiceEngine* voiceEngine_ = nullptr;
    String activeEmotion_ = "calm";
};

}  // namespace Flic
