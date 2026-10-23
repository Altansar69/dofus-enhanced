#include "appearance/features.hpp"
#include "core/module.hpp"
#include "ui/options-ui.hpp"
#include "animations/instant-combat.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Exercise the actual movement-dispatch.cpp with synthetic native calls only.
// No client, process enumeration, memory access, helper or injection is used.
namespace fake {
using namespace managed;
using MoveFn = bool(*)(Obj, Obj, Obj, uint64_t, uint64_t, uint64_t, Method);
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Type { const char* name; };
struct MethodInfo { uintptr_t rva; };
Type entityClass{"AnimatedEntity"};
MethodInfo movement{0x2616140};
MoveFn hook{};
std::function<bool()> nativeBody;
std::vector<std::string> events, logs;
unsigned originalCalls = 0, nativeDepth = 0, liveCaptures = 0;
Obj entity = reinterpret_cast<Obj>(0x123456789ABCULL);
Obj route = reinterpret_cast<Obj>(0xABCDEF123456ULL);
Obj behavior = reinterpret_cast<Obj>(0xFEDCBA654321ULL);
Method callerMethod = reinterpret_cast<Method>(0x778899AABBCCULL);
constexpr uint64_t firstArg = 0x8877665544332211ULL;
constexpr uint64_t secondArg = 0xFEDCBA9876543210ULL;
constexpr uint64_t thirdArg = 0x1020304050607080ULL;
struct Capture {
    Capture() { ++liveCaptures; }
    ~Capture() { --liveCaptures; }
};
bool Original(Obj target, Obj path, Obj movementBehavior, uint64_t first, uint64_t second, uint64_t third, Method method) {
    Check(target == entity && path == route && movementBehavior == behavior && first == firstArg &&
        second == secondArg && third == thirdArg && method == callerMethod,
        "rfw ABI or original arguments changed");
    ++originalCalls;
    struct Depth { Depth() { ++nativeDepth; } ~Depth() { --nativeDepth; } } depth;
    const auto run = nativeBody;
    Check(static_cast<bool>(run), "Missing fake native transaction");
    return run();
}
bool Move() { return hook(entity, route, behavior, firstArg, secondArg, thirdArg, callerMethod); }
void Reset() {
    Check(nativeDepth == 0 && liveCaptures == 0, "Previous dispatch leaked frame state or queued captures");
    originalCalls = 0;
    nativeBody = {};
    events.clear(); logs.clear();
    module->stopRequested = false;
    unsigned unexpected = 0;
    Check(!DeferInstantMovement([&] { ++unexpected; }) && unexpected == 0,
        "No queue may survive outside the native wrapper");
}
bool Queue(const char* name) {
    auto capture = std::make_shared<Capture>();
    return DeferInstantMovement([name = std::string(name), capture] {
        Check(nativeDepth == 0, "Terminal action ran before the outer native rfw returned");
        events.push_back(name);
    });
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const {
    fake::Check(std::string(name) == "AnimatedEntity", "Unexpected dispatch class");
    return &fake::entityClass;
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    fake::Check(klass == &fake::entityClass && std::string(name) == "rfw" && argc == 5,
        "Unexpected dispatch method or parameter count");
    return &fake::movement;
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(method == &fake::movement && rva == 0x2616140, "Unexpected rfw RVA");
    return reinterpret_cast<void*>(fake::Original);
}
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string& message) { fake::logs.push_back(message); }
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    fake::Check(rva == 0x2616140, "Unexpected dispatch hook target");
    fake::hook = reinterpret_cast<fake::MoveFn>(replacement);
    *original = reinterpret_cast<void*>(fake::Original);
}

