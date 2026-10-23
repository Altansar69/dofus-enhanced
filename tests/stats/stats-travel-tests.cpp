#include "appearance/features.hpp"
#include "core/module.hpp"
#include "ui/options-ui.hpp"
#include "core/signatures.hpp"
#include "stats/stats.hpp"
#include "stats/stats-travel.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// The real stats-travel.cpp (included below, so the tests drive its clock)
// against an in-memory IL2CPP fixture and a recording store: no game, process,
// file or network. Layouts and natives are the pinned 3.7.12.12 contracts
// (work/r74-analysis/contracts-new.json); maps, worlds, items and names are
// labels for these tests.
namespace fake {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
struct Class { std::string image, space, name; Class* parent = nullptr; std::map<std::string, size_t> layout; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};  // raw fields at their offsets
    Class* klass = nullptr;
    int64_t scalar = 0;        // a boxed value, an item's gid, a subarea's world
    bool flag = false;         // epf.xkj: the map service has finished loading its map
    std::string text;
    std::vector<Obj> items;    // a list
    Obj link = nullptr;        // map information -> subarea, item -> item data
};
struct MethodInfo { Class* klass; std::string name; int argc; };
using TeleportFn = bool(*)(Obj, Obj, Method);
using UseFn = void(*)(Obj, Obj, Obj, Method);
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x57A700000000ULL;
std::map<std::string, Obj> services;
std::map<int64_t, Obj> mapInfos;  // map id -> MapInformationData
std::set<int64_t> havenbagMaps;
Obj havenbagRoot = nullptr;
std::set<std::string> layouts;    // class.field offsets checked at installation
std::set<uintptr_t> guarded;      // natives verified at installation
std::map<uintptr_t, void*> hooks;
std::vector<std::string> logs;
std::string fail;                 // "Class.member", a method name or "FindService:name" that fails
uint64_t now = 1000;
bool combat = false, counting = true, teleportResult = true;
unsigned lookups = 0, effectReads = 0, teleportCalls = 0, useCalls = 0, nameCalls = 0, unknownCharacter = 0;
std::array<Obj, 3> teleportArgs{}, useArgs{};
// The recording store: sums, records (largest), tables, names.
std::map<std::pair<int64_t, int>, int64_t> counters, records;
std::map<std::tuple<int64_t, int, int>, int64_t> tables;
std::map<int64_t, std::string> names;
const Method caller = reinterpret_cast<Method>(0x5EED);

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Object& Get(Obj object) { Check(object != nullptr, "Null fake statistics object"); return *static_cast<Object*>(object); }
void Define(const std::string& image, const std::string& space, const std::string& name,
            std::map<std::string, size_t> layout = {}, const std::string& parent = {}) {
    classes[name] = std::make_unique<Class>(Class{image, space, name, parent.empty() ? nullptr : classes.at(parent).get(), std::move(layout)});
}
Obj Make(const std::string& type) {
    auto object = std::make_unique<Object>();
    object->klass = classes.at(type).get();
    const auto result = object.get();
    objects.push_back(std::move(object));
    return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).memory.size(), "Fake field outside storage");
    std::memcpy(Get(object).memory.data() + offset, &value, sizeof(value));
}
Obj Box(int64_t value) { auto box = Make("Box"); Get(box).scalar = value; return box; }
Obj Text(const std::string& value) { auto text = Make("String"); Get(text).text = value; return text; }
Obj List(std::vector<Obj> items) { auto list = Make("List"); Get(list).items = std::move(items); return list; }
void Initialize() {
    for (const auto name : {"Box", "String", "List", "Other"}) Define("", "", name);
    Define("Core.dll", "", "esg", {{"dpzj", 0x128}, {"<dpzm>k__BackingField", 0x130}, {"dqab", 0x1A0}});
    Define("Core.dll", "", "z", {{"dbrl", 0x10}, {"dbrm", 0x18}, {"dbrn", 0x20}});
    Define("Core.dll", "", "gb", {{"dcjh", 0x10}, {"dcjl", 0x30}});
    Define(DataCenter, "Core.DataCenter.Metadata.World", "WorldPoint", {{"m_mapId", 0x10}});
    Define("Core.dll", "", "tk", {}, "WorldPoint");
    Define("Core.dll", "", "epf", {{"<doqb>k__BackingField", 0x118}});
    Define("AleCore.dll", "AleCore.Data.UnityClient", "ClientMapData", {{"id", 0x18}});
    Define(DataCenter, "Core.DataCenter.Metadata.World", "MapInformationData");
    Define(DataCenter, "Core.DataCenter.Metadata.World", "SubAreaData", {{"id", 0x10}});
    Define(DataCenter, "Core.DataCenter", "DataCenterModule");
    Define(DataCenter, "Core.DataCenter.Metadata.House", "HavenbagThemesDataRoot");
    Define("Core.dll", "", "xq");
    Define("Core.dll", "", "xm", {{"dhjx", 0x20}, {"dhjy", 0x28}, {"dhjz", 0x2C}, {"dhkb", 0x38}});
    Define("Core.dll", "", "enw", {{"<dodg>k__BackingField", 0x10}});
    Define("Core.dll", "Core.PathFinding.WorldData", "MapPoint", {{"docz", 0x18}});
    Define("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated", {{"fhtc", 0x1C8}});
    Define("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity", {}, "Animated");
    Define("Core.dll", "", "dhc", {{"dipi", 0x10}, {"dipj", 0x18}, {"dipk", 0x1C}, {"dipl", 0x20}});
    Define("Core.dll", "", "fbq", {{"dspa", 0x28}, {"<dspb>k__BackingField", 0x30}});
    Define("Core.dll", "", "feh");
    Define("Core.dll", "", "mc");
    Define(DataCenter, "Core.DataCenter.Metadata.Item", "ItemData", {{"possibleEffects", 0x68}});
    Define(DataCenter, "Core.DataCenter.Metadata.Effect", "EffectInstanceData", {{"actionId", 0x1A}});
    Define(DataCenter, "Core.DataCenter.Metadata.Effect.Instance", "EffectInstanceDice", {}, "EffectInstanceData");
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_is_assignable_from = [](Klass expected, Klass actual) {
        for (auto type = static_cast<Class*>(actual); type; type = type->parent) if (type == expected) return true;
        return false;
    };
    api.object_unbox = [](Obj object) -> void* { return &Get(object).scalar; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { handles.emplace(++nextHandle, object); return nextHandle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Unknown statistics GC handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Statistics GC handle released twice"); };
    api.class_get_field_from_name = [](Klass klass, const char* name) -> void* {
        auto& type = *static_cast<Class*>(klass);
        const auto found = type.layout.find(name);
        if (type.name + "." + name == fail || found == type.layout.end()) return nullptr;
        layouts.insert(type.name + "." + name);
        return &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
}
bool TeleportOriginal(Obj service, Obj request, Method method) {
    ++teleportCalls; teleportArgs = {service, request, const_cast<void*>(method)}; return teleportResult;
}
void UseOriginal(Obj inventory, Obj action, Obj item, Method method) {
    ++useCalls; useArgs = {inventory, action, item}; Check(method == caller, "The item use's method changed");
}

// The game's state as the statistics read it: the character service (esg)
// with its character (z), sheet (gb) and map (tk), the map service (epf) with
// its ClientMapData, and the teleport service (fbq) with the character service
// it checks a request's cost with (dspa) and the saved zaap.
struct World {
    Obj character, info{}, sheet{}, map{}, scene, data{}, zaaps;
    explicit World(int64_t id = 1001, const std::string& name = "Testeur", int level = 50, int64_t experience = 1000, int64_t at = 1000) {
        character = Make("esg"); scene = Make("epf"); zaaps = Make("fbq"); Put(zaaps, 0x28, character);
        Become(id, name, level); Sheet(experience); Arrive(at);
        services = {{"fng", character}, {"fpm", scene}, {"fpf", zaaps}};
    }
    // A new z: a character selected, a reconnection, a look or name update.
    void Become(int64_t id, const std::string& name, int level) {
        info = Make("z"); Put(info, 0x10, id); Put(info, 0x18, Text(name)); Put(info, 0x20, level); Put(character, 0x128, info);
    }
    void Level(int level) { Put(info, 0x20, level); }  // esf.zbg writes it in place
    // Every characteristics message builds a new sheet (esg.zht); so does a kamas change (gb is immutable).
    void Sheet(int64_t experience, int64_t kamas = 0) {
        sheet = Make("gb"); Put(sheet, 0x10, experience); Put(sheet, 0x30, kamas); Put(character, 0x130, sheet);
    }
    // PreLoadMap: the map service holds the next map, still loading; the character still shows the last one.
    void Load(int64_t id) { data = Make("ClientMapData"); Put(data, 0x18, static_cast<int32_t>(id)); Put(scene, 0x118, data); Get(scene).flag = false; }
    // LoadMap done: ready, and esf.zat gives the character a new tk (a reload too).
    void Loaded(int64_t id) { Get(scene).flag = true; map = Make("tk"); Put(map, 0x10, id); Put(character, 0x1A0, map); }
    void Arrive(int64_t id) { Load(id); Loaded(id); }
    void Leave() { Put(scene, 0x118, Obj{}); Get(scene).flag = false; }  // the map service disabled
    void Save(int64_t id) { Put(zaaps, 0x30, id); }
};
// A map's world and subarea: world 0 no map information, -1 a subarea without a world map (get_worldmapId throws).
void Place(int64_t map, int64_t world, int32_t subarea = 0) {
    if (!world) { mapInfos.erase(map); return; }
    auto info = Make("MapInformationData"), area = Make("SubAreaData");
    Get(info).link = area; Get(area).scalar = world; Put(area, 0x10, subarea); mapInfos[map] = info;
}
Obj Entity(int64_t id) { auto entity = Make("AnimatedEntity"); Put(entity, 0x1C8, id); return entity; }
// A walk as xq.rjq creates it: target -1, progress 0, its route starting on `start`.
Obj Walk(Obj entity, int start) {
    auto state = Make("xm"), route = Make("enw"), point = Make("MapPoint");
    Put(point, 0x18, start); Put(route, 0x10, point);
    Put(state, 0x20, entity); Put(state, 0x38, route); Put(state, 0x2C, -1); Put(state, 0x28, 0.0f);
    return state;
}
// xq.rjr's entry from xq.Update (or the instant drain): the target reached, progress 1.
void Reach(Obj state, int cell, float progress = 1.0f) {
    Put(state, 0x2C, cell); Put(state, 0x28, progress); StatsWalkStep(state);
}
// A TeleportRequest: its map (an anomaly's: its subarea), teleporter, destination type and cost.
Obj Request(int64_t map, int source, int destination = 0, uint32_t cost = 0) {
    auto request = Make("dhc");
    Put(request, 0x10, map); Put(request, 0x18, source); Put(request, 0x1C, destination); Put(request, 0x20, cost);
    return request;
}
Obj Effect(uint16_t action) { auto effect = Make("EffectInstanceDice"); Put(effect, 0x1A, action); return effect; }
Obj Item(int gid, std::vector<Obj> effects) {
    auto item = Make("mc"), data = Make("ItemData");
    Get(item).scalar = gid; Get(item).link = data; Put(data, 0x68, List(std::move(effects)));
    return item;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char* image, const char* space, const char* name) const {
    const auto found = fake::classes.find(name);
    fake::Check(found != fake::classes.end() && found->second->image == image && found->second->space == space,
                "Statistics class requested from the wrong image or namespace");
    return found->second.get();
}
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    fake::Check(static_cast<fake::Class*>(declaringClass)->name == "xq" && std::string(name) == "xm",
                "The walk state must be xq's nested xm");
    return fake::classes.at("xm").get();
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    auto& type = *static_cast<fake::Class*>(klass);
    const auto key = type.name + "." + name;
    static const std::map<std::string, std::pair<int, uintptr_t>> expected{
        {"epf.xkj", {0, 0x18389C0}}, {"MapInformationData.GetMapInformationById", {1, 0x7C4200}},
        {"MapInformationData.get_subArea", {0, 0x7C44B0}}, {"SubAreaData.get_worldmapId", {0, 0x7D1040}},
        {"DataCenterModule.get_havenBagThemesDataRoot", {0, 0x7BDF80}}, {"HavenbagThemesDataRoot.IsMapIdInHavenbag", {1, 0x7E41B0}},
        {"fbq.bcla", {1, 0x1A4A5E0}}, {"mc.myn", {0, 0xF20D40}}, {"mc.myx", {0, 0x2AAC6B0}}, {"feh.bdsh", {2, 0x1B57D20}}};
    if (key == fake::fail) throw std::runtime_error("Verified method unavailable");
    const auto found = expected.find(key);
    fake::Check(found != expected.end() && found->second == std::pair(argc, rva), "Statistics native outside the pinned contract");
    // The real lookup ends in NativeAt, which refuses an RVA without a 16-byte guard.
    fake::Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& guard) { return guard.rva == rva; }),
                "Statistics native without a signature guard");
    fake::guarded.insert(rva);
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{&type, name, argc});
    const auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    using namespace fake;
    const auto& m = *static_cast<const MethodInfo*>(method);
    const std::vector<void*> args(values);
    Check(static_cast<int>(args.size()) == m.argc, "Wrong statistics method arity");
    if (m.name == fail) throw std::runtime_error("Injected native failure");
    if (m.name == "GetMapInformationById") {
        Check(self == nullptr, "GetMapInformationById is static");
        const auto found = mapInfos.find(*static_cast<int64_t*>(args.at(0)));
        return found == mapInfos.end() ? nullptr : found->second;
    }
    if (m.name == "get_havenBagThemesDataRoot") { Check(self == nullptr, "The data root getter is static"); return havenbagRoot; }
    auto& object = Get(self);
    Check(api.class_is_assignable_from(m.klass, object.klass), "Statistics method called on a foreign object");
    if (m.name == "xkj") return Box(object.flag);
    if (m.name == "get_subArea") return object.link;
    if (m.name == "get_worldmapId") {
        if (object.scalar < 0) throw std::runtime_error("NullReferenceException");
        return Box(object.scalar);
    }
    if (m.name == "IsMapIdInHavenbag") return Box(havenbagMaps.contains(*static_cast<int64_t*>(args.at(0))));
    if (m.name == "myn") return Box(object.scalar);
    if (m.name == "myx") { ++effectReads; return object.link; }
    throw std::runtime_error("Unexpected statistics method: " + m.name);
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> values) const {
    using namespace fake;
    const std::string call = name;
    const std::vector<void*> args(values);
    auto& object = Get(self);
    Check(object.klass->name == "List", "Managed call outside a list");
    if (call == "get_Count" && args.empty()) return Box(static_cast<int64_t>(object.items.size()));
    if (call == "get_Item" && args.size() == 1) return object.items.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected statistics call: " + call);
}
std::string Api::String(Obj self) const {
    if (!self) return {};
    fake::Check(fake::Get(self).klass->name == "String", "Text read from a non-string object");
    return fake::Get(self).text;
}
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return fake::combat; }
managed::Obj Module::FindService(const char* name) const {
    ++fake::lookups;
    if (fake::fail == std::string("FindService:") + name) throw std::runtime_error("Injected service failure");
    const auto found = fake::services.find(name);
    return found == fake::services.end() ? nullptr : found->second;
}
void Log(const std::string& line) { fake::logs.push_back(line); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    if (rva == 0x1A4A5E0) { fake::Check(m.name == "bcla", "The teleport hook is fbq.bcla"); *original = reinterpret_cast<void*>(&fake::TeleportOriginal); }
    else if (rva == 0x1B57D20) { fake::Check(m.name == "bdsh", "The item hook is feh.bdsh"); *original = reinterpret_cast<void*>(&fake::UseOriginal); }
    else throw std::runtime_error("Unexpected statistics hook");
    fake::hooks[rva] = replacement;
}
namespace stats {
void Add(int64_t character, Counter counter, int64_t amount) {
    if (!character) { ++fake::unknownCharacter; return; }
    if (fake::counting) fake::counters[{character, static_cast<int>(counter)}] += amount;
}
void Raise(int64_t character, Record record, int64_t value) {
    if (!character) { ++fake::unknownCharacter; return; }
    auto& best = fake::records[{character, static_cast<int>(record)}];
    if (fake::counting) best = std::max(best, value);
}
void AddTo(int64_t character, Table table, int key, int64_t amount) {
    if (!character) { ++fake::unknownCharacter; return; }
    if (fake::counting) fake::tables[{character, static_cast<int>(table), key}] += amount;
}
void Name(int64_t character, const std::string& name) { ++fake::nameCalls; fake::names[character] = name; }
bool Counting() { return fake::counting; }
}

