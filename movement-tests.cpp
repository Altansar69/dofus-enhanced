#include "features.hpp"
#include "module.hpp"
#include "options-ui.hpp"
#include "instant-combat.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

// Link the actual movement.cpp against this in-memory fake IL2CPP runtime.
namespace fake {
using namespace managed;
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using NextFn = void(*)(Obj, Obj, bool, Method);
using FinishFn = void(*)(Obj, Obj, Method);
using LandingFn = bool(*)(int, Method);
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
MethodInfo start{"rjn", 0x263E860}, position{"rfv", 0x2616050}, next{"rjr", 0x263EE10}, lookup{"TryGetValue", 0};
MethodInfo appearance{"daxp", 0x781A20}, landing{"dbdk", 0x78F5D0};
MethodInfo finish{"rjw", 0x263FA00};
std::map<std::string, Type> classes;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
StartFn hook{};
NextFn nextHook{};
FinishFn finishHook{};
LandingFn landingHook{};
bool enabled = true, member = true, replaceOnCell = false, throwOnCell = false, reenterOnEnd = false;
bool landingBody = false, nativeThrowOnCell = false;
bool throwLookup = false;
bool canDefer = true, initialNext = false;
bool engageLandingAtStart = false;
bool retainJobs = false;
std::vector<std::function<void()>> jobs;
std::function<void()> insideNext, insideFinish, insideInitialNext, afterLandingPredicate;
Obj probeState{}, probeBehavior{};
bool probeResult = false;
unsigned initialNextCalls = 0, finishCalls = 0, landingStarts = 0, predicateCalls = 0;
unsigned starts = 0, positions = 0, nextCalls = 0, completions = 0;
std::vector<int> visited;
Obj activeBehavior{}, activeEntity{}, activeRoute{}, activeCallback{}, activeSpeed{}, nestedRoute{};
constexpr int64_t entityId = -900123456789LL;
constexpr uint64_t firstArg = 0x8877665544332211ULL, secondArg = 0xFEDCBA9876543210ULL, thirdArg = 0x1020304050607080ULL;
constexpr float speedArg = 1.375f;
Method callerMethod = reinterpret_cast<Method>(0x12345);
bool ProbeLanding(uintptr_t rva);

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
void ChooseNext(Obj state) {
    auto route = Read<Obj>(state, 0x38); auto& path = Get(route);
    const int cell = path.next < path.cells.size() ? path.cells[path.next++] : path.cells.back();
    Put(state, 0x2C, cell); Put(state, 0x28, 0.0f);
    Put(Read<Obj>(route, 0x20), 0x18, static_cast<int>(path.cells.size() - path.next));
}
Obj NewState(Obj entity, Obj route, Obj callback) {
    auto state = Make("xm"); Put(state, 0x20, entity); Put(state, 0x38, route); Put(state, 0x30, callback);
    ChooseNext(state); return state;
}
void Dispatch(Obj route) {
    hook(activeBehavior, activeEntity, speedArg, route, activeCallback, firstArg, secondArg, thirdArg, activeSpeed, callerMethod);
}
void Original(Obj behavior, Obj entity, float speed, Obj route, Obj callback,
              uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    Check(behavior == activeBehavior && entity == activeEntity && speed == speedArg && callback == activeCallback &&
          first == firstArg && second == secondArg && third == thirdArg && entitySpeed == activeSpeed && method == callerMethod,
          "Movement start ABI or arguments changed");
    ++starts;
    const auto state = NewState(entity, route, callback);
    Get(Read<Obj>(behavior, 0x38)).entries[entityId] = state;
    struct Initial { Initial() { initialNext = true; } ~Initial() { initialNext = false; } } initial;
    nextHook(behavior, state, true, nullptr);
}
void Position(Obj entity, int from, int to, float progress, Method method) {
    Check(entity == activeEntity && from == Read<int>(entity, 0x348) && progress == 1.0f && method == nullptr,
          "Terminal position ABI differs from native Update");
    ++positions; visited.push_back(to); Put(entity, 0x348, to); Put(entity, 0x2E0, to);
    if (replaceOnCell) {
        replaceOnCell = false;
        Get(Read<Obj>(activeBehavior, 0x38)).entries[entityId] = NewState(entity, nestedRoute, activeCallback);
    }
    if (throwOnCell) throw std::runtime_error("Cell callback failed after dispatch");
    if (nativeThrowOnCell) throw 7;
}
int Appearance(Obj entity, Method method) { Check(entity == activeEntity && method == nullptr, "Appearance ABI"); return 42; }
bool Landing(int bone, Method method) {
    Check(bone == 42 && method == nullptr, "Landing predicate ABI");
    ++predicateCalls;
    return landingBody;
}
void Finish(Obj behavior, Obj state, Method method) {
    Check(method == nullptr, "Native finish ABI changed");
    if (state == probeState) { probeResult = ProbeLanding(0x263FB17); return; }
    ++finishCalls;
    if (insideFinish) { auto run = std::move(insideFinish); insideFinish = {}; run(); }
    if (Read<int>(behavior, 0x48) != 5 || !ProbeLanding(0x263FB17)) Put(activeEntity, 0xB4, 6);
    Get(Read<Obj>(behavior, 0x38)).entries.erase(entityId);
    ++completions;
    if (reenterOnEnd) { reenterOnEnd = false; Dispatch(nestedRoute); }
}
void Next(Obj behavior, Obj state, bool restart, Method method) {
    Check(restart && method == nullptr, "Native next ABI changed");
    if (state == probeState) { probeResult = ProbeLanding(0x263F03C); return; }
    if (initialNext) {
        ++initialNextCalls;
        if (insideInitialNext) { auto run = std::move(insideInitialNext); insideInitialNext = {}; run(); }
        if (engageLandingAtStart && Read<int>(behavior, 0x48) == 5 && ProbeLanding(0x263F03C)) {
            ++landingStarts; Put(activeEntity, 0xB8, true);
        }
        return;
    }
    if (insideNext) { auto run = std::move(insideNext); insideNext = {}; run(); }
    if (Read<int>(behavior, 0x48) == 5) {
        const bool nativeLanding = ProbeLanding(0x263F03C);
        if (afterLandingPredicate) { auto run = std::move(afterLandingPredicate); afterLandingPredicate = {}; run(); }
        if (nativeLanding) { ++landingStarts; Put(activeEntity, 0xB8, true); return; }
    }
    Check(Read<float>(state, 0x28) == 1.0f, "Terminal step progress mismatch");
    ++nextCalls;
    auto route = Read<Obj>(state, 0x38);
    if (Read<int>(activeEntity, 0x2E0) == Get(route).cells.back()) {
        finishHook(behavior, state, nullptr);
        return;
    }
    ChooseNext(state);
}
void Reset() {
    Check(jobs.empty() && handles.empty(), "Previous movement test leaked jobs or managed references");
    starts = positions = nextCalls = completions = 0; visited.clear();
    enabled = member = true; replaceOnCell = throwOnCell = reenterOnEnd = false;
    landingBody = nativeThrowOnCell = false;
    throwLookup = false;
    canDefer = true; initialNext = retainJobs = engageLandingAtStart = false;
    insideNext = insideFinish = insideInitialNext = afterLandingPredicate = {};
    probeState = probeBehavior = nullptr; probeResult = false;
    initialNextCalls = finishCalls = landingStarts = predicateCalls = 0;
    activeBehavior = Make("xq"); activeEntity = Make("AnimatedEntity"); activeCallback = Make("Action"); activeSpeed = Make("EntitySpeed");
    Put(activeBehavior, 0x38, Make("Dictionary")); Put(activeBehavior, 0x48, 3);
    Put(activeEntity, 0x1C8, entityId); Put(activeEntity, 0x348, 10); Put(activeEntity, 0x2E0, 10);
    activeRoute = Route({11, 12, 13}); nestedRoute = Route({14, 15}); module->stopRequested = false;
    module->settings.animationMode = 3;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const {
    fake::Check(std::string(name) != "xm", "xm must be resolved as a nested type of xq");
    return fake::Class(name);
}
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    fake::Check(declaringClass == fake::Class("xq") && std::string(name) == "xm",
                "Movement state must be resolved from its exact declaring type");
    return fake::Class("xm");
}
Method Api::Find(Klass, const char* name, int argc) const {
    using namespace fake;
    if (std::string(name) == "rjn" && argc == 8) return &start;
    if (std::string(name) == "rfv" && argc == 3) return &position;
    if (std::string(name) == "rjr" && argc == 2) return &next;
    if (std::string(name) == "rjw" && argc == 1) return &finish;
    if (std::string(name) == "daxp" && argc == 0) return &appearance;
    if (std::string(name) == "dbdk" && argc == 1) return &landing;
    Check(std::string(name) == "TryGetValue" && argc == 2, "Unexpected reflection lookup"); return &lookup;
}
void* Api::Native(Method method, uintptr_t rva) const {
    using namespace fake; Check(static_cast<const MethodInfo*>(method)->rva == rva, "Unexpected native method RVA");
    if (rva == start.rva) return reinterpret_cast<void*>(Original);
    if (rva == position.rva) return reinterpret_cast<void*>(Position);
    if (rva == appearance.rva) return reinterpret_cast<void*>(Appearance);
    if (rva == landing.rva) return reinterpret_cast<void*>(Landing);
    if (rva == finish.rva) return reinterpret_cast<void*>(Finish);
    Check(rva == next.rva, "Unexpected native target"); return reinterpret_cast<void*>(Next);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x263F037 || rva == 0x263FB12, "Unexpected landing call-site guard");
    return reinterpret_cast<void*>(rva);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake; Check(method == &lookup && args.size() == 2, "Wrong dictionary lookup ABI");
    if (throwLookup) throw std::runtime_error("Host dictionary invocation failed");
    const auto parameters = args.begin(); auto found = Get(self).entries.find(*static_cast<int64_t*>(parameters[0]));
    *static_cast<Obj*>(parameters[1]) = found == Get(self).entries.end() ? nullptr : found->second;
    return Box(found != Get(self).entries.end());
}
}
// Include the production implementation so the caller filter can be tested
// with saved call-site addresses, without fabricating stack return addresses.
#include "movement.cpp"
namespace fake {
bool ProbeLanding(uintptr_t rva) { return LandingAt(42, nullptr, managed::gameBase + rva); }
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string&) {}
bool InstantCombatEnabled() { return fake::enabled && module->settings.animationMode == 3 && !module->stopRequested; }
bool IsCurrentFightEntity(managed::Obj entity) { return fake::member && entity == fake::activeEntity; }
bool CanDeferInstantMovement() { return fake::canDefer; }
bool DeferInstantMovement(std::function<void()> action) {
    if (!fake::canDefer) return false;
    if (fake::retainJobs) fake::jobs.push_back(std::move(action));
    else action();
    return true;
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x263EE10) {
        fake::nextHook = reinterpret_cast<fake::NextFn>(replacement); *original = reinterpret_cast<void*>(fake::Next); return;
    }
    if (rva == 0x263FA00) {
        fake::finishHook = reinterpret_cast<fake::FinishFn>(replacement); *original = reinterpret_cast<void*>(fake::Finish); return;
    }
    if (rva == 0x78F5D0) {
        fake::landingHook = reinterpret_cast<fake::LandingFn>(replacement); *original = reinterpret_cast<void*>(fake::Landing); return;
    }
    fake::Check(rva == 0x263E860, "Unexpected hook target");
    fake::hook = reinterpret_cast<fake::StartFn>(replacement); *original = reinterpret_cast<void*>(fake::Original);
}

