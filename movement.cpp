#include "instant-combat.hpp"
#include "module.hpp"
#include <array>
#include <intrin.h>
#include <memory>

using namespace managed;

namespace {
// EntityMovementParam is an explicit-layout, eight-byte union. Preserve all
// three complete values, the float register, callback and optional EntitySpeed.
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using PositionFn = void(*)(Obj, int, int, float, Method);
using NextFn = void(*)(Obj, Obj, bool, Method);
using FinishFn = void(*)(Obj, Obj, Method);
using AppearanceFn = int(*)(Obj, Method);
using LandingFn = bool(*)(int, Method);
StartFn originalStart;
PositionFn finishCell;
NextFn originalNext;
FinishFn originalFinish;
AppearanceFn appearanceBone;
LandingFn originalLanding;
Klass routeClass, walkClass, stateClass;
constexpr int maxSegments = 560;

struct Draining {
    Obj behavior, entity, route, callback, state;
    bool allowLanding;
    int64_t id;
    bool committed = false;
    Draining* previous;
    bool reentered = false;
    static thread_local Draining* current;
    Draining(Obj owner, Obj value, Obj path, Obj end, bool landing, Obj expected = nullptr, int64_t entityId = 0)
        : behavior(owner), entity(value), route(path), callback(end), state(expected),
          allowLanding(landing), id(entityId), previous(current) { current = this; }
    ~Draining() { current = previous; }
    static bool Busy(Obj value) {
        unsigned depth = 0;
        bool busy = false;
        for (auto frame = current; frame; frame = frame->previous) {
            if (frame->entity == value) { frame->reentered = true; busy = true; }
            if (++depth >= 32) busy = true;
        }
        return busy;
    }
};
thread_local Draining* Draining::current = nullptr;

Obj FindState(Obj dictionary, Method lookup, int64_t id) {
    Obj state = nullptr;
    return api.Value<bool>(api.Invoke(lookup, dictionary, {&id, &state})) ? state : nullptr;
}

bool OrdinaryBehavior(Obj behavior, Obj entity, bool allowLanding) {
    const int kind = Read<int>(behavior, 0x48);
    if (kind == 2 || kind == 3 || kind == 7) return true;
    if (kind != 5) return false;
    // A known rfw transaction may omit a landing not yet engaged by native
    // start. Unknown callers retain their original landing behavior.
    if (allowLanding) return true;
    const int bone = appearanceBone(entity, nullptr);
    return bone >= 0 && !originalLanding(bone, nullptr);
}

struct LandingScope {
    bool allowed;
    Draining* frame;
    LandingScope* previous;
    static thread_local LandingScope* current;
    explicit LandingScope(bool value) : allowed(value), frame(Draining::current), previous(current) { current = this; }
    ~LandingScope() { current = previous; }
};
thread_local LandingScope* LandingScope::current = nullptr;

bool AllowLandingOverride(Obj behavior, Obj state) {
    const auto frame = Draining::current;
    if (!frame || !frame->allowLanding || frame->reentered || behavior != frame->behavior || !state ||
        api.object_get_class(state) != stateClass || Read<int>(behavior, 0x48) != 5 ||
        (frame->state && state != frame->state) || Read<Obj>(state, 0x20) != frame->entity ||
        Read<int64_t>(frame->entity, 0x1C8) != frame->id ||
        Read<Obj>(state, 0x38) != frame->route || Read<Obj>(state, 0x30) != frame->callback) return false;
    if (!frame->committed && (!InstantMovementEnabled() || !IsCurrentFightEntity(frame->entity) ||
        Read<bool>(frame->entity, 0xB8))) return false;
    const auto dictionary = Read<Obj>(behavior, 0x38);
    if (!dictionary) return false;
    const auto lookup = api.Find(api.object_get_class(dictionary), "TryGetValue", 2);
    return FindState(dictionary, lookup, frame->id) == state;
}
bool CheckedLandingOverride(Obj behavior, Obj state) {
    try { return AllowLandingOverride(behavior, state); }
    catch (const std::exception& error) {
        Log(std::string("LANDING_SCOPE_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
        return false;
    }
}
bool LandingAt(int bone, Method method, uintptr_t caller) {
    const auto scope = LandingScope::current;
    if (scope && scope->allowed &&
        (caller == gameBase + 0x263F03C || caller == gameBase + 0x263FB17)) {
        if (caller == gameBase + 0x263F03C) scope->frame->committed = true;
        return false;
    }
    return originalLanding(bone, method);
}
bool LandingHook(int bone, Method method) {
    return LandingAt(bone, method, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
void NextHook(Obj behavior, Obj state, bool restart, Method method) {
    // Every invocation masks the enclosing permission, including other
    // entities with the same appearance, stale states and non-combat calls.
    LandingScope scope(CheckedLandingOverride(behavior, state));
    originalNext(behavior, state, restart, method);
}
void FinishHook(Obj behavior, Obj state, Method method) {
    LandingScope scope(CheckedLandingOverride(behavior, state));
    originalFinish(behavior, state, method);
}

bool SameState(Obj behavior, Obj dictionary, Method lookup, int64_t id,
               Obj state, Obj entity, Obj route, Obj callback, const Draining& frame) {
    return (frame.committed || (InstantMovementEnabled() && IsCurrentFightEntity(entity) &&
            (!frame.allowLanding || !Read<bool>(entity, 0xB8)))) &&
        OrdinaryBehavior(behavior, entity, frame.allowLanding) && Read<int64_t>(entity, 0x1C8) == id &&
        Read<Obj>(behavior, 0x38) == dictionary &&
        FindState(dictionary, lookup, id) == state &&
        Read<Obj>(state, 0x20) == entity && Read<Obj>(state, 0x38) == route && Read<Obj>(state, 0x30) == callback;
}

void StartHook(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
               uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    int segmentCount = -1;
    bool allowLanding = false;
    const bool busy = Draining::Busy(entity);
    try {
        if (InstantMovementEnabled() && behavior && entity && route && !busy &&
            api.object_get_class(behavior) == walkClass && api.object_get_class(route) == routeClass &&
            IsCurrentFightEntity(entity)) {
            // Slides, jumps and carried-character throws use other classes.
            allowLanding = Read<int>(behavior, 0x48) == 5 && CanDeferInstantMovement();
            if (OrdinaryBehavior(behavior, entity, allowLanding)) {
                auto points = Read<Obj>(route, 0x20);
                if (points) {
                    const int count = Read<int>(points, 0x18);
                    if (count >= 0 && count <= maxSegments) segmentCount = count;
                }
            }
        }
    } catch (const std::exception& error) {
        Log(std::string("MOVEMENT_ELIGIBILITY_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }

    // Native start creates and registers the movement state and sends the
    // normal start notification. It must run exactly once, even on fallback.
    // Keep rjn entirely native. An already engaged landing has entity+B8 set
    // and will be excluded before deferred acceleration starts.
    Draining starting(behavior, entity, route, callback, false);
    originalStart(behavior, entity, speed, route, callback, first, second, third, entitySpeed, method);
    if (segmentCount < 0 || starting.reentered) return;

    try {
        auto dictionary = Read<Obj>(behavior, 0x38);
        if (!dictionary) return;
        auto lookup = api.Find(api.object_get_class(dictionary), "TryGetValue", 2);
        const auto id = Read<int64_t>(entity, 0x1C8);
        auto state = FindState(dictionary, lookup, id);
        if (!state || Read<Obj>(state, 0x20) != entity || Read<Obj>(state, 0x38) != route ||
            Read<Obj>(state, 0x30) != callback) return;
        auto held = std::make_shared<std::array<Ref, 5>>();
        (*held)[0].reset(behavior); (*held)[1].reset(entity); (*held)[2].reset(route);
        (*held)[3].reset(dictionary); (*held)[4].reset(state);
        DeferInstantMovement([=, held = std::move(held)] {
          (void)held;
          Draining draining(behavior, entity, route, callback, allowLanding, state, id);
          for (int remaining = segmentCount + 2; remaining > 0; --remaining) {
            if (draining.reentered) return;
            if (!SameState(behavior, dictionary, lookup, id, state, entity, route, callback, draining)) return;
            // These are the exact terminal-progress operations in xq.Update
            // (2616D99..2616DE1). Native rfv updates cell registration, hitboxes
            // and position; rjr consumes the next route segment or calls rjw.
            *reinterpret_cast<float*>(static_cast<char*>(state) + 0x28) = 1.0f;
            finishCell(entity, Read<int>(entity, 0x348), Read<int>(state, 0x2C), 1.0f, nullptr);
            // Cell notifications can replace/cancel this movement synchronously.
            if (!SameState(behavior, dictionary, lookup, id, state, entity, route, callback, draining)) return;
            NextHook(behavior, state, true, nullptr);
            // rjw removes the state before its callback. Never complete it a
            // second time or consume a new movement started by that callback.
          }
        });
        // An unusual route can continue through the ordinary native Update;
        // the bound never forces completion or changes global clocks/speeds.
    } catch (const std::exception& error) {
        Log(std::string("MOVEMENT_DRAIN_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
        // A failed host-side validation leaves any still-registered movement
        // available to the game's ordinary Update. Never restart it.
    } catch (...) {
        if (module) module->stopRequested.store(true);
        // A cell/end callback may already have run. Never restart the movement.
        throw;
    }
}
}

void InstallInstantMovement() {
    walkClass = api.Class("Core.dll", "", "xq");
    routeClass = api.Class("Core.dll", "", "enw");
    stateClass = api.NestedClass(walkClass, "xm");
    auto entityClass = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    auto animatedClass = api.Class("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated");
    auto appearancesClass = api.Class("Ankama.Dofus.Core.Characters.dll", "", "mig");
    api.Native(api.Find(walkClass, "rjn", 8), 0x263E860);
    finishCell = reinterpret_cast<PositionFn>(api.Native(api.Find(entityClass, "rfv", 3), 0x2616050));
    api.Native(api.Find(walkClass, "rjr", 2), 0x263EE10);
    api.Native(api.Find(walkClass, "rjw", 1), 0x263FA00);
    appearanceBone = reinterpret_cast<AppearanceFn>(api.Native(api.Find(animatedClass, "daxp", 0), 0x781A20));
    api.Native(api.Find(appearancesClass, "dbdk", 1), 0x78F5D0);
    api.NativeAt(0x263F037);
    api.NativeAt(0x263FB12);
    AddNativeHook(0x78F5D0, reinterpret_cast<void*>(LandingHook), reinterpret_cast<void**>(&originalLanding));
    AddNativeHook(0x263EE10, reinterpret_cast<void*>(NextHook), reinterpret_cast<void**>(&originalNext));
    AddNativeHook(0x263FA00, reinterpret_cast<void*>(FinishHook), reinterpret_cast<void**>(&originalFinish));
    AddNativeHook(0x263E860, reinterpret_cast<void*>(StartHook), reinterpret_cast<void**>(&originalStart));
}
