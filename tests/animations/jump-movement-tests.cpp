#include "appearance/features.hpp"
#include "core/module.hpp"
#include "ui/options-ui.hpp"
#include "animations/instant-combat.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

void InstallInstantJumpMovement();

// Link the production jump hooks against an in-memory native/managed model.
namespace fake {
using namespace managed;
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using NextFn = void(*)(Obj, Obj, Method);
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Type { std::string name; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};
    Type* type{};
    bool boolean = false;
    std::map<int64_t, Obj> entries;
    int destination = 13;
};
struct MethodInfo { std::string name; uintptr_t rva; };
struct NativeFailure {};
MethodInfo start{"rjn", 0x2643FE0}, advance{"rko", 0x2644F50}, next{"rkj", 0x2644790},
    lookup{"TryGetValue", 0};
std::map<std::string, Type> classes;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
StartFn hook{};
NextFn nextHook{};
bool enabled = true, member = true, replaceOnCell = false, changeCallbackOnCell = false;
bool hostThrowOnCell = false, nativeThrowOnCell = false, throwLookup = false, throwLookupAfterCell = false;
bool reenterOnStart = false, reenterOnCell = false, reenterOnEnd = false, sameRouteReentry = false, stall = false;
bool allowDefer = true, holdDeferred = false, futureStart = false;
std::vector<std::function<void()>> deferred;
unsigned starts = 0, advances = 0, nextCalls = 0, completions = 0, startEvents = 0, endEvents = 0;
std::vector<std::string> events;
Obj activeBehavior{}, activeEntity{}, activeRoute{}, activeCallback{}, activeSpeed{}, nestedRoute{};
uint64_t clockValue = 10000;
float stateRate = 0.0025f;
constexpr int64_t entityId = -900123456789LL;
constexpr uint64_t firstArg = 0x8877665500000002ULL, secondArg = 0xFEDCBA9800000080ULL, thirdArg = 0x1020304050607080ULL;
constexpr float speedArg = 1.375f;
Method callerMethod = reinterpret_cast<Method>(0x12345);

