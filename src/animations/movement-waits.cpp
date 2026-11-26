#include "animations/instant-combat.hpp"
#include "core/module.hpp"
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
    return MovementAnimationWait(entity, originalMonsterAnimation(entity, method), caller, 0x2674FD7);
}
bool PendingAnimationHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return MovementAnimationWait(entity, originalPendingAnimation(entity, method), caller, 0x2674FEF);
}

bool AppearanceReady(Obj entity, bool nativeResult, uintptr_t caller) {
    if (nativeResult || caller - gameBase != 0x2045C53) return nativeResult;
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
    api.Native(api.Find(monster, "qgp", 1), 0x1F4AB60);
    api.Native(api.Find(animated, "daso", 0), 0x773090);
    api.Native(api.Find(animated, "dbbg", 0), 0x77F820);
    // duz.uia waits for three animation events only. Returning false here
    // chooses native ujb before listeners are installed; no timer is skipped.
    api.NativeAt(0x2674FD2);
    api.NativeAt(0x2674FEA);
    // gpj has already applied dasv/dasw. Its native ready branch selects idle
    // and completes without subscribing an animation-end listener. Keep all
    // appearance changes and completion bookkeeping in the original method.
    api.NativeAt(0x2045C4E);
    AddNativeHook(0x1F4AB60, reinterpret_cast<void*>(MonsterAnimationHook), reinterpret_cast<void**>(&originalMonsterAnimation));
    AddNativeHook(0x773090, reinterpret_cast<void*>(PendingAnimationHook), reinterpret_cast<void**>(&originalPendingAnimation));
    AddNativeHook(0x77F820, reinterpret_cast<void*>(AppearanceReadyHook), reinterpret_cast<void**>(&originalAppearanceReady));
}
