#include "appearance/features.hpp"
#include "core/module.hpp"
#include "ui/options-ui.hpp"
#include "animations/instant-combat.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

void InstallInstantPushMovement();

// The test executable links the actual push-movement.cpp, without a client.
namespace fake {
using namespace managed;
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Type { std::string name; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};
    Type* type{};
    bool boolean = false;
    std::map<int64_t, Obj> entries;
    std::vector<int> cells;
    size_t next = 0;
};
struct MethodInfo { std::string name; uintptr_t rva; };
struct NativeFailure {};
MethodInfo start{"rjn", 0x2646B60}, position{"rfv", 0x2616050}, next{"rkv", 0x2647140}, lookup{"TryGetValue", 0};
std::map<std::string, Type> classes;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
StartFn hook{};
bool enabled = true, member = true, replaceOnCell = false, changeCallbackOnCell = false;
bool hostThrowOnCell = false, nativeThrowOnCell = false, throwLookup = false;
bool reenterOnStart = false, reenterOnEnd = false, sameRouteReentry = false, stall = false;
bool allowDefer = true, holdDeferred = false;
std::vector<std::function<void()>> deferred;
unsigned starts = 0, positions = 0, nextCalls = 0, completions = 0, startEvents = 0, endEvents = 0;
std::vector<int> visited;
Obj activeBehavior{}, activeEntity{}, activeRoute{}, activeCallback{}, activeSpeed{}, nestedRoute{};
constexpr int64_t entityId = -900123456789LL;
uint64_t firstArg = 0x8877665500000002ULL;
constexpr uint64_t secondArg = 0xFEDCBA9800000080ULL, thirdArg = 0x1020304050607080ULL;
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
Obj Route(std::vector<int> cells) {
    auto route = Make("enw"), list = Make("List"); Get(route).cells = std::move(cells);
    Put(route, 0x20, list); Put(list, 0x18, static_cast<int>(Get(route).cells.size())); return route;
}
Obj Dictionary() { return Read<Obj>(activeBehavior, 0x28); }
void Dispatch(Obj route) {
    hook(activeBehavior, activeEntity, speedArg, route, activeCallback, firstArg, secondArg, thirdArg, activeSpeed, callerMethod);
}
void ChooseNext(Obj state) {
    auto route = Read<Obj>(state, 0x48); auto& path = Get(route);
    const int cell = path.next < path.cells.size() ? path.cells[path.next++] : path.cells.back();
    Put(state, 0x38, cell); Put(state, 0x34, 0.0f);
    Put(state, 0x50, Read<int>(state, 0x50) + 1);
    Put(Read<Obj>(route, 0x20), 0x18, static_cast<int>(path.cells.size() - path.next));
}
Obj NewState(Obj entity, Obj route, Obj callback) {
    auto state = Make("xw"); Put(state, 0x28, entity); Put(state, 0x48, route); Put(state, 0x40, callback);
    const int raw = static_cast<int32_t>(firstArg); Put(state, 0x10, raw == 0 ? 1 : raw);
    Put(state, 0x54, Read<int>(Read<Obj>(route, 0x20), 0x18));
    ChooseNext(state); return state;
}
void Finish(Obj state) {
    Check(Get(Dictionary()).entries.at(entityId) == state, "Completion dispatched for replaced state");
    Get(Dictionary()).entries.erase(entityId); ++completions;
    if (reenterOnEnd) { reenterOnEnd = false; Dispatch(nestedRoute); }
    ++endEvents;
}
void Original(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
              uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    Check(behavior == activeBehavior && entity == activeEntity && speed == speedArg && callback == activeCallback &&
          first == firstArg && second == secondArg && third == thirdArg && entitySpeed == activeSpeed && method == callerMethod,
          "Push start ABI/arguments changed");
    ++starts;
    auto state = NewState(entity, route, callback); Get(Dictionary()).entries[entityId] = state;
    if (Read<int>(entity, 0x2E0) == Get(route).cells.back()) Finish(state);
    ++startEvents;
    if (reenterOnStart) {
        reenterOnStart = false;
        // Native callers can cancel the first movement during start dispatch.
        Get(Dictionary()).entries.erase(entityId);
        Dispatch(sameRouteReentry ? route : nestedRoute);
    }
}
void Position(Obj entity, int from, int to, float progress, Method method) {
    Check(entity == activeEntity && from == Read<int>(entity, 0x348) && progress == 1.0f && method == nullptr,
          "Push terminal position ABI differs from native Update");
    ++positions; visited.push_back(to); Put(entity, 0x348, to); Put(entity, 0x2E0, to);
    if (replaceOnCell) {
        replaceOnCell = false; Get(Dictionary()).entries[entityId] = NewState(entity, nestedRoute, activeCallback);
    }
    if (changeCallbackOnCell) Put(Get(Dictionary()).entries.at(entityId), 0x40, Make("ReplacementAction"));
    if (hostThrowOnCell) throw std::runtime_error("Host position invocation failed");
    if (nativeThrowOnCell) throw NativeFailure{};
}
void Next(Obj behavior, Obj state, Method method) {
    Check(behavior == activeBehavior && method == nullptr && Read<float>(state, 0x34) == 1.0f,
          "Push next-segment ABI/progress mismatch");
    ++nextCalls;
    if (stall) return;
    if (Read<int>(activeEntity, 0x2E0) == Get(Read<Obj>(state, 0x48)).cells.back()) Finish(state);
    else ChooseNext(state);
}
void LaterNativeUpdate() {
    auto copy = Get(Dictionary()).entries;
    for (const auto& [id, state] : copy) {
        Put(state, 0x34, 1.0f);
        Position(activeEntity, Read<int>(activeEntity, 0x348), Read<int>(state, 0x38), 1.0f, nullptr);
        Next(activeBehavior, state, nullptr);
    }
}
void FinishDeferred() {
    auto queued = std::move(deferred); deferred.clear();
    for (auto& finish : queued) finish();
}
void Reset() {
    starts = positions = nextCalls = completions = startEvents = endEvents = 0; visited.clear();
    enabled = member = true; replaceOnCell = changeCallbackOnCell = false;
    hostThrowOnCell = nativeThrowOnCell = throwLookup = false;
    reenterOnStart = reenterOnEnd = sameRouteReentry = stall = false;
    allowDefer = true; holdDeferred = false; deferred.clear();
    firstArg = 0x8877665500000002ULL;
    activeBehavior = Make("xy"); activeEntity = Make("AnimatedEntity"); activeCallback = Make("Action"); activeSpeed = Make("EntitySpeed");
    Put(activeBehavior, 0x28, Make("Dictionary")); Put(activeBehavior, 0x38, 6);
    Put(activeEntity, 0x1C8, entityId); Put(activeEntity, 0x348, 10); Put(activeEntity, 0x2E0, 10); Put(activeEntity, 0xB4, 5);
    activeRoute = Route({11, 12, 13}); nestedRoute = Route({14, 15}); module->stopRequested = false;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass, const char* name, int argc) const {
    using namespace fake;
    if (std::string(name) == "rjn" && argc == 8) return &start;
    if (std::string(name) == "rfv" && argc == 3) return &position;
    if (std::string(name) == "rkv" && argc == 1) return &next;
    Check(std::string(name) == "TryGetValue" && argc == 2, "Unexpected reflection lookup"); return &lookup;
}
void* Api::Native(Method method, uintptr_t rva) const {
    using namespace fake; Check(static_cast<const MethodInfo*>(method)->rva == rva, "Unexpected native method RVA");
    if (rva == start.rva) return reinterpret_cast<void*>(Original);
    if (rva == position.rva) return reinterpret_cast<void*>(Position);
    Check(rva == next.rva, "Unexpected native target"); return reinterpret_cast<void*>(Next);
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
    if (fake::holdDeferred) fake::deferred.push_back(std::move(finish));
    else finish();
    return true;
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    fake::Check(rva == 0x2646B60, "Unexpected push hook target");
    fake::hook = reinterpret_cast<fake::StartFn>(replacement); *original = reinterpret_cast<void*>(fake::Original);
}

