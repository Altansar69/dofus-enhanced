#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "animations/script-waits.cpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace fake {
struct Type { int id; };
Type waitType{1}, fightType{2}, contextType{3}, otherType{4}, contextInterface{5};
struct Object { alignas(16) std::array<unsigned char, 0x100> bytes{}; Klass type; };
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x100000000ULL;
bool enabled = true, failLookup = false, failComplete = false, reenter = false, clearDuring = false, failRoot = false;
unsigned starts = 0, completions = 0, ctors = 0;
Obj context{}, fight{}, service{}, nested{};
Method passed = reinterpret_cast<Method>(0x4567);
ScriptCtorFn ctorHook{};
ScriptStepFn startHook{};
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void Put(Obj object, size_t offset, T value) { std::memcpy(static_cast<Object*>(object)->bytes.data() + offset, &value, sizeof(value)); }
Obj Make(Klass type) {
    auto object = std::make_unique<Object>(); object->type = type;
    auto result = object.get(); objects.push_back(std::move(object)); return result;
}
Obj Wait() { auto step = Make(&waitType); Put(step, 0x40, context); return step; }
void Start(Obj, Method m) { Check(m == passed, "Original start MethodInfo changed"); ++starts; }
void Ctor(Obj step, Obj c, Method m) { Check(m == passed, "Original constructor MethodInfo changed"); ++ctors; Put(step, 0x40, c); }
void Complete(Obj step, Method m) {
    Check(m == nullptr, "Completion ABI changed");
    Check(!Read<Obj>(step, 0x30) && !Read<Obj>(step, 0x38), "Already running timer accelerated");
    ++completions;
    if (reenter) startHook(step, passed);
    if (nested) { auto next = nested; nested = nullptr; startHook(next, passed); }
    if (clearDuring) {
        ClearScriptWaits();
        bool held = false;
        for (auto [handle, value] : handles) { (void)handle; if (value == step) held = true; }
        Check(held, "Step must remain rooted when completion clears history");
    }
    if (failComplete) throw std::runtime_error("Callback partly dispatched");
    Put(step, 0x1D, true);
}
void Reset() {
    ClearScriptWaits(); starts = completions = ctors = 0;
    enabled = true; failLookup = failComplete = reenter = clearDuring = failRoot = false; nested = nullptr;
    module->stopRequested = false;
    Put(service, 0x18, fight); Put(fight, 0xE0, context);
}
void Track(Obj step, uintptr_t caller = 0x2740093) { RememberScriptWait(step, context, gameBase + caller); }
}
namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000;
Klass Api::Class(const char*, const char*, const char* name) const {
    if (std::strcmp(name, "gqe") == 0) return &fake::waitType;
    if (std::strcmp(name, "fyb") == 0) return &fake::fightType;
    fake::Check(std::strcmp(name, "fws") == 0, "Unknown script wait class"); return &fake::contextInterface;
}
Method Api::Find(Klass type, const char* name, int argc) const {
    fake::Check(type == &fake::waitType, "Wrong script wait method owner");
    if (std::strcmp(name, ".ctor") == 0 && argc == 1) return reinterpret_cast<Method>(0x2089970);
    if (std::strcmp(name, "uia") == 0 && argc == 0) return reinterpret_cast<Method>(0x2089A50);
    fake::Check(std::strcmp(name, "blye") == 0 && argc == 0, "Wrong script completion method");
    return reinterpret_cast<Method>(0x20899A0);
}
void* Api::Native(Method m, uintptr_t rva) const {
    fake::Check(reinterpret_cast<uintptr_t>(m) == rva, "Wrong script wait RVA");
    return rva == 0x20899A0 ? reinterpret_cast<void*>(fake::Complete) : nullptr;
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x274008E || rva == 0x274019B || rva == 0x274164A, "Wrong script constructor caller"); return nullptr;
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string&) {}
bool InstantWaitsEnabled() { return fake::enabled && !module->stopRequested; }
void AddNativeHook(uintptr_t rva, void* hook, void** original) {
    if (rva == 0x2089970) { fake::ctorHook = reinterpret_cast<ScriptCtorFn>(hook); *original = reinterpret_cast<void*>(fake::Ctor); }
    else {
        fake::Check(rva == 0x2089A50, "Unknown script hook");
        fake::startHook = reinterpret_cast<ScriptStepFn>(hook); *original = reinterpret_cast<void*>(fake::Start);
    }
}
int main() {
    using namespace fake;
    try {
        api.object_get_class = [](Obj o) -> Klass {
            if (failLookup) throw std::runtime_error("Class lookup failed");
            return static_cast<Object*>(o)->type;
        };
        api.class_is_assignable_from = [](Klass a, Klass b) {
            return a == b || (a == &contextInterface && b == &contextType);
        };
        api.gchandle_new = [](Obj o, bool) -> GCHandle { if (failRoot) return 0; auto h = ++nextHandle; handles[h] = o; return h; };
        api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
        api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Root released twice"); };
        Module state{L"offline-script-waits"}; module = &state;
        service = Make(&otherType); fight = Make(&fightType); context = Make(&contextType);
        state.fight.reset(service); Reset(); InstallScriptWaits();
        auto step = Wait(); ctorHook(step, context, passed); startHook(step, passed);
        Check(ctors == 1 && starts == 1 && completions == 0 && scriptWaits.empty(), "Unknown real caller must keep native constructor/start");
        for (auto caller : {0x2740093, 0x27401A0, 0x274164F}) {
            Reset(); step = Wait(); Track(step, caller); Track(step, caller);
            Check(scriptWaits.size() == 1, "Duplicate script wait tracked");
            startHook(step, passed); startHook(step, passed);
            Check(starts == 0 && completions == 1 && Read<bool>(step, 0x1D), "Known pause must complete once without scheduling timer");
        }
        Reset(); step = Wait(); Track(step, 0x2740094); startHook(step, passed);
        Check(starts == 1 && completions == 0, "Adjacent caller must remain native");
        Reset(); step = Wait(); enabled = false; Track(step); enabled = true; startHook(step, passed);
        Check(starts == 1 && completions == 0, "Creation in older modes must not track");
        Reset(); step = Wait(); Track(step); enabled = false; startHook(step, passed);
        Check(starts == 1 && completions == 0, "Older modes must preserve start");
        for (size_t offset : {size_t(0x30), size_t(0x38)}) {
            Reset(); step = Wait(); Track(step); Put(step, offset, Make(&otherType)); startHook(step, passed);
            Check(starts == 1 && completions == 0, "Existing timer or stopwatch must remain native");
        }
        Reset(); step = Wait(); Track(step); Put(step, 0x40, Make(&contextType)); startHook(step, passed);
        Check(starts == 1 && completions == 0, "Replaced step context must remain native");
        Reset(); step = Wait(); Track(step); Put(fight, 0xE0, Make(&contextType)); startHook(step, passed);
        Check(starts == 1 && completions == 0, "Replaced fight context must remain native");
        Reset(); step = Wait(); Track(step); auto replacement = Make(&fightType); Put(replacement, 0xE0, context);
        Put(service, 0x18, replacement); startHook(step, passed);
        Check(starts == 1 && completions == 0, "Another fight reusing context must remain native");
        Reset(); step = Wait(); Put(fight, 0xE0, Obj{}); Track(step); startHook(step, passed);
        Check(starts == 1 && completions == 0 && scriptWaits.empty(), "Foreign context must not be recorded");
        Reset(); step = Wait(); Track(step); failLookup = true; startHook(step, passed);
        Check(starts == 1 && completions == 0 && state.stopRequested, "Validation failure must fall back once and stop");
        Reset(); step = Wait(); Track(step); failRoot = true; startHook(step, passed);
        Check(starts == 1 && completions == 0 && state.stopRequested && !scriptWaits.front().consumed,
              "Local GC root failure must preserve the native start and leave no consumed record");
        Reset(); step = Wait(); Track(step); reenter = true; nested = Wait(); Track(nested); startHook(step, passed);
        Check(starts == 0 && completions == 2, "Same step reentry rejected, distinct nested step allowed");
        Reset(); step = Wait(); Track(step); clearDuring = true; startHook(step, passed);
        Check(scriptWaits.empty() && completions == 1 && handles.size() == 1, "Completion must tolerate history clearing and release local root");
        Reset(); step = Wait(); Track(step); failComplete = true; bool threw = false;
        try { startHook(step, passed); } catch (const std::runtime_error&) { threw = true; }
        Check(threw && completions == 1 && starts == 0 && state.stopRequested, "Partially dispatched completion must never retry start");
        state.stopRequested = false; failComplete = false; startHook(step, passed);
        Check(completions == 1 && starts == 0, "Consumed tombstone must prevent retry after exception");
        Reset(); step = Wait(); Track(step); scriptWaits.front().when = GetTickCount64() - ScriptWaitLifetime;
        PumpScriptWaits(true); startHook(step, passed);
        Check(scriptWaits.empty() && starts == 1 && completions == 0, "Expired record must fall back and release roots");
        Reset(); step = Wait(); Track(step); PumpScriptWaits(false);
        Check(scriptWaits.empty() && handles.size() == 1, "Leaving fight must clear all pause roots");
        Track(step); enabled = false; PumpScriptWaits(true);
        Check(scriptWaits.empty(), "Disabling mode must clear pause history");
        Reset(); step = Wait(); Track(step);
        for (size_t i = 0; i < MaxScriptWaits; ++i) Track(Wait());
        Check(scriptWaits.size() == MaxScriptWaits, "Pause history must remain bounded");
        startHook(step, passed); Check(starts == 1 && completions == 0, "Evicted step must not match later allocation");
        ClearScriptWaits(); state.fight.reset(); Check(handles.empty(), "All script roots must be released");
        module = nullptr;
        std::cout << "PASS: exact script pause provenance, fight/context lifetime, native completion, reentry and fallback\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
