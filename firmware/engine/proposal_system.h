#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Flic {

class MemoryManager;
class EmotionEngine;

class ProposalSystem {
public:
    bool begin(MemoryManager* memoryManager, EmotionEngine* emotionEngine = nullptr);
    void update(bool animationPlaying);
    void proposeChange(const String& proposal);
    void proposeGrowthIdeas();
    void proposeUsbDevice(const String& deviceId, const String& reason);
    void proposeUsbCommand(const String& deviceId, const String& command, const String& reason);
    void proposeUsbResponse(const String& deviceId, const String& response, const String& reason);
    void proposeUsbCapability(const String& deviceId, const String& capability, const String& reason);
    void proposeUsbSensor(const String& deviceId, const String& sensorPattern, const String& reason);
    bool consumeApprovedUsbCommand(const String& deviceId, String& commandOut);

private:
    bool loadProposals();
    bool hasProposal(const String& proposal) const;
    bool hasUsbProposal(const String& type, const String& deviceId, const char* valueKey, const String& value) const;
    bool isSafeUsbCommand(const String& command) const;
    bool isValidUsbCommandProposal(const JsonObject& entry, const String& deviceId) const;

    MemoryManager* memoryManager_ = nullptr;
    EmotionEngine* emotionEngine_ = nullptr;
    bool proposedThisBoot_ = false;
};

}  // namespace Flic
