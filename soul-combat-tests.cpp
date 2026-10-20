#include "soul-combat.hpp"
#include "module.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>

// Offline fake objects exercise the production reader and phase hook. This
// executable never loads a game DLL or accesses a process, account or settings.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    alignas(16) std::array<unsigned char, 0x200> bytes{};
    Class* klass = nullptr;
    std::map<int, int> integers;
    int flags = 0, team = 0;
    bool hasTeam = true, summon = false, forbidden = false, captureAllowed = true;
    bool boss = true, arch = false;
    Obj souls = nullptr;
    int64_t box = 0, id = 0;
    std::vector<Obj> items;
    std::map<int64_t, Obj> byId;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<Obj, Object*> lookup;
std::unordered_map<GCHandle, Obj> handles;
std::map<int64_t, Obj> monsters;
std::map<int64_t, Obj> maps;
GCHandle nextHandle = 0xFA1234560000ULL;
unsigned originalCalls = 0;
using PhaseFn = void(*)(Obj, Method);
PhaseFn phaseHook = nullptr;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Class* Type(const std::string& name) {
    auto& klass = classes[name];
    if (!klass) klass = std::make_unique<Class>(Class{name});
    return klass.get();
}
Object& Get(Obj obj) {
    auto it = lookup.find(obj);
    if (it == lookup.end()) throw std::runtime_error("Unknown fake combat object");
    return *it->second;
}
Obj Make(const char* name) {
    auto result = std::make_unique<Object>();
    result->klass = Type(name);
    auto obj = result->bytes.data();
    lookup[obj] = result.get();
    objects.push_back(std::move(result));
    if (std::string(name) == "MonsterData") {
        Get(obj).souls = Make("List");
        Get(Get(obj).souls).items.push_back(Make("MonsterSoulData"));
    }
    return obj;
}
template<class T> void Set(Obj obj, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(obj).bytes.size(), "Fake offset out of range");
    std::memcpy(Get(obj).bytes.data() + offset, &value, sizeof(value));
}
Obj Box(int64_t value) { auto obj = Make("Box"); Get(obj).box = value; return obj; }
void NativePhase(Obj, Method) { ++originalCalls; }
void Initialize() {
    api.object_get_class = [](Obj obj) -> Klass { return Get(obj).klass; };
    api.class_get_name = [](Klass k) -> const char* { return static_cast<Class*>(k)->name.c_str(); };
    api.object_unbox = [](Obj obj) -> void* { return &Get(obj).box; };
    api.gchandle_new = [](Obj obj, bool) -> GCHandle {
        auto handle = ++nextHandle; handles.emplace(handle, obj); return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Invalid or truncated soul combat GC handle");
        return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Duplicate combat GC free"); };
}
struct Fixture {
    Obj fight, cached, info, context, character, entities, local, enemy, location, map;
    Fixture() {
        soul::ResetCombat(); monsters.clear(); maps.clear(); originalCalls = 0;
        fight = Make("fxc"); cached = Make("CachedDataService"); info = Make("fxj");
        context = Make("fyb"); character = Make("esg"); entities = Make("evv");
        Set(fight, 0x18, context); Set(fight, 0xC0, info); Set(fight, 0xB0, true);
        Set(context, 0x58, entities); Set(context, 0xF0, character);
        Set(character, 0x128, Make("CharacterInformation")); Get(character).id = 1234567890123LL;
        location = Make("tk"); Get(location).id = 9876543210LL;
        map = Make("MapInformationData"); maps[Get(location).id] = map;
        Set(character, 0x1A0, location);
        Set(info, 0x64, 1); Set(info, 0x60, uint32_t{0}); Set(info, 0x68, 4);
        local = Entity(1 | 0x80, 0, 0, 200); Get(entities).byId[Get(character).id] = local;
        enemy = Entity(1 | 8, 1, 42, 51);
    }
    ~Fixture() { soul::ResetCombat(); Check(handles.empty(), "Combat GC handles leaked"); }
    Obj Entity(int flags, int team, int monsterId, int level) {
        auto result = Make("EntityInfo"); auto& e = Get(result);
        e.flags = flags; e.team = team; e.integers[6] = level; e.integers[11] = monsterId;
        Get(entities).items.push_back(result);
        if (monsterId > 0 && !monsters.contains(monsterId)) monsters[monsterId] = Make("MonsterData");
        return result;
    }
    soul::CombatSnapshot Read() { return soul::ReadCombat(fight, cached); }
    void Phase(int phase) { Set(info, 0x64, phase); phaseHook(info, nullptr); }
};
}

