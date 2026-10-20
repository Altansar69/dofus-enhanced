#include "instant-combat.hpp"
#include "module.hpp"
#include <cmath>

using namespace managed;

namespace {
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using AdvanceFn = void(*)(Obj, Obj, Method);
using ClockFn = uint64_t(*)(Method);
StartFn originalStart;
AdvanceFn advanceJump, originalNext;
ClockFn clockMilliseconds;
Klass jumpClass, routeClass;
constexpr int maxSegments = 560;

struct ActiveJump {
    Obj entity;
    ActiveJump* previous;
    bool reentered = false;
    static thread_local ActiveJump* current;
    explicit ActiveJump(Obj value) : entity(value), previous(current) { current = this; }
    ~ActiveJump() { current = previous; }
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
thread_local ActiveJump* ActiveJump::current = nullptr;

Obj FindState(Obj dictionary, Method lookup, int64_t id) {
    Obj state = nullptr;
    return api.Value<bool>(api.Invoke(lookup, dictionary, {&id, &state})) ? state : nullptr;
}

struct JumpFinish {
    Ref behavior, entity, route, callback, dictionary, state;
    Method lookup;
    int64_t id;
    int segmentCount;
    JumpFinish(Obj behaviorValue, Obj entityValue, Obj routeValue, Obj callbackValue,
               Obj dictionaryValue, Obj stateValue, Method lookupValue, int64_t idValue, int countValue)
        : behavior(behaviorValue), entity(entityValue), route(routeValue), callback(callbackValue),
          dictionary(dictionaryValue), state(stateValue), lookup(lookupValue), id(idValue), segmentCount(countValue) {}
    bool IsCurrent() const {
        auto currentEntity = entity.get(), currentState = state.get();
        return InstantCombatEnabled() && IsCurrentFightEntity(currentEntity) &&
            Read<Obj>(behavior.get(), 0x18) == dictionary.get() &&
            FindState(dictionary.get(), lookup, id) == currentState &&
            Read<Obj>(currentState, 0x28) == currentEntity &&
            Read<Obj>(currentState, 0x40) == route.get() &&
            Read<Obj>(currentState, 0x38) == callback.get();
    }
};

// rko publishes cell notifications before calling rkj. A notification can
// replace the movement; only our accelerated invocation needs this terminal
// ownership check. All ordinary native rkj calls remain untouched.
struct TerminalFrame {
    const JumpFinish& context;
    const ActiveJump& active;
    Obj behavior, state;
    TerminalFrame* previous;
    static thread_local TerminalFrame* current;
    TerminalFrame(const JumpFinish& value, const ActiveJump& guard)
        : context(value), active(guard), behavior(value.behavior.get()), state(value.state.get()), previous(current) {
        current = this;
    }
    ~TerminalFrame() { current = previous; }
};
thread_local TerminalFrame* TerminalFrame::current = nullptr;

void NextHook(Obj behavior, Obj state, Method method) {
    for (auto frame = TerminalFrame::current; frame; frame = frame->previous) {
        if (frame->behavior != behavior || frame->state != state) continue;
        try {
            if (frame->active.reentered || !frame->context.IsCurrent()) return;
        } catch (const std::exception& error) {
            Log(std::string("JUMP_TERMINAL_ERROR ") + error.what());
            if (module) module->stopRequested.store(true);
            return;
        } catch (...) {
            if (module) module->stopRequested.store(true);
            throw;
        }
        break;
    }
    originalNext(behavior, state, method);
}

void FinishJump(const std::shared_ptr<JumpFinish>& context) {
    try {
        auto behavior = context->behavior.get(), entity = context->entity.get(), state = context->state.get();
        ActiveJump active(entity);
        for (int remaining = context->segmentCount + 2; remaining > 0; --remaining) {
            if (active.reentered || !context->IsCurrent()) return;
            const float rate = Read<float>(state, 0x20);
            if (!std::isfinite(rate) || rate <= 0.0f) return;
            const double duration = std::ceil(1.0 / static_cast<double>(rate)) + 2.0;
            if (!std::isfinite(duration) || duration > 60000.0) return;
            const auto required = static_cast<uint64_t>(duration);
            const auto now = clockMilliseconds(nullptr);
            const auto started = Read<uint64_t>(state, 0x48);
            if (now < required || started > now) return;
            // Only this xt's elapsed time changes. rko itself performs native
            // position, registry-cell, rotation, scale and route completion.
            const auto terminalStart = now - required;
            *reinterpret_cast<uint64_t*>(static_cast<char*>(state) + 0x48) =
                started < terminalStart ? started : terminalStart;
            TerminalFrame terminal(*context, active);
            advanceJump(behavior, state, nullptr);
        }
        // Unusual unfinished routes remain registered for native Update.
    } catch (const std::exception& error) {
        Log(std::string("JUMP_DRAIN_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw;
    }
}

void StartHook(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
               uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    int segmentCount = -1;
    const bool nested = ActiveJump::ObserveReentry(entity);
    try {
        if (InstantCombatEnabled() && behavior && entity && route && !nested &&
            api.object_get_class(behavior) == jumpClass && api.object_get_class(route) == routeClass &&
            IsCurrentFightEntity(entity)) {
            auto points = Read<Obj>(route, 0x20);
            if (points) {
                const int count = Read<int>(points, 0x18);
                if (count >= 0 && count <= maxSegments) segmentCount = count;
            }
        }
    } catch (const std::exception& error) {
        Log(std::string("JUMP_ELIGIBILITY_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    }
    ActiveJump active(entity);
    originalStart(behavior, entity, speed, route, callback, first, second, third, entitySpeed, method);
    if (segmentCount < 0 || active.reentered) return;
    try {
        auto dictionary = Read<Obj>(behavior, 0x18);
        if (!dictionary) return;
        auto lookup = api.Find(api.object_get_class(dictionary), "TryGetValue", 2);
        const auto id = Read<int64_t>(entity, 0x1C8);
        auto state = FindState(dictionary, lookup, id);
        if (!state || Read<Obj>(state, 0x28) != entity || Read<Obj>(state, 0x40) != route ||
            Read<Obj>(state, 0x38) != callback) return;
        auto context = std::make_shared<JumpFinish>(behavior, entity, route, callback, dictionary, state,
                                                  lookup, id, segmentCount);
        DeferInstantMovement([context] { FinishJump(context); });
    } catch (const std::exception& error) {
        Log(std::string("JUMP_CAPTURE_ERROR ") + error.what());
        if (module) module->stopRequested.store(true);
    } catch (...) {
        if (module) module->stopRequested.store(true);
        throw;
    }
}
}

void InstallInstantJumpMovement() {
    jumpClass = api.Class("Core.dll", "", "xu");
    routeClass = api.Class("Core.dll", "", "enw");
    api.Native(api.Find(jumpClass, "rjn", 8), 0x2643FE0);
    api.Native(api.Find(jumpClass, "rkj", 1), 0x2644790);
    advanceJump = reinterpret_cast<AdvanceFn>(api.Native(api.Find(jumpClass, "rko", 1), 0x2644F50));
    // ILSpy omits this shared static clock declaration; rkj calls the guarded
    // native body directly. Its ABI is UInt64(MethodInfo*).
    clockMilliseconds = reinterpret_cast<ClockFn>(api.NativeAt(0x263E800));
    AddNativeHook(0x2644790, reinterpret_cast<void*>(NextHook), reinterpret_cast<void**>(&originalNext));
    AddNativeHook(0x2643FE0, reinterpret_cast<void*>(StartHook), reinterpret_cast<void**>(&originalStart));
}
