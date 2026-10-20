#include "instant-combat.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
namespace fake {
using namespace managed;
using Fn = void(*)(Obj, Method);
struct Type { int id; };
Type waiter{1}, animation{2}, other{3}, delayType{4};
struct Object {
    alignas(16) std::array<unsigned char, 0x100> memory{};
    Klass type;
};
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123400000000ULL;
Fn hook{};
Obj entity{}, nested{};
bool enabled = true, member = true, failLookup = false, failNative = false, reenter = false;
unsigned calls = 0, completed = 0, removed = 0;
unsigned delayCalls = 0;
int lastDelay = -1;
void (*delayHook)(Obj, int, Method){};
void Check(bool v, const char* text) { if (!v) throw std::runtime_error(text); }
template<class T> void Put(Obj obj, size_t offset, T value) {
    std::memcpy(static_cast<Object*>(obj)->memory.data() + offset, &value, sizeof(value));
}
Obj Make(Klass type) {
    auto object = std::make_unique<Object>(); object->type = type;
    auto result = object.get(); objects.push_back(std::move(object)); return result;
}
Obj Animation() { auto s = Make(&animation); Put(s, 0x78, entity); return s; }
Obj Waiter(Obj animationStep) { auto w = Make(&waiter); Put(w, 0x40, animationStep); return w; }
void Original(Obj w, Method) {
    ++calls;
    if (!Read<bool>(w, 0x48)) return;
    ++removed;
    Put(w, 0x40, Obj{});
    if (reenter) hook(w, nullptr);
    if (nested) { auto next = nested; nested = nullptr; hook(next, nullptr); }
    ++completed;
    if (failNative) throw std::runtime_error("Native callback partially dispatched");
    Put(w, 0x1D, true);
}
void ResetCounts() { calls = completed = removed = 0; reenter = failNative = failLookup = false; nested = nullptr; }
void OriginalDelay(Obj, int delay, Method) { ++delayCalls; lastDelay = delay; }
void Setup() {
    api.object_get_class = [](Obj o) -> Klass { return static_cast<Object*>(o)->type; };
    api.class_is_assignable_from = [](Klass a, Klass b) { return a == b; };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { auto h = ++nextHandle; handles.emplace(h, o); return h; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "GC root freed twice"); };
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const {
    if (std::strcmp(name, "gqg") == 0) return &fake::delayType;
    return std::strcmp(name, "gqf") == 0 ? &fake::waiter : &fake::animation;
}
Method Api::Find(Klass type, const char* name, int argc) const {
    if (type == &fake::delayType) {
        fake::Check(std::strcmp(name, ".ctor") == 0 && argc == 1, "Wrong delay constructor"); return type;
    }
    fake::Check(type == &fake::waiter && std::strcmp(name, "uia") == 0 && argc == 0, "Wrong method identity");
    return type;
}
void* Api::Native(Method m, uintptr_t rva) const {
    if (m == &fake::delayType) {
        fake::Check(rva == 0x208A1A0, "Wrong delay constructor RVA"); return reinterpret_cast<void*>(fake::OriginalDelay);
    }
    fake::Check(m == &fake::waiter && rva == 0x208A020, "Wrong verified RVA");
    return reinterpret_cast<void*>(fake::Original);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x1E59C1F, "Wrong visual-delay callsite guard"); return reinterpret_cast<void*>(rva);
}
}
// Compile the actual hook here so expiry and capacity can be checked without
// sleeping or replacing the process clock. No game binary is loaded.
#include "instant-waits.cpp"
Module* module = nullptr;
void Log(const std::string&) {}
void PumpScriptWaits(bool) {}
void ClearScriptWaits() {}
void PumpCombatCleanup(bool) {}
void ClearCombatCleanup() {}
bool InstantCombatEnabled() { return fake::enabled; }
bool IsCurrentFightEntity(managed::Obj e) {
    if (fake::failLookup) throw std::runtime_error("Lookup failed");
    return fake::member && e == fake::entity;
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x208A1A0) {
        fake::delayHook = reinterpret_cast<decltype(fake::delayHook)>(replacement);
        *original = reinterpret_cast<void*>(fake::OriginalDelay); return;
    }
    fake::Check(rva == 0x208A020, "Wrong hook RVA");
    fake::hook = reinterpret_cast<fake::Fn>(replacement);
    *original = reinterpret_cast<void*>(fake::Original);
}
int main() {
    using namespace fake;
    try {
        Setup(); InstallInstantWaits(); entity = Make(&other);
        auto a = Animation();
        enabled = false; RememberSkippedAnimation(a);
        Check(handles.empty(), "Older modes must not track");
        hook(Waiter(a), nullptr);
        Check(calls == 1 && completed == 0, "Older mode must run original unchanged");
        enabled = true; ResetCounts();
        hook(Waiter(a), nullptr);
        Check(calls == 1 && completed == 0, "Unrecorded animation must remain native");
        RememberSkippedAnimation(a); RememberSkippedAnimation(a);
        Check(handles.size() == 1, "Duplicate record must keep one root");
        Put(a, 0x1D, true);
        auto first = Waiter(a), second = Waiter(a);
        ResetCounts(); hook(first, nullptr); hook(second, nullptr); hook(first, nullptr);
        Check(calls == 2 && completed == 2 && removed == 2, "Multiple waiters / completed tombstone");
        Check(Read<Obj>(first, 0x40) == nullptr, "Native waiter cleanup must run");
        ResetCounts(); member = false; hook(Waiter(a), nullptr);
        Check(calls == 1 && completed == 0, "Replaced fighter must fall back"); member = true;
        ResetCounts(); auto wrong = Make(&other); Put(wrong, 0x40, a); hook(wrong, nullptr);
        Check(calls == 1 && completed == 0, "Unknown waiter type must remain native");
        ResetCounts(); failLookup = true; hook(Waiter(a), nullptr);
        Check(calls == 1 && completed == 0, "Lookup failure must call original once");
        ResetCounts(); reenter = true; nested = Waiter(a); hook(Waiter(a), nullptr);
        Check(calls == 2 && completed == 2, "Reject same-waiter reentry, allow distinct next waiter");
        ResetCounts(); failNative = true; bool threw = false;
        try { hook(Waiter(a), nullptr); } catch (const std::runtime_error&) { threw = true; }
        Check(threw && calls == 1 && completed == 1, "No retry after partial native completion");
        ResetCounts(); skipped.front().when = GetTickCount64() - RememberMilliseconds;
        PumpInstantWaits(true); Check(handles.empty(), "Expired record must release its root");
        hook(Waiter(a), nullptr); Check(completed == 0, "Expired animation must remain native");
        RememberSkippedAnimation(a); PumpInstantWaits(false);
        Check(handles.empty(), "Leaving combat clears roots");
        RememberSkippedAnimation(a); enabled = false; PumpInstantWaits(true);
        Check(handles.empty(), "Disabling instant mode clears roots"); enabled = true;
        RememberSkippedAnimation(a);
        for (size_t i = 0; i < MaxRemembered; ++i) RememberSkippedAnimation(Animation());
        Check(handles.size() == MaxRemembered, "History must stay bounded");
        ResetCounts(); hook(Waiter(a), nullptr);
        Check(calls == 1 && completed == 0, "Evicted animation must not match a new allocation");
        ClearInstantWaits(); Check(handles.empty(), "Stop cleanup must release every root");
        Check(VisualStaggerDelay(900, gameBase + 0x1E59C24) == 0, "Verified visual stagger should be removed");
        Check(VisualStaggerDelay(900, gameBase + 0x1E59C23) == 900, "Other timer caller must keep duration");
        Check(VisualStaggerDelay(-1, gameBase + 0x1E59C24) == -1, "Special negative timer value must remain unchanged");
        enabled = false;
        Check(VisualStaggerDelay(900, gameBase + 0x1E59C24) == 900, "Older modes must keep visual stagger");
        enabled = true; delayHook(Make(&delayType), 600, nullptr);
        Check(delayCalls == 1 && lastDelay == 600, "Real constructor hook must preserve unrelated caller and call original once");
        std::cout << "PASS: actual wait hook eligibility, identity history, cleanup, bounds and callback safety\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
