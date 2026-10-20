#include "soul-capture.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>

// These tests substitute the managed API. They never load a game DLL, inspect
// a process or touch game configuration. The native state evaluator's expiry
// and dispel semantics are verified separately in saved disassembly.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    alignas(16) std::array<unsigned char, 0x200> bytes{};
    Class* klass = nullptr;
    int64_t box = 0, id = 0;
    int flags = 1 | 0x80, team = 0;
    bool hasTeam = true, summon = false;
    std::map<int64_t, Obj> entities;
    std::set<std::pair<int64_t, int>> activeStates;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<Obj, Object*> lookup;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xFE1234560000ULL;
std::string failMethod;
std::vector<std::string> logs;
unsigned stateQueries = 0, guardCount = 0;
int64_t lastEntityId = 0;
bool nullQueryResult = false;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Class* Type(const std::string& name) {
    auto& klass = classes[name];
    if (!klass) klass = std::make_unique<Class>(Class{name});
    return klass.get();
}
Object& Get(Obj obj) {
    auto it = lookup.find(obj);
    if (it == lookup.end()) throw std::runtime_error("Unknown fake capture object");
    return *it->second;
}
Obj Make(const char* name) {
    auto result = std::make_unique<Object>();
    result->klass = Type(name);
    Obj object = result->bytes.data();
    lookup[object] = result.get();
    objects.push_back(std::move(result));
    return object;
}
template<class T> void Set(Obj obj, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(obj).bytes.size(), "Fake field outside object");
    std::memcpy(Get(obj).bytes.data() + offset, &value, sizeof(value));
}
Obj Box(int64_t value) { auto obj = Make("Box"); Get(obj).box = value; return obj; }
void Initialize() {
    api.object_get_class = [](Obj obj) -> Klass { return Get(obj).klass; };
    api.class_get_name = [](Klass klass) -> const char* { return static_cast<Class*>(klass)->name.c_str(); };
    api.object_unbox = [](Obj obj) -> void* { return &Get(obj).box; };
    api.gchandle_new = [](Obj obj, bool) -> GCHandle {
        auto handle = ++nextHandle; handles.emplace(handle, obj); return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Invalid or truncated capture GC handle");
        return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) {
        Check(handles.erase(handle) == 1, "Duplicate capture GC free");
    };
}
struct Fixture {
    Obj fight, cached, information, context, character, entities, local, stateService, current;
    Fixture() {
        soul::ResetCapture(); failMethod.clear(); logs.clear(); stateQueries = 0; nullQueryResult = false;
        fight = Make("fxc"); cached = Make("CachedDataService"); information = Make("fxj");
        context = Make("fyb"); character = Make("esg"); entities = Make("evv");
        local = Make("EntityInfo"); stateService = Make("ebs"); current = Make("FightState");
        Set(fight, 0x18, context); Set(fight, 0xB0, true); Set(fight, 0xC0, information);
        Set(information, 0x64, 2); Set(information, 0x60, uint32_t{0});
        Set(context, 0x58, entities); Set(context, 0x60, stateService); Set(context, 0xF0, character);
        Set(character, 0x128, Make("CharacterInformation")); Get(character).id = 0x12345678ABCDLL;
        Get(entities).entities[Get(character).id] = local;
        Set(stateService, 0x20, context); Set(stateService, 0x40, current); Set(stateService, 0x10, true);
    }
    ~Fixture() {
        soul::ResetCapture();
        Check(handles.empty(), "Capture GC references retained between reads");
    }
    soul::CaptureState Read() {
        const auto result = soul::ReadCapture(fight, cached);
        Check(handles.empty(), "Capture GC reference leak after read");
        return result;
    }
    void Active(bool active = true) {
        auto key = std::make_pair(Get(character).id, 2);
        if (active) Get(current).activeStates.insert(key);
        else Get(current).activeStates.erase(key);
    }
};
}

