#pragma once

#include <Arduino.h>

namespace Flic {

class MemoryManager {
public:
    bool begin();
    void recordEvent(String type, String detail);
    void saveMemory();
    void loadMemory();
    size_t eventCount() const;
    String lastEventType() const;
    String lastEventDetail() const;
    size_t countEventsOfType(const String& type) const;

private:
    void trimToLimit();

    static constexpr size_t kMaxEvents = 24;
    String types_[kMaxEvents];
    String details_[kMaxEvents];
    uint32_t timestamps_[kMaxEvents] = {};
    size_t count_ = 0;
    uint32_t sequence_ = 0;
};

}  // namespace Flic
