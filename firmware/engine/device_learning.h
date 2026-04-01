#pragma once

#include <Arduino.h>

namespace Flic {

class MemoryManager;
class ProposalSystem;

class DeviceLearning {
public:
    bool begin(MemoryManager* memoryManager, ProposalSystem* proposalSystem);
    bool processMessage(const String& deviceId, const String& message);
    bool learnedSomething() const;
    String lastLearningNote() const;
    String lastDeviceId() const;
    String lastPattern() const;
    void clearLearningSignal();

private:
    void loadPatterns();
    void savePatterns();
    void recordPattern(const String& deviceId, const String& message, const String& kind);
    size_t patternCountForDevice(const String& deviceId, const String& pattern, const String& kind) const;
    String classifyMessage(const String& message) const;
    bool isLikelyCommandToken(const String& message) const;
    bool isLikelySensorPattern(const String& message) const;

    MemoryManager* memoryManager_ = nullptr;
    ProposalSystem* proposalSystem_ = nullptr;
    String lastLearningNote_;
    String lastDeviceId_;
    String lastPattern_;
    bool learnedSomething_ = false;
};

}  // namespace Flic
