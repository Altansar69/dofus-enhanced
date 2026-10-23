#include "animations/instant-combat.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <intrin.h>
#include <vector>

using namespace managed;

namespace {
// gpy(Animated, string, bool, bool, mhp, int, string, bool, frv, bool).
// Keep every constructor argument, including the trailing MethodInfo.
using AnimationCtorFn = void(*)(Obj, Obj, Obj, bool, bool, int, int, Obj, bool, Obj, bool, Method);
AnimationCtorFn originalAnimationCtor;
constexpr uintptr_t DeathAnimationCtorReturn = 0x26CFAA9;
constexpr size_t MaxRetainedAnimations = 512;
struct RecordedAnimation {
    Ref step;
    bool death;
    RecordedAnimation(Obj value, bool isDeath) : step(value), death(isDeath) {}
};
std::vector<RecordedAnimation> animations;

void RemoveCompletedAnimations() {
    std::erase_if(animations, [](const RecordedAnimation& item) {
        auto step = item.step.get();
        return !step || Read<bool>(step, 0x1D);
    });
}

void AnimationCtorAt(Obj step, Obj entity, Obj name, bool idleAfter, bool wait,
                     int eventMode, int repetitions, Obj suffix, bool orient,
                     Obj services, bool extra, Method method, uintptr_t caller) {
    originalAnimationCtor(step, entity, name, idleAfter, wait, eventMode, repetitions,
                          suffix, orient, services, extra, method);
    if (!step || !module || module->stopped.load() || module->stopRequested.load()) return;
    try {
        // This single call site constructs dwp's first sequence step after
        // subscribing dwp.uko to Animated+238. Skipping that step would bypass
        // its rendered-entity disposal; gpa.blyf does not emit Animated+238.
        // Capture every constructor in every mode. A step constructed before
        // hook installation has unknown provenance and must also stay native.
        const bool death = caller == gameBase + DeathAnimationCtorReturn;
        RemoveCompletedAnimations();
        for (auto& item : animations) {
            if (item.step.get() == step) { item.death = death; return; }
        }
        if (animations.size() >= MaxRetainedAnimations)
            throw std::runtime_error("Too many pending animation provenance records");
        animations.emplace_back(step, death);
    } catch (const std::exception& error) {
        Log(std::string("COMBAT_CLEANUP_TRACKING_ERROR ") + error.what());
        module->stopRequested.store(true);
    } catch (...) {
        module->stopRequested.store(true);
        throw;
    }
    // Never retry the constructor: it already subscribed its disposal callback.
}

void AnimationCtorHook(Obj step, Obj entity, Obj name, bool idleAfter, bool wait,
                       int eventMode, int repetitions, Obj suffix, bool orient,
                       Obj services, bool extra, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    AnimationCtorAt(step, entity, name, idleAfter, wait, eventMode, repetitions,
                    suffix, orient, services, extra, method, caller);
}
}

AnimationStepKind ClassifyAnimationStep(Obj step) {
    if (!step) return AnimationStepKind::Unknown;
    for (const auto& item : animations)
        if (item.step.get() == step) return item.death ? AnimationStepKind::Death : AnimationStepKind::Ordinary;
    return AnimationStepKind::Unknown; // The step may have been constructed before hooks were installed.
}

void ClearCombatCleanup() { animations.clear(); }

void PumpCombatCleanup(bool inCombat) {
    // A temporary toggle change must not discard provenance of pending steps.
    if (!inCombat || !module || module->stopped.load()) { ClearCombatCleanup(); return; }
    RemoveCompletedAnimations();
}

void InstallCombatCleanup() {
    api.NativeAt(0x26CFAA4); // Exact dwp.uia constructor call, not a shared ctor alias.
    auto animationClass = api.Class("Core.dll", "", "gpy");
    api.Native(api.Find(animationClass, ".ctor", 10), 0x2081890);
    AddNativeHook(0x2081890, reinterpret_cast<void*>(AnimationCtorHook),
                  reinterpret_cast<void**>(&originalAnimationCtor));
}
