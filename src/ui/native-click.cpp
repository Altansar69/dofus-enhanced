#include "ui/native-click.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <vector>

namespace nativeClick {
namespace {
using namespace managed;
constexpr const char* UiAssembly = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
struct Vec2 { float x, y; };
using UpFn = void(*)(Obj, Obj, Vec2, int, Method);
UpFn originalUp = nullptr;
Method getActive{}, containsPointer{}, getTarget{};
std::vector<bool (*)(Obj)> handlers;
bool installed = false, reported = false;
bool Ready() { return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested; }
void UpHook(Obj self, Obj event, Vec2 position, int pointerId, Method method) {
    // The game decides a click from the manipulator being active and the
    // pointer still over its target; read both before the original resets them.
    bool click = false;
    try {
        if (Ready() && !handlers.empty() && self)
            click = api.Value<bool>(api.Invoke(getActive, self)) && api.Value<bool>(api.Invoke(containsPointer, self, {&pointerId}));
    } catch (const std::exception& error) { if (!reported) Log(std::string("NATIVE_CLICK_ERROR read: ") + error.what()); reported = true; click = false; }
    originalUp(self, event, position, pointerId, method);
    if (!click) return;
    try {
        Ref target(api.Invoke(getTarget, self));
        if (!target.get() || !api.Value<bool>(api.Call(target.get(), "get_enabledInHierarchy"))) return;
        for (const auto handler : handlers) if (handler(target.get())) return;
    } catch (const std::exception& error) { if (!reported) Log(std::string("NATIVE_CLICK_ERROR dispatch: ") + error.what()); reported = true; }
}
}
void Install() {
    if (installed) return;
    auto clickable = api.Class(UiAssembly, UiSpace, "Clickable");
    auto manipulator = api.Class(UiAssembly, UiSpace, "Manipulator");
    auto extended = api.Class("Core.dll", "Core.UILogic.Components.Figma", "ExtendedClickable");
    getActive = api.FindVerified(clickable, "get_active", 0, 0x752840);
    containsPointer = api.FindVerified(clickable, "ContainsPointer", 1, 0x5590600);
    getTarget = api.FindVerified(manipulator, "get_target", 0, 0x30D6960);
    api.FindVerified(api.Class(UiAssembly, UiSpace, "VisualElement"), "get_enabledInHierarchy", 0, 0x55F5EF0);
    AddHook(api.FindVerified(extended, "ProcessUpEvent", 3, 0x172B6A0), 0x172B6A0, reinterpret_cast<void*>(&UpHook), reinterpret_cast<void**>(&originalUp));
    installed = true;
}
void AddHandler(bool (*handler)(Obj)) {
    if (handler && std::find(handlers.begin(), handlers.end(), handler) == handlers.end()) handlers.push_back(handler);
}
}
