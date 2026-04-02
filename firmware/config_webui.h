#pragma once

#include <Arduino.h>

#if __has_include("config_webui.local.h")
#include "config_webui.local.h"
#endif

namespace Flic {

// Optional WebUI Wi-Fi configuration.
// Leave SSID empty to keep WebUI disabled at runtime.
#ifndef FLIC_WEBUI_LOCAL_CONFIG
constexpr const char* kWebUiSsid = "";
constexpr const char* kWebUiPassword = "";

// Default ports used by WebUiEngine.
constexpr uint16_t kWebUiHttpPort = 80;
constexpr uint16_t kWebUiWsPort = 81;
#endif

}  // namespace Flic
