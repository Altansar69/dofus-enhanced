#include "map-transitions.hpp"
#include "module.hpp"
#include <cmath>
#include <intrin.h>

namespace {
using namespace managed;
// These are the DOColor calls in MapTransition.ugs/ugt's sprite-fade branches.
constexpr uintptr_t FadeOutCall = 0x2696DFD, FadeInCall = 0x26971F8;
constexpr float FastFadeSeconds = 0.01f;
struct Color4 { float r, g, b, a; };
static_assert(sizeof(Color4) == 16);
// Windows x64 passes the native 16-byte Color value by address. Preserve that
// address, sprite, returned tween and MethodInfo; only duration may change.
using ColorFn = Obj(*)(Obj, const Color4*, float, Method);
ColorFn originalColor = nullptr;
bool reported = false;
bool loading = false;
ULONGLONG loadingSince = 0;
bool SessionReady() {
    return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
void ObserveFade(uintptr_t caller) {
    if (caller != gameBase + FadeOutCall + 5 && caller != gameBase + FadeInCall + 5) return;
    if (!SessionReady()) { loading = false; return; }
    try { if (module->InCombat()) { loading = false; return; } }
    catch (const std::exception&) { loading = false; return; }
    const auto now = GetTickCount64();
    if (caller == gameBase + FadeOutCall + 5) {
        loading = true; loadingSince = now;
    } else if (loading) {
        Log("MAP_TRANSITION_READY fadeWindowMs=" + std::to_string(now - loadingSince));
        loading = false;
    }
}
float AdjustDuration(float seconds, uintptr_t caller) {
    const char* phase = caller == gameBase + FadeOutCall + 5 ? "out" :
                        caller == gameBase + FadeInCall + 5 ? "in" : nullptr;
    if (!phase || !std::isfinite(seconds) || seconds <= FastFadeSeconds ||
        !module || !module->ready || !module->servicesReady || module->stopped ||
        module->stopRequested || !module->settings.fastMapTransitions) return seconds;
    try {
        if (module->InCombat()) return seconds;
    } catch (const std::exception& error) {
        if (!reported) Log(std::string("MAP_TRANSITION_STATE_ERROR ") + error.what());
        reported = true;
        return seconds;
    }
    reported = false;
    Log(std::string("MAP_TRANSITION_FADE phase=") + phase + " nativeSeconds=" +
        std::to_string(seconds) + " seconds=0.010");
    return FastFadeSeconds;
}
Obj DispatchColor(Obj sprite, const Color4* color, float seconds, Method method, uintptr_t caller) {
    // Native OnComplete handlers are attached by the original caller, normally.
    // No forced completion, skipped asset wait or early map acknowledgement.
    const auto tween = originalColor(sprite, color, AdjustDuration(seconds, caller), method);
    if (tween && std::isfinite(seconds) && seconds >= 0) ObserveFade(caller);
    return tween;
}
Obj ColorHook(Obj sprite, const Color4* color, float seconds, Method method) {
    return DispatchColor(sprite, color, seconds, method, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
}
bool MapTransitionLoading() {
    if (!loading) return false;
    if (!SessionReady() || GetTickCount64() - loadingSince >= 5000) { loading = false; return false; }
    try { if (module->InCombat()) loading = false; }
    catch (const std::exception&) { loading = false; }
    return loading;
}
void InstallFastMapTransitions() {
    api.NativeAt(FadeOutCall);
    api.NativeAt(FadeInCall);
    auto type = api.Class("DOTween.Modules.dll", "DG.Tweening", "DOTweenModuleSprite");
    AddHook(api.FindVerified(type, "DOColor", 3, 0x30AAEC0), 0x30AAEC0,
        reinterpret_cast<void*>(&ColorHook), reinterpret_cast<void**>(&originalColor));
}
