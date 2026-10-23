#include "animations/instant-combat.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <deque>
#include <intrin.h>

using namespace managed;
namespace {
using ScriptCtorFn = void(*)(Obj, Obj, Method);
using ScriptStepFn = void(*)(Obj, Method);
ScriptCtorFn originalScriptCtor{};
ScriptStepFn originalScriptStart{}, completeScriptWait{};
Klass scriptWaitClass{}, scriptFightClass{}, scriptContextClass{};
constexpr size_t MaxScriptWaits = 512;
constexpr ULONGLONG ScriptWaitLifetime = 120000;
struct ScriptWait {
    Ref step, context, fight;
    ULONGLONG when;
    bool consumed = false;
    ScriptWait(Obj s, Obj c, Obj f, ULONGLONG now) : step(s), context(c), fight(f), when(now) {}
};
std::deque<ScriptWait> scriptWaits;

void ExpireScriptWaits(ULONGLONG now) {
    std::erase_if(scriptWaits, [now](const ScriptWait& item) {
        return now - item.when >= ScriptWaitLifetime || !item.step.get();
    });
}
bool ScriptWaitCaller(uintptr_t caller) {
    switch (caller - gameBase) {
    case 0x2740093: // SpellScriptRunner.uwu, absent script data fallback.
    case 0x27401A0: // SpellScriptRunner.uwv, same fallback.
    case 0x274164F: // dzh.uxc, dedicated visual pause step.
        return true;
    default: return false;
    }
}
Obj CurrentScriptFight(Obj context) {
    if (!context || !api.class_is_assignable_from(scriptContextClass, api.object_get_class(context)) ||
        !module || !module->fight.get()) return nullptr;
    auto state = Read<Obj>(module->fight.get(), 0x18);
    return state && api.object_get_class(state) == scriptFightClass &&
        Read<Obj>(state, 0xE0) == context ? state : nullptr;
}
void RememberScriptWait(Obj step, Obj context, uintptr_t caller) {
    if (!ScriptWaitCaller(caller) || !step || !InstantWaitsEnabled() ||
        api.object_get_class(step) != scriptWaitClass || Read<Obj>(step, 0x40) != context) return;
    auto fight = CurrentScriptFight(context);
    if (!fight) return;
    const auto now = GetTickCount64();
    ExpireScriptWaits(now);
    for (const auto& item : scriptWaits) if (item.step.get() == step) return;
    if (scriptWaits.size() >= MaxScriptWaits) scriptWaits.pop_front();
    scriptWaits.emplace_back(step, context, fight, now);
}
void ScriptCtorHook(Obj step, Obj context, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    originalScriptCtor(step, context, method);
    try { RememberScriptWait(step, context, caller); }
    catch (const std::exception& error) {
        Log(std::string("SCRIPT_WAIT_TRACKING_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
}
struct CompletingScriptWait {
    Obj step;
    CompletingScriptWait* previous;
    static thread_local CompletingScriptWait* current;
    explicit CompletingScriptWait(Obj value) : step(value), previous(current) { current = this; }
    ~CompletingScriptWait() { current = previous; }
    static bool Contains(Obj value) {
        for (auto frame = current; frame; frame = frame->previous)
            if (frame->step == value) return true;
        return false;
    }
};
thread_local CompletingScriptWait* CompletingScriptWait::current = nullptr;
void ScriptStartHook(Obj step, Method method) {
    if (CompletingScriptWait::Contains(step)) return;
    bool finish = false;
    Ref heldStep;
    try {
        if (step && InstantWaitsEnabled() && api.object_get_class(step) == scriptWaitClass) {
            if (Read<bool>(step, 0x1D)) return;
            // Never intervene after the native pause has scheduled its timer.
            if (!Read<Obj>(step, 0x30) && !Read<Obj>(step, 0x38)) {
                ExpireScriptWaits(GetTickCount64());
                for (auto& item : scriptWaits) {
                    if (item.step.get() != step) continue;
                    if (item.consumed) return;
                    auto context = item.context.get();
                    finish = Read<Obj>(step, 0x40) == context && CurrentScriptFight(context) == item.fight.get();
                    if (finish) {
                        heldStep.reset(step); // Native callbacks can clear the history.
                        item.consumed = true;
                    }
                    break;
                }
            }
        }
    } catch (const std::exception& error) {
        finish = false;
        Log(std::string("SCRIPT_WAIT_VALIDATION_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    if (!finish) { originalScriptStart(step, method); return; }
    CompletingScriptWait completing(step);
    try {
        // This native end path clears timer/stopwatch, calls ujq (not blye),
        // marks completed, and dispatches the sequence callback. A zero delay
        // would instead leave gpa.blyg without any completion callback.
        completeScriptWait(step, nullptr);
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw; // Completion may already have advanced a sequence.
    }
}
}

void ClearScriptWaits() { scriptWaits.clear(); }
void PumpScriptWaits(bool inCombat) {
    if (!inCombat || !InstantWaitsEnabled()) { ClearScriptWaits(); return; }
    ExpireScriptWaits(GetTickCount64());
}
void InstallScriptWaits() {
    scriptWaitClass = api.Class("Core.dll", "", "gqe");
    scriptFightClass = api.Class("Core.dll", "", "fyb");
    scriptContextClass = api.Class("Core.dll", "", "fws");
    api.Native(api.Find(scriptWaitClass, ".ctor", 1), 0x2089970);
    api.Native(api.Find(scriptWaitClass, "uia", 0), 0x2089A50);
    completeScriptWait = reinterpret_cast<ScriptStepFn>(api.Native(api.Find(scriptWaitClass, "blye", 0), 0x20899A0));
    for (auto rva : {0x274008E, 0x274019B, 0x274164A}) api.NativeAt(rva);
    AddNativeHook(0x2089970, reinterpret_cast<void*>(ScriptCtorHook), reinterpret_cast<void**>(&originalScriptCtor));
    AddNativeHook(0x2089A50, reinterpret_cast<void*>(ScriptStartHook), reinterpret_cast<void**>(&originalScriptStart));
}