Type* Class(const std::string& name) {
    auto [entry, inserted] = classes.try_emplace(name, Type{name}); return &entry->second;
}
Object& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Object*>(object); }
Obj Make(const char* name) {
    auto value = std::make_unique<Object>(); value->type = Class(name);
    auto result = value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).memory.size(), "Fake field outside storage");
    std::memcpy(Get(object).memory.data() + offset, &value, sizeof(value));
}
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
Obj Route(int destination) {
    auto route = Make("enw"), list = Make("List"); Get(route).destination = destination;
    Put(route, 0x20, list); Put(list, 0x18, 3); return route;
}
Obj Dictionary() { return Read<Obj>(activeBehavior, 0x18); }
void Dispatch(Obj route) {
    hook(activeBehavior, activeEntity, speedArg, route, activeCallback, firstArg, secondArg, thirdArg, activeSpeed, callerMethod);
}
Obj NewState(Obj entity, Obj route, Obj callback) {
    auto state = Make("xt"); Put(state, 0x28, entity); Put(state, 0x40, route); Put(state, 0x38, callback);
    Put(state, 0x20, stateRate); Put(state, 0x30, 0.0f); Put(state, 0x34, Get(route).destination);
    Put(state, 0x48, clockValue + (futureStart ? 1ULL : 0ULL)); return state;
}
void Finish(Obj state) {
    Check(Get(Dictionary()).entries.at(entityId) == state, "Old terminal removed replacement state");
    events.push_back("idle"); Put(activeEntity, 0xB4, 5); Put(activeEntity, 0xB8, 1.0f);
    Get(Dictionary()).entries.erase(entityId); ++completions; events.push_back("callback");
    if (reenterOnEnd) { reenterOnEnd = false; Dispatch(nestedRoute); }
    ++endEvents; events.push_back("end");
}
void Next(Obj behavior, Obj state, Method method) {
    Check(behavior == activeBehavior && method == nullptr, "Jump next ABI mismatch");
    ++nextCalls;
    if (stall) return;
    if (Read<int>(activeEntity, 0x2E0) == Get(Read<Obj>(state, 0x40)).destination) Finish(state);
    else { Put(state, 0x48, clockValue); Put(state, 0x30, 0.0f); }
}
void Original(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
              uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    Check(behavior == activeBehavior && entity == activeEntity && speed == speedArg && callback == activeCallback &&
          first == firstArg && second == secondArg && third == thirdArg && entitySpeed == activeSpeed && method == callerMethod,
          "Jump start ABI/arguments changed");
    ++starts; events.push_back("start");
    auto state = NewState(entity, route, callback); Get(Dictionary()).entries[entityId] = state;
    if (Read<int>(entity, 0x2E0) == Get(route).destination) Finish(state);
    ++startEvents;
    if (reenterOnStart) {
        reenterOnStart = false; Get(Dictionary()).entries.erase(entityId);
        Dispatch(sameRouteReentry ? route : nestedRoute);
    }
}
uint64_t Clock(Method method) { Check(method == nullptr, "Clock MethodInfo must be null"); return clockValue; }
void Advance(Obj behavior, Obj state, Method method) {
    Check(behavior == activeBehavior && method == nullptr, "Jump rko ABI mismatch");
    ++advances;
    const float elapsed = static_cast<float>(clockValue - Read<uint64_t>(state, 0x48));
    const float progress = std::clamp(elapsed * Read<float>(state, 0x20), 0.0f, 1.0f);
    Put(state, 0x30, progress);
    Check(progress == 1.0f, "Production hook did not cause terminal native progress");
    events.push_back("position"); Put(activeEntity, 0x2E0, Read<int>(state, 0x34));
    if (replaceOnCell) {
        replaceOnCell = false; Get(Dictionary()).entries[entityId] = NewState(activeEntity, nestedRoute, activeCallback);
    }
    if (changeCallbackOnCell) Put(state, 0x38, Make("ReplacementAction"));
    if (reenterOnCell) { reenterOnCell = false; Dispatch(sameRouteReentry ? activeRoute : nestedRoute); }
    if (throwLookupAfterCell) throwLookup = true;
    if (hostThrowOnCell) throw std::runtime_error("Host position invocation failed");
    if (nativeThrowOnCell) throw NativeFailure{};
    events.push_back("registry-cell");
    events.push_back("rotation"); Put(activeEntity, 0xB4, 7);
    events.push_back("scale"); Put(activeEntity, 0xB8, 0.75f);
    // Like native rko, still calls the next method after a cell callback may
    // have replaced the state. The production terminal hook must detect it.
    nextHook(behavior, state, nullptr);
}
void LaterNativeUpdate() {
    clockValue += 1000;
    auto copy = Get(Dictionary()).entries;
    for (const auto& [id, state] : copy) Advance(activeBehavior, state, nullptr);
}
void FinishDeferred() {
    auto queued = std::move(deferred); deferred.clear();
    for (auto& finish : queued) finish();
}
void Reset() {
    deferred.clear();
    Check(handles.empty(), "Jump temporary references leaked");
    starts = advances = nextCalls = completions = startEvents = endEvents = 0; events.clear();
    enabled = member = true; replaceOnCell = changeCallbackOnCell = false;
    hostThrowOnCell = nativeThrowOnCell = throwLookup = throwLookupAfterCell = false;
    reenterOnStart = reenterOnCell = reenterOnEnd = sameRouteReentry = stall = false;
    allowDefer = true; holdDeferred = futureStart = false;
    clockValue = 10000; stateRate = 0.0025f;
    activeBehavior = Make("xu"); activeEntity = Make("AnimatedEntity"); activeCallback = Make("Action"); activeSpeed = Make("EntitySpeed");
    Put(activeBehavior, 0x18, Make("Dictionary"));
    Put(activeEntity, 0x1C8, entityId); Put(activeEntity, 0x2E0, 10); Put(activeEntity, 0xB4, 5); Put(activeEntity, 0xB8, 1.0f);
    activeRoute = Route(13); nestedRoute = Route(15); module->stopRequested = false;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass, const char* name, int argc) const {
    using namespace fake;
    if (std::string(name) == "rjn" && argc == 8) return &start;
    if (std::string(name) == "rko" && argc == 1) return &advance;
    if (std::string(name) == "rkj" && argc == 1) return &next;
    Check(std::string(name) == "TryGetValue" && argc == 2, "Unexpected reflection lookup"); return &lookup;
}
void* Api::Native(Method method, uintptr_t rva) const {
    using namespace fake; Check(static_cast<const MethodInfo*>(method)->rva == rva, "Unexpected native method RVA");
    if (rva == start.rva) return reinterpret_cast<void*>(Original);
    if (rva == advance.rva) return reinterpret_cast<void*>(Advance);
    Check(rva == next.rva, "Unexpected native target"); return reinterpret_cast<void*>(Next);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x263E800, "Unexpected native clock RVA");
    return reinterpret_cast<void*>(fake::Clock);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake; Check(method == &lookup && args.size() == 2, "Wrong dictionary lookup ABI");
    if (throwLookup) throw std::runtime_error("Host dictionary invocation failed");
    const auto parameters = args.begin(); auto found = Get(self).entries.find(*static_cast<int64_t*>(parameters[0]));
    *static_cast<Obj*>(parameters[1]) = found == Get(self).entries.end() ? nullptr : found->second;
    return Box(found != Get(self).entries.end());
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string&) {}
bool InstantMovementEnabled() { return fake::enabled && !module->stopRequested; }
bool IsCurrentFightEntity(managed::Obj entity) { return fake::member && entity == fake::activeEntity; }
bool DeferInstantMovement(std::function<void()> finish) {
    if (!fake::allowDefer) return false;
    if (fake::holdDeferred) fake::deferred.push_back(std::move(finish)); else finish();
    return true;
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x2643FE0) {
        fake::hook = reinterpret_cast<fake::StartFn>(replacement); *original = reinterpret_cast<void*>(fake::Original);
    } else {
        fake::Check(rva == 0x2644790, "Unexpected jump hook target");
        fake::nextHook = reinterpret_cast<fake::NextFn>(replacement); *original = reinterpret_cast<void*>(fake::Next);
    }
}