#include "stats/stats-travel.cpp"

namespace {
using namespace fake;
using stats::Counter;
int64_t Total(int64_t character, Counter counter) {
    const auto found = counters.find({character, static_cast<int>(counter)});
    return found == counters.end() ? 0 : found->second;
}
int64_t Best(int64_t character) {
    const auto found = records.find({character, static_cast<int>(stats::Record::LongestSessionMs)});
    return found == records.end() ? 0 : found->second;
}
int64_t Arrivals(int64_t character, int world) {
    const auto found = tables.find({character, static_cast<int>(stats::Table::Dimensions), world});
    return found == tables.end() ? 0 : found->second;
}
// Module::Pump every 200 ms; nothing may escape towards it.
void Pump(int times = 1) {
    for (int i = 0; i < times; ++i) {
        now += 200;
        try { TickStatsTravel(); }
        catch (...) { throw std::runtime_error("An exception escaped TickStatsTravel towards Module::Pump"); }
    }
}
bool Logged(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
TeleportFn Teleport() { return reinterpret_cast<TeleportFn>(hooks.at(0x1A4A5E0)); }
UseFn Use() { return reinterpret_cast<UseFn>(hooks.at(0x1B57D20)); }
// A new scenario: nothing remembered by the statistics or the store.
void Fresh() {
    RestoreStatsTravel();
    counters.clear(); records.clear(); tables.clear(); names.clear(); logs.clear();
    fail.clear(); combat = false; counting = true; teleportResult = true; nameCalls = effectReads = 0;
    module->stopped = false; module->stopRequested = false; module->ready = true; module->servicesReady = true;
    mapInfos.clear(); havenbagMaps.clear(); havenbagRoot = Make("HavenbagThemesDataRoot");
    Place(1000, 1); Place(2000, 1); Place(3000, 14); Place(3100, 14);
}
void Install() {
    // Unused before the installation: the other features' one-line calls do nothing.
    StatsWalkStep(nullptr); StatsModTrip();
    Check(!stats_travel::walkStep && !stats_travel::modTrip, "Nothing is called before the contracts are verified");
    const auto attempt = [](const std::string& failing) { fail = failing; logs.clear(); hooks.clear(); InstallStatsTravel(); fail.clear(); };
    attempt("xm.dhjz");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=walk"} && !stats_travel::walkStep && stats_travel::modTrip &&
          hooks.size() == 2, "A moved walk field turns off the cells only");
    attempt("HavenbagThemesDataRoot.IsMapIdInHavenbag");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=places"} && stats_travel::walkStep,
          "An unverified Havre-Sac test turns off the worlds and Havre-Sac entries only");
    attempt("fbq.bcla");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=teleports"} && hooks.size() == 1 && hooks.contains(0x1B57D20),
          "An unverified teleport request leaves the recall potions");
    attempt("fbq.dspa");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=teleports"} && hooks.size() == 1 && hooks.contains(0x1B57D20),
          "Without the character service a request's cost is checked with, no teleport hook");
    attempt("EffectInstanceData.actionId");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=recall"} && hooks.size() == 1 && hooks.contains(0x1A4A5E0),
          "A moved effect field turns off the recall potions only, before their hook is created");
    attempt("z.dbrl");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=character"} && hooks.empty() &&
          !stats_travel::walkStep && !stats_travel::modTrip, "Without the character, nothing of this part is installed");
    World world; lookups = 0; Pump(3);
    Check(lookups == 0 && counters.empty(), "A disabled tracker reads nothing");
    attempt("ClientMapData.id");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_DISABLED stage=map"} && hooks.empty(), "Without the map service, nothing either");
    layouts.clear(); guarded.clear();
    attempt("");
    Check(logs.empty(), "The pinned client contract is accepted");
    Check(layouts == std::set<std::string>{"esg.dpzj", "esg.<dpzm>k__BackingField", "esg.dqab", "z.dbrl", "z.dbrm", "z.dbrn",
        "gb.dcjh", "WorldPoint.m_mapId", "epf.<doqb>k__BackingField", "ClientMapData.id", "xm.dhjx", "xm.dhjy", "xm.dhjz",
        "xm.dhkb", "enw.<dodg>k__BackingField", "MapPoint.docz", "Animated.fhtc", "dhc.dipi", "dhc.dipj", "dhc.dipk", "dhc.dipl",
        "gb.dcjl", "fbq.dspa", "SubAreaData.id", "fbq.<dspb>k__BackingField", "ItemData.possibleEffects", "EffectInstanceData.actionId"},
        "Every field read has its pinned offset checked by name");
    Check(guarded == std::set<uintptr_t>{0x18389C0, 0x7C4200, 0x7C44B0, 0x7D1040, 0x7BDF80, 0x7E41B0, 0x1A4A5E0, 0xF20D40,
        0x2AAC6B0, 0x1B57D20}, "Exactly the verified natives, each behind a signature guard");
    Check(hooks.size() == 2 && stats_travel::walkStep && stats_travel::modTrip, "Both hooks and both calls are installed");
}
void CountingOff() {
    Fresh(); World world; counting = false; lookups = 0;
    Pump(5); StatsModTrip(); Reach(Walk(Entity(1001), 10), 11);
    Teleport()(world.zaaps, Request(2000, 0), caller);
    Check(lookups == 0 && counters.empty() && records.empty() && !nameCalls, "Counting off: nothing is read nor counted");
    Check(teleportCalls > 0, "Counting off: the game's teleport still runs");
    counting = true; world.Sheet(5000); world.Arrive(2000); Pump(2);
    Check(Total(1001, Counter::Xp) == 0 && Total(1001, Counter::Maps) == 0 && Total(1001, Counter::TeleportZaap) == 0 &&
          Total(1001, Counter::PlayMs) == 200, "Counting on again: baselines, nothing gained while it was off");
    module->stopped = true; lookups = 0; Pump(2);
    Check(lookups == 0 && Total(1001, Counter::PlayMs) == 200, "A stopped module reads nothing");
}
void PlayTime() {
    Fresh(); World world;
    Pump();
    Check(Total(1001, Counter::PlayMs) == 0 && names[1001] == "Testeur" && nameCalls == 1, "A session starts from nothing, with the character's name");
    Check(logs == std::vector<std::string>{"STATS_TRAVEL_FIRST sheet=1 map=1 loaded=1", "STATS_TRAVEL_SESSION reason=character"},
          "Its start is logged with fixed codes, with what the game showed at the first look");
    Pump(5);
    Check(Total(1001, Counter::PlayMs) == 1000 && Best(1001) == 1000, "The time between two ticks in the world");
    world.Leave(); Pump(2);
    Check(Total(1001, Counter::PlayMs) == 1000, "Out of the world (loading, character selection): no time");
    world.Arrive(1000); Pump();
    Check(Total(1001, Counter::PlayMs) == 1000, "The time away is never counted");
    Pump(2);
    Check(Total(1001, Counter::PlayMs) == 1400 && Best(1001) == 1400, "Back within the minute: the same session goes on");
    now += 29800; Pump();
    Check(Total(1001, Counter::PlayMs) == 11400 && Best(1001) == 11400,
          "A long interval between two ticks in the world counts ten seconds at most (frozen game, unseen reconnection)");
    now += 61000; Pump();
    Check(Total(1001, Counter::PlayMs) == 11400 && Logged("STATS_TRAVEL_SESSION reason=return"),
          "More than a minute between two ticks: not counted, another session");
    Pump(3);
    Check(Total(1001, Counter::PlayMs) == 12000 && Best(1001) == 11400 && nameCalls == 1,
          "Then another session, its own length; the name is given once");
    world.Become(2002, "Autre", 10); world.Sheet(5); world.Arrive(3000); Pump(3);
    Check(Total(2002, Counter::PlayMs) == 400 && Best(2002) == 400 && Total(1001, Counter::PlayMs) == 12000 && names[2002] == "Autre",
          "Another character: its own time, record and name");
    world.Become(2002, "Renommée", 10); now += 61000; Pump(2);
    Check(names[2002] == "Renommée" && nameCalls == 3, "A new name is given at the next session");
    world.Become(2002, std::string("Bad\x01Name"), 10); now += 61000; Pump();
    Check(names[2002] == "Renommée" && nameCalls == 3, "An unprintable name is never given");
}
void Maps() {
    Fresh(); World world;
    Pump(); world.Arrive(2000); Pump();
    Check(Total(1001, Counter::Maps) == 0,
          "The map shown at the first look may be a previous character's: the next one loaded is the session's baseline");
    world.Arrive(1000); Pump();
    Check(Total(1001, Counter::Maps) == 1, "A map change");
    world.Arrive(1000); Pump(2);
    Check(Total(1001, Counter::Maps) == 1, "A reload of the same map (fight end, refresh) is no move");
    world.Load(2000); Pump(2);
    Check(Total(1001, Counter::Maps) == 1, "A map still loading is not reached yet");
    world.Loaded(2000); Pump(2);
    Check(Total(1001, Counter::Maps) == 2, "Reached once loaded, counted once");
    // The map service's own test (epf.xkj): its map and its loaded state.
    world.Loaded(1000); Get(world.scene).flag = false; Put(world.data, 0x18, int32_t{1000}); Pump(2);
    Check(Total(1001, Counter::Maps) == 2, "Not before the map service reports its map loaded");
    Get(world.scene).flag = true; Pump();
    Check(Total(1001, Counter::Maps) == 3, "Then reached");
    world.Arrive(2000); Pump();
    Check(Total(1001, Counter::Maps) == 4, "Each change once");
    // esg.zhh's placeholder: a map the map service does not hold.
    world.map = Make("tk"); Put(world.map, 0x10, int64_t{241437185}); Put(world.character, 0x1A0, world.map); Pump(2);
    Check(Total(1001, Counter::Maps) == 4, "The character service's placeholder map is never a move");
    world.Loaded(2000); Pump();
    world.Leave(); now += 61000; Pump(); world.Arrive(3000); Pump(2);
    Check(Total(1001, Counter::Maps) == 4, "A new session starts from its own first map");
    world.Arrive(1000); Pump();
    Check(Total(1001, Counter::Maps) == 5, "Then its changes count");
    // A reconnection: out of the world a few seconds, back on the same map.
    world.Leave(); Pump(10); world.Become(1001, "Testeur", 50); world.Arrive(1000); Pump(2);
    Check(Total(1001, Counter::Maps) == 5, "A reconnection on the same map is no move");
}
void Places() {
    Fresh(); World world; havenbagMaps = {9000, 9001, 9002}; Place(9000, 1); Place(9001, 1); Place(9002, 1);
    Pump(); world.Arrive(1000); Pump();
    world.Arrive(3000); Pump();
    Check(Total(1001, Counter::Dimensions) == 1 && Arrivals(1001, 14) == 1, "Monde des Douze to Srambad: one dimension change");
    world.Arrive(3100); Pump();
    Check(Total(1001, Counter::Dimensions) == 1, "Within one world: none");
    world.Arrive(9000); Pump();
    Check(Total(1001, Counter::HavenBag) == 1 && Total(1001, Counter::Dimensions) == 1, "Entering the Havre-Sac: an entry, never a dimension");
    world.Arrive(9001); Pump();
    Check(Total(1001, Counter::HavenBag) == 1, "Another room of the Havre-Sac: no new entry");
    world.Arrive(3000); Pump();
    Check(Total(1001, Counter::Dimensions) == 1 && Total(1001, Counter::Maps) == 5, "Back in Srambad: no dimension change");
    world.Arrive(9000); Pump(); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::HavenBag) == 2 && Total(1001, Counter::Dimensions) == 2 && Arrivals(1001, 1) == 1,
          "Through the Havre-Sac to the Monde des Douze: one more entry, one more dimension change");
    Place(4000, -1); world.Arrive(4000); Pump();
    Check(Total(1001, Counter::Dimensions) == 2 && Total(1001, Counter::Maps) == 8, "A subarea without a world: a map, no dimension");
    world.Arrive(3000); Pump();
    Check(Total(1001, Counter::Dimensions) == 3 && Arrivals(1001, 14) == 2, "The next known world compares with the last known one");
    // The Havre-Sac test unavailable: neither an entry nor a dimension from that map.
    havenbagRoot = nullptr; world.Arrive(9002); Pump(); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::HavenBag) == 2 && Total(1001, Counter::Dimensions) == 4, "An unknown Havre-Sac status counts no entry");
    havenbagRoot = Make("HavenbagThemesDataRoot");
    // A session starting inside the Havre-Sac: no entry, and its world is unknown until it leaves.
    world.Arrive(9001); Pump(); world.Leave(); now += 61000; Pump(); world.Arrive(9000); Pump(2);
    world.Arrive(3000); Pump();
    Check(Total(1001, Counter::HavenBag) == 3 && Total(1001, Counter::Dimensions) == 4, "A session's first world is a baseline");
    world.Arrive(9000); Pump();
    Check(Total(1001, Counter::HavenBag) == 4, "Entries count again within the session");
}
void Experience() {
    Fresh(); World world(1001, "Testeur", 50, 1000);
    Pump(); world.Sheet(1500); Pump();
    Check(Total(1001, Counter::Xp) == 0,
          "The sheet shown at the first look may be a previous character's: the next one the game sends is the baseline");
    world.Level(51); world.Sheet(2500); Pump();
    Check(Total(1001, Counter::Xp) == 1000 && Total(1001, Counter::Levels) == 1, "Gains and a level");
    world.Level(53); world.Sheet(9000); Pump();
    Check(Total(1001, Counter::Levels) == 3 && Total(1001, Counter::Xp) == 7500, "Several levels at once count all");
    world.Sheet(100); Pump(); world.Sheet(300); Pump();
    Check(Total(1001, Counter::Xp) == 7700, "A decrease is a new baseline");
    // A reconnection: new character object and sheet with the same values.
    world.Leave(); Pump(); world.Become(1001, "Testeur", 53); world.Sheet(300); world.Arrive(1000); Pump(2);
    Check(Total(1001, Counter::Xp) == 7700 && Total(1001, Counter::Levels) == 3, "A reconnection gains nothing");
    // A server transfer: the fight's experience lands while out of the world a few seconds.
    world.Leave(); Pump(); world.Sheet(700); now += 5000; world.Arrive(1000); Pump();
    Check(Total(1001, Counter::Xp) == 8100, "Experience applied during a short absence still counts");
    // Back after more than a minute: the sheet left is not trusted until the game sends one again.
    world.Leave(); Pump(); now += 61000; world.Arrive(1000); Pump(2);
    world.Sheet(50000); Pump();
    Check(Total(1001, Counter::Xp) == 8100, "Possibly gained elsewhere: the new sheet is the baseline");
    world.Sheet(50100); Pump();
    Check(Total(1001, Counter::Xp) == 8200, "Then gains count again");
}
void Characters() {
    Fresh(); World world(1001, "Testeur", 50, 1000, 1000);
    Pump(); world.Sheet(1000); Pump(); world.Sheet(1500); world.Arrive(2000); Pump();
    // The character selection: the map service disabled, then another character
    // whose sheet and map come later.
    world.Leave(); Pump(); world.Become(2002, "Autre", 120); Pump(2);
    world.Arrive(3000); Pump(2);
    Check(Total(2002, Counter::PlayMs) == 200 && !Total(2002, Counter::Xp), "In the world, its experience still unknown");
    Check(Logged("STATS_TRAVEL_CHARACTER sheet=previous map=previous loaded=0"), "The change is logged with what the game still showed");
    world.Sheet(80000); Pump(); world.Sheet(80100); Pump();
    Check(Total(2002, Counter::Xp) == 100 && Total(1001, Counter::Xp) == 500 && !Total(2002, Counter::Maps),
          "The previous character's sheet is never this one's baseline");
    // The map service not disabled (unverified in game): the map shown is the previous character's until replaced.
    world.Become(3003, "Troisième", 1); Pump(3);
    Check(!Total(3003, Counter::PlayMs) && !names.contains(3003), "Not in the world until the game shows its own map");
    Check(Logged("STATS_TRAVEL_CHARACTER sheet=previous map=previous loaded=1"), "A map service still loaded at the change is logged");
    world.Sheet(10); world.Arrive(1000); Pump(); world.Arrive(2000); Pump();
    Check(Total(3003, Counter::Maps) == 1 && Total(3003, Counter::PlayMs) == 200 && !Total(2002, Counter::Maps),
          "Its maps start from its own first map");
    // Sheet and map replaced in the same interval as the character: theirs at once.
    world.Leave(); Pump(); world.Become(4004, "Quatre", 7); world.Sheet(42); world.Arrive(3000); Pump(); world.Sheet(50); Pump();
    Check(Total(4004, Counter::Xp) == 8 && Total(3003, Counter::Xp) == 0 && Logged("STATS_TRAVEL_CHARACTER sheet=new map=new loaded=1"),
          "Replaced since the last tick: the new character's");
    Check(!unknownCharacter, "Nothing is ever counted for an unknown character");
}
void Walking() {
    Fresh(); World world; const auto me = Entity(1001), other = Entity(4004);
    auto early = Walk(me, 1); StatsWalkStep(early); Reach(early, 2);
    Pump();
    Check(!Total(1001, Counter::CellsRoleplay), "Before the character is known, no step");
    auto walk = Walk(me, 100);
    StatsWalkStep(walk);           // xq.rjn's first rjr: the start cell
    Reach(walk, 100);              // its first segment: the turn on the start cell
    Reach(walk, 101); Reach(walk, 102); Reach(walk, 103);
    Reach(walk, 103);              // xq.rju resuming on the reached cell
    Reach(walk, 104, 0.5f);        // not reached yet
    Pump();
    Check(Total(1001, Counter::CellsRoleplay) == 3 && !Total(1001, Counter::CellsFight), "Each cell reached once, never the start");
    auto theirs = Walk(other, 200); StatsWalkStep(theirs); Reach(theirs, 201); Reach(theirs, 202);
    StatsWalkStep(Make("Other")); StatsWalkStep(nullptr);
    Pump();
    Check(Total(1001, Counter::CellsRoleplay) == 3, "Another entity's walk is not the character's");
    combat = true;
    auto fight = Walk(me, 300); StatsWalkStep(fight); Reach(fight, 300); Reach(fight, 301); Reach(fight, 302);
    Pump(); combat = false;
    Check(Total(1001, Counter::CellsFight) == 2 && Total(1001, Counter::CellsRoleplay) == 3, "In a fight: fight cells");
    // An interrupted walk: a new state, its own start.
    auto first = Walk(me, 400); StatsWalkStep(first); Reach(first, 401);
    auto second = Walk(me, 401); StatsWalkStep(second); Reach(second, 401); Reach(second, 402);
    Pump();
    Check(Total(1001, Counter::CellsRoleplay) == 5, "A walk replaced on its way: only the cells reached");
    world.Leave(); Pump();
    auto away = Walk(me, 500); StatsWalkStep(away); Reach(away, 501); world.Arrive(1000); Pump(2);
    Check(Total(1001, Counter::CellsRoleplay) == 5, "Out of the world: no step");
    auto last = Walk(me, 600); StatsWalkStep(last); Reach(last, 601); Reach(last, 602);
    RestoreStatsTravel();
    Check(Total(1001, Counter::CellsRoleplay) == 7, "Module::Stop gives the last steps to the store before it is written");
    Reach(last, 603); Pump();
    Check(Total(1001, Counter::CellsRoleplay) == 7, "Nothing after the restoration until a new session");
}
void Teleports() {
    Fresh(); World world; Pump(); world.Arrive(1000); Pump();
    const auto request = Request(2000, 0);
    teleportCalls = 0;
    Check(Teleport()(world.zaaps, request, caller) && teleportCalls == 1 && teleportArgs[0] == world.zaaps && teleportArgs[1] == request &&
          teleportArgs[2] == caller, "The game's request runs once, with its arguments, and its result is returned");
    teleportResult = false;
    Check(!Teleport()(world.zaaps, request, caller), "A false result is returned unchanged");
    teleportResult = true;
    Pump();
    Check(!Total(1001, Counter::TeleportZaap), "A request alone counts nothing");
    world.Arrive(2000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 1 && Total(1001, Counter::Maps) == 1, "A zaap counts at its arrival");
    const std::vector<std::tuple<int, int64_t, Counter>> kinds{{1, 3000, Counter::TeleportZaapi}, {2, 1000, Counter::TeleportPrism},
        {3, 2000, Counter::TeleportZaap}, {5, 3000, Counter::TeleportZaap}, {9, 1000, Counter::TeleportOther}};
    for (const auto& [source, map, counter] : kinds) {
        const auto before = Total(1001, counter);
        Teleport()(world.zaaps, Request(map, source), caller); Pump(); world.Arrive(map); Pump();
        Check(Total(1001, counter) == before + 1, "Each kind of teleporter to its counter");
    }
    Check(Total(1001, Counter::TeleportZaap) == 3 && Total(1001, Counter::TeleportZaapi) == 1 && Total(1001, Counter::TeleportPrism) == 1 &&
          Total(1001, Counter::TeleportOther) == 1, "Zaaps (0, 3, 5), zaapis, prisms, others");
    Teleport()(world.zaaps, Request(1000, 0), caller); world.Arrive(3100); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 3, "Another map than the requested one is not the teleport");
    world.Arrive(1000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 4, "The request still waits for its own map");
    // An anomaly (destination type 4): BaseZaapSelectionUI.ValidateZaapChoice
    // puts its subarea in the request, not a map; it lands on one of its maps.
    Place(5000, 1, 77); Place(5100, 1, 78);
    Teleport()(world.zaaps, Request(77, 0, 4), caller); world.Arrive(5100); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 4, "An anomaly is not reached on a map of another subarea");
    world.Arrive(5000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 5 && Total(1001, Counter::TeleportOther) == 1,
          "It is reached on a map of its subarea, with the teleporter it was taken from");
    Teleport()(world.zaaps, Request(77, 4), caller); world.Arrive(5100); Pump(); world.Arrive(5000); Pump();
    Check(Total(1001, Counter::TeleportOther) == 1, "Another teleporter (source 4) to a map waits for that very map");
    Teleport()(world.zaaps, Request(2000, 4), caller); world.Arrive(2000); Pump();
    Check(Total(1001, Counter::TeleportOther) == 2, "There it counts as another teleport");
    Place(6000, 0); Teleport()(world.zaaps, Request(77, 0, 4), caller); world.Arrive(6000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 6, "An arrival whose subarea is unknown: the next map, as before");
    // fbq.bcla's own refusal: a cost above the kamas of its character service's
    // sheet shows « not_enough_rich », sends nothing, and still returns true.
    world.Sheet(1000, 499); teleportCalls = 0;
    Check(Teleport()(world.zaaps, Request(77, 0, 4, 500), caller) && teleportCalls == 1 && teleportArgs[0] == world.zaaps,
          "The game's own refusal still runs, its result unchanged");
    world.Arrive(5000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 6, "A request the game refuses for its kamas is none, whatever map comes next");
    Teleport()(world.zaaps, Request(3000, 0, 0, 499), caller); Teleport()(world.zaaps, Request(1000, 1, 0, 500), caller);
    world.Arrive(3000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 7 && Total(1001, Counter::TeleportZaapi) == 1,
          "A cost equal to the kamas is sent; a refused request leaves the earlier one waiting");
    world.Sheet(1000, 100000);
    Teleport()(world.zaaps, Request(1000, 0, 0, 0x80000000u), caller); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 7, "The cost is unsigned, compared as bcla does");
    Teleport()(world.zaaps, Request(3000, 0), caller); Pump(); now += 31000; world.Arrive(3000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 7, "Refused or never sent: no arrival within half a minute");
    Teleport()(world.zaaps, Make("Other"), caller); Teleport()(world.zaaps, nullptr, caller); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 7 && Total(1001, Counter::TeleportOther) == 2, "Only the game's TeleportRequest counts");
    Teleport()(world.zaaps, Request(2000, 0), caller); world.Leave(); now += 61000; Pump(); world.Arrive(2000); Pump(2);
    Check(Total(1001, Counter::TeleportZaap) == 7, "A new session forgets an earlier request");
}
void Recall() {
    Fresh(); World world; Pump(); world.Arrive(1000); Pump(); world.Save(2000);
    const auto potion = Item(548, {Effect(100), Effect(600)}), bread = Item(311, {Effect(110)}), action = Make("Other");
    useCalls = 0;
    Use()(world.zaaps, action, potion, caller);
    Check(useCalls == 1 && useArgs[0] == world.zaaps && useArgs[1] == action && useArgs[2] == potion, "The game's item use runs once, unchanged");
    Pump(); world.Arrive(2000); Pump();
    Check(Total(1001, Counter::RecallPotions) == 1 && !Total(1001, Counter::TeleportOther), "A recall potion counts at its arrival on the saved zaap");
    Use()(world.zaaps, action, bread, caller); Pump(); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::RecallPotions) == 1, "Any other item counts nothing");
    Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(3000); Pump(); now += 16000; world.Arrive(2000); Pump();
    Check(Total(1001, Counter::RecallPotions) == 1, "Another map than the saved zaap, then too late: not a recall");
    world.Save(0); Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::RecallPotions) == 2, "The saved zaap unknown: the next map change");
    Check(effectReads == 2, "Each item's data are read once");
    world.Save(2000);
    Teleport()(world.zaaps, Request(3000, 0), caller); Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(3000); Pump();
    world.Arrive(2000); Pump();
    Check(Total(1001, Counter::TeleportZaap) == 1 && Total(1001, Counter::RecallPotions) == 2, "A zaap and a potion never confirm each other");
    Use()(world.zaaps, action, Make("Other"), caller); Use()(world.zaaps, action, nullptr, caller); Pump();
    Check(useCalls == 7, "Every use reaches the game, whatever the item");
    // Another character: fbq keeps the previous one's saved zaap until the game
    // sends this one's (bclc, bcld), so the one shown is unknown.
    world.Leave(); Pump(); world.Become(2002, "Autre", 10); world.Sheet(5); world.Arrive(1000); Pump();
    Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(3100); Pump();
    Check(Total(2002, Counter::RecallPotions) == 1, "Its potion lands on its own saved zaap, still unknown to this client");
    world.Save(3000); Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(1000); Pump(); now += 16000; Pump();
    Check(Total(2002, Counter::RecallPotions) == 1, "Once the game sends its own, another map is no recall");
    Use()(world.zaaps, action, potion, caller); Pump(); world.Arrive(3000); Pump();
    Check(Total(2002, Counter::RecallPotions) == 2 && Total(1001, Counter::RecallPotions) == 2, "Its own saved zaap: its recall");
}
// The tracker's first look (the mod loaded at the character selection after
// another character, or counting turned on): the game may still show that
// character's sheet, map and saved zaap, its map service still loaded
// (unverified in game).
void FirstLook() {
    Fresh(); World world(1001, "Avant", 200, 1000, 1000); world.Save(2000);
    world.Become(2002, "Après", 100);  // selected; its own sheet and map not sent yet
    Pump();
    Check(Logged("STATS_TRAVEL_FIRST sheet=1 map=1 loaded=1") && names[2002] == "Après" && !names.contains(1001),
          "The first look is logged with what the game showed; the session is the selected character's");
    world.Sheet(900000000); Pump(); world.Sheet(900000100); Pump();
    Check(Total(2002, Counter::Xp) == 100, "The sheet shown at the first look is never a baseline");
    world.Arrive(3000); Pump();
    Check(!Total(2002, Counter::Maps) && !Total(2002, Counter::Dimensions) && !Arrivals(2002, 14),
          "Nor is the map shown then its first map: the first one loaded is");
    Use()(world.zaaps, Make("Other"), Item(548, {Effect(600)}), caller); Pump(); world.Arrive(3100); Pump();
    Check(Total(2002, Counter::RecallPotions) == 1, "Nor is the saved zaap shown then its own");
    world.Arrive(1000); Pump();
    Check(Total(2002, Counter::Maps) == 2 && Total(2002, Counter::Dimensions) == 1 && Arrivals(2002, 1) == 1, "Then its moves count");
    Check(!Total(1001, Counter::PlayMs) && !unknownCharacter, "Nothing for the character played before");
    // Ticks without the character service, then the selected character: what
    // the game shows was not seen appearing either; a tick missing the
    // service in between forgets nothing.
    Fresh(); World again(1001, "Avant", 200, 1000, 1000);
    services.erase("fng"); Pump(2); services["fng"] = again.character;
    again.Become(2002, "Après", 100); Pump();
    services.erase("fng"); Pump(); services["fng"] = again.character; Pump();
    again.Sheet(5000); Pump(); again.Sheet(5100); Pump();
    Check(Total(2002, Counter::Xp) == 100 && Logged("STATS_TRAVEL_FIRST sheet=1 map=1 loaded=1"),
          "A look without the character service neither starts nor ends the first look's doubt");
    // Its first look without a sheet yet: the first one sent is the baseline.
    Fresh(); World third(3003, "Trois", 10, 0, 1000); Put(third.character, 0x130, Obj{});
    Pump(); third.Sheet(400); Pump(); third.Sheet(450); Pump();
    Check(Total(3003, Counter::Xp) == 50 && Logged("STATS_TRAVEL_FIRST sheet=0 map=1 loaded=1"), "A sheet sent after the first look is its own");
}
void ModTrips() {
    Fresh(); StatsModTrip();
    World world; StatsModTrip(); Check(counters.empty(), "No trip before the character is known");
    Pump(); StatsModTrip(); StatsModTrip();
    Check(Total(1001, Counter::ModTrips) == 2, "Each journey of the mod, for the character");
    counting = false; Pump(); StatsModTrip(); counting = true;
    Check(Total(1001, Counter::ModTrips) == 2, "Counting off: no trip");
}
void Failures() {
    Fresh(); World world; Pump(2);
    fail = "FindService:fng"; Pump(3);
    Check(Logged("STATS_TRAVEL_ERROR stage=tick") && std::count(logs.begin(), logs.end(), "STATS_TRAVEL_ERROR stage=tick") == 1,
          "A failing read is contained and logged once");
    fail.clear(); Pump(2);
    Check(Total(1001, Counter::PlayMs) == 400, "No time across a failed read, then it goes on");
    world.Arrive(1000); Pump();
    fail = "IsMapIdInHavenbag"; world.Arrive(2000); Pump();
    Check(Logged("STATS_TRAVEL_ERROR stage=havenbag") && Total(1001, Counter::Maps) == 1 && !Total(1001, Counter::Dimensions),
          "An unknown Havre-Sac status: the map counts, nothing else");
    fail = "get_worldmapId"; world.Arrive(3000); Pump(); fail.clear(); world.Arrive(1000); Pump();
    Check(Total(1001, Counter::Maps) == 3 && !Total(1001, Counter::Dimensions), "A failing world read is skipped");
    fail = "myn"; Use()(world.zaaps, Make("Other"), Item(548, {Effect(600)}), caller); Pump();
    Check(Logged("STATS_TRAVEL_ERROR stage=item"), "A failing item read is contained");
    fail.clear();
}
}

int main() {
    using namespace fake;
    try {
        Initialize();
        Module owner{"offline-stats-travel"};
        module = &owner;
        owner.ready = true; owner.servicesReady = true;
        stats_travel::clockMs = [] { return fake::now; };
        Install();
        CountingOff();
        PlayTime();
        Maps();
        Places();
        Experience();
        Characters();
        FirstLook();
        Walking();
        Teleports();
        Recall();
        ModTrips();
        Failures();
        RestoreStatsTravel();
        module = nullptr;
        Check(handles.empty(), "The travel statistics leaked GC references");
        std::cout << "PASS: stats travel: contracts by name and guard, counting off reads nothing, time played between in-world ticks "
                     "(sessions, minute gap, record, names), maps from the map service (baseline, reloads, loading, placeholder), "
                     "dimensions and Havre-Sac entries, experience and levels (baselines, stale sheet after a character change or a long "
                     "absence), the first look (sheet, map and saved zaap not yet the character's), cells walked (start, turn, other "
                     "entities, fights), teleports (kamas refusal, anomaly subarea) and recall potions confirmed by their arrival, "
                     "the mod's trips, contained failures\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
