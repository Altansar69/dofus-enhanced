#include "features.hpp"
#include "module.hpp"
#include "options-ui.hpp"
#include <iostream>
#include <map>
#include "movement-waits.cpp"

namespace fake {
bool enabled = true, member = true, fail = false, nativeValue = true;
int calls = 0;
Obj entity = reinterpret_cast<Obj>(0x1234);
Method passed = reinterpret_cast<Method>(0x5678);
std::map<uintptr_t, PredicateFn> hooks;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Original(Obj value, Method method) {
    Check(value == entity && method == passed, "Predicate ABI changed"); ++calls; return nativeValue;
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000;
Klass Api::Class(const char*, const char*, const char*) const { return reinterpret_cast<Klass>(1); }
Method Api::Find(Klass, const char* name, int argc) const {
    if (std::string(name) == "qgp" && argc == 1) return reinterpret_cast<Method>(0x1F83E30);
    if (std::string(name) == "dbbg" && argc == 0) return reinterpret_cast<Method>(0x785ED0);
    fake::Check(std::string(name) == "daso" && argc == 0, "Unknown wait predicate");
    return reinterpret_cast<Method>(0x779910);
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(reinterpret_cast<uintptr_t>(method) == rva, "Wait predicate RVA mismatch");
    return reinterpret_cast<void*>(fake::Original);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x26B9E02 || rva == 0x26B9E1A || rva == 0x205FD3E, "Unknown wait caller guard"); return nullptr;
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string&) {}
bool InstantCombatEnabled() { return fake::enabled && !module->stopRequested; }
bool IsCurrentFightEntity(Obj entity) {
    if (fake::fail) throw std::runtime_error("Registry failure");
    return fake::member && entity == fake::entity;
}
void AddNativeHook(uintptr_t rva, void* hook, void** original) {
    fake::Check(rva == 0x1F83E30 || rva == 0x779910 || rva == 0x785ED0, "Unknown wait hook");
    fake::hooks[rva] = reinterpret_cast<PredicateFn>(hook);
    *original = reinterpret_cast<void*>(fake::Original);
}
int main() {
    using namespace fake;
    try {
        Module state{L"offline-movement-waits"}; module = &state;
        InstallInstantMovementWaits();
        for (auto [rva, hook] : hooks) {
            (void)rva;
            for (bool value : {false, true}) {
                calls = 0; nativeValue = value;
                Check(hook(entity, passed) == value && calls == 1, "Unrelated caller must preserve native predicate exactly once");
            }
        }
        for (uintptr_t caller : {0x26B9E07, 0x26B9E1F}) {
            enabled = member = true; fail = false; module->stopRequested = false;
            Check(!MovementAnimationWait(entity, true, gameBase + caller, caller), "Known visual wait must use immediate native start");
            Check(!MovementAnimationWait(entity, false, gameBase + caller, caller), "False native predicate changed");
            Check(MovementAnimationWait(entity, true, gameBase + caller + 1, caller), "Other caller was shortened");
            Check(MovementAnimationWait(nullptr, true, gameBase + caller, caller), "Missing entity was shortened");
            member = false;
            Check(MovementAnimationWait(entity, true, gameBase + caller, caller), "Unknown or outside entity was shortened");
            member = true; enabled = false;
            Check(MovementAnimationWait(entity, true, gameBase + caller, caller), "Other animation mode was shortened");
            enabled = true; fail = true;
            Check(MovementAnimationWait(entity, true, gameBase + caller, caller) && module->stopRequested,
                  "Validation failure must preserve result and stop module");
        }
        enabled = member = true; fail = false; module->stopRequested = false;
        Check(AppearanceReady(entity, false, gameBase + 0x205FD43), "Known appearance wait must take native idle/completion branch");
        Check(AppearanceReady(entity, true, gameBase + 0x205FD43), "Already ready appearance changed");
        Check(!AppearanceReady(entity, false, gameBase + 0x205FD44), "Other appearance caller changed");
        Check(!AppearanceReady(nullptr, false, gameBase + 0x205FD43), "Missing appearance entity changed");
        member = false;
        Check(!AppearanceReady(entity, false, gameBase + 0x205FD43), "Replaced appearance entity changed");
        member = true; enabled = false;
        Check(!AppearanceReady(entity, false, gameBase + 0x205FD43), "Old appearance mode changed");
        enabled = true; fail = true;
        Check(!AppearanceReady(entity, false, gameBase + 0x205FD43) && module->stopRequested,
              "Appearance validation error must preserve native result and stop");
        module = nullptr;
        std::cout << "PASS: native movement animation-read ABI, exact callers, eligibility and failure fallback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
