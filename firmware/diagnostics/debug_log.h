#pragma once

#include <Arduino.h>

// Log levels: 0=off, 1=error, 2=warn, 3=info, 4=debug, 5=trace
#ifndef FLIC_LOG_LEVEL
#define FLIC_LOG_LEVEL 3
#endif

#ifndef FLIC_LOG_USE_MILLIS
#define FLIC_LOG_USE_MILLIS 1
#endif

#ifndef FLIC_LOG_STREAM
#define FLIC_LOG_STREAM Serial
#endif

namespace Flic {
namespace Debug {

inline uint8_t& runtimeLogLevel() {
    static uint8_t level = FLIC_LOG_LEVEL;
    return level;
}

inline void setRuntimeLogLevel(uint8_t level) {
    if (level > 5) {
        level = 5;
    }
    runtimeLogLevel() = level;
}

inline const char* levelName(uint8_t level) {
    switch (level) {
        case 1:
            return "E";
        case 2:
            return "W";
        case 3:
            return "I";
        case 4:
            return "D";
        case 5:
            return "T";
        default:
            return "-";
    }
}

inline bool shouldLog(uint8_t level) {
    return level <= runtimeLogLevel();
}

inline void printPrefix(uint8_t level, const char* module) {
#if FLIC_LOG_USE_MILLIS
    FLIC_LOG_STREAM.print('[');
    FLIC_LOG_STREAM.print(millis());
    FLIC_LOG_STREAM.print(" ms][");
#else
    FLIC_LOG_STREAM.print('[');
#endif
    FLIC_LOG_STREAM.print(levelName(level));
    FLIC_LOG_STREAM.print("][");
    FLIC_LOG_STREAM.print(module != nullptr ? module : "APP");
    FLIC_LOG_STREAM.print("] ");
}

inline void log(uint8_t level, const char* module, const String& message) {
    if (!shouldLog(level)) {
        return;
    }
    printPrefix(level, module);
    FLIC_LOG_STREAM.println(message);
}

inline bool everyMs(unsigned long periodMs, unsigned long& lastMs) {
    const unsigned long now = millis();
    if ((now - lastMs) < periodMs) {
        return false;
    }
    lastMs = now;
    return true;
}

}  // namespace Debug
}  // namespace Flic

#define FLIC_LOGE(module, msg) Flic::Debug::log(1, module, msg)
#define FLIC_LOGW(module, msg) Flic::Debug::log(2, module, msg)
#define FLIC_LOGI(module, msg) Flic::Debug::log(3, module, msg)
#define FLIC_LOGD(module, msg) Flic::Debug::log(4, module, msg)
#define FLIC_LOGT(module, msg) Flic::Debug::log(5, module, msg)

#define FLIC_LOGI_EVERY_MS(module, periodMs, lastMsVar, msgExpr) \
    do { \
        if (Flic::Debug::everyMs(periodMs, lastMsVar)) { \
            FLIC_LOGI(module, msgExpr); \
        } \
    } while (0)
