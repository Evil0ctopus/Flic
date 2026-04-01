#pragma once

#include <Arduino.h>

namespace Flic {

class MemoryManager;

class MilestoneEngine {
public:
    bool begin(MemoryManager* memoryManager);
    void update();
    bool unlock(const String& id, const String& reason);

private:
    void save();
    bool hasUnlocked(const String& id);

    MemoryManager* memoryManager_ = nullptr;
    bool wroteIntro_ = false;
};

}  // namespace Flic
