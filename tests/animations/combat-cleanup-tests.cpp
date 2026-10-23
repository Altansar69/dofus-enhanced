#include "animations/instant-combat.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace fake {
using namespace managed;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Object { alignas(16) std::array<unsigned char, 0x100> memory{}; };
struct Type {} animationClass;
struct MethodInfo { uintptr_t rva; } constructor{0x2081890};
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
unsigned constructors = 0, nativeStarts = 0, completions = 0, cleanups = 0;
bool inCombat = true, failHandle = false, throwConstructor = false;
Obj expectedEntity{}, expectedName{}, expectedSuffix{}, expectedServices{};
Method expectedMethod = reinterpret_cast<Method>(0x12345678);

Obj Make() {
    objects.push_back(std::make_unique<Object>());
    return objects.back().get();
}
template<class T> void Put(Obj obj, size_t offset, T value) {
    std::memcpy(static_cast<Object*>(obj)->memory.data() + offset, &value, sizeof(T));
}
void OriginalCtor(Obj step, Obj entity, Obj name, bool idleAfter, bool wait, int eventMode,
                  int repetitions, Obj suffix, bool orient, Obj services, bool extra, Method method) {
    ++constructors;
    Check(entity == expectedEntity && name == expectedName && suffix == expectedSuffix &&
          services == expectedServices && method == expectedMethod,
          "Constructor object arguments and MethodInfo must remain unchanged");
    Check(!idleAfter && wait && eventMode == -7 && repetitions == 23 && orient && !extra,
          "Constructor bool/int arguments must preserve their native ABI");
    if (throwConstructor) throw 37;
    Put(step, 0x78, entity); Put(step, 0x1D, false);
}
void InitializeApi() {
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        if (failHandle) return 0;
        const auto handle = ++nextHandle; handles.emplace(handle, object); return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC reference freed twice"); };
    expectedEntity = Make(); expectedName = Make(); expectedSuffix = Make(); expectedServices = Make();
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000ULL;
Klass Api::Class(const char* assembly, const char* space, const char* name) const {
    fake::Check(std::string(assembly) == "Core.dll" && !*space && std::string(name) == "gpy",
                "Unexpected cleanup class lookup");
    return &fake::animationClass;
}
Method Api::Find(Klass klass, const char* name, int count) const {
    fake::Check(klass == &fake::animationClass && std::string(name) == ".ctor" && count == 10,
                "Animation constructor must have ten managed arguments");
    return &fake::constructor;
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(method == &fake::constructor && rva == fake::constructor.rva, "Wrong constructor RVA");
    return reinterpret_cast<void*>(fake::OriginalCtor);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x26CFAA4, "Wrong death animation provenance guard");
    return reinterpret_cast<void*>(gameBase + rva);
}
}

// Test the production call-site predicate without fabricating stack addresses.
#include "animations/combat-cleanup.cpp"

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string&) {}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    fake::Check(rva == 0x2081890 && replacement, "Unexpected cleanup hook");
    *original = reinterpret_cast<void*>(fake::OriginalCtor);
}

namespace fake {
Obj Construct(uintptr_t returnRva = 0x26CFAA9) {
    auto step = Make();
    AnimationCtorAt(step, expectedEntity, expectedName, false, true, -7, 23,
                    expectedSuffix, true, expectedServices, false, expectedMethod, gameBase + returnRva);
    return step;
}
// animations.cpp: a step plays natively unless the toggles skip its provenance.
bool Skipped(Obj step) { return SkipFighterAnimation(module->settings, ClassifyAnimationStep(step)); }
void StartAsParentWould(Obj step) {
    if (!Skipped(step)) {
        ++nativeStarts;
        // The native dwp sequence receives its normal end event before the
        // child completes. The production fallback preserves this whole path.
        ++cleanups;
    }
    ++completions; Put(step, 0x1D, true);
}
void Reset() {
    ClearCombatCleanup(); Check(handles.empty(), "Cleanup roots leaked between cases");
    module->settings.ApplyLegacyAnimationMode(3); module->stopRequested = false; module->stopped = false;
    inCombat = true; failHandle = throwConstructor = false;
    constructors = nativeStarts = completions = cleanups = 0;
}
}

