#include "asr_engine.h"

namespace Flic {

bool AsrEngine::begin() {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    return true;
}

void AsrEngine::update() {
    // Placeholder for future on-device ASR updates.
}

bool AsrEngine::pushTranscript(const String& text, const String& source) {
    if (text.length() == 0) {
        return false;
    }

    if (count_ >= kQueueSize) {
        // Drop oldest to keep latest user speech.
        head_ = (head_ + 1) % kQueueSize;
        --count_;
    }

    transcriptQueue_[tail_] = text;
    sourceQueue_[tail_] = source;
    tail_ = (tail_ + 1) % kQueueSize;
    ++count_;
    return true;
}

bool AsrEngine::popTranscript(String& textOut, String& sourceOut) {
    if (count_ == 0) {
        return false;
    }

    textOut = transcriptQueue_[head_];
    sourceOut = sourceQueue_[head_];
    head_ = (head_ + 1) % kQueueSize;
    --count_;
    return true;
}

}  // namespace Flic
