#include "soul-capture.hpp"

namespace soul {
namespace {
using namespace managed;
struct Methods {
    Method inCombat{}, information{}, phase{}, flags{}, localId{}, findEntity{};
    Method flagsTest{}, boolGet{}, teamTryGet{}, currentState{}, hasState{};
} methods;
bool installed = false;
bool readFailureLogged = false;

bool Is(Obj object, const char* name) {
    return object && std::string(api.class_get_name(api.object_get_class(object))) == name;
}
Method Verified(const char* space, const char* type, const char* name, int argc, uintptr_t rva) {
    auto method = api.Find(api.Class("Core.dll", space, type), name, argc);
    api.Native(method, rva);
    return method;
}
bool Flag(Obj entity, int flag) {
    return api.Value<bool>(api.Invoke(methods.flagsTest, entity, {&flag}));
}
CaptureState ReadCurrent(Obj fight, Obj cached, int64_t* participant = nullptr) {
    if (!installed || !cached || !Is(fight, "fxc")) return CaptureState::Unknown;
    if (!api.Value<bool>(api.Invoke(methods.inCombat, fight))) return CaptureState::Unknown;
    Ref information(api.Invoke(methods.information, fight));
    if (!Is(information.get(), "fxj") ||
        api.Value<int>(api.Invoke(methods.phase, information.get())) != 2 ||
        (api.Value<uint32_t>(api.Invoke(methods.flags, information.get())) & 8) != 0)
        return CaptureState::Unknown;

    Ref context(Read<Obj>(fight, 0x18));
    if (!Is(context.get(), "fyb")) return CaptureState::Unknown;
    Ref character(Read<Obj>(context.get(), 0xF0)), entities(Read<Obj>(context.get(), 0x58));
    Ref stateService(Read<Obj>(context.get(), 0x60));
    if (!Is(character.get(), "esg") || !Is(entities.get(), "evv") ||
        !Is(stateService.get(), "ebs") || !Read<Obj>(character.get(), 0x128) ||
        Read<Obj>(stateService.get(), 0x20) != context.get())
        return CaptureState::Unknown;

    int64_t localId = api.Value<int64_t>(api.Invoke(methods.localId, character.get()));
    if (localId == 0) return CaptureState::Unknown;
    Obj found = nullptr;
    if (!api.Value<bool>(api.Invoke(methods.findEntity, entities.get(), {&localId, &found})) || !found)
        return CaptureState::Unknown;
    Ref local(found);
    int summonKey = 1, teamKey = 6;
    int8_t team = -1;
    if (!Is(local.get(), "EntityInfo") || !Flag(local.get(), 1) || !Flag(local.get(), 0x80) ||
        api.Value<bool>(api.Invoke(methods.boolGet, local.get(), {&summonKey})) ||
        !api.Value<bool>(api.Invoke(methods.teamTryGet, local.get(), {&teamKey, &team})) ||
        team < 0 || team > 3)
        return CaptureState::Unknown;

    Ref current(api.Invoke(methods.currentState, stateService.get()));
    if (!Is(current.get(), "FightState")) return CaptureState::Unknown;
    if (participant) *participant = localId;
    // State 2 belongs to the caster / eligible allied player, not the monsters.
    // vuk queries the current buffs and the native active-state evaluator, which
    // accounts for state removal, disabling, delays and remaining duration.
    // Never retain its result or use the separate dispelled-buffs dictionary.
    int stateId = 2;
    return api.Value<bool>(api.Invoke(methods.hasState, current.get(), {&localId, &stateId}))
        ? CaptureState::Active : CaptureState::Missing;
}
}

void InstallCapture() {
    if (installed) return;
    methods.inCombat = Verified("", "fxc", "bfzw", 0, 0x73E5E0);
    methods.information = Verified("", "fxc", "bfzz", 0, 0x11E7E80);
    methods.phase = Verified("", "fxj", "bhxx", 0, 0x956680);
    methods.flags = Verified("", "fxj", "bhxv", 0, 0x96A340);
    methods.localId = Verified("", "esg", "zcx", 0, 0x18BB140);
    methods.findEntity = Verified("", "evv", "bamm", 2, 0x1957A70);
    constexpr const char* entityNs = "Core.Services.Entities.EntityInformationServices";
    methods.flagsTest = Verified(entityNs, "EntityInfo", "bajs", 1, 0x19518F0);
    methods.boolGet = Verified(entityNs, "EntityInfo", "bake", 1, 0x1951DB0);
    methods.teamTryGet = Verified(entityNs, "EntityInfo", "bajy", 2, 0x1951B60);
    methods.currentState = Verified("", "ebs", "vpl", 0, 0x27855B0);
    methods.hasState = Verified("Core.Features.Fight.FightStates.State", "FightState", "vuk", 2, 0x2773360);
    installed = true;
}

void ResetCapture() {
    // No retained managed objects or last-cast/capture state exists to restore.
    readFailureLogged = false;
}

CaptureState ReadCapture(Obj fight, Obj cached) {
    try {
        const auto result = ReadCurrent(fight, cached);
        if (result != CaptureState::Unknown) readFailureLogged = false;
        return result;
    } catch (const std::exception& error) {
        if (!readFailureLogged) {
            readFailureLogged = true;
            Log(std::string("Soul capture read unavailable: ") + error.what());
        }
        return CaptureState::Unknown;
    }
}
int64_t CaptureParticipant(Obj fight, Obj cached) {
    int64_t participant = 0;
    try { ReadCurrent(fight, cached, &participant); }
    catch (const std::exception& error) { Log(std::string("SOUL_PARTICIPANT_ERROR ") + error.what()); }
    return participant;
}

}