namespace managed {
Api api{};
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass k, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(k), name, argc});
    auto result = method.get(); fake::methods.push_back(std::move(method)); return result;
}
void* Api::Native(Method, uintptr_t rva) const { return reinterpret_cast<void*>(rva); }
Obj Api::Field(Obj self, const char* name) const {
    fake::Check(std::string(name)=="souls", "Unexpected monster field");
    return fake::Get(self).souls;
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    auto& object = fake::Get(self);
    if (std::string(name) == "get_Count") return fake::Box(static_cast<int>(object.items.size()));
    if (std::string(name) == "get_Item") return object.items.at(*static_cast<int*>(*args.begin()));
    throw std::runtime_error(std::string("Unexpected combat call ") + name);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    const std::vector<void*> args(values);
    fake::Check(static_cast<int>(args.size()) == m.argc, "Wrong managed combat arity");
    if (m.name == "GetById") {
        fake::Check(self == nullptr, "Monster GetById must be static");
        auto id = *static_cast<int64_t*>(args.at(0));
        auto it = fake::monsters.find(id);
        return it == fake::monsters.end() ? nullptr : it->second;
    }
    if (m.name == "GetMapInformationById") {
        fake::Check(self == nullptr, "Map lookup must be static");
        auto it = fake::maps.find(*static_cast<int64_t*>(args.at(0)));
        return it == fake::maps.end() ? nullptr : it->second;
    }
    auto& object = fake::Get(self);
    if (m.name == "bfzw") return fake::Box(Read<bool>(self, 0xB0));
    if (m.name == "bfzz") return Read<Obj>(self, 0xC0);
    if (m.name == "bhxx") return fake::Box(Read<int>(self, 0x64));
    if (m.name == "bhxv") return fake::Box(Read<uint32_t>(self, 0x60));
    if (m.name == "bhxz") return fake::Box(Read<int>(self, 0x68));
    if (m.name == "zcx") return fake::Box(object.id);
    if (m.name == "zeo") return Read<Obj>(self, 0x1A0);
    if (m.name == "get_mapId") return fake::Box(object.id);
    if (m.name == "get_allowSoulCapture") return fake::Box(object.captureAllowed);
    if (m.name == "baml") return self;
    if (m.name == "bamm") {
        auto it = object.byId.find(*static_cast<int64_t*>(args.at(0)));
        *static_cast<Obj*>(args.at(1)) = it == object.byId.end() ? nullptr : it->second;
        return fake::Box(it != object.byId.end());
    }
    if (m.name == "bajs") {
        int flag = *static_cast<int*>(args.at(0)); return fake::Box((object.flags & flag) == flag);
    }
    if (m.name == "bajy") {
        fake::Check(*static_cast<int*>(args.at(0)) == 6, "Wrong combat team key");
        *static_cast<int8_t*>(args.at(1)) = static_cast<int8_t>(object.team);
        return fake::Box(object.hasTeam);
    }
    if (m.name == "bake") {
        fake::Check(*static_cast<int*>(args.at(0)) == 1, "Wrong combat summon key");
        return fake::Box(object.summon);
    }
    if (m.name == "baks") {
        auto it = object.integers.find(*static_cast<int*>(args.at(0)));
        if (it != object.integers.end()) *static_cast<int*>(args.at(1)) = it->second;
        return fake::Box(it != object.integers.end());
    }
    if (m.name == "get_soulCaptureForbidden") return fake::Box(object.forbidden);
    if (m.name == "get_isBoss") return fake::Box(object.boss);
    if (m.name == "get_isMiniBoss") return fake::Box(object.arch);
    throw std::runtime_error("Unexpected combat invoke " + m.name);
}
}
void AddHook(managed::Method, uintptr_t rva, void* replacement, void** original) {
    fake::Check(rva == 0x1E05670, "Unexpected combat hook");
    fake::phaseHook = reinterpret_cast<fake::PhaseFn>(replacement);
    *original = reinterpret_cast<void*>(fake::NativePhase);
}