int main() {
    using namespace fake;
    try {
        Module instance(L"."); module = &instance; InitializeApi(); InstallCombatCleanup();
        using Kind = AnimationStepKind;
        Reset(); auto death = Construct(), ordinary = Construct(0x26CFAA8);
        Check(constructors == 2 && ClassifyAnimationStep(death) == Kind::Death && ClassifyAnimationStep(ordinary) == Kind::Ordinary &&
              !Skipped(death) && Skipped(ordinary),
              "Exact dwp provenance must stay native while a known neighboring call site may skip");
        auto preexisting = Make();
        Check(ClassifyAnimationStep(preexisting) == Kind::Unknown && !Skipped(preexisting),
              "A step constructed before hook installation must retain its unknown native cleanup");
        StartAsParentWould(death); StartAsParentWould(ordinary);
        Check(nativeStarts == 1 && cleanups == 1 && completions == 2,
              "Death cleanup must retain native ordering while ordinary animations still skip");
        PumpCombatCleanup(true); PumpCombatCleanup(true);
        Check(handles.empty() && cleanups == 1 && completions == 2,
              "Later pumping must not redispatch native cleanup or completion");

        for (int previousMode : {0, 1, 2}) {
            Reset(); module->settings.ApplyLegacyAnimationMode(previousMode); death = Construct(); ordinary = Construct(0x26CFAA8);
            Check(ClassifyAnimationStep(death) == Kind::Death && ClassifyAnimationStep(ordinary) == Kind::Ordinary,
                  "Provenance is recorded whatever the animation toggles");
            Check(Skipped(death) == (previousMode == 2) && Skipped(ordinary) == (previousMode == 2),
                  "Previous animation modes must retain their previous policy");
            PumpCombatCleanup(true); module->settings.ApplyLegacyAnimationMode(3);
            Check(!Skipped(death), "Switching to instant toggles must preserve constructor provenance");
            Check(Skipped(ordinary), "Known ordinary steps must still skip after a toggle change");
        }
        // « Sans animations de mort » alone skips the death; with instant
        // movements or waits it stays native, so its own end event disposes.
        for (int config = 0; config < 4; ++config) {
            Reset(); module->settings.ApplyLegacyAnimationMode(0); module->settings.noDeathAnimations = true;
            module->settings.instantMovements = (config & 1) != 0; module->settings.noVisualWaits = (config & 2) != 0;
            death = Construct(); ordinary = Construct(0x26CFAA8);
            StartAsParentWould(death); StartAsParentWould(ordinary);
            Check(nativeStarts == (config == 0 ? 1u : 2u) && completions == 2,
                  "A death is skipped alone only without instant movements or waits; ordinary steps follow their own toggle");
        }
        Reset(); death = Construct(); inCombat = false;
        PumpCombatCleanup(false); Check(handles.empty(), "Combat end must release pending roots");
        Check(ClassifyAnimationStep(death) == Kind::Unknown, "Released provenance is unknown again");

        Reset(); failHandle = true; death = Construct();
        Check(constructors == 1 && module->stopRequested && handles.empty(),
              "Host capture failure must stop conservatively without repeating constructor subscriptions");
        Reset(); throwConstructor = true; bool propagated = false;
        try { Construct(); } catch (int value) { propagated = value == 37; }
        Check(propagated && constructors == 1 && handles.empty(),
              "Native constructor failure must propagate without capture or retry");

        Reset();
        for (size_t i = 0; i <= MaxRetainedAnimations; ++i) Construct(i % 2 ? 0x26CFAA8 : 0x26CFAA9);
        Check(module->stopRequested && handles.size() == MaxRetainedAnimations &&
              constructors == MaxRetainedAnimations + 1,
              "Capacity exhaustion must stop acceleration instead of evicting pending provenance");
        ClearCombatCleanup(); Check(handles.empty(), "Shutdown must release every retained managed root");
        module = nullptr;
        std::cout << "PASS: death provenance, constructor ABI, toggle changes, death toggle vs instant movements/waits, native fallback, cleanup and failure bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
