#include "features.hpp"
#include "module.hpp"
#include "options-ui.hpp"
#include "instant-combat.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

// Link the actual throw-movement.cpp against an in-memory managed runtime.
// This executable never enumerates processes or loads a game/helper DLL.
void InstallInstantThrowMovement();

namespace fake {
using namespace managed;
using StartFn = void(*)(Obj, Obj, float, Obj, Obj, uint64_t, uint64_t, uint64_t, Obj, Method);
using FinishFn = void(*)(Obj, Obj, Method);
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Type { std::string name; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};
    Type* type{};
    bool boolean = false;
    bool attachedToCarrier = false;
    float heightOffset = 0.0f;
    int finalCell = 0;
    std::map<int64_t, Obj> entries;
    std::set<int64_t> pending;
};
struct MethodInfo { std::string name; uintptr_t rva; };
// Exact declarations: movement-r6-yd.cs and special-r7-ye.cs.
MethodInfo start{"rjn", 0x264A8D0}, kill{"rmd", 0x264FC80}, arrive{"rlz", 0x264B480}, lookup{"TryGetValue", 0};
MethodInfo finish{"rmb", 0x264B810}, cancel{"rma", 0x264B6D0}, contains{"Contains", 0};
std::map<std::string, Type> classes;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
StartFn hook{};
FinishFn finishHook{};
bool inCombat = true, member = true, bounce = true, registerState = true;
bool allowDefer = true;
bool retainDeferred = false;
std::vector<std::function<void()>> deferred;
bool completeAtStart = false, retainOnArrival = false;
bool throwClass = false, throwLookup = false, throwContains = false;
int throwOriginal = 0, throwKill = 0, throwArrival = 0, throwFinish = 0;
unsigned starts = 0, kills = 0, arrivals = 0, completions = 0, finishes = 0, cancels = 0;
const char* stateType = "ye";
Obj behavior{}, entity{}, route{}, callback{}, speedObject{}, lastState{};
std::vector<std::string> events, logs;
std::vector<Obj> killedStates, arrivedStates, createdStates;
std::function<void(Obj)> afterStart, afterKill, atArrival, afterCompletion;
constexpr int64_t entityId = -900123456789LL;
constexpr uint64_t firstArg = 0x8877665544332211ULL;
constexpr uint64_t secondArg = 0xFEDCBA9876543210ULL;
constexpr uint64_t thirdArg = 0x1020304050607080ULL;
constexpr float speedArg = 1.375f;
Method callerMethod = reinterpret_cast<Method>(0x12345);

