#include "souls/soul-spell.hpp"
#include "souls/soul-combat.hpp"
#include "souls/soul-capture.hpp"
#include "souls/soul-item-view.hpp"
#include "core/module.hpp"

namespace soul {
namespace {
using namespace managed;
Method controlled{}, list{}, id{}, owner{}, level{}, grade{}, range{}, allowed{}, cast{};
bool installed = false, busy = false, eligibilityReported = false;
uint64_t nextClick = 0, shownFight = 0;
Obj shownCached = nullptr;
struct Selection { Ref spell, fighter, casting; int64_t localId = 0; };
Method Verify(const char* type, const char* name, int argc, uintptr_t rva) {
    return api.FindVerified(api.Class("Core.dll", "", type), name, argc, rva);
}
bool Available() {
    return installed && module && module->ready && module->servicesReady && !module->stopped &&
        !module->stopRequested && module->settings.soulReminder && module->InCombat();
}
Selection Select() {
    Selection result;
    if (!Available() || ReadCombat(module->fight.get(), module->cached.get()).state != CombatState::Fighting) return result;
    result.localId = CaptureParticipant(module->fight.get(), module->cached.get());
    Ref spells(module->FindService("fvq"));
    if (!result.localId || !spells.get()) return result;
    int mode = 2;
    Ref known(api.Invoke(list, spells.get(), {&result.localId, &mode}));
    if (!known.get()) return result;
    const int count = api.Value<int>(api.Call(known.get(), "get_Count"));
    if (count < 0 || count > 1024) return result;
    for (int i = 0; i < count; ++i) {
        Ref candidate(api.Call(known.get(), "get_Item", {&i}));
        if (!candidate.get() || api.Value<unsigned>(api.Invoke(id, candidate.get())) != 413) continue;
        // The list is already scoped to the local participant. Native learned
        // wrappers can retain the factory's default actor0 before a cast;
        // reject only an explicit different owner, never synthesize a spell.
        const auto actor = api.Value<int64_t>(api.Invoke(owner, candidate.get()));
        if (actor && actor != result.localId) continue;
        if (result.spell.get()) return Selection{}; // Ambiguous wrappers cannot authorize a cast.
        result.spell.reset(candidate.get());
    }
    return result;
}
bool CanCast(Selection& result) {
    if (!result.spell.get()) return false;
    result.fighter.reset(module->FindService("frs"));
    result.casting.reset(module->FindService("fsf"));
    if (!result.fighter.get() || !result.casting.get() ||
        api.Value<int64_t>(api.Invoke(controlled, result.fighter.get())) != result.localId) return false;
    Ref wrapper(api.Invoke(level, result.spell.get()));
    if (!wrapper.get() || api.Value<int>(api.Invoke(range, result.spell.get())) != 0) return false;
    auto spellGrade = api.Value<unsigned>(api.Invoke(grade, wrapper.get()));
    unsigned capture = 413;
    return api.Value<bool>(api.Invoke(allowed, result.fighter.get(),
        {&capture, &spellGrade, &result.localId, nullptr, &result.localId}));
}
}
void InstallSpell() {
    controlled = Verify("fvj", "bfty", 0, 0x68D2B0);
    list = Verify("fvu", "bhlm", 2, 0x1DBFCF0);
    id = Verify("ht", "nhp", 0, 0x27106C0);
    owner = Verify("ht", "nhs", 0, 0x681FE0);
    level = Verify("ht", "nhm", 0, 0x68D2C0);
    grade = Verify("hr", "ngl", 0, 0xF3D460);
    range = Verify("ht", "nim", 0, 0x27110D0);
    allowed = Verify("fvj", "bfua", 5, 0x1DAA6A0);
    cast = Verify("fyc", "bgaf", 2, 0x1E5DF80);
    SetSpellClick(CastCaptureSpell);
    installed = true;
}
Obj CaptureSpell(bool& enabled) {
    enabled = false;
    shownFight = 0; shownCached = nullptr;
    try {
        auto selection = Select();
        if (selection.spell.get()) { shownFight = ReadCombat(module->fight.get(), module->cached.get()).fightIdentity; shownCached = module->cached.get(); }
        // Visibility depends on a learned spell, not turn/PA/cooldown readiness.
        // A transient native eligibility error must only disable the action.
        try {
            enabled = GetTickCount64() >= nextClick && CanCast(selection);
            eligibilityReported = false;
        } catch (const std::exception& e) {
            if (!eligibilityReported) Log(std::string("SOUL_SPELL_ELIGIBILITY_ERROR ") + e.what());
            eligibilityReported = true;
        }
        return selection.spell.get();
    }
    catch (const std::exception& e) { Log(std::string("SOUL_SPELL_READ_ERROR ") + e.what()); return nullptr; }
}
void CastCaptureSpell(Obj displayedSpell) {
    if (busy || GetTickCount64() < nextClick) return;
    busy = true;
    struct Guard { ~Guard() { busy = false; } } guard;
    try {
        auto selection = Select();
        if (!Available() || !displayedSpell || selection.spell.get()!=displayedSpell ||
            !shownFight || module->cached.get()!=shownCached ||
            ReadCombat(module->fight.get(),module->cached.get()).fightIdentity!=shownFight || !CanCast(selection)) return;
        nextClick = GetTickCount64() + 1000; // Gate before native reentrant dispatch.
        api.Invoke(cast, selection.casting.get(), {selection.spell.get(), &selection.localId});
    } catch (const std::exception& e) { Log(std::string("SOUL_SPELL_CAST_ERROR ") + e.what()); }
}
}