namespace managed {
Api api{};
Klass Api::Class(const char* image, const char* space, const char* name) const {
    fake::Check(std::string(image) == "Core.dll", "Wrong capture image");
    if (std::string(name) == "FightState")
        fake::Check(std::string(space) == "Core.Features.Fight.FightStates.State", "Wrong state namespace");
    return fake::Type(name);
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto result = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc});
    auto method = result.get(); fake::methods.push_back(std::move(result)); return method;
}
void* Api::Native(Method method, uintptr_t rva) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    static const std::map<std::string, std::pair<uintptr_t, int>> expected{
        {"fxc.bfzw", {0x73E5E0, 0}}, {"fxc.bfzz", {0x11E7E80, 0}},
        {"fxj.bhxx", {0x956680, 0}}, {"fxj.bhxv", {0x96A340, 0}},
        {"esg.zcx", {0x18BB140, 0}}, {"evv.bamm", {0x1957A70, 2}},
        {"EntityInfo.bajs", {0x19518F0, 1}}, {"EntityInfo.bake", {0x1951DB0, 1}},
        {"EntityInfo.bajy", {0x1951B60, 2}}, {"ebs.vpl", {0x27855B0, 0}},
        {"FightState.vuk", {0x2773360, 2}}
    };
    auto it = expected.find(m.klass->name + "." + m.name);
    fake::Check(it != expected.end() && it->second.first == rva && it->second.second == m.argc,
                "Wrong capture native signature/guard");
    ++fake::guardCount;
    return reinterpret_cast<void*>(rva);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    const std::vector<void*> args(values);
    fake::Check(static_cast<int>(args.size()) == m.argc, "Wrong capture managed arity");
    auto& object = fake::Get(self);
    fake::Check(object.klass == m.klass, "Wrong capture receiver type");
    if (m.name == fake::failMethod) throw std::runtime_error("Fixture native read unavailable");
    if (m.name == "bfzw") return fake::Box(Read<bool>(self, 0xB0));
    if (m.name == "bfzz") return Read<Obj>(self, 0xC0);
    if (m.name == "bhxx") return fake::Box(Read<int>(self, 0x64));
    if (m.name == "bhxv") return fake::Box(Read<uint32_t>(self, 0x60));
    if (m.name == "zcx") return fake::Box(object.id);
    if (m.name == "bamm") {
        const auto id = *static_cast<int64_t*>(args.at(0));
        auto it = object.entities.find(id);
        *static_cast<Obj*>(args.at(1)) = it == object.entities.end() ? nullptr : it->second;
        return fake::Box(it != object.entities.end());
    }
    if (m.name == "bajs") {
        const int flag = *static_cast<int*>(args.at(0));
        return fake::Box((object.flags & flag) == flag);
    }
    if (m.name == "bake") {
        fake::Check(*static_cast<int*>(args.at(0)) == 1, "Wrong summon key");
        return fake::Box(object.summon);
    }
    if (m.name == "bajy") {
        fake::Check(*static_cast<int*>(args.at(0)) == 6, "Wrong team key");
        *static_cast<int8_t*>(args.at(1)) = static_cast<int8_t>(object.team);
        return fake::Box(object.hasTeam);
    }
    if (m.name == "vpl") return Read<bool>(self, 0x10) ? Read<Obj>(self, 0x40) : nullptr;
    if (m.name == "vuk") {
        ++fake::stateQueries;
        fake::Check(*static_cast<int*>(args.at(1)) == 2, "Wrong capture state ID");
        fake::lastEntityId = *static_cast<int64_t*>(args.at(0));
        if (fake::nullQueryResult) return nullptr;
        return fake::Box(object.activeStates.contains({fake::lastEntityId, 2}));
    }
    throw std::runtime_error("Unexpected capture method: " + m.name);
}
}
void Log(const std::string& message) { fake::logs.push_back(message); }

