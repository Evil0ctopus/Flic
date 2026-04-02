#pragma once

#include <Arduino.h>

#if __has_include("config.local.h")
#include "config.local.h"
#endif

namespace Flic {

constexpr const char* kMemoryRoot = "/ai/memory";
constexpr uint32_t kDefaultUsbBaud = 115200;
constexpr uint32_t kFrameDelayMs = 16;
// M5GO Battery Bottom3 (A014-D, CoreS3): 10x WS2812 on M5-Bus RGB line (GPIO5)
constexpr int8_t kExternalRgbLedPin = 5;
constexpr uint8_t kExternalRgbLedCount = 10;

// WiFi Configuration
#ifndef FLIC_LOCAL_CONFIG
constexpr const char* kWiFiSSID = "";
constexpr const char* kWiFiPassword = "";
#endif
constexpr uint32_t kWiFiConnectTimeoutMs = 30000;

}  // namespace Flic
