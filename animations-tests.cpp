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
#include <vector>

// Exercise the actual animations.cpp dispatch with fake managed objects only.
namespace fake {
using namespace managed;
using StepFn = void(*)(Obj, Method);
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Type { std::string name; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};
    Type* type{};
    bool boolean = false;
    std::map<int64_t, Obj> entries;
};
struct MethodInfo { std::string name; uintptr_t rva; };
std::map<std::string, Type> classes;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
MethodInfo lookup{"vkq", 0x27695E0}, fighter{"bamm", 0x1957A70}, flags{"bajs", 0x19518F0};
StepFn hook{};
unsigned starts = 0, completions = 0, remembered = 0, getterCalls = 0;
bool reenter = false, throwCompletion = false, throwLookup = false;
unsigned char accountValue = 1; // the account's allowSpellEffects, as the native getter returns it
AnimationStepKind kind = AnimationStepKind::Ordinary; // recorded provenance (combat-cleanup.cpp)
Obj nestedStep{};
Type* Class(const std::string& name) {
    auto [entry, inserted] = classes.try_emplace(name, Type{name});
    return &entry->second;
}
Object& Get(Obj object) { Check(object != nullptr, "Null fake receiver"); return *static_cast<Object*>(object); }
Obj Make(const char* name) {
    auto value = std::make_unique<Object>(); value->type = Class(name);
    auto result = value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).memory.size(), "Fake field outside storage");
    std::memcpy(Get(object).memory.data() + offset, &value, sizeof(value));
}
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
void Original(Obj, Method) { ++starts; }
void OriginalGetter(Obj, void* result, Method) { ++getterCalls; if (result) *static_cast<unsigned char*>(result) = accountValue; }
void Complete(Obj step, Method method) {
    Check(method == nullptr, "Completion MethodInfo must match native call site");
    Check(Read<bool>(step, 0x71), "Native disposal callback must see the terminal marker before completion");
    ++completions;
    if (reenter) hook(step, nullptr); // Before native done flag is set.
    if (nestedStep) { auto next = nestedStep; nestedStep = nullptr; hook(next, nullptr); }
    if (throwCompletion) throw std::runtime_error("Native callback failed after partial dispatch");
    Put(step, 0x1D, true);
}
void InitializeApi() {
    api.object_get_class = [](Obj object) -> Klass { return Get(object).type; };
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.object_unbox = [](Obj object) -> void* { return &Get(object).boolean; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { auto id = ++nextHandle; handles.emplace(id, object); return id; };
    api.gchandle_get_target = [](GCHandle id) -> Obj { return handles.at(id); };
    api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "GC handle freed twice"); };
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass type, const char* name, int count) const {
    fake::Check(static_cast<fake::Type*>(type)->name == "ebm" && std::string(name) == "vkq" && count == 1,
                "Unexpected animation method lookup");
    return &fake::lookup;
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(static_cast<const fake::MethodInfo*>(method)->rva == rva, "Native method RVA mismatch");
    return reinterpret_cast<void*>(rva);
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x205E360, "Unexpected completion RVA");
    return reinterpret_cast<void*>(fake::Complete);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake;
    auto name = static_cast<const MethodInfo*>(method)->name;
    if (throwLookup) throw std::runtime_error("Membership lookup failed before dispatch");
    const auto parameters = args.begin();
    if (name == "bajs") {
        Check(args.size() == 1 && *static_cast<int*>(parameters[0]) == 1, "Wrong fighter flag");
        return Box(Get(self).boolean);
    }
    auto found = Get(self).entries.find(*static_cast<int64_t*>(parameters[0]));
    auto result = found == Get(self).entries.end() ? nullptr : found->second;
    if (name == "vkq") { Check(args.size() == 1, "Wrong entity lookup ABI"); return result; }
    Check(name == "bamm" && args.size() == 2, "Wrong fighter lookup ABI");
    *static_cast<Obj*>(parameters[1]) = result;
    return Box(result != nullptr);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return servicesReady && fight.get() && managed::Read<bool>(fight.get(), 0xB0); }
