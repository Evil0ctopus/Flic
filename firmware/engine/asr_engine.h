#pragma once

#include <Arduino.h>

namespace Flic {

class AsrEngine {
public:
    bool begin();
    void update();

    bool pushTranscript(const String& text, const String& source = "unknown");
    bool popTranscript(String& textOut, String& sourceOut);

private:
    static constexpr size_t kQueueSize = 6;
    String transcriptQueue_[kQueueSize];
    String sourceQueue_[kQueueSize];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
};

}  // namespace Flic