namespace fake {
void NativeOrdering() {
    Reset();
    nativeBody = [] {
        events.emplace_back("rjn");
        Check(Queue("end"), "Known wrapper must accept a deferred finish");
        Check(events == std::vector<std::string>{"rjn"}, "Finish ran during native start");
        events.emplace_back("followers");
        events.emplace_back("start-event");
        return true;
    };
    Check(Move(), "Successful native start result changed");
    events.emplace_back("caller-return");
    Check(originalCalls == 1 && events == std::vector<std::string>({"rjn", "followers", "start-event", "end", "caller-return"}),
        "Finalization must follow native follower/start notifications and precede wrapper return");
    Check(liveCaptures == 0, "Completed dispatch retained a captured movement");
}
void NestedWrapperOrdering() {
    Reset();
    nativeBody = [] {
        if (nativeDepth == 1) {
            events.emplace_back("outer-rjn");
            Check(Queue("outer-end"), "Outer queue rejected");
            Check(Move(), "Nested start result changed");
            Check(events == std::vector<std::string>({"outer-rjn", "inner-rjn", "inner-start"}),
                "Nested wrapper drained before its parent native transaction ended");
            events.emplace_back("outer-followers");
            events.emplace_back("outer-start");
        } else {
            events.emplace_back("inner-rjn");
            Check(Queue("inner-end"), "Nested queue rejected");
            events.emplace_back("inner-start");
        }
        return true;
    };
    Check(Move() && originalCalls == 2, "Nested wrapper must call each native original once");
    Check(events == std::vector<std::string>({"outer-rjn", "inner-rjn", "inner-start", "outer-followers", "outer-start", "outer-end", "inner-end"}),
        "Nested jobs must drain in queue order only after the outer wrapper returns");
}
void FailedAndThrowingNativeTransactions() {
    for (int failure : {0, 1, 2}) {
        Reset();
        nativeBody = [failure] {
            Check(Queue("must-not-run"), "Failure fixture queue rejected");
            if (failure == 1) throw std::runtime_error("Native wrapper failed");
            if (failure == 2) throw 7;
            return false;
        };
        bool result = true, propagated = false;
        try { result = Move(); }
        catch (const std::runtime_error&) { propagated = failure == 1; }
        catch (int value) { propagated = failure == 2 && value == 7; }
        Check((failure == 0 ? !result : propagated) && originalCalls == 1 && events.empty() && liveCaptures == 0,
            "Rejected or throwing native start must discard jobs and retain its result/exception");
    }
    for (int innerFailure : {0, 1, 2}) {
        Reset();
        nativeBody = [innerFailure] {
            if (nativeDepth == 1) {
                Check(Queue("before"), "Outer pre-checkpoint queue rejected");
                bool rejected = false;
                try { rejected = !Move(); }
                catch (const std::runtime_error&) { rejected = innerFailure == 1; }
                catch (int value) { rejected = innerFailure == 2 && value == 7; }
                Check(rejected, "Nested failure did not preserve native semantics");
                Check(Queue("after"), "Outer post-checkpoint queue rejected");
                return true;
            }
            Check(Queue("discard-inner"), "Nested failure queue rejected");
            if (innerFailure == 1) throw std::runtime_error("Inner native failure");
            if (innerFailure == 2) throw 7;
            return false;
        };
        Check(Move() && originalCalls == 2 && events == std::vector<std::string>({"before", "after"}) && liveCaptures == 0,
            "Nested failure must roll back only actions added since its own checkpoint");
    }
    Reset();
    nativeBody = [] {
        Check(Queue(nativeDepth == 1 ? "discard-outer" : "discard-child"), "Outer rollback queue rejected");
        if (nativeDepth == 1) { Check(Move(), "Nested successful result changed"); return false; }
        return true;
    };
    Check(!Move() && originalCalls == 2 && events.empty() && liveCaptures == 0,
        "Failed outer transaction must discard its successful child's queued jobs too");
}
void QueueAndRecursionBounds() {
    Reset();
    unsigned accepted = 0, ran = 0;
    nativeBody = [&] {
        for (unsigned i = 0; i != 1025; ++i) {
            auto capture = std::make_shared<Capture>();
            if (DeferInstantMovement([&, capture] { Check(nativeDepth == 0, "Bounded action ran too early"); ++ran; })) ++accepted;
        }
        Check(ran == 0 && accepted == 1024 && liveCaptures == 1024, "Queue cap must reject the 1025th action without running it");
        return true;
    };
    Check(Move() && accepted == 1024 && ran == 1024 && originalCalls == 1 && liveCaptures == 0,
        "Queue cap must preserve exactly the accepted actions and release captures");
    Reset();
    accepted = ran = 0;
    unsigned rejected = 0;
    nativeBody = [&] {
        const bool queued = DeferInstantMovement([&] {
            ++ran;
            Check(!DeferInstantMovement([] {}), "Callbacks must not append to the vector being drained");
            Check(Move(), "Callback-started native movement result changed");
        });
        if (queued) ++accepted; else ++rejected;
        return true;
    };
    Check(Move(), "Recursive movement wrapper result changed");
    Check(accepted == 32 && ran == 32 && rejected == 1 && originalCalls == 33,
        "Recursive completion must stop deferral at depth 32 while preserving every original native call");
    Check(nativeDepth == 0 && !module->stopRequested, "Bounded recursion must restore state without stopping the module");
    nativeBody = [] { return Queue("after-recursion"); };
    Check(Move() && events == std::vector<std::string>{"after-recursion"}, "Recursion guard remained active after unwinding");
}
void CallbackFailures() {
    for (int failure : {1, 2}) {
        Reset();
        nativeBody = [failure] {
            Check(Queue("first"), "First action queue rejected");
            auto capture = std::make_shared<Capture>();
            Check(DeferInstantMovement([failure, capture] {
                events.emplace_back("failure");
                if (failure == 1) throw std::runtime_error("Terminal host failure");
                throw 7;
            }), "Throwing action queue rejected");
            Check(Queue("must-not-run"), "Trailing action queue rejected");
            return true;
        };
        bool success = false, propagated = false;
        try { success = Move(); } catch (int value) { propagated = value == 7; }
        Check((failure == 1 ? success : propagated) && originalCalls == 1 && module->stopRequested &&
            events == std::vector<std::string>({"first", "failure"}) && liveCaptures == 0,
            "Callback failure must stop draining, retain native start count and release remaining captures");
        if (failure == 1)
            Check(!logs.empty() && logs.back().rfind("MOVEMENT_DISPATCH_ERROR ", 0) == 0, "Host callback failure must be logged");
    }
    Reset();
    nativeBody = [] { return Queue("recovered"); };
    Check(Move() && events == std::vector<std::string>{"recovered"}, "A previous exception leaked batch or depth state");
}
}

int main() {
    using namespace fake;
    try {
        Module state{L"offline-movement-dispatch-tests"}; module = &state;
        InstallMovementDispatch();
        NativeOrdering();
        NestedWrapperOrdering();
        FailedAndThrowingNativeTransactions();
        QueueAndRecursionBounds();
        CallbackFailures();
        Check(liveCaptures == 0 && nativeDepth == 0, "Dispatch leaked captures or native frame state");
        module = nullptr;
        std::cout << "PASS: actual rfw dispatch ABI, native ordering, nested rollback, bounded deferral and exceptions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
