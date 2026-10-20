#include "soul-combat.hpp"
#include "soul-monster.hpp"
#include "module.hpp"
#include <algorithm>

namespace soul {
namespace {
using namespace managed;
struct Methods {
    Method inCombat{}, information{}, phase{}, flags{}, fightType{};
    Method localId{}, findEntity{}, entities{}, flagsTest{}, teamTryGet{}, boolGet{}, intTryGet{};
    MonsterCaptureData monsters;
    Method currentMap{}, mapId{}, mapById{}, mapCaptureAllowed{};
} methods;
struct State {
    enum class Kind { Unknown, Pvm, Pvp } kind = Kind::Unknown;
    bool installed = false;
    Obj observedInformation = nullptr; // Compared only; never dereferenced after the call.
    Obj observedContext = nullptr;
    int observedPhase = -1;
    int observedTeam = -1;
    uint64_t generation = 1;
    int64_t localId = 0;
    int64_t observedMap = 0;
    bool captureEligible = false;
    int highestLevel = 0;
    Ref fight, cached;
} state;
using PhaseFn = void(*)(Obj, Method);
PhaseFn originalPhase = nullptr;

void Advance() {
    if (++state.generation == 0) ++state.generation;
}
void ClearClassification() {
    state.kind = State::Kind::Unknown;
    state.captureEligible = false;
    state.highestLevel = 0;
}
void Observe(Obj information, int phase) {
    if (state.observedInformation != information || state.observedPhase != phase) {
        // Information wrappers are reused. Placement after an active fight is
        // a new fight even if no intermediate idle phase was observed.
        if (state.observedInformation != information || phase == 0 ||
            (phase == 1 && state.observedPhase == 2))
            ClearClassification();
        state.observedInformation = information;
        state.observedPhase = phase;
        Advance();
    }
}
void PhaseHook(Obj information, Method method) {
    originalPhase(information, method);
    // bhyl writes this verified int field before publishing the native phase event.
    // Observing every transition also invalidates a click if leave/rejoin occurs
    // entirely between two UI refreshes. No managed call is made from this hook.
    Observe(information, Read<int>(information, 0x64));
}
bool Is(Obj object, const char* name) {
    return object && std::string(api.class_get_name(api.object_get_class(object))) == name;
}
Method Verified(const char* image, const char* space, const char* type,
                const char* name, int argc, uintptr_t rva) {
    auto method = api.Find(api.Class(image, space, type), name, argc);
    api.Native(method, rva);
    return method;
}
bool Flag(Obj entity, int flag) {
    return api.Value<bool>(api.Invoke(methods.flagsTest, entity, {&flag}));
}
bool Summon(Obj entity) {
    int key = 1;
    return api.Value<bool>(api.Invoke(methods.boolGet, entity, {&key}));
}
bool Team(Obj entity, int8_t& team) {
    int key = 6;
    return api.Value<bool>(api.Invoke(methods.teamTryGet, entity, {&key, &team})) &&
        team >= 0 && team <= 3;
}
bool Integer(Obj entity, int key, int& value) {
    return api.Value<bool>(api.Invoke(methods.intTryGet, entity, {&key, &value}));
}
CombatSnapshot Snapshot(CombatState status, int level = 0) {
    return {status, state.generation, level};
}
}

void InstallCombat() {
    if (state.installed) return;
    methods.inCombat = Verified("Core.dll", "", "fxc", "bfzw", 0, 0x73E5E0);
    methods.information = Verified("Core.dll", "", "fxc", "bfzz", 0, 0x11E7E80);
    methods.phase = Verified("Core.dll", "", "fxj", "bhxx", 0, 0x956680);
    methods.flags = Verified("Core.dll", "", "fxj", "bhxv", 0, 0x96A340);
    methods.fightType = Verified("Core.dll", "", "fxj", "bhxz", 0, 0x753E30);
    methods.localId = Verified("Core.dll", "", "esg", "zcx", 0, 0x18BB140);
    methods.findEntity = Verified("Core.dll", "", "evv", "bamm", 2, 0x1957A70);
    methods.entities = Verified("Core.dll", "", "evv", "baml", 0, 0x19579E0);
    constexpr const char* ns = "Core.Services.Entities.EntityInformationServices";
    methods.flagsTest = Verified("Core.dll", ns, "EntityInfo", "bajs", 1, 0x19518F0);
    methods.teamTryGet = Verified("Core.dll", ns, "EntityInfo", "bajy", 2, 0x1951B60);
    methods.boolGet = Verified("Core.dll", ns, "EntityInfo", "bake", 1, 0x1951DB0);
    methods.intTryGet = Verified("Core.dll", ns, "EntityInfo", "baks", 2, 0x1952330);
    constexpr const char* data = "Ankama.Dofus.Core.DataCenter.dll";
    methods.monsters.Install();
    methods.currentMap = Verified("Core.dll", "", "esg", "zeo", 0, 0x185C1E0);
    constexpr const char* worldNs = "Core.DataCenter.Metadata.World";
    methods.mapId = Verified(data, worldNs, "WorldPoint", "get_mapId", 0, 0x68D2B0);
    methods.mapById = Verified(data, worldNs, "MapInformationData", "GetMapInformationById", 1, 0x7C4200);
    methods.mapCaptureAllowed = Verified(data, worldNs, "MapInformationData", "get_allowSoulCapture", 0, 0x7C4360);
    AddHook(api.Find(api.Class("Core.dll", "", "fxj"), "bhyl", 0), 0x1E05670,
        reinterpret_cast<void*>(PhaseHook), reinterpret_cast<void**>(&originalPhase));
    state.installed = true;
}

void ResetCombat() {
    state.fight.reset();
    state.cached.reset();
    state.localId = 0;
    state.observedMap = 0;
    state.observedInformation = nullptr;
    state.observedContext = nullptr;
    state.observedPhase = -1;
    state.observedTeam = -1;
    ClearClassification();
    Advance();
}

CombatSnapshot ReadCombat(Obj fight, Obj cached) {
    if (state.fight.get() != fight || state.cached.get() != cached) {
        state.fight.reset(fight);
        state.cached.reset(cached);
        state.localId = 0;
        state.observedMap = 0;
        state.observedContext = nullptr;
        state.observedTeam = -1;
        ClearClassification();
        Advance();
    }
    if (!state.installed || !cached || !Is(fight, "fxc")) return Snapshot(CombatState::Hidden);
    if (!api.Value<bool>(api.Invoke(methods.inCombat, fight))) {
        Observe(nullptr, 0);
        return Snapshot(CombatState::Hidden);
    }
    Ref information(api.Invoke(methods.information, fight));
    if (!Is(information.get(), "fxj")) return Snapshot(CombatState::Hidden);
    const int phase = api.Value<int>(api.Invoke(methods.phase, information.get()));
    Observe(information.get(), phase);
    if (phase != 1 && phase != 2) return Snapshot(CombatState::Hidden);
    if ((api.Value<uint32_t>(api.Invoke(methods.flags, information.get())) & 8) != 0)
        return Snapshot(CombatState::Hidden);
    const int fightType = api.Value<int>(api.Invoke(methods.fightType, information.get()));
    // The pinned enum defines -1 (no fight) and 0..10, but its semantic labels
    // are obfuscated. Never guess PvP/PvM from numeric values alone.
    if (fightType < 0 || fightType > 10) return Snapshot(CombatState::Hidden);

    Ref context(Read<Obj>(fight, 0x18));
    if (state.observedContext != context.get()) {
        state.observedContext = context.get();
        ClearClassification();
        state.observedTeam = -1;
        Advance();
    }
    const auto unavailable = [&] { return Snapshot(CombatState::Hidden); };
    if (!Is(context.get(), "fyb")) return unavailable();
    Ref character(Read<Obj>(context.get(), 0xF0)), entities(Read<Obj>(context.get(), 0x58));
    if (!Is(character.get(), "esg") || !Is(entities.get(), "evv") || !Read<Obj>(character.get(), 0x128))
        return unavailable();
    // This is the participant's current map, not a chat/world-map selection.
    // Re-read the native capability even after a previously eligible roster.
    Ref location(api.Invoke(methods.currentMap, character.get()));
    int64_t mapId = Is(location.get(), "tk") ? api.Value<int64_t>(api.Invoke(methods.mapId, location.get())) : 0;
    if (state.observedMap != mapId) {
        state.observedMap = mapId;
        ClearClassification();
        Advance();
    }
    Ref map(mapId > 0 ? api.Invoke(methods.mapById, nullptr, {&mapId}) : nullptr);
    if (!Is(map.get(), "MapInformationData") ||
        !api.Value<bool>(api.Invoke(methods.mapCaptureAllowed, map.get()))) {
        state.captureEligible = false;
        return Snapshot(CombatState::Hidden);
    }
    int64_t localId = api.Value<int64_t>(api.Invoke(methods.localId, character.get()));
    if (localId == 0) return unavailable();
    if (state.localId != localId) {
        state.localId = localId;
        ClearClassification();
        state.observedTeam = -1;
        Advance();
    }
    Obj found = nullptr;
    if (!api.Value<bool>(api.Invoke(methods.findEntity, entities.get(), {&localId, &found})) || !found)
        return unavailable();
    Ref local(found);
    if (!Is(local.get(), "EntityInfo") || !Flag(local.get(), 1) || !Flag(local.get(), 0x80) || Summon(local.get()))
        return Snapshot(CombatState::Hidden);
    int8_t localTeam = -1;
    if (!Team(local.get(), localTeam)) return unavailable();
    if (state.observedTeam != localTeam) {
        state.observedTeam = localTeam;
        ClearClassification();
        Advance();
    }
    if (state.kind == State::Kind::Pvp) return Snapshot(CombatState::Hidden);
    Ref list(api.Invoke(methods.entities, entities.get()));
    if (!list.get()) return unavailable();
    const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
    if (count < 0 || count > 512) return unavailable();
    bool pending = false, rosterPending = false, ordinaryMonster = false, capturable = false;
    int highest = 0;
    for (int i = 0; i < count; ++i) {
        Ref entity(api.Call(list.get(), "get_Item", {&i}));
        if (!Is(entity.get(), "EntityInfo")) { pending = rosterPending = true; continue; }
        if (!Flag(entity.get(), 1) || Summon(entity.get()) || Flag(entity.get(), 0x10)) continue;
        int8_t team = -1;
        if (!Team(entity.get(), team)) { pending = rosterPending = true; continue; }
        if (team == localTeam) continue;
        if (Flag(entity.get(), 0x80)) {
            state.kind = State::Kind::Pvp;
            return Snapshot(CombatState::Hidden);
        }
        if (!Flag(entity.get(), 8)) continue;
        int monsterId = 0, level = 0;
        if (!Integer(entity.get(), 11, monsterId) || monsterId <= 0) { pending = rosterPending = true; continue; }
        int64_t id = monsterId; // GetById takes Int64, even though entity key11 is Int32.
        Ref monster(methods.monsters.Find(id));
        if (!Is(monster.get(), "MonsterData")) { pending = rosterPending = true; continue; }
        ordinaryMonster = true;
        if (methods.monsters.Eligible(monster.get())) capturable = true;
        // Keep the requirement available after placement so equipment warnings
        // can take priority over the capture-effect reminder during combat.
        if (!Integer(entity.get(), 6, level) || level <= 0) { pending = true; continue; }
        highest = std::max(highest, level);
    }
    if (state.kind == State::Kind::Unknown) {
        if (rosterPending || !ordinaryMonster) return Snapshot(CombatState::Hidden);
        state.kind = State::Kind::Pvm;
    }
    if (phase == 2) {
        // Keep a confirmed eligible soul after that monster dies or disappears
        // from the entity list. The latch is scoped to this fight/map/participant.
        if (!rosterPending && capturable) state.captureEligible = true;
        if (!pending) state.highestLevel = std::max(state.highestLevel,highest);
        return Snapshot(state.captureEligible ? CombatState::Fighting : CombatState::Hidden,
            pending ? 0 : state.highestLevel);
    }
    state.captureEligible = capturable && !rosterPending;
    state.highestLevel = pending ? 0 : highest;
    if (!state.captureEligible) return Snapshot(CombatState::Hidden);
    // Type10 uses special native level scaling; retain the established
    // unsupported message only after a positive ordinary-monster roster.
    if (fightType == 10 || highest > 1000) return Snapshot(CombatState::Unsupported);
    if (pending || highest == 0) return Snapshot(CombatState::Pending);
    return Snapshot(CombatState::Ready, highest);
}
}