Type* Class(const std::string& name) {
    auto [entry, inserted] = classes.try_emplace(name, Type{name});
    return &entry->second;
}
Object& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Object*>(object); }
Obj Make(const char* name) {
    auto value = std::make_unique<Object>();
    value->type = Class(name);
    auto result = value.get();
    objects.push_back(std::move(value));
    return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).memory.size(), "Fake field outside storage");
    std::memcpy(Get(object).memory.data() + offset, &value, sizeof(value));
}
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
Obj Route(int finalCell = 13) {
    auto result = Make("enw");
    auto points = Make("List");
    Put(result, 0x20, points);
    Put(points, 0x18, 3);
    Get(result).finalCell = finalCell;
    return result;
}
Obj Dictionary() { return Read<Obj>(behavior, 0x20); }
Obj Current() {
    if (!Dictionary()) return nullptr;
    auto& entries = Get(Dictionary()).entries;
    const auto found = entries.find(entityId);
    return found == entries.end() ? nullptr : found->second;
}
Obj NewState(Obj path, Obj endCallback) {
    const auto state = Make(stateType);
    Put(state, 0x10, entityId);
    Put(state, 0x18, entity);
    Put(state, 0x30, bounce);
    Put(state, 0x38, path);
    Put(state, 0x40, endCallback);
    const auto tween = Make("Tween");
    Put(tween, 0xE8, true);
    Put(tween, 0xE9, false);
    Put(tween, 0x111, false);
    Put(state, 0x48, tween);
    Put(state, 0x50, static_cast<Obj>(nullptr));
    createdStates.push_back(state);
    return state;
}
void Once(std::function<void(Obj)>& operation, Obj state) {
    auto run = std::move(operation);
    operation = {};
    if (run) run(state);
}
void ThrowIf(int kind, const char* message) {
    if (kind == 1) throw std::runtime_error(message);
    if (kind == 2) throw 7;
}
void Dispatch(Obj path = nullptr) {
    hook(behavior, entity, speedArg, path ? path : route, callback,
        firstArg, secondArg, thirdArg, speedObject, callerMethod);
}
void Original(Obj owner, Obj target, float speed, Obj path, Obj endCallback,
              uint64_t first, uint64_t second, uint64_t third, Obj entitySpeed, Method method) {
    Check(owner == behavior && target == entity && speed == speedArg && endCallback == callback &&
        first == firstArg && second == secondArg && third == thirdArg && entitySpeed == speedObject && method == callerMethod,
        "Throw start ABI or original arguments changed");
    ++starts;
    events.emplace_back("start");
    Get(entity).attachedToCarrier = false;
    events.emplace_back("detach");
    ThrowIf(throwOriginal, "Original native start failed");
    if (completeAtStart) { ++completions; events.emplace_back("callback"); return; }
    if (!registerState || !Dictionary()) return;
    lastState = NewState(path, endCallback);
    Get(Dictionary()).entries[entityId] = lastState;
    Once(afterStart, lastState);
}
void Kill(Obj state, Method method) {
    Check(method == nullptr && state != nullptr && !Get(entity).attachedToCarrier,
        "Flight kill must follow the original detach and preserve its ABI");
    ++kills;
    events.emplace_back("kill");
    killedStates.push_back(state);
    // ye.rmd kills the flight tween and clears +0x48; native completion owns
    // the cancellation source and final cleanup, not this helper.
    Put(state, 0x48, static_cast<Obj>(nullptr));
    Once(afterKill, state);
    ThrowIf(throwKill, "Flight kill callback failed");
}
void Complete(Obj state, bool cancelled = false) {
    if (!cancelled) Get(entity).heightOffset = 0.0f;
    Put(state, 0x48, static_cast<Obj>(nullptr));
    Put(state, 0x50, static_cast<Obj>(nullptr));
    Check(Current() == state, "Native arrival reached an obsolete state");
    Get(Dictionary()).entries.erase(entityId);
    events.emplace_back("remove");
    Check(Current() == nullptr, "Native completion callback must follow removal");
    ++completions;
    events.emplace_back("callback");
    Once(afterCompletion, state);
}
void NativeFinish(Obj owner, Obj state, Method) {
    Check(owner == behavior, "Native rmb owner argument changed");
    ++finishes;
    ThrowIf(throwFinish, "Native finish callback failed");
    Complete(state);
}
void Cancel(Obj owner, Obj state, Method method) {
    Check(owner == behavior && method == nullptr, "Native rma cancellation ABI changed");
    ++cancels;
    Complete(state, true);
    Get(Read<Obj>(behavior, 0x28)).pending.erase(Read<int64_t>(state, 0x10));
}
void Arrive(Obj owner, Obj state, Method method) {
    Check(owner == behavior && method == nullptr && !Get(entity).attachedToCarrier,
        "Native arrival ABI changed or parent detach was skipped");
    Check(Read<Obj>(state, 0x48) == nullptr && !Read<bool>(state, 0x30),
        "Instant arrival requires flight killed first and only its bounce disabled");
    ++arrivals;
    events.emplace_back("arrival");
    arrivedStates.push_back(state);
    const auto path = Read<Obj>(state, 0x38);
    Put(entity, 0x348, Get(path).finalCell);
    Put(entity, 0x2E0, Get(path).finalCell);
    // Native final placement preserves facing; notifications inside qgb may
    // start or cancel another movement before rlz reaches rmb.
    Once(atArrival, state);
    ThrowIf(throwArrival, "Native arrival callback failed");
    if (!retainOnArrival) finishHook(owner, state, nullptr);
}
void NativeLaterUpdate() {
    // A completed throw has no live tween/registered state left to finish.
    if (const auto state = Current(); state && Read<Obj>(state, 0x48)) {
        Put(entity, 0x348, Get(Read<Obj>(state, 0x38)).finalCell);
        Put(entity, 0x2E0, Get(Read<Obj>(state, 0x38)).finalCell);
        finishHook(behavior, state, nullptr);
    }
}
void Reset() {
    Check(deferred.empty(), "Previous throw case retained deferred jobs");
    Check(handles.empty(), "Temporary throw GC references leaked from previous case");
    inCombat = member = bounce = registerState = allowDefer = true;
    retainDeferred = false;
    completeAtStart = retainOnArrival = throwClass = throwLookup = throwContains = false;
    throwOriginal = throwKill = throwArrival = throwFinish = 0;
    starts = kills = arrivals = completions = finishes = cancels = 0;
    stateType = "ye";
    events.clear(); logs.clear(); killedStates.clear(); arrivedStates.clear(); createdStates.clear();
    afterStart = {}; afterKill = {}; atArrival = {}; afterCompletion = {};
    behavior = Make("yd"); entity = Make("AnimatedEntity"); callback = Make("Action"); speedObject = Make("EntitySpeed");
    Put(behavior, 0x20, Make("Dictionary"));
    Put(behavior, 0x28, Make("HashSet"));
    Put(entity, 0x1C8, entityId); Put(entity, 0x348, 10); Put(entity, 0x2E0, 10); Put(entity, 0xB4, 2);
    Get(entity).attachedToCarrier = true;
    Get(entity).heightOffset = 24.0f;
    route = Route(); lastState = nullptr;
    module->settings.animationMode = 3;
    module->stopRequested = false;
}
void CheckFallback(const char* message) {
    Check(starts == 1 && kills == 0 && arrivals == 0 && completions == 0, message);
    Check(!Get(entity).attachedToCarrier && Get(entity).heightOffset == 24.0f,
        "Fallback must run native start and leave subsequent movement alone");
    if (Current()) Check(Read<bool>(Current(), 0x30) == bounce, "Fallback changed bounce preference");
}
void RunDeferred() {
    auto jobs = std::move(deferred);
    deferred.clear();
    for (auto& job : jobs) {
        try { job(); }
        catch (const std::exception& error) {
            logs.push_back(std::string("MOVEMENT_DISPATCH_ERROR ") + error.what());
            module->stopRequested = true;
            break;
        } catch (...) { module->stopRequested = true; throw; }
    }
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    using namespace fake;
    const auto& type = static_cast<fake::Type*>(klass)->name;
    const std::string methodName = name;
    if (type == "yd" && methodName == "rjn" && argc == 8) return &start;
    if (type == "yd" && methodName == "rlz" && argc == 1) return &arrive;
    if (type == "yd" && methodName == "rmb" && argc == 1) return &finish;
    if (type == "yd" && methodName == "rma" && argc == 1) return &cancel;
    if (type == "ye" && methodName == "rmd" && argc == 0) return &kill;
    if (type == "HashSet" && methodName == "Contains" && argc == 1) return &contains;
    Check(type == "Dictionary" && methodName == "TryGetValue" && argc == 2, "Unexpected throw reflection lookup");
    return &lookup;
}
void* Api::Native(Method method, uintptr_t rva) const {
    using namespace fake;
    Check(static_cast<const MethodInfo*>(method)->rva == rva, "Throw method resolved at an unexpected RVA");
    if (rva == start.rva) return reinterpret_cast<void*>(Original);
    if (rva == kill.rva) return reinterpret_cast<void*>(Kill);
    if (rva == finish.rva) return reinterpret_cast<void*>(NativeFinish);
    if (rva == cancel.rva) return reinterpret_cast<void*>(Cancel);
    Check(rva == arrive.rva, "Unexpected throw native target");
    return reinterpret_cast<void*>(Arrive);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake;
    if (method == &contains) {
        Check(args.size() == 1, "Wrong throw cancellation set lookup ABI");
        if (throwContains) throw std::runtime_error("Throw cancellation lookup failed");
        return Box(Get(self).pending.contains(*static_cast<int64_t*>(*args.begin())));
    }
    Check(method == &lookup && args.size() == 2, "Wrong throw registry lookup ABI");
    if (throwLookup) throw std::runtime_error("Throw dictionary invocation failed");
    const auto parameters = args.begin();
    const auto found = Get(self).entries.find(*static_cast<int64_t*>(parameters[0]));
    *static_cast<Obj*>(parameters[1]) = found == Get(self).entries.end() ? nullptr : found->second;
    return Box(found != Get(self).entries.end());
}
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string& message) { fake::logs.push_back(message); }
bool InstantCombatEnabled() {
    return module && module->settings.animationMode == 3 && fake::inCombat && !module->stopRequested;
}
bool IsCurrentFightEntity(managed::Obj target) { return fake::member && target == fake::entity; }
bool DeferInstantMovement(std::function<void()> finish) {
    if (!fake::allowDefer) return false;
    if (fake::retainDeferred) { fake::deferred.push_back(std::move(finish)); return true; }
    finish();
    return true;
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x264B810) {
        fake::finishHook = reinterpret_cast<fake::FinishFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeFinish);
        return;
    }
    fake::Check(rva == 0x264A8D0, "Unexpected throw hook target");
    fake::hook = reinterpret_cast<fake::StartFn>(replacement);
    *original = reinterpret_cast<void*>(fake::Original);
}