namespace fake {
void PrepareScopeProbe(bool otherEntity, bool otherBehavior, bool unknownClass = false) {
    probeState = Make(unknownClass ? "DerivedXm" : "xm");
    const auto target = otherEntity ? Make("AnimatedEntity") : activeEntity;
    Put(target, 0x1C8, otherEntity ? entityId + 1 : entityId);
    Put(probeState, 0x20, target); Put(probeState, 0x38, activeRoute); Put(probeState, 0x30, activeCallback);
    probeBehavior = otherBehavior ? Make("xq") : activeBehavior;
    if (otherBehavior) {
        Put(probeBehavior, 0x38, Make("Dictionary")); Put(probeBehavior, 0x48, 5);
        Get(Read<Obj>(probeBehavior, 0x38)).entries[Read<int64_t>(target, 0x1C8)] = probeState;
    }
}
void LandingIsolation() {
    Reset(); landingBody = true;
    Check(ProbeLanding(0x263F03C) && ProbeLanding(0x263FB17) && landingHook(42, nullptr),
        "Recognized callers without a movement scope and all other callers must retain native landing");
    for (int mode : {0, 1, 2}) {
        Reset(); landingBody = engageLandingAtStart = true; Put(activeBehavior, 0x48, 5); module->settings.animationMode = mode;
        Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && landingStarts == 1,
            "Every previous animation mode must retain its native landing predicate");
    }
    for (int fallback : {0, 1, 2, 3}) {
        Reset(); landingBody = engageLandingAtStart = true; Put(activeBehavior, 0x48, 5);
        if (fallback == 0) canDefer = false;
        if (fallback == 1) enabled = false;
        if (fallback == 2) member = false;
        if (fallback == 3) Put(Read<Obj>(activeRoute, 0x20), 0x18, 561);
        Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && completions == 0 && landingStarts == 1,
            "Unknown caller, old mode, outside fight or invalid route must keep native landing setup");
    }
    for (int nestedKind : {0, 1, 2, 3}) {
        Reset(); landingBody = true; Put(activeBehavior, 0x48, 5);
        insideInitialNext = [] {
            Check(ProbeLanding(0x263F03C), "Original rjn landing setup must be entirely preserved");
        };
        PrepareScopeProbe(nestedKind == 0, nestedKind == 1, nestedKind == 3);
        insideNext = [] {
            nextHook(probeBehavior, probeState, true, nullptr);
            Check(probeResult, "Nested unrelated or stale state with the same bone inherited landing suppression");
            Check(!ProbeLanding(0x263F03C), "Nested rjr failed to restore the enclosing landing scope");
            Check(ProbeLanding(0x263F03D) && ProbeLanding(0x263FB12) && landingHook(42, nullptr),
                "Only the two exact native return addresses may change the landing predicate");
        };
        insideFinish = [] {
            finishHook(probeBehavior, probeState, nullptr);
            Check(probeResult, "Nested native finish inherited another state's landing suppression");
            Check(!ProbeLanding(0x263FB17), "Nested rjw failed to restore the enclosing landing scope");
        };
        Dispatch(activeRoute);
        Check(starts == 1 && positions == 3 && completions == 1 && landingStarts == 0 && Read<int>(activeEntity, 0xB4) == 6,
            "Scoped landing suppression must preserve the primary movement's cells and idle restoration");
        Check(ProbeLanding(0x263F03C) && ProbeLanding(0x263FB17), "Landing scope leaked after the native calls returned");
    }
    Reset(); landingBody = true; Put(activeBehavior, 0x48, 5); retainJobs = true; Dispatch(activeRoute);
    Check(starts == 1 && initialNextCalls == 1 && landingStarts == 0 && positions == 0 && jobs.size() == 1,
        "Movement must wait until the parent wrapper returns and retain untouched native start");
    auto pending = std::move(jobs); jobs.clear();
    for (auto& job : pending) job();
    pending.clear();
    Check(positions == 3 && completions == 1 && landingStarts == 0 && Read<int>(activeEntity, 0xB4) == 6,
        "Deferred landing movement must recreate its scoped permission during the drain");
    Reset(); landingBody = engageLandingAtStart = true; Put(activeBehavior, 0x48, 5); Dispatch(activeRoute);
    Check(starts == 1 && landingStarts == 1 && Read<bool>(activeEntity, 0xB8) && positions == 0 && completions == 0,
        "A landing tween/listener already engaged by rjn must stay on the native path");
    for (int change : {0, 1, 2}) {
        Reset(); landingBody = true; Put(activeBehavior, 0x48, 5);
        afterLandingPredicate = [change] {
            if (change == 0) module->settings.animationMode = 0;
            if (change == 1) member = false;
            if (change == 2) module->stopRequested = true;
        };
        Dispatch(activeRoute);
        Check(starts == 1 && positions == 3 && completions == 1 && landingStarts == 0 && Read<int>(activeEntity, 0xB4) == 6,
            "Once landing suppression commits, its owned transaction must finish idle cleanup despite mode/fight/stop changes");
    }
    Reset(); landingBody = true; Put(activeBehavior, 0x48, 5); retainJobs = true; Dispatch(activeRoute);
    enabled = false;
    pending = std::move(jobs); jobs.clear();
    for (auto& job : pending) job();
    pending.clear();
    Check(starts == 1 && positions == 0 && completions == 0 && !Read<bool>(activeEntity, 0xB8) &&
        ProbeLanding(0x263F03C) && ProbeLanding(0x263FB17),
        "Before commitment, a mode change must leave all native landing behavior available");
    Reset(); landingBody = true; Put(activeBehavior, 0x48, 5);
    insideFinish = [] { throw 7; };
    bool propagated = false;
    try { Dispatch(activeRoute); } catch (int value) { propagated = value == 7; }
    Check(propagated && starts == 1 && finishCalls == 1 && completions == 0 && module->stopRequested &&
        ProbeLanding(0x263F03C) && ProbeLanding(0x263FB17),
        "Native finish exception must propagate once and unwind all landing scopes");
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
        Module state{L"offline-movement-tests"}; module = &state; InstallInstantMovement();
        for (int kind : {2, 3, 5, 7}) {
            Reset(); Put(activeBehavior, 0x48, kind); Dispatch(activeRoute);
            Check(starts == 1 && positions == 3 && completions == 1 && nextCalls == 3 && visited == std::vector<int>({11, 12, 13}),
                  "Ordinary movement must visit every cell and complete exactly once");
            Check(Read<int>(activeEntity, 0x2E0) == 13 && Read<int>(activeEntity, 0xB4) == 6, "Final native position/orientation lost");
        }
        for (int kind : {0, 1, 4, 6, 8}) {
            Reset(); Put(activeBehavior, 0x48, kind); Dispatch(activeRoute);
            Check(starts == 1 && positions == 0 && completions == 0, "Special movement must remain native");
        }
        Reset(); landingBody = true; Put(activeBehavior, 0x48, 5); Dispatch(activeRoute);
        Check(starts == 1 && positions == 3 && completions == 1 && landingStarts == 0 && finishCalls == 1 &&
            Read<int>(activeEntity, 0xB4) == 6, "Kind-5 landing must skip tween/listener and restore native idle");
        Reset(); enabled = false; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Disabled mode must remain native");
        Reset(); member = false; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Unknown/noncombat entity must remain native");
        Reset(); Put(Read<Obj>(activeRoute, 0x20), 0x18, 561); Dispatch(activeRoute);
        Check(starts == 1 && positions == 0, "Oversized route must remain native");
        Reset(); replaceOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 1 && nextCalls == 0 && completions == 0, "Cell callback replacement must stop draining old state");
        Reset(); reenterOnEnd = true; Dispatch(activeRoute);
        Check(starts == 2 && positions == 3 && completions == 1, "Completion must not drain/recomplete a reentrant movement");
        Reset(); throwOnCell = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 1 && nextCalls == 0 && module->stopRequested,
              "Host exception after partial dispatch must stop without restarting or escaping");
        Reset(); throwLookup = true; Dispatch(activeRoute);
        Check(starts == 1 && positions == 0 && nextCalls == 0 && module->stopRequested,
              "Host lookup failure must stop without restarting the already started movement");
        Reset(); nativeThrowOnCell = true; bool threw = false;
        try { Dispatch(activeRoute); } catch (int) { threw = true; }
        Check(threw && starts == 1 && positions == 1 && nextCalls == 0 && module->stopRequested,
              "Native exception after partial dispatch must propagate without restarting");
        LandingIsolation();
        Check(handles.empty(), "Temporary movement GC references leaked");
        module = nullptr;
        std::cout << "PASS: actual movement hook ABI, ordinary/special modes, cells, completion, replacement, reentry and exceptions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
