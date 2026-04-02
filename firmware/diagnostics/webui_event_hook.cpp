#include "webui_event_hook.h"

namespace Flic {
namespace WebUiEventHook {
namespace {
Sender gSender = nullptr;
}

void setSender(Sender sender) {
    gSender = sender;
}

void emit(const String& type, const String& payload) {
    if (gSender == nullptr || type.length() == 0) {
        return;
    }
    gSender(type, payload);
}

}  // namespace WebUiEventHook
}  // namespace Flic
