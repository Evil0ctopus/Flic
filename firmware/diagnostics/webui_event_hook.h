#pragma once

#include <Arduino.h>

namespace Flic {
namespace WebUiEventHook {

using Sender = void (*)(const String& type, const String& payload);

void setSender(Sender sender);
void emit(const String& type, const String& payload);

}  // namespace WebUiEventHook
}  // namespace Flic