namespace {
using namespace fake;
using soul::CombatState;
void PlacementAndMaximum() {
    Fixture f;
    auto first = f.Read(); Check(first.state == CombatState::Ready && first.highestLevel == 51, "Current enemy level unavailable");
    Check(first.fightIdentity != 0 && f.Read().fightIdentity == first.fightIdentity, "Stable placement changed identity");
    f.Entity(1 | 8, 1, 43, 50);
    Check(f.Read().highestLevel == 51, "Levels were summed instead of taking maximum");
    Get(f.enemy).integers[6] = 190;
    Check(f.Read().highestLevel == 190, "Current level changes not refreshed");
    Set(f.fight, 0xB0, false); Check(f.Read().state == CombatState::Hidden, "Shown outside combat");
    Set(f.fight, 0xB0, true);
    f.Phase(2); Check(f.Read().state == CombatState::Fighting, "Active combat must retain a non-equipment status panel");
    f.Phase(0); Check(f.Read().state == CombatState::Hidden, "Shown after combat end");
    f.Phase(1); Set(f.info, 0x60, uint32_t{8});
    Check(f.Read().state == CombatState::Hidden, "Spectator received helper");
    f.Phase(2); Check(f.Read().state == CombatState::Hidden, "Active spectator received helper");
}
void ExclusionsAndUnknown() {
    Fixture f;
    f.Entity(1 | 8, 0, 43, 900); // Allied monster.
    auto summon = f.Entity(1 | 8, 1, 44, 1000); Get(summon).summon = true;
    f.Entity(8, 1, 45, 950); // Non-fighter cached world entity.
    f.Entity(1 | 8 | 0x10, 1, 46, 950); // Companion.
    Check(f.Read().highestLevel == 51, "Excluded entities changed capture maximum");
    auto unknown = f.Entity(1 | 8, 1, 47, 500);
    Get(unknown).integers.erase(6);
    Check(f.Read().state == CombatState::Pending, "Partial unknown roster understated requirement");
    Get(unknown).integers[6] = 500; Get(unknown).hasTeam = false;
    Check(f.Read().state == CombatState::Hidden, "Unknown enemy team displayed a capture popup");
    Get(unknown).hasTeam = true; monsters.erase(47);
    Check(f.Read().state == CombatState::Hidden, "Unknown monster metadata displayed a capture popup");
    Get(unknown).summon = true;
    Check(f.Read().state == CombatState::Ready, "Unknown excluded summon prevented recommendation");
    f.Entity(1 | 0x80, 1, 0, 200);
    Check(f.Read().state == CombatState::Hidden, "Enemy player fight displayed a soul helper");
}
void ForbiddenAndSpecial() {
    Fixture f;
    Get(monsters.at(42)).forbidden = true;
    Check(f.Read().state == CombatState::Hidden, "All forbidden group displayed a popup");
    f.Entity(1 | 8, 1, 43, 20);
    Check(f.Read().state == CombatState::Ready && f.Read().highestLevel == 51, "Mixed forbidden group understated highest enemy level");
    Get(f.enemy).integers[6] = 1001;
    Check(f.Read().state == CombatState::Unsupported, "Unsupported capacity was silently capped");
    Get(f.enemy).integers[6] = 51; Set(f.info, 0x68, 10);
    Check(f.Read().state == CombatState::Unsupported, "Special scaled fight was recommended");
}
void ParticipantAndSession() {
    Fixture f;
    const auto first = f.Read().fightIdentity;
    // These transitions happen between refreshes: a stale prior button must fail.
    f.Phase(0); f.Phase(1);
    Check(originalCalls == 2 && f.Read().fightIdentity != first, "Native phase lifecycle was not observed");
    auto prior = f.Read().fightIdentity;
    f.cached = Make("CachedDataService");
    Check(f.Read().fightIdentity != prior, "Cache/session replacement retained click identity");
    prior = f.Read().fightIdentity;
    Get(f.character).id += 1; // Deliberate local character change.
    Get(f.entities).byId[Get(f.character).id] = f.local;
    Check(f.Read().fightIdentity != prior, "Local character change retained click identity");
    Get(f.entities).byId.clear();
    Check(f.Read().state == CombatState::Hidden, "Missing participant received a capture popup");
    Get(f.entities).byId[Get(f.character).id] = f.local;
    Get(f.local).flags = 1 | 8;
    Check(f.Read().state == CombatState::Hidden, "Controlled non-player treated as own participant");
}
void PvpHiddenDuringEveryPhase() {
    Fixture f;
    Get(f.enemy).flags = 1 | 0x80;
    auto summon = f.Entity(1 | 8, 1, 44, 500); Get(summon).summon = true;
    f.Entity(1 | 8 | 0x10, 1, 45, 500);
    Check(f.Read().state == CombatState::Hidden, "PvP placement was visible");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "PvP active phase was visible");
    Get(f.entities).items = {f.local, summon};
    auto ordinary = f.Entity(1 | 8, 1, 46, 51);
    Check(f.Read().state == CombatState::Hidden, "PvP reappeared after hostile player removal");
    f.Phase(1);
    Check(f.Read().state == CombatState::Ready, "New placement inherited prior PvP classification");
    Get(ordinary).flags = 1 | 0x80;
    Check(f.Read().state == CombatState::Hidden, "Late hostile player did not override PvM");
    f.Phase(0); Get(ordinary).flags = 1 | 8; f.Phase(1);
    Check(f.Read().state == CombatState::Ready, "Idle phase did not clear PvP latch");
}
void PositivePvmClassification() {
    Fixture f;
    Get(f.entities).items = {f.local};
    Check(f.Read().state == CombatState::Hidden, "Empty placement roster displayed a helper");
    auto summon = f.Entity(1 | 8, 1, 44, 500); Get(summon).summon = true;
    f.Entity(1 | 8 | 0x10, 1, 45, 500);
    f.Entity(8, 1, 46, 500);
    Check(f.Read().state == CombatState::Hidden, "Summon/companion/world monster qualified as PvM");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "Unclassified active combat was visible");
    Get(f.entities).items.push_back(f.enemy);
    Get(f.enemy).hasTeam = false;
    Check(f.Read().state == CombatState::Hidden, "Unknown team qualified as PvM");
    Get(f.enemy).hasTeam = true; monsters.erase(42);
    Check(f.Read().state == CombatState::Hidden, "Unresolved monster metadata qualified as PvM");
    monsters[42] = Make("MonsterData"); Get(f.enemy).integers.erase(6);
    Check(f.Read().state == CombatState::Fighting, "Positive PvM in active combat required obsolete level data");
    Get(f.entities).items = {f.local};
    Check(f.Read().state == CombatState::Fighting, "Confirmed PvM disappeared as monsters died");
    f.Entity(1 | 0x80, 1, 0, 200);
    Check(f.Read().state == CombatState::Hidden, "Active late enemy player did not override PvM latch");
}
void PvpIdentityAndUnknownKinds() {
    Fixture f;
    Get(f.enemy).flags = 1 | 0x80;
    Check(f.Read().state == CombatState::Hidden, "Initial PvP not hidden");
    Get(f.enemy).flags = 1 | 8;
    f.info = Make("fxj"); Set(f.fight, 0xC0, f.info);
    Set(f.info, 0x64, 1); Set(f.info, 0x68, 4);
    Check(f.Read().state == CombatState::Ready, "New information owner retained PvP latch");
    Get(f.enemy).flags = 1 | 0x80; Check(f.Read().state == CombatState::Hidden, "Changed roster not hidden");
    Get(f.enemy).flags = 1 | 8;
    f.context = Make("fyb"); Set(f.context, 0x58, f.entities); Set(f.context, 0xF0, f.character);
    Set(f.fight, 0x18, f.context);
    Check(f.Read().state == CombatState::Ready, "New fight context retained PvP latch");
    Get(f.enemy).flags = 1 | 0x80; Check(f.Read().state == CombatState::Hidden, "Second PvP not hidden");
    Set(f.fight, 0xB0, false); Check(f.Read().state == CombatState::Hidden, "Outside combat not hidden");
    Get(f.enemy).flags = 1 | 8; Set(f.fight, 0xB0, true);
    Check(f.Read().state == CombatState::Ready, "Combat exit without phase callback retained PvP latch");
    for (int kind : {-1, 11, 999}) {
        Set(f.info, 0x68, kind);
        Check(f.Read().state == CombatState::Hidden, "Unknown fight kind displayed a helper");
        f.Phase(2); Check(f.Read().state == CombatState::Hidden, "Unknown active fight kind displayed a helper");
        f.Phase(1);
    }
    Set(f.info, 0x68, 4); Get(f.enemy).integers.erase(6);
    Check(f.Read().state == CombatState::Pending, "Known ordinary PvM with missing level must remain pending");
}
void MapPermissionAndMissingData() {
    Fixture f;
    Check(f.Read().state == CombatState::Ready, "Allowed map lost eligible placement");
    Get(f.map).captureAllowed = false;
    Check(f.Read().state == CombatState::Hidden, "Map forbids capture but placement popup remains");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "Map forbids capture but active popup remains");
    Get(f.map).captureAllowed = true;
    Check(f.Read().state == CombatState::Fighting, "Map capability was cached instead of re-read");
    maps.clear();
    Check(f.Read().state == CombatState::Hidden, "Missing map metadata retained capture popup");
    maps[Get(f.location).id] = Make("UnrelatedData");
    Check(f.Read().state == CombatState::Hidden, "Unrelated metadata authorized capture");
    maps[Get(f.location).id] = f.map;
    Set(f.character, 0x1A0, static_cast<Obj>(nullptr));
    Check(f.Read().state == CombatState::Hidden, "Missing current map retained capture popup");
    Set(f.character, 0x1A0, f.location);
    Check(f.Read().state == CombatState::Fighting, "Restored map and eligible roster were not accepted");
    Get(f.location).id = 0;
    Check(f.Read().state == CombatState::Hidden, "Invalid map ID displayed capture popup");
}
void ForbiddenInBothPhasesAndLateLoad() {
    Fixture f;
    Get(monsters.at(42)).forbidden = true;
    Check(f.Read().state == CombatState::Hidden, "Forbidden placement displayed unsupported popup");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "Starting fight bypassed monster capture prohibition");
    soul::ResetCombat();
    Check(f.Read().state == CombatState::Hidden, "Late active load bypassed capture prohibition");
    auto summon = f.Entity(1 | 8, 1, 44, 20); Get(summon).summon = true;
    f.Entity(1 | 8, 0, 45, 20);
    f.Entity(1 | 8 | 0x10, 1, 46, 20);
    Check(f.Read().state == CombatState::Hidden, "Excluded capturable entity authorized popup");
    f.Phase(1); Get(monsters.at(42)).forbidden = false;
    Check(f.Read().state == CombatState::Ready, "Capturable placement was not shown");
    Get(monsters.at(42)).forbidden = true;
    Check(f.Read().state == CombatState::Hidden, "Changed placement retained former eligible roster");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "Changed placement eligibility leaked into fight");
}
void EligibilityLifetime() {
    Fixture f;
    auto forbidden = f.Entity(1 | 8, 1, 43, 20);
    Get(monsters.at(43)).forbidden = true;
    Check(f.Read().state == CombatState::Ready, "Mixed roster lost existing capture eligibility");
    f.Phase(2);
    Get(f.entities).items = {f.local, forbidden};
    Check(f.Read().state == CombatState::Fighting, "Killing capturable monster hid same-fight status");
    const auto identity = f.Read().fightIdentity;
    ++Get(f.location).id; maps[Get(f.location).id] = f.map;
    Check(f.Read().state == CombatState::Hidden && f.Read().fightIdentity != identity,
        "Different map inherited eligible roster or click identity");
    Get(f.entities).items.push_back(f.enemy);
    Check(f.Read().state == CombatState::Fighting, "Fresh eligible active roster was not accepted");
    Get(f.entities).items = {f.local, forbidden};
    f.Phase(0); f.Phase(1);
    Check(f.Read().state == CombatState::Hidden, "Next forbidden placement inherited eligibility");
    f.Phase(2);
    Check(f.Read().state == CombatState::Hidden, "Next forbidden active fight inherited eligibility");
    Get(f.entities).items.push_back(f.enemy);
    Check(f.Read().state == CombatState::Fighting, "Fresh active roster could not qualify");
    Get(f.entities).items = {f.local, forbidden};
    f.cached = Make("CachedDataService");
    Check(f.Read().state == CombatState::Hidden, "Changed session inherited eligible roster");
}
void OrdinaryPiousAndCaptureDefinitions() {
    Fixture f;
    Get(f.entities).items = {f.local};
    // IDs/flags/souls verified in the installed 3.7.11.11 monster bundle.
    // These have SoulCaptureForbidden=false but no boss/arch flag or souls.
    for (int id : {236,489,490,491,492,493}) {
        f.Entity(1|8,1,id,15);auto data=monsters.at(id);
        Get(data).boss=false;Get(Get(data).souls).items.clear();
    }
    Check(f.Read().state==CombatState::Hidden,"Ordinary Pious must not display a capture popup even when captureForbidden is false");
    f.Phase(2);Check(f.Read().state==CombatState::Hidden,"Ordinary Pious became capturable at combat start");
    soul::ResetCombat();Check(f.Read().state==CombatState::Hidden,"Loading during a Piou fight must keep popup hidden");
    f.Phase(1);
    auto quest=monsters.at(236);Get(Get(quest).souls).items.push_back(Make("MonsterSoulData"));
    Check(f.Read().state==CombatState::Hidden,"Automatic ordinary quest souls must not enable stone/spell helper");
    auto arch=f.Entity(1|8,1,2341,15);Get(monsters.at(2341)).boss=false;Get(monsters.at(2341)).arch=true;
    Check(f.Read().state==CombatState::Ready,"Pioustone archimonster with ordinary Pious must retain helper");
    f.Phase(2);Get(f.entities).items={f.local};
    Check(f.Read().state==CombatState::Fighting,"Eligible archimonster death must retain status for the current fight");
    f.Phase(0);f.Phase(1);Get(f.entities).items={f.local,arch};Get(monsters.at(2341)).forbidden=true;
    Check(f.Read().state==CombatState::Hidden,"Explicit capture prohibition overrides archimonster category");
    Get(f.entities).items={f.local,f.enemy};auto boss=monsters.at(42);
    Get(Get(boss).souls).items.clear();Check(f.Read().state==CombatState::Hidden,"Boss without soul definition must be hidden");
    Get(boss).souls=nullptr;Check(f.Read().state==CombatState::Hidden,"Missing soul metadata must be hidden");
    Get(boss).souls=Make("List");Get(Get(boss).souls).items.assign(33,Make("MonsterSoulData"));
    Check(f.Read().state==CombatState::Hidden,"Unbounded soul list must not enable popup");
    Get(Get(boss).souls).items.resize(1);Check(f.Read().state==CombatState::Ready,"Defined eligible boss must remain visible");
}
}

int main() {
    try {
        fake::Initialize(); soul::InstallCombat();
        OrdinaryPiousAndCaptureDefinitions();
        PlacementAndMaximum(); ExclusionsAndUnknown(); ForbiddenAndSpecial(); ParticipantAndSession();
        PvpHiddenDuringEveryPhase(); PositivePvmClassification(); PvpIdentityAndUnknownKinds();
        MapPermissionAndMissingData(); ForbiddenInBothPhasesAndLateLoad(); EligibilityLifetime();
        std::cout << "Soul combat offline fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
