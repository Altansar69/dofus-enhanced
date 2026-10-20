#include "instant-combat.hpp"
#include "module.hpp"
#include <algorithm>
#include <deque>
#include <intrin.h>

using namespace managed;
namespace {
using WaitFn = void(*)(Obj, Method);
WaitFn originalWaitStart{};
using DelayCtorFn = void(*)(Obj, int, Method);
DelayCtorFn originalDelayCtor{};
Klass waiterClass{}, animationClass{};
constexpr size_t MaxRemembered = 512;
constexpr ULONGLONG RememberMilliseconds = 120000;
struct Skipped {
    Ref step;
    ULONGLONG when;
    Skipped(Obj value, ULONGLONG at) : step(value), when(at) {}
};
std::deque<Skipped> skipped;
int VisualStaggerDelay(int delay, uintptr_t caller) {
    // fxz.bfva's per-entity visual stagger only. Other delay steps, including
    // all loading waits and watchdogs, retain their duration and scheduler.
    return delay > 0 && caller == gameBase + 0x1E59C24 && InstantCombatEnabled() ? 0 : delay;
}
void DelayCtorHook(Obj step, int delay, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    originalDelayCtor(step, VisualStaggerDelay(delay, caller), method);
}
void Expire(ULONGLONG now) {
    std::erase_if(skipped, [now](const Skipped& item) {
        return now - item.when >= RememberMilliseconds || !item.step.get();
    });
}
struct WaitFrame {
    Obj waiter;
    WaitFrame* previous;
    static thread_local WaitFrame* current;
    explicit WaitFrame(Obj value) : waiter(value), previous(current) { current = this; }
    ~WaitFrame() { current = previous; }
    static bool Contains(Obj value) {
        for (auto frame = current; frame; frame = frame->previous)
            if (frame->waiter == value) return true;
        return false;
    }
};
thread_local WaitFrame* WaitFrame::current = nullptr;
bool WasSkipped(Obj animation) {
    if (!animation || !api.class_is_assignable_from(animationClass, api.object_get_class(animation))) return false;
    Expire(GetTickCount64());
    for (const auto& item : skipped)
        if (item.step.get() == animation)
            return IsCurrentFightEntity(Read<Obj>(animation, 0x78));
    return false;
}
void WaitStartHook(Obj waiter, Method method) {
    if (WaitFrame::Contains(waiter)) return;
    bool finish = false;
    try {
        if (waiter && InstantCombatEnabled() && api.object_get_class(waiter) == waiterClass) {
            if (Read<bool>(waiter, 0x1D)) return;
            finish = WasSkipped(Read<Obj>(waiter, 0x40));
        }
    } catch (const std::exception& error) {
        Log(std::string("INSTANT_WAIT_MEMBERSHIP_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    if (!finish) { originalWaitStart(waiter, method); return; }
    WaitFrame completing(waiter);
    // Native event-seen branch removes this waiter's own listener, clears its
    // gpy reference, then completes through the native sequence callback chain.
    *reinterpret_cast<bool*>(static_cast<char*>(waiter) + 0x48) = true;
    try { originalWaitStart(waiter, method); }
    catch (...) {
        if (module) module->stopRequested.store(true);
        throw; // A callback may have advanced the sequence: never retry it.
    }
}
}

void RememberSkippedAnimation(Obj step) {
    if (!step || !InstantCombatEnabled() ||
        !api.class_is_assignable_from(animationClass, api.object_get_class(step))) return;
    const auto now = GetTickCount64();
    Expire(now);
    for (const auto& item : skipped) if (item.step.get() == step) return;
    if (skipped.size() >= MaxRemembered) skipped.pop_front();
    skipped.emplace_back(step, now);
}
void ClearInstantWaits() { skipped.clear(); ClearScriptWaits(); ClearCombatCleanup(); }
void PumpInstantWaits(bool inCombat) {
    PumpCombatCleanup(inCombat);
    PumpScriptWaits(inCombat);
    // Cleanup-dependent animations must remain known across animation-mode
    // changes; they may have been queued before Instantané was selected.
    if (!inCombat || !InstantCombatEnabled()) { skipped.clear(); ClearScriptWaits(); return; }
    Expire(GetTickCount64());
}
void InstallInstantWaits() {
    waiterClass = api.Class("Core.dll", "", "gqf");
    animationClass = api.Class("Core.dll", "", "gpy");
    api.Native(api.Find(waiterClass, "uia", 0), 0x208A020);
    AddNativeHook(0x208A020, reinterpret_cast<void*>(WaitStartHook),
                  reinterpret_cast<void**>(&originalWaitStart));
    auto delay = api.Class("Core.dll", "", "gqg");
    api.Native(api.Find(delay, ".ctor", 1), 0x208A1A0);
    api.NativeAt(0x1E59C1F); // Exact constructor call in the verified visual path.
    AddNativeHook(0x208A1A0, reinterpret_cast<void*>(DelayCtorHook),
                  reinterpret_cast<void**>(&originalDelayCtor));
}
