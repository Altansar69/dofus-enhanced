#include "instant-combat.hpp"
#include "module.hpp"
#include <intrin.h>

using namespace managed;
namespace {
using PredicateFn = bool(*)(Obj, Method);
PredicateFn originalMonsterAnimation, originalPendingAnimation, originalAppearanceReady;

bool MovementAnimationWait(Obj entity, bool nativeResult, uintptr_t caller, uintptr_t expected) {
    if (!nativeResult || caller - gameBase != expected) return nativeResult;
    try {
        if (InstantWaitsEnabled() && entity && IsCurrentFightEntity(entity)) return false;
    } catch (const std::exception& error) {
        Log(std::string("MOVEMENT_WAIT_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    return nativeResult;
}

bool MonsterAnimationHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return MovementAnimationWait(entity, originalMonsterAnimation(entity, method), caller, 0x26B9E07);
}
bool PendingAnimationHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return MovementAnimationWait(entity, originalPendingAnimation(entity, method), caller, 0x26B9E1F);
}

bool AppearanceReady(Obj entity, bool nativeResult, uintptr_t caller) {
    if (nativeResult || caller - gameBase != 0x205FD43) return nativeResult;
    try {
        if (InstantWaitsEnabled() && entity && IsCurrentFightEntity(entity)) return true;
    } catch (const std::exception& error) {
        Log(std::string("APPEARANCE_WAIT_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    return nativeResult;
}

bool AppearanceReadyHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return AppearanceReady(entity, originalAppearanceReady(entity, method), caller);
}
}

void InstallInstantMovementWaits() {
    auto monster = api.Class("Core.dll", "", "tn");
    auto animated = api.Class("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated");
    api.Native(api.Find(monster, "qgp", 1), 0x1F83E30);
    api.Native(api.Find(animated, "daso", 0), 0x779910);
    api.Native(api.Find(animated, "dbbg", 0), 0x785ED0);
    // duz.uia waits for three animation events only. Returning false here
    // chooses native ujb before listeners are installed; no timer is skipped.
    api.NativeAt(0x26B9E02);
    api.NativeAt(0x26B9E1A);
    // gpj has already applied dasv/dasw. Its native ready branch selects idle
    // and completes without subscribing an animation-end listener. Keep all
    // appearance changes and completion bookkeeping in the original method.
    api.NativeAt(0x205FD3E);
    AddNativeHook(0x1F83E30, reinterpret_cast<void*>(MonsterAnimationHook), reinterpret_cast<void**>(&originalMonsterAnimation));
    AddNativeHook(0x779910, reinterpret_cast<void*>(PendingAnimationHook), reinterpret_cast<void**>(&originalPendingAnimation));
    AddNativeHook(0x785ED0, reinterpret_cast<void*>(AppearanceReadyHook), reinterpret_cast<void**>(&originalAppearanceReady));
}
