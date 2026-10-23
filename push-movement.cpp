#include "instant-combat.hpp"
#include "module.hpp"

using namespace managed;

namespace {
// The three explicit-layout EntityMovementParam unions occupy eight bytes
// each. The float speed remains in XMM2 under the Windows x64 ABI.
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using PositionFn = void(*)(Obj, int, int, float, Method);
using NextFn = void(*)(Obj, Obj, Method);
StartFn originalStart;
PositionFn finishCell;
NextFn nextCell;
Klass pushClass, routeClass;
constexpr int maxSegments = 560;

struct ActivePush {
    Obj entity;
    ActivePush* previous;
    bool reentered = false;
    static thread_local ActivePush* current;
    explicit ActivePush(Obj value) : entity(value), previous(current) { current = this; }
    ~ActivePush() { current = previous; }
    static bool ObserveReentry(Obj value) {
        unsigned depth = 0;
        bool blocked = false;
        for (auto frame = current; frame; frame = frame->previous) {
            if (frame->entity == value) { frame->reentered = true; blocked = true; }
            if (++depth >= 32) blocked = true;
        }
        return blocked;
    }
};
thread_local ActivePush* ActivePush::current = nullptr;

Obj FindState(Obj dictionary, Method lookup, int64_t id) {
    Obj state = nullptr;
    return api.Value<bool>(api.Invoke(lookup, dictionary, {&id, &state})) ? state : nullptr;
}

bool SameState(Obj behavior, Obj dictionary, Method lookup, int64_t id,
               Obj state, Obj entity, Obj route, Obj callback, int easing) {
    return InstantMovementEnabled() && IsCurrentFightEntity(entity) &&
        Read<int>(behavior, 0x38) == 6 && Read<Obj>(behavior, 0x28) == dictionary &&
        FindState(dictionary, lookup, id) == state && Read<int>(state, 0x10) == easing &&
        Read<Obj>(state, 0x28) == entity && Read<Obj>(state, 0x48) == route &&
        Read<Obj>(state, 0x40) == callback;
}

struct PushFinish {
    Ref behavior, entity, route, callback, dictionary, state;
    Method lookup;
    int64_t id;
    int segmentCount, easing;
    PushFinish(Obj behaviorValue, Obj entityValue, Obj routeValue, Obj callbackValue,
               Obj dictionaryValue, Obj stateValue, Method lookupValue, int64_t idValue,
               int countValue, int easingValue)
        : behavior(behaviorValue), entity(entityValue), route(routeValue), callback(callbackValue),
          dictionary(dictionaryValue), state(stateValue), lookup(lookupValue), id(idValue),
          segmentCount(countValue), easing(easingValue) {}
};

void FinishPush(const std::shared_ptr<PushFinish>& context) {
    try {
        auto behavior = context->behavior.get(), entity = context->entity.get();
        auto route = context->route.get(), callback = context->callback.get();
        auto dictionary = context->dictionary.get(), state = context->state.get();
        ActivePush active(entity);
        for (int remaining = context->segmentCount + 2; remaining > 0; --remaining) {
            if (active.reentered || !SameState(behavior, dictionary, context->lookup, context->id,
                                               state, entity, route, callback, context->easing)) return;
            // Exact terminal-progress operations from xy.Update:
            // 2620DE4 sets progress=1; 2620E0C calls rfv; 2620E2D calls rkv.
            // xy has no tween to kill and does not turn the pushed character.
            *reinterpret_cast<float*>(static_cast<char*>(state) + 0x34) = 1.0f;
            finishCell(entity, Read<int>(entity, 0x348), Read<int>(state, 0x38), 1.0f, nullptr);
            if (active.reentered || !SameState(behavior, dictionary, context->lookup, context->id,
                                               state, entity, route, callback, context->easing)) return;
            // rkv chooses the next cell, or rkz removes the state and invokes
            // the original end callback before publishing movement-end.
            nextCell(behavior, state, nullptr);
        }
        // Any unfinished unusual route remains registered for native Update.
    } catch (const std::exception& error) {
        Log(std::string("PUSH_DRAIN_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw;
    }
}

void StartHook(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
               uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    int segmentCount = -1;
    const bool nested = ActivePush::ObserveReentry(entity);
    const auto parameter = static_cast<int32_t>(first);
    const int easing = parameter == 0 ? 1 : parameter;
    try {
        if (InstantMovementEnabled() && behavior && entity && route && !nested &&
            api.object_get_class(behavior) == pushClass && Read<int>(behavior, 0x38) == 6 &&
            api.object_get_class(route) == routeClass && (easing == 1 || easing == 2) &&
            IsCurrentFightEntity(entity)) {
            auto points = Read<Obj>(route, 0x20);
            if (points) {
                const int count = Read<int>(points, 0x18);
                if (count >= 0 && count <= maxSegments) segmentCount = count;
            }
        }
    } catch (const std::exception& error) {
        Log(std::string("PUSH_ELIGIBILITY_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }

    // Start notifications and immediate native completion can themselves
    // start a movement. Guard the whole original call, not only the drain.
    ActivePush active(entity);
    originalStart(behavior, entity, speed, route, callback, first, second, third, entitySpeed, method);
    if (segmentCount < 0 || active.reentered) return;

    try {
        auto dictionary = Read<Obj>(behavior, 0x28);
        if (!dictionary) return;
        auto lookup = api.Find(api.object_get_class(dictionary), "TryGetValue", 2);
        const auto id = Read<int64_t>(entity, 0x1C8);
        auto state = FindState(dictionary, lookup, id);
        if (!state || Read<Obj>(state, 0x28) != entity || Read<Obj>(state, 0x48) != route ||
            Read<Obj>(state, 0x40) != callback || Read<int>(state, 0x10) != easing) return;
        auto context = std::make_shared<PushFinish>(behavior, entity, route, callback, dictionary, state,
                                                  lookup, id, segmentCount, easing);
        // Capture this exact xw now, but finish only after native rfw has run
        // child/follower propagation and the entity's +358 start notification.
        // A rejected queue request leaves the registered movement fully native.
        DeferInstantMovement([context] { FinishPush(context); });
    } catch (const std::exception& error) {
        Log(std::string("PUSH_CAPTURE_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw;
    }
    // Never call originalStart again: cell/end callbacks may have run already.
}
}

void InstallInstantPushMovement() {
    pushClass = api.Class("Core.dll", "", "xy");
    routeClass = api.Class("Core.dll", "", "enw");
    auto entityClass = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    api.Native(api.Find(pushClass, "rjn", 8), 0x2646B60);
    finishCell = reinterpret_cast<PositionFn>(api.Native(api.Find(entityClass, "rfv", 3), 0x2616050));
    nextCell = reinterpret_cast<NextFn>(api.Native(api.Find(pushClass, "rkv", 1), 0x2647140));
    AddNativeHook(0x2646B60, reinterpret_cast<void*>(StartHook), reinterpret_cast<void**>(&originalStart));
}
