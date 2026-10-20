#include "instant-combat.hpp"
#include "module.hpp"
#include <array>
#include <memory>

using namespace managed;

namespace {
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using StateFn = void(*)(Obj, Method);
using FinishFn = void(*)(Obj, Obj, Method);
StartFn originalStart;
StateFn stopFlight;
FinishFn arrive, originalFinish, cancel;
Klass throwClass, routeClass, stateClass;

struct Starting {
    Obj entity;
    bool reentered = false;
    Starting* previous;
    static thread_local Starting* current;
    explicit Starting(Obj value) : entity(value), previous(current) { current = this; }
    ~Starting() { current = previous; }
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
thread_local Starting* Starting::current = nullptr;

Obj FindState(Obj dictionary, Method lookup, int64_t id) {
    Obj state = nullptr;
    return api.Value<bool>(api.Invoke(lookup, dictionary, {&id, &state})) ? state : nullptr;
}

struct Finishing {
    Obj behavior, dictionary, state, entity, route, callback;
    Method lookup, contains;
    int64_t id;
    Finishing* previous;
    static thread_local Finishing* current;
    Finishing(Obj b, Obj d, Obj s, Obj e, Obj r, Obj cb, Method find, Method has, int64_t value)
        : behavior(b), dictionary(d), state(s), entity(e), route(r), callback(cb),
          lookup(find), contains(has), id(value), previous(current) { current = this; }
    ~Finishing() { current = previous; }
    bool OwnsState() const {
        return Read<Obj>(behavior, 0x20) == dictionary && FindState(dictionary, lookup, id) == state &&
            Read<int64_t>(entity, 0x1C8) == id && Read<int64_t>(state, 0x10) == id &&
            Read<Obj>(state, 0x18) == entity && Read<Obj>(state, 0x38) == route &&
            Read<Obj>(state, 0x40) == callback;
    }
    bool Cancelled() const {
        auto pending = Read<Obj>(behavior, 0x28);
        if (!pending) throw std::runtime_error("Missing throw cancellation registry");
        auto value = id;
        return api.Value<bool>(api.Invoke(contains, pending, {&value}));
    }
};
thread_local Finishing* Finishing::current = nullptr;

struct WithoutBounce {
    Obj state;
    bool previous;
    explicit WithoutBounce(Obj value) : state(value), previous(Read<bool>(value, 0x30)) {
        *reinterpret_cast<bool*>(static_cast<char*>(state) + 0x30) = false;
    }
    ~WithoutBounce() {
        // ye is freshly allocated by rjn, not pooled. Only this held object is
        // restored, even if its ID now belongs to a different movement.
        *reinterpret_cast<bool*>(static_cast<char*>(state) + 0x30) = previous;
    }
};

void FinishHook(Obj behavior, Obj state, Method method) {
    for (auto frame = Finishing::current; frame; frame = frame->previous) {
        if (frame->behavior != behavior || frame->state != state) continue;
        try {
            // rlz -> qgb can synchronously notify a replacement/cancellation.
            // Native rmb/rma remove by ID, so never let them remove a new state.
            if (!frame->OwnsState()) return;
            if (frame->Cancelled()) cancel(behavior, state, nullptr);
            else originalFinish(behavior, state, method);
        } catch (const std::exception& error) {
            Log(std::string("THROW_FINISH_ERROR ") + error.what());
            if (module) module->stopRequested.store(true);
        } catch (...) {
            if (module) module->stopRequested.store(true);
            throw;
        }
        return;
    }
    originalFinish(behavior, state, method);
}

void StartHook(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
               uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    bool eligible = false;
    const bool busy = Starting::Busy(entity);
    try {
        eligible = !busy && InstantCombatEnabled() && behavior && entity && route &&
            api.object_get_class(behavior) == throwClass && api.object_get_class(route) == routeClass &&
            IsCurrentFightEntity(entity);
    } catch (const std::exception& error) {
        Log(std::string("THROW_ELIGIBILITY_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    if (!eligible) {
        originalStart(behavior, entity, speed, route, callback, first, second, third, entitySpeed, method);
        return;
    }

    Starting starting(entity);
    // Keep registration, parent detachment and carried-state cleanup in rly.
    originalStart(behavior, entity, speed, route, callback, first, second, third, entitySpeed, method);
    if (starting.reentered) return;

    try {
        if (!InstantCombatEnabled() || !IsCurrentFightEntity(entity)) return;
        auto dictionary = Read<Obj>(behavior, 0x20), pending = Read<Obj>(behavior, 0x28);
        if (!dictionary || !pending) return;
        auto lookup = api.Find(api.object_get_class(dictionary), "TryGetValue", 2);
        auto contains = api.Find(api.object_get_class(pending), "Contains", 1);
        const auto id = Read<int64_t>(entity, 0x1C8);
        auto state = FindState(dictionary, lookup, id);
        if (!state || api.object_get_class(state) != stateClass) return;
        Finishing finishing(behavior, dictionary, state, entity, route, callback, lookup, contains, id);
        if (!finishing.OwnsState() || finishing.Cancelled()) return;
        auto held = std::make_shared<std::array<Ref, 5>>();
        (*held)[0].reset(behavior); (*held)[1].reset(entity); (*held)[2].reset(route);
        (*held)[3].reset(dictionary); (*held)[4].reset(state);
        DeferInstantMovement([=, held = std::move(held)] {
            (void)held;
            Starting draining(entity);
            Finishing finishing(behavior, dictionary, state, entity, route, callback, lookup, contains, id);
            if (!InstantCombatEnabled() || !IsCurrentFightEntity(entity) ||
                !finishing.OwnsState() || finishing.Cancelled()) return;
            auto tween = Read<Obj>(state, 0x48);
            // Only the initial arc. rly installs no OnKill subscriber; existing
            // async bounces and unexpected tweens stay on their native path.
            if (!tween || Read<Obj>(state, 0x50) || Read<Obj>(tween, 0x88) ||
                !Read<bool>(tween, 0xE8) || Read<bool>(tween, 0xE9) || Read<bool>(tween, 0x111)) return;
            // Exact yd+ya.rll order, after rfw has finished notifying its start.
            stopFlight(state, nullptr);
            if (!finishing.OwnsState()) return;
            // Once the arc is killed, finish its owned transaction even if the
            // option changes or the module stops, otherwise it would be stranded.
            WithoutBounce noBounce(state);
            arrive(behavior, state, nullptr);
        });
    } catch (const std::exception& error) {
        Log(std::string("THROW_DRAIN_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
        // Never restart a movement after native start or partial completion.
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw;
    }
}
}

void InstallInstantThrowMovement() {
    throwClass = api.Class("Core.dll", "", "yd");
    routeClass = api.Class("Core.dll", "", "enw");
    stateClass = api.Class("Core.dll", "", "ye");
    api.Native(api.Find(throwClass, "rjn", 8), 0x264A8D0);
    stopFlight = reinterpret_cast<StateFn>(api.Native(api.Find(stateClass, "rmd", 0), 0x264FC80));
    arrive = reinterpret_cast<FinishFn>(api.Native(api.Find(throwClass, "rlz", 1), 0x264B480));
    cancel = reinterpret_cast<FinishFn>(api.Native(api.Find(throwClass, "rma", 1), 0x264B6D0));
    api.Native(api.Find(throwClass, "rmb", 1), 0x264B810);
    AddNativeHook(0x264B810, reinterpret_cast<void*>(FinishHook), reinterpret_cast<void**>(&originalFinish));
    AddNativeHook(0x264A8D0, reinterpret_cast<void*>(StartHook), reinterpret_cast<void**>(&originalStart));
}