namespace fake {
void SuccessfulCompletion() {
    for (bool originalBounce : {false, true}) {
        Reset(); bounce = originalBounce; Dispatch();
        Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 1,
            "An eligible throw must start, kill flight and complete exactly once");
        Check(events == std::vector<std::string>({"start", "detach", "kill", "arrival", "remove", "callback"}),
            "Throw completion ordering changed");
        Check(Current() == nullptr && Read<Obj>(lastState, 0x48) == nullptr && Read<Obj>(lastState, 0x50) == nullptr,
            "Native throw cleanup must release flight/cancellation and registry state");
        Check(Read<int>(entity, 0x348) == 13 && Read<int>(entity, 0x2E0) == 13 && Read<int>(entity, 0xB4) == 2 &&
            Get(entity).heightOffset == 0.0f, "Native final cell, orientation or offset cleanup was lost");
        Check(Read<bool>(lastState, 0x30) == originalBounce,
            "Temporary bounce suppression must restore its held original state after completion");
        NativeLaterUpdate();
        Check(completions == 1, "Later native update completed an already removed throw");
    }
}
void EligibilityFallbacks() {
    for (int mode : {0, 1, 2, 4, -1}) {
        Reset(); module->settings.animationMode = mode; Dispatch(); CheckFallback("Non-instant animation mode must remain native");
    }
    Reset(); inCombat = false; Dispatch(); CheckFallback("Outside-combat throw must remain native");
    Reset(); member = false; Dispatch(); CheckFallback("Unknown fighter must remain native");
    Reset(); allowDefer = false; Dispatch(); CheckFallback("Unknown movement caller must keep its ordinary native throw");
    Reset(); Get(behavior).type = Class("DerivedYd"); Dispatch(); CheckFallback("Unknown behavior type must remain native");
    Reset(); Get(route).type = Class("DerivedEnw"); Dispatch(); CheckFallback("Unknown route type must remain native");
    Reset(); stateType = "DerivedYe"; Dispatch(); CheckFallback("Unknown registered state type must remain native");
    Reset(); Put(behavior, 0x20, static_cast<Obj>(nullptr)); Dispatch(); CheckFallback("Missing registry must remain native");
    Reset(); Put(behavior, 0x28, static_cast<Obj>(nullptr)); Dispatch(); CheckFallback("Missing cancellation set must remain native");
    Reset(); registerState = false; Dispatch(); CheckFallback("Absent registered state must remain native");
    Reset(); Get(Read<Obj>(behavior, 0x28)).pending.insert(entityId); Dispatch();
    CheckFallback("Already-cancelled throw must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x48, static_cast<Obj>(nullptr)); }; Dispatch();
    CheckFallback("Missing flight tween must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x50, Make("CancellationTokenSource")); }; Dispatch();
    CheckFallback("An existing asynchronous bounce must remain native");
    Reset(); afterStart = [](Obj state) { Put(Read<Obj>(state, 0x48), 0x88, Make("Action")); }; Dispatch();
    CheckFallback("Unexpected OnKill subscriber must remain native");
    Reset(); afterStart = [](Obj state) { Put(Read<Obj>(state, 0x48), 0xE8, false); }; Dispatch();
    CheckFallback("Inactive flight tween must remain native");
    Reset(); afterStart = [](Obj state) { Put(Read<Obj>(state, 0x48), 0xE9, true); }; Dispatch();
    CheckFallback("Sequenced flight tween must remain native");
    Reset(); afterStart = [](Obj state) { Put(Read<Obj>(state, 0x48), 0x111, true); }; Dispatch();
    CheckFallback("Completed flight tween must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x10, entityId + 1); }; Dispatch();
    CheckFallback("State belonging to another entity ID must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x18, Make("AnimatedEntity")); }; Dispatch();
    CheckFallback("State entity identity mismatch must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x38, Route(99)); }; Dispatch();
    CheckFallback("State route identity mismatch must remain native");
    Reset(); afterStart = [](Obj state) { Put(state, 0x40, Make("Action")); }; Dispatch();
    CheckFallback("State callback identity mismatch must remain native");
    Reset(); afterStart = [](Obj) { member = false; }; Dispatch();
    CheckFallback("Lost fighter membership after native start must block acceleration");
    Reset(); completeAtStart = true; Dispatch();
    Check(starts == 1 && kills == 0 && arrivals == 0 && completions == 1,
        "Synchronous native completion must not be completed again");
}
void ReentryAndReplacement() {
    // Identical input pointers still represent a new movement when native
    // callbacks recursively call start. The parent frame must be invalidated.
    Reset(); afterStart = [](Obj) { Dispatch(route); }; Dispatch();
    Check(starts == 2 && kills == 0 && arrivals == 0 && completions == 0 && Current() == createdStates.back(),
        "Start reentry with identical inputs must invalidate both acceleration attempts");
    Reset(); afterKill = [](Obj) { Dispatch(route); }; Dispatch();
    Check(starts == 2 && kills == 1 && arrivals == 0 && completions == 0 && Read<bool>(Current(), 0x30),
        "Kill reentry must not arrive or change bounce on a replacement throw");
    Reset(); afterCompletion = [](Obj) { Dispatch(Route(17)); }; Dispatch();
    Check(starts == 2 && kills == 1 && arrivals == 1 && completions == 1 && Current() == createdStates.back() &&
        Read<bool>(Current(), 0x30) && Read<Obj>(Current(), 0x48) != nullptr,
        "End callback reentry must leave the replacement throw native");
    Reset(); afterKill = [](Obj) { Get(Dictionary()).entries[entityId] = NewState(Route(99), callback); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 0 && completions == 0 && Read<bool>(Current(), 0x30),
        "Registry replacement during kill must stop before arrival");
    Reset(); afterKill = [](Obj) { Put(behavior, 0x20, Make("Dictionary")); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 0 && completions == 0,
        "Registry object replacement during kill must stop before arrival");
    Reset(); afterKill = [](Obj state) { Put(state, 0x40, Make("Action")); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 0 && Read<bool>(lastState, 0x30),
        "Callback mutation during kill must leave the original bounce untouched");
    Reset(); afterKill = [](Obj) { member = false; }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 1,
        "Committed throw must finish even if fighter membership changes after flight kill");
    Reset(); afterKill = [](Obj) { module->settings.animationMode = 0; }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 1,
        "Committed throw must finish even if animation mode changes after flight kill");
    Reset(); afterKill = [](Obj) { module->stopRequested = true; }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 1,
        "Committed throw must finish even if stop is requested after flight kill");
    Reset(); atArrival = [](Obj) { Dispatch(route); }; Dispatch();
    Check(starts == 2 && kills == 1 && arrivals == 1 && finishes == 0 && completions == 0 &&
        Read<bool>(Current(), 0x30) && Read<bool>(createdStates.front(), 0x30),
        "Arrival reentry must skip native ID-based removal and restore only the held old state");
    Reset(); atArrival = [](Obj) { Get(Dictionary()).entries[entityId] = NewState(route, callback); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && finishes == 0 && completions == 0 &&
        Current() == createdStates.back() && Read<bool>(createdStates.front(), 0x30),
        "State replacement during final placement must not be removed by old native rmb");
    Reset(); atArrival = [](Obj) { Get(Dictionary()).entries.erase(entityId); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && finishes == 0 && completions == 0,
        "State removal during final placement must not receive a second completion");
    Reset(); atArrival = [](Obj) { Get(Read<Obj>(behavior, 0x28)).pending.insert(entityId); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && finishes == 0 && cancels == 1 && completions == 1 &&
        Current() == nullptr && Get(Read<Obj>(behavior, 0x28)).pending.empty(),
        "Cancellation during final placement must use native rma and clear its cancellation entry");
    Reset(); module->settings.animationMode = 0; Dispatch();
    finishHook(behavior, Current(), callerMethod);
    Check(starts == 1 && kills == 0 && arrivals == 0 && finishes == 1 && completions == 1,
        "Unscoped native rmb must pass through unchanged in old animation modes");
}
void BounceRestorationAndFailures() {
    for (bool originalBounce : {false, true}) {
        Reset(); bounce = originalBounce; retainOnArrival = true; Dispatch();
        Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 0 &&
            Read<bool>(Current(), 0x30) == originalBounce,
            "An unfinished still-owned native arrival must regain its original bounce flag");
    }
    Reset(); throwClass = true; Dispatch();
    Check(starts == 1 && kills == 0 && arrivals == 0 && module->stopRequested,
        "Eligibility exception must fall back to native start exactly once and request stop");
    Reset(); throwLookup = true; Dispatch();
    Check(starts == 1 && kills == 0 && arrivals == 0 && module->stopRequested,
        "Registry exception must not replay already-started movement");
    Reset(); throwContains = true; Dispatch();
    Check(starts == 1 && kills == 0 && arrivals == 0 && module->stopRequested,
        "Cancellation lookup failure must not replay already-started movement");
    Reset(); throwKill = 1; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 0 && module->stopRequested && Read<bool>(lastState, 0x30),
        "Host kill exception must stop without arrival or original restart");
    Reset(); throwArrival = 1; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 0 && module->stopRequested &&
        Read<bool>(lastState, 0x30), "Host arrival exception must restore the still-owned bounce and stop");
    Reset(); throwFinish = 1; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && finishes == 1 && completions == 0 &&
        module->stopRequested && Read<bool>(lastState, 0x30),
        "Host finish exception must restore bounce without native start replay");
    Reset(); afterCompletion = [](Obj) { throw std::runtime_error("Completion notification failed"); }; Dispatch();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 1 && Current() == nullptr &&
        module->stopRequested && Read<bool>(lastState, 0x30),
        "Failure after completion must not replay the callback or touch a new state");
    for (int stage : {0, 1}) {
        Reset();
        if (stage == 0) throwKill = 2; else throwArrival = 2;
        bool propagated = false;
        try { Dispatch(); } catch (int value) { propagated = value == 7; }
        Check(propagated && starts == 1 && kills == 1 && arrivals == static_cast<unsigned>(stage) &&
            completions == 0 && module->stopRequested && Read<bool>(lastState, 0x30),
            "Native exception must propagate after cleanup without replaying original movement");
    }
    for (int kind : {1, 2}) {
        Reset(); throwOriginal = kind;
        bool propagated = false;
        try { Dispatch(); } catch (const std::runtime_error&) { propagated = kind == 1; }
        catch (int value) { propagated = kind == 2 && value == 7; }
        Check(propagated && starts == 1 && kills == 0 && arrivals == 0,
            "Original start exception must retain native propagation and must never restart");
    }
}
void DeferredStateLifecycle() {
    Reset(); retainDeferred = true; Dispatch();
    Check(starts == 1 && kills == 0 && arrivals == 0 && deferred.size() == 1 && !handles.empty(),
        "Throw must retain its managed objects while waiting for parent native notifications");
    RunDeferred();
    Check(kills == 1 && arrivals == 1 && completions == 1 && handles.empty(),
        "Queued throw must finish once and release held objects after draining");

    Reset(); retainDeferred = true; Dispatch();
    const auto firstState = Current();
    Dispatch(); // Same route and callback, distinct native ye allocation.
    Check(starts == 2 && deferred.size() == 2 && Current() != firstState,
        "Second native start must replace the old registered state");
    RunDeferred();
    Check(kills == 1 && arrivals == 1 && completions == 1 && killedStates.front() == createdStates.back() &&
        Read<Obj>(firstState, 0x48) != nullptr && Read<bool>(firstState, 0x30) && handles.empty(),
        "Queued old throw must not finish a replacement with identical route/callback pointers");

    for (int mutation : {0, 1, 2, 3, 4, 5, 6}) {
        Reset(); retainDeferred = true; Dispatch();
        const auto state = Current();
        switch (mutation) {
        case 0: Get(Dictionary()).entries.erase(entityId); break;
        case 1: Get(Read<Obj>(behavior, 0x28)).pending.insert(entityId); break;
        case 2: member = false; break;
        case 3: module->settings.animationMode = 0; break;
        case 4: Put(state, 0x40, Make("Action")); break;
        case 5: Put(state, 0x50, Make("CancellationTokenSource")); break;
        case 6: Put(Read<Obj>(state, 0x48), 0xE8, false); break;
        }
        RunDeferred();
        Check(starts == 1 && kills == 0 && arrivals == 0 && completions == 0 && Read<bool>(state, 0x30) && handles.empty(),
            "Queued throw must revalidate state, cancellation, combat mode and flight before committing");
    }

    Reset(); retainDeferred = true; Dispatch();
    deferred.clear(); // Parent rfw returned false or threw: discard without running.
    Check(starts == 1 && kills == 0 && arrivals == 0 && Current() != nullptr && handles.empty(),
        "Discarded wrapper transaction must release captures and preserve ordinary native movement");

    Reset(); retainDeferred = true; throwArrival = 1; Dispatch(); RunDeferred();
    Check(starts == 1 && kills == 1 && arrivals == 1 && completions == 0 && module->stopRequested &&
        Read<bool>(lastState, 0x30) && handles.empty(),
        "Deferred host exception must restore bounce and release captures through dispatcher policy");
}
}

int main() {
    using namespace fake;
    try {
        api.object_get_class = [](Obj object) -> Klass {
            if (throwClass) { throwClass = false; throw std::runtime_error("Class lookup failed"); }
            return Get(object).type;
        };
        api.object_unbox = [](Obj object) -> void* { return &Get(object).boolean; };
        api.gchandle_new = [](Obj object, bool) -> GCHandle { auto id = ++nextHandle; handles.emplace(id, object); return id; };
        api.gchandle_get_target = [](GCHandle id) -> Obj { return handles.at(id); };
        api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "GC handle freed twice"); };
        Module state{L"offline-throw-tests"}; module = &state;
        InstallInstantThrowMovement();
        SuccessfulCompletion();
        EligibilityFallbacks();
        ReentryAndReplacement();
        BounceRestorationAndFailures();
        DeferredStateLifecycle();
        Check(handles.empty(), "Temporary throw GC references leaked");
        module = nullptr;
        std::cout << "PASS: actual throw hook ABI, gates, detach, flight kill, native arrival, cleanup, reentry and exceptions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