int main() {
    using namespace fake;
    try {
        api.object_get_class = [](Obj object) -> Klass { return Get(object).type; };
        api.object_unbox = [](Obj object) -> void* { return &Get(object).boolean; };
        api.gchandle_new = [](Obj object, bool) -> GCHandle { auto id = ++nextHandle; handles.emplace(id, object); return id; };
        api.gchandle_get_target = [](GCHandle id) -> Obj { return handles.at(id); };
        api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "GC handle freed twice"); };
        Module state{L"offline-push-tests"}; module = &state; InstallInstantPushMovement();
        for (uint64_t easing : {0ULL, 1ULL, 2ULL}) {
            Reset(); firstArg = 0x8877665500000000ULL | easing; Dispatch(activeRoute);
            Check(starts == 1 && positions == 3 && completions == 1 && nextCalls == 3 && visited == std::vector<int>({11, 12, 13}),
                  "Push must visit every cell and complete exactly once");
            Check(Read<int>(activeEntity, 0x2E0) == 13 && Read<int>(activeEntity, 0xB4) == 5, "Push must preserve orientation");
            Check(startEvents == 1 && endEvents == 1 && Get(Dictionary()).entries.empty(), "Push start/end cleanup missing");
            LaterNativeUpdate(); Check(positions == 3 && completions == 1, "Later update must not repeat completed push");
        }
        Reset(); enabled = false; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Disabled mode must remain native");
        Reset(); allowDefer = false; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && completions == 0, "Rejected/outside-rfw defer must remain native");
        LaterNativeUpdate(); LaterNativeUpdate(); LaterNativeUpdate();
        Check(completions == 1, "Rejected defer must retain native completion");
        Reset(); holdDeferred = true; Dispatch(activeRoute);
        Check(starts == 1 && startEvents == 1 && positions == 0, "Push must wait until parent rfw permits completion");
        FinishDeferred();
        Check(positions == 3 && completions == 1, "Deferred push must finish its captured state");
        Reset(); holdDeferred = true; Dispatch(activeRoute);
        Get(Dictionary()).entries[entityId] = NewState(activeEntity, activeRoute, activeCallback);
        FinishDeferred();
        Check(positions == 0 && completions == 0, "Deferred state replacement must not drain new xw even with same route/callback");
        Reset(); member = false; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Unknown/noncombat entity must remain native");
        Reset(); Put(activeBehavior, 0x38, 8); Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Unknown behavior must remain native");
        Reset(); firstArg = 9; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Unknown easing subtype must remain native");
        Reset(); Put(Read<Obj>(activeRoute, 0x20), 0x18, 561); Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Oversized route must remain native");
        Reset(); replaceOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 1 && nextCalls == 0, "Cell callback replacement must stop draining old state");
        Reset(); changeCallbackOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 1 && nextCalls == 0, "Changed callback must stop drain");
        for (bool sameRoute : {false, true}) {
            Reset(); reenterOnStart = true; sameRouteReentry = sameRoute; Dispatch(activeRoute);
            Check(starts == 2 && positions == 0 && completions == 0, "Start reentry must leave replacement native even for same route/callback");
        }
        Reset(); reenterOnEnd = true; Dispatch(activeRoute);
        Check(starts == 2 && positions == 3 && completions == 1, "End reentry must not drain/recomplete replacement");
        LaterNativeUpdate(); LaterNativeUpdate(); LaterNativeUpdate();
        Check(completions == 2 && endEvents == 2, "Replacement must complete once on later native updates");
        Reset(); activeRoute = Route({10}); Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && completions == 1, "Already-completed native start must not be completed again");
        Reset(); stall = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 5 && nextCalls == 5 && completions == 0, "Malformed route must have finite drain bound");
        stall = false; LaterNativeUpdate(); LaterNativeUpdate(); LaterNativeUpdate();
        Check(completions == 1, "Bounded unfinished movement must remain available to native Update");
        Reset(); hostThrowOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 1 && nextCalls == 0 && module->stopRequested, "Host failure after mutation must stop without retry");
        Reset(); throwLookup = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && module->stopRequested, "Host lookup failure must stop without retry");
        Reset(); nativeThrowOnCell = true; bool threw = false;
        try { Dispatch(activeRoute); } catch (const NativeFailure&) { threw = true; }
        Check(threw && starts == 1 && positions == 1 && nextCalls == 0 && module->stopRequested, "Native failure must propagate without retry");
        Check(handles.empty(), "Temporary push GC references leaked");
        module = nullptr;
        std::cout << "PASS: actual push hook ABI, cells/orientation, native completion, later updates, start/end reentry, bounds and exceptions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
