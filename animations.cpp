#include "features.hpp"
#include "module.hpp"
#include "instant-combat.hpp"
#include <intrin.h>

using namespace managed;

// This build shares OptionJson<T>.get_property between generic instances.
// Native callers pass (this, result buffer, inflated MethodInfo); a bool is not
// returned in AL. Preserve that ABI for every type using the shared function.
using OptionGetterFn = void(*)(Obj, void*, Method);
static OptionGetterFn originalOptionGetter;
using AnimationStepFn = void(*)(Obj, Method);
static AnimationStepFn originalAnimationStart, completeAnimationStep;
static Klass animatedEntityClass, fightStateClass, renderedEntitiesClass, entityRegistryClass, fighterRegistryClass;
static Method renderedEntityGet;

// A native completion can synchronously advance another step. Block only a
// second entry for the same step, not nested steps belonging to the sequence.
struct CompletingStep {
    Obj step;
    CompletingStep* previous;
    static thread_local CompletingStep* current;
    explicit CompletingStep(Obj value) : step(value), previous(current) { current = this; }
    ~CompletingStep() { current = previous; }
    static bool Contains(Obj value) {
        for (auto frame = current; frame; frame = frame->previous)
            if (frame->step == value) return true;
        return false;
    }
};
thread_local CompletingStep* CompletingStep::current = nullptr;

bool InstantCombatEnabled() {
    return module && module->ready.load() && !module->stopped.load() &&
        !module->stopRequested.load() && module->settings.animationMode == 3 && module->InCombat();
}

bool IsCurrentFightEntity(Obj entity) {
    if (!entity || !api.class_is_assignable_from(animatedEntityClass, api.object_get_class(entity))) return false;
    auto state = Read<Obj>(module->fight.get(), 0x18);
    if (!state || api.object_get_class(state) != fightStateClass) return false;
    auto fighters = Read<Obj>(state, 0x58);
    auto rendered = Read<Obj>(state, 0x68);
    if (!fighters || api.object_get_class(fighters) != fighterRegistryClass ||
        !rendered || api.object_get_class(rendered) != renderedEntitiesClass) return false;
    auto registry = Read<Obj>(rendered, 0x150);
    if (!registry || api.object_get_class(registry) != entityRegistryClass) return false;

    auto id = Read<int64_t>(entity, 0x1C8);
    Obj info = nullptr;
    if (!api.Value<bool>(api.Invoke(module->features->fighterTryGet, fighters, {&id, &info})) || !info) return false;
    Ref heldInfo(info);
    int fighterFlag = 1;
    if (!api.Value<bool>(api.Invoke(module->features->flagsTest, info, {&fighterFlag}))) return false;
    // A matching ID alone could refer to a replaced entity or a child visual.
    return api.Invoke(renderedEntityGet, registry, {&id}) == entity;
}

static void AnimationStartHook(Obj step, Method method) {
    if (CompletingStep::Contains(step)) return;
    bool skip = false;
    try {
        skip = step && module && module->ready.load() && !module->stopped.load() &&
            !module->stopRequested.load() && module->settings.animationMode >= 2 &&
            module->InCombat() && IsCurrentFightEntity(Read<Obj>(step, 0x78));
        if (skip && module->settings.animationMode == 3 && MustKeepNativeAnimation(step)) skip = false;
    } catch (const std::exception& error) {
        // No native start or completion has happened yet: the original is safe.
        skip = false;
        Log(std::string("ANIMATION_MEMBERSHIP_ERROR ") + error.what());
        module->stopRequested.store(true);
    }
    if (!skip) {
        originalAnimationStart(step, method);
        return;
    }
    if (Read<bool>(step, 0x1D)) return;

    // Only the instant mode may release an independent waiter for this exact
    // skipped animation. Record it before synchronous native callbacks run.
    if (module->settings.animationMode == 3) {
        try { RememberSkippedAnimation(step); }
        catch (const std::exception& error) {
            Log(std::string("ANIMATION_TRACKING_ERROR ") + error.what());
            module->stopRequested.store(true);
            originalAnimationStart(step, method);
            return;
        }
    }

    CompletingStep completing(step);
    try {
        // The constructor also subscribes a disposal callback. The normal
        // nonblocking exit sets this marker before completion (2075C45); keep
        // that callback from completing the already skipped step again later.
        *reinterpret_cast<bool*>(static_cast<char*>(step) + 0x71) = true;
        // gpy.bmat already uses this exact completion before playing an
        // invisible fighter's animation (20758C8 -> 2075C49). It runs the
        // native cleanup slot, cancels the watchdog, and advances the sequence.
        // Do not start, seek, freeze, or replace its duration with a blank wait.
        completeAnimationStep(step, nullptr);
    } catch (...) {
        module->stopRequested.store(true);
        // Completion may have dispatched callbacks. Never retry via the
        // original animation; let the native sequencer handle the exception.
        throw;
    }
}

static bool IsCombatAnimationRead(uintptr_t caller) {
    switch (caller - gameBase) {
    case 0x1ECE82C: // FightSequence.biwv: native projectile/effect step filtering.
    case 0x272E250: // dyq.uus: native caster animation selection.
    case 0x2744303: // dzj.uxg
        return true;
    default:
        return false;
    }
}

static void OptionGetterHook(Obj option, void* result, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    originalOptionGetter(option, result, method);
    if (!IsCombatAnimationRead(caller) || !result || !module ||
        !module->ready.load() || module->stopped.load() ||
        module->stopRequested.load() || module->settings.animationMode == 0)
        return;

    try {
        if (option != module->spellEffectsOption.get() || !module->InCombat()) return;
        // Only alter this native read. Keep the account option, notifications,
        // UI and saved preferences intact; the game runs its own shortened path.
        *static_cast<unsigned char*>(result) = 0;
    } catch (const std::exception& error) {
        Log(std::string("ANIMATION_OPTION_ERROR ") + error.what());
        module->stopRequested.store(true);
    }
}

void Features::InstallAnimation() {
    animatedEntityClass = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    fightStateClass = api.Class("Core.dll", "", "fyb");
    renderedEntitiesClass = api.Class("Core.dll", "", "fxw");
    entityRegistryClass = api.Class("Core.dll", "", "ebm");
    fighterRegistryClass = api.Class("Core.dll", "", "evv");
    renderedEntityGet = api.Find(entityRegistryClass, "vkq", 1);
    api.Native(renderedEntityGet, 0x27695E0);
    completeAnimationStep = reinterpret_cast<AnimationStepFn>(api.NativeAt(0x205E360));
    // Install the verified shared body directly: open generic metadata need not
    // have an inflated method pointer before the account options are discovered.
    AddNativeHook(0x39005D0, reinterpret_cast<void*>(OptionGetterHook),
                  reinterpret_cast<void**>(&originalOptionGetter));
    AddNativeHook(0x20853E0, reinterpret_cast<void*>(AnimationStartHook),
                  reinterpret_cast<void**>(&originalAnimationStart));
    InstallMovementDispatch();
    InstallInstantMovement();
    InstallInstantPushMovement();
    InstallInstantThrowMovement();
    InstallInstantJumpMovement();
    InstallInstantMovementWaits();
    InstallInstantWaits();
    InstallScriptWaits();
    InstallCombatCleanup();
}

void Features::Restore(bool) {
    sizes.pending.clear();
    ClearInstantWaits();
    // No animation state or persistent native option has been modified.
}