namespace {
using fake::Check;
using fake::Fixture;
using fake::Get;
using fake::Make;
using fake::Set;
using managed::Obj;
using soul::CaptureState;
void CurrentLocalEffectOnly() {
    Fixture f;
    Check(f.Read() == CaptureState::Missing, "Empty current buff state should be Missing");
    const auto localId = Get(f.character).id;
    Get(f.current).activeStates.insert({localId + 1, 2}); // An ally's active effect is insufficient.
    Get(f.current).activeStates.insert({-42, 2}); // Nor an effect on a monster.
    Get(f.current).activeStates.insert({localId, 2768}); // Equipped-stone state is insufficient.
    Check(f.Read() == CaptureState::Missing, "Other state/entity must not turn local capture green");
    f.Active();
    Check(f.Read() == CaptureState::Active, "Current local capture state should be Active");
    Check(fake::lastEntityId == localId && localId > UINT32_MAX, "Local ID was truncated");
    f.Active(false); // Native evaluator now reports expired / removed / dispelled / disabled.
    Check(f.Read() == CaptureState::Missing, "Ended capture state must not stay latched");
    f.Active();
    Check(f.Read() == CaptureState::Active, "Reapplied/collective capture state should be read directly");
}
void ActiveParticipantRequired() {
    Fixture f; f.Active();
    for (int phase : {0, 1, 3}) {
        Set(f.information, 0x64, phase);
        Check(f.Read() == CaptureState::Unknown, "Capture outside active phase must be Unknown");
    }
    Set(f.information, 0x64, 2);
    Set(f.fight, 0xB0, false);
    Check(f.Read() == CaptureState::Unknown, "World state must be Unknown");
    Set(f.fight, 0xB0, true);
    Set(f.information, 0x60, uint32_t{8});
    Check(f.Read() == CaptureState::Unknown, "Spectator must not query capture");
    Set(f.information, 0x60, uint32_t{0});
    Get(f.local).flags = 1 | 8;
    Check(f.Read() == CaptureState::Unknown, "A monster cannot be the local player");
    Get(f.local).flags = 0x80;
    Check(f.Read() == CaptureState::Unknown, "A world player is not a fighter");
    Get(f.local).flags = 1 | 0x80; Get(f.local).summon = true;
    Check(f.Read() == CaptureState::Unknown, "Summon must not be treated as local fighter");
    Get(f.local).summon = false; Get(f.local).hasTeam = false;
    Check(f.Read() == CaptureState::Unknown, "Unresolved team must be Unknown");
    Get(f.local).hasTeam = true; Get(f.local).team = 4;
    Check(f.Read() == CaptureState::Unknown, "Invalid team must be Unknown");
    Check(fake::stateQueries == 0, "Invalid participant reached native state query");
    Get(f.local).team = 0;
    Check(f.Read() == CaptureState::Active, "Valid active participant should recover");
}
void UnavailableDataIsNotMissing() {
    Fixture f; f.Active();
    const auto savedCached = f.cached; f.cached = nullptr;
    Check(f.Read() == CaptureState::Unknown, "Null cached service must be Unknown");
    f.cached = savedCached;
    Set(f.fight, 0xC0, Obj{nullptr});
    Check(f.Read() == CaptureState::Unknown, "Null fight information must be Unknown");
    Set(f.fight, 0xC0, Make("WrongType"));
    Check(f.Read() == CaptureState::Unknown, "Wrong information type must be Unknown");
    Set(f.fight, 0xC0, f.information);
    Set(f.context, 0x60, Obj{nullptr});
    Check(f.Read() == CaptureState::Unknown, "Missing state service must be Unknown");
    Set(f.context, 0x60, Make("WrongType"));
    Check(f.Read() == CaptureState::Unknown, "Wrong state service must be Unknown");
    Set(f.context, 0x60, f.stateService);
    Set(f.stateService, 0x20, Make("fyb"));
    Check(f.Read() == CaptureState::Unknown, "Mismatched service context must be Unknown");
    Set(f.stateService, 0x20, f.context);
    Set(f.stateService, 0x10, false);
    Check(f.Read() == CaptureState::Unknown, "Disabled native state service must be Unknown");
    Set(f.stateService, 0x10, true); Set(f.stateService, 0x40, Obj{nullptr});
    Check(f.Read() == CaptureState::Unknown, "Missing current FightState must be Unknown");
    Set(f.stateService, 0x40, Make("WrongType"));
    Check(f.Read() == CaptureState::Unknown, "Wrong current state type must be Unknown");
    Set(f.stateService, 0x40, f.current);
    const auto data = managed::Read<Obj>(f.character, 0x128);
    Set(f.character, 0x128, Obj{nullptr});
    Check(f.Read() == CaptureState::Unknown, "Uninitialized character must be Unknown");
    Set(f.character, 0x128, data);
    const auto id = Get(f.character).id; Get(f.character).id = 0;
    Check(f.Read() == CaptureState::Unknown, "Zero local ID must be Unknown");
    Get(f.character).id = id; Get(f.entities).entities.clear();
    Check(f.Read() == CaptureState::Unknown, "Local player absent from roster must be Unknown");
    Get(f.entities).entities[id] = Make("WrongType");
    Check(f.Read() == CaptureState::Unknown, "Wrong local entity type must be Unknown");
    Check(fake::stateQueries == 0, "Incomplete data reached native state query");
    Get(f.entities).entities[id] = f.local;
    Check(f.Read() == CaptureState::Active, "Available data should recover");
}
void NewFightOrCharacterDoesNotInheritGreen() {
    Fixture f; f.Active();
    Check(f.Read() == CaptureState::Active, "Initial capture should be Active");
    f.current = Make("FightState"); Set(f.stateService, 0x40, f.current);
    Check(f.Read() == CaptureState::Missing, "New current FightState inherited old effect");
    f.Active();
    Get(f.character).id += 100;
    Check(f.Read() == CaptureState::Unknown, "New character absent from roster must be Unknown");
    Get(f.entities).entities[Get(f.character).id] = f.local;
    Check(f.Read() == CaptureState::Missing, "New local ID inherited old character effect");
    f.Active();
    Check(f.Read() == CaptureState::Active, "New character live state should be readable");
    soul::ResetCapture(); f.Active(false);
    Check(f.Read() == CaptureState::Missing, "Reset retained capture state");
}
void ReadFailureIsUnknownAndRecoverable() {
    Fixture f; f.Active();
    for (const char* method : {"bfzz", "zcx", "bamm", "bajy", "vpl", "vuk"}) {
        fake::failMethod = method;
        Check(f.Read() == CaptureState::Unknown, "Native read exception must be Unknown");
    }
    Check(fake::logs.size() == 1, "Repeated native failures flooded logs");
    fake::failMethod.clear();
    Check(f.Read() == CaptureState::Active, "Reader failed to recover");
    fake::nullQueryResult = true;
    Check(f.Read() == CaptureState::Unknown, "Null bool return must not mean Missing");
    Check(fake::logs.size() == 2, "New failure after recovery should be logged");
    fake::nullQueryResult = false; f.Active(false);
    Check(f.Read() == CaptureState::Missing, "Recovery should read actual missing state");
}
}

int main() {
    try {
        fake::Initialize();
        Check(soul::ReadCapture(nullptr, nullptr) == CaptureState::Unknown, "Uninstalled capture must be Unknown");
        soul::InstallCapture(); soul::InstallCapture();
        Check(fake::guardCount == 11, "Install must guard every method exactly once");
        CurrentLocalEffectOnly();
        ActiveParticipantRequired();
        UnavailableDataIsNotMissing();
        NewFightOrCharacterDoesNotInheritGreen();
        ReadFailureIsUnknownAndRecoverable();
        std::cout << "SoulCaptureTests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SoulCaptureTests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