void Log(const std::string&) {}
void InstallInstantMovement() {}
void InstallInstantPushMovement() {}
void InstallInstantThrowMovement() {}
void InstallInstantJumpMovement() {}
void InstallInstantMovementWaits() {}
void InstallMovementDispatch() {}
void InstallInstantWaits() {}
void InstallScriptWaits() {}
void InstallCombatCleanup() {}
AnimationStepKind ClassifyAnimationStep(managed::Obj) { return fake::kind; }
void RememberSkippedAnimation(managed::Obj) { ++fake::remembered; }
void ClearInstantWaits() {}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x20853E0) {
        fake::hook = reinterpret_cast<fake::StepFn>(replacement);
        *original = reinterpret_cast<void*>(fake::Original);
    } else {
        fake::Check(rva == 0x39005D0, "Unexpected hook RVA");
        *original = reinterpret_cast<void*>(fake::OriginalGetter);
    }
}

// The actual animations.cpp, compiled here so that the option read can be
// given each native return address (the hook itself takes _ReturnAddress()).
#include "animations.cpp"

int main() {
    using namespace fake;
    try {
        InitializeApi();
        Module state{L"offline-animation-tests"}; module = &state;
        state.features = std::make_unique<Features>();
        state.features->fighterTryGet = &fighter; state.features->flagsTest = &flags;
        state.features->lookAnimatedEntityClass = Class("AnimatedEntity");
        state.features->lookEntityRegistryClass = Class("evv");
        state.features->InstallAnimation(); state.ready = true; state.servicesReady = true;
        Check(state.features->lookAnimatedEntityClass==Class("AnimatedEntity") &&
              state.features->lookEntityRegistryClass==Class("evv"),
              "Animation installation must not overwrite the appearance registry classes");
        auto fight = Make("fxc"), aggregate = Make("fyb"), rendered = Make("fxw");
        auto registry = Make("ebm"), fighters = Make("evv"), entity = Make("AnimatedEntity"), info = Make("EntityInfo");
        constexpr int64_t id = -900123456789LL;
        Put(fight, 0xB0, true); Put(fight, 0x18, aggregate); Put(aggregate, 0x58, fighters);
        Put(aggregate, 0x68, rendered); Put(rendered, 0x150, registry); Put(entity, 0x1C8, id);
        Get(registry).entries[id] = entity; Get(fighters).entries[id] = info; Get(info).boolean = true;
        state.fight.reset(fight);
        auto NewStep = [&] { auto step = Make("gpy"); Put(step, 0x78, entity); return step; };
        auto Reset = [&] { starts = completions = remembered = getterCalls = 0; reenter = throwCompletion = throwLookup = false; nestedStep = nullptr; state.stopRequested = false; kind = AnimationStepKind::Ordinary; };
        auto Toggles = [&](unsigned mask) {
            for (size_t i = 0; i < std::size(AnimationToggles); ++i) state.settings.*AnimationToggles[i] = ((mask >> i) & 1) != 0;
        };

        // Every toggle combination and step provenance through the real start hook.
        for (unsigned mask = 0; mask < 64; ++mask) {
            for (auto stepKind : {AnimationStepKind::Ordinary, AnimationStepKind::Death, AnimationStepKind::Unknown}) {
                Reset(); Toggles(mask); kind = stepKind;
                auto step = NewStep(); hook(step, nullptr); hook(step, nullptr);
                const bool skip = SkipFighterAnimation(state.settings, stepKind);
                Check(skip ? starts == 0 && completions == 1 : starts == 2 && completions == 0,
                      "A fighter step is skipped exactly when its provenance's toggle allows it, once");
                Check(remembered == (skip && state.settings.noVisualWaits ? 1u : 0u),
                      "Only Sans attentes visuelles may release the waiter of a skipped step");
            }
            Check(InstantMovementEnabled() == state.settings.instantMovements && InstantWaitsEnabled() == state.settings.noVisualWaits,
                  "Instant movements and waits follow their own toggle");
        }
        for (int mode : {0, 1}) {
            Reset(); state.settings.ApplyLegacyAnimationMode(mode); hook(NewStep(), nullptr);
            Check(starts == 1 && completions == 0, "Normal/shortened mode must start the native step");
        }
        Reset(); state.settings.ApplyLegacyAnimationMode(2);
        auto step = NewStep(); hook(step, nullptr); hook(step, nullptr);
        Check(starts == 0 && completions == 1, "Skip must complete once without starting animation");
        Check(remembered == 0 && !InstantMovementEnabled() && !InstantWaitsEnabled(), "Older animation mode must not enable instant movement/waits");
        Reset(); state.settings.ApplyLegacyAnimationMode(3); step = NewStep();
        Check(InstantMovementEnabled() && InstantWaitsEnabled(), "Instant mode eligibility rejected");
        hook(step, nullptr); hook(step, nullptr);
        Check(starts == 0 && completions == 1 && remembered == 1, "Instant mode must remember exactly one skipped animation");
        for (auto keep : {AnimationStepKind::Death, AnimationStepKind::Unknown}) {
            Reset(); kind = keep; hook(NewStep(), nullptr);
            Check(starts == 1 && completions == 0 && remembered == 0, "Cleanup-dependent animation must remain native in instant mode");
        }
        Reset(); state.settings.ApplyLegacyAnimationMode(2); kind = AnimationStepKind::Death; hook(NewStep(), nullptr);
        Check(starts == 0 && completions == 1, "Cleanup fallback must not alter old mode behavior");

        Reset(); Get(registry).entries[id] = Make("AnimatedEntity"); hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0, "Same ID with different entity must fall back");
        Get(registry).entries[id] = entity;
        Reset(); Get(fighters).entries.clear(); hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0, "Unknown fighter must fall back"); Get(fighters).entries[id] = info;
        Reset(); Get(info).boolean = false; hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0, "Non-fighter must fall back"); Get(info).boolean = true;
        Reset(); Put(fight, 0xB0, false); hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0 && !InstantMovementEnabled() && !InstantWaitsEnabled(), "Roleplay must remain native");
        Put(fight, 0xB0, true);
        Reset(); state.stopped = true; hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0 && !InstantMovementEnabled() && !InstantWaitsEnabled(), "A stopped module stays native");
        state.stopped = false;

        Reset(); reenter = true; nestedStep = NewStep(); hook(NewStep(), nullptr);
        Check(starts == 0 && completions == 2, "Reentry must skip duplicate but allow next distinct step");
        Reset(); throwLookup = true; hook(NewStep(), nullptr);
        Check(starts == 1 && completions == 0 && state.stopRequested, "Pre-dispatch failure must fall back once");
        Reset(); throwCompletion = true; bool threw = false;
        try { hook(NewStep(), nullptr); } catch (const std::runtime_error&) { threw = true; }
        Check(threw && starts == 0 && completions == 1 && state.stopRequested,
              "Post-dispatch failure must never retry the original step");

        // The allowSpellEffects reads: each verified return address answers false
        // exactly when its toggle is ticked; any other read keeps the account value.
        auto option = Make("AccountOptionJson`1");
        state.spellEffectsOption.reset(option);
        const uintptr_t callers[] = {0x1ECE82C, 0x272E250, 0x2744303, 0x1ECE827, 0x272E24B, 0x2744304, 0x39005D0};
        for (unsigned mask = 0; mask < 64; ++mask) for (auto caller : callers) for (int account : {0, 1}) {
            Reset(); Toggles(mask); accountValue = static_cast<unsigned char>(account);
            unsigned char result = 0xAA;
            OptionGetterAt(option, &result, nullptr, gameBase + caller);
            const bool shortened = OverrideSpellEffectsRead(state.settings, caller);
            Check(getterCalls == 1 && result == (shortened ? 0 : account), "Option read answered against its toggle");
        }
        Reset(); state.settings.ApplyLegacyAnimationMode(1); accountValue = 1;
        unsigned char result = 0xAA;
        OptionGetterAt(Make("OtherOption"), &result, nullptr, gameBase + 0x1ECE82C);
        Check(getterCalls == 1 && result == 1, "Another option's read keeps its value");
        Put(fight, 0xB0, false); result = 0xAA;
        OptionGetterAt(option, &result, nullptr, gameBase + 0x2744303);
        Check(result == 1, "Out of combat the account value stays"); Put(fight, 0xB0, true);
        state.stopRequested = true; result = 0xAA;
        OptionGetterAt(option, &result, nullptr, gameBase + 0x272E250);
        Check(result == 1, "A stopping module keeps the account value"); state.stopRequested = false;
        OptionGetterAt(option, nullptr, nullptr, gameBase + 0x1ECE82C);
        Check(getterCalls == 4, "Every read runs the native getter once, even without a result buffer");
        module = nullptr;
        std::cout << "PASS: actual animation hooks for all 64 toggle combinations x step provenance (skip, waiter release), "
                     "per-caller allowSpellEffects reads, instant toggles, combat membership, reentry and failure boundaries\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
