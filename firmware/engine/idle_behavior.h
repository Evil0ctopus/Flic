#pragma once

namespace Flic {

class LightEngine;
class EmotionEngine;
class PersonalityUI;

class IdleBehavior {
public:
    void begin(LightEngine* lightEngine, EmotionEngine* emotionEngine, PersonalityUI* personalityUi);
    void update(bool animationPlaying);

private:
    LightEngine* lightEngine_ = nullptr;
    EmotionEngine* emotionEngine_ = nullptr;
    PersonalityUI* personalityUi_ = nullptr;
    unsigned long lastUpdateMs_ = 0;
    int level_ = 6;
    int delta_ = 1;
};

}  // namespace Flic