int main() {
    using namespace fake;
    try {
        api.object_get_class = [](Obj object) -> Klass { return Get(object).type; };
        api.object_unbox = [](Obj object) -> void* { return &Get(object).boolean; };
        api.gchandle_new = [](Obj object, bool) -> GCHandle { auto id = ++nextHandle; handles.emplace(id, object); return id; };
        api.gchandle_get_target = [](GCHandle id) -> Obj { return handles.at(id); };
        api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "GC handle freed twice"); };
        Module moduleState{L"offline-jump-tests"}; module = &moduleState; InstallInstantJumpMovement();
        for (float rate : {0.0025f, 0.002f, 0.0022222223f}) {
            Reset(); stateRate = rate; Dispatch(activeRoute);
            Check(starts == 1 && advances == 1 && completions == 1 && nextCalls == 1, "Jump must complete once through native rko");
            Check(events == std::vector<std::string>({"start", "position", "registry-cell", "rotation", "scale", "idle", "callback", "end"}),
                  "Jump must preserve native geometry and terminal event order");
            Check(Read<int>(activeEntity, 0x2E0) == 13 && Read<int>(activeEntity, 0xB4) == 5 && Read<float>(activeEntity, 0xB8) == 1.0f,
                  "Native terminal position/orientation/scale must be preserved");
            Check(clockValue == 10000 && stateRate == rate, "No global clock/rate alteration");
            LaterNativeUpdate(); Check(advances == 1 && completions == 1 && endEvents == 1, "Later update must not duplicate completion");
        }
        for (float rate : {0.0f, -0.0025f, 1e-10f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            Reset(); stateRate = rate; Dispatch(activeRoute);
            auto state = Get(Dictionary()).entries.at(entityId);
            Check(starts == 1 && advances == 0 && Read<uint64_t>(state, 0x48) == clockValue, "Invalid rate must retain native timestamp");
        }
        Reset(); clockValue = 10; Dispatch(activeRoute);
        Check(starts == 1 && advances == 0, "Young level clock cannot be underflowed");
        Reset(); futureStart = true; Dispatch(activeRoute);
        Check(starts == 1 && advances == 0, "Future start must not be altered");
        Reset(); enabled = false; Dispatch(activeRoute); Check(starts == 1 && advances == 0, "Disabled mode must remain native");
        Reset(); member = false; Dispatch(activeRoute); Check(starts == 1 && advances == 0, "Unknown entity must remain native");
        Reset(); Get(activeBehavior).type = Class("Other"); Dispatch(activeRoute);
        Check(starts == 1 && advances == 0, "Unknown movement kind must remain native");
        Reset(); Put(Read<Obj>(activeRoute, 0x20), 0x18, 561); Dispatch(activeRoute);
        Check(starts == 1 && advances == 0, "Oversized route must remain native");
        Reset(); allowDefer = false; Dispatch(activeRoute);
        Check(starts == 1 && advances == 0, "Rejected defer must remain native");
        LaterNativeUpdate(); Check(completions == 1, "Rejected defer must retain native completion");
        Reset(); holdDeferred = true; Dispatch(activeRoute);
        Check(starts == 1 && advances == 0 && startEvents == 1, "Jump must wait for native rfw start notifications");
        FinishDeferred(); Check(completions == 1, "Deferred jump must complete captured state");
        Reset(); holdDeferred = true; Dispatch(activeRoute);
        Get(Dictionary()).entries[entityId] = NewState(activeEntity, activeRoute, activeCallback);
        FinishDeferred(); Check(advances == 0 && completions == 0, "Deferred replacement with same route/callback must not drain");
        Reset(); replaceOnCell = true; Dispatch(activeRoute);
        Check(advances == 1 && nextCalls == 0 && completions == 0, "Intra-rko replacement must block old terminal dispatch");
        LaterNativeUpdate(); Check(completions == 1, "Replacement must complete once on native update");
        Reset(); changeCallbackOnCell = true; Dispatch(activeRoute);
        Check(advances == 1 && nextCalls == 0 && completions == 0, "Intra-rko callback replacement must stop terminal dispatch");
        for (bool sameRoute : {false, true}) {
            Reset(); reenterOnStart = true; sameRouteReentry = sameRoute; Dispatch(activeRoute);
            Check(starts == 2 && advances == 0 && completions == 0, "Start reentry must remain native even for same route/callback");
        }
        Reset(); reenterOnCell = true; Dispatch(activeRoute);
        Check(starts == 2 && advances == 1 && nextCalls == 0 && completions == 0, "Cell reentry must preserve new state");
        LaterNativeUpdate(); Check(completions == 1, "Cell replacement must complete once on later update");
        Reset(); reenterOnEnd = true; Dispatch(activeRoute);
        Check(starts == 2 && advances == 1 && completions == 1, "End reentry must leave new movement native");
        LaterNativeUpdate(); Check(completions == 2 && endEvents == 2, "Reentrant movement must complete once");
        Reset(); activeRoute = Route(10); Dispatch(activeRoute);
        Check(starts == 1 && advances == 0 && completions == 1, "Native start that already completed must not be repeated");
        Reset(); stall = true; Dispatch(activeRoute);
        Check(advances == 5 && completions == 0, "Stalled route must respect finite drain bound");
        stall = false; LaterNativeUpdate(); Check(completions == 1, "Bounded fallback must retain native completion");
        Reset(); hostThrowOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && advances == 1 && nextCalls == 0 && module->stopRequested, "Host failure must stop without retry");
        Reset(); throwLookupAfterCell = true; Dispatch(activeRoute);
        Check(starts == 1 && advances == 1 && nextCalls == 0 && module->stopRequested, "Terminal validation failure must stop without old completion");
        Reset(); throwLookup = true; Dispatch(activeRoute);
        Check(starts == 1 && advances == 0 && module->stopRequested, "Capture lookup failure must stop without retry");
        Reset(); nativeThrowOnCell = true; bool threw = false;
        try { Dispatch(activeRoute); } catch (const NativeFailure&) { threw = true; }
        Check(threw && starts == 1 && advances == 1 && nextCalls == 0 && module->stopRequested, "Native exception must propagate without retry");
        Check(handles.empty(), "Jump temporary GC references leaked"); module = nullptr;
        std::cout << "PASS: actual jump hooks ABI, terminal geometry, timestamps, native cleanup, deferred ordering, reentry/replacement and exceptions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
