#include "stats/stats-fights.hpp"
#include "stats/stats.hpp"
#include "appearance/features.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

// Real stats-fights.cpp linked against a managed fixture laid out as IL2CPP
// lays its objects (the class first, then the fields at their pinned 3.7.12.12
// offsets): no game, process, file or network. The store is a fake that
// records what the tracker would count; the hook is called as the game would.
namespace fake {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* Protocol = "Ankama.Dofus.Protocol.Game.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
struct Class {
    std::string image, space, name;
    Class* parent = nullptr;
    Class* declaring = nullptr;
    std::map<std::string, size_t> layout;
};
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Raw {
    Class* klass = nullptr;
    void* monitor = nullptr;
    alignas(8) unsigned char fields[0x1C0]{};
};
static_assert(offsetof(Raw, fields) == 0x10, "IL2CPP fields start after the object header");
std::vector<std::unique_ptr<Class>> types;
std::map<std::string, Class*> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Raw>> objects;
std::map<Obj, std::vector<Obj>> lists;
std::map<Obj, std::pair<uint32_t, int>> itemInfo;   // an item's type and quantity, as mc.mzb and mc.myo return them
std::map<int, Obj> dungeonMaps;                     // GetDungeonByMapId
Obj dungeonRoot = nullptr;
std::map<GCHandle, Obj> handles;
std::map<std::string, Obj> services;
std::set<std::string> layouts;   // class.field offsets checked at installation
std::set<uintptr_t> guarded;     // natives verified at installation
std::vector<std::string> logs, earlier;
std::string fail;                // "Class.field", method, "nested.Name" or "FindService:name[:int]" that fails
GCHandle nextHandle = 0x57A700000000ULL;
unsigned lookups = 0, verifications = 0, hooks = 0, rootCalls = 0;
bool failHook = false;
// The store.
std::map<std::pair<int64_t, int>, int64_t> counters, records;
std::map<std::tuple<int64_t, int, int>, int64_t> tables;
bool counting = true, storeThrows = false, named = false;
// The hooked listener and the game's own handler behind it.
using ActionFn = void(*)(Obj, Obj, Method);
ActionFn actionHook = nullptr;
Method listener = nullptr;
Obj handler = nullptr;
std::vector<std::tuple<Obj, Obj, Method>> handled;
void NativeAction(Obj self, Obj message, Method method) { handled.emplace_back(self, message, method); }

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Raw& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Raw*>(object); }
Class* Define(const std::string& image, const std::string& space, const std::string& name,
              std::map<std::string, size_t> layout = {}, const std::string& parent = {}, const std::string& declaring = {}) {
    types.push_back(std::make_unique<Class>(Class{image, space, name, parent.empty() ? nullptr : classes.at(parent),
        declaring.empty() ? nullptr : classes.at(declaring), std::move(layout)}));
    return classes[name] = types.back().get();
}
Obj Make(const std::string& type) {
    auto object = std::make_unique<Raw>();
    object->klass = classes.at(type);
    const Obj result = object.get();
    objects.push_back(std::move(object));
    return result;
}
template<class T> void Set(Obj object, size_t offset, T value) {
    Check(object && offset >= 0x10 && offset + sizeof(T) <= sizeof(Raw), "Field outside the fake object");
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(T));
}
template<class T> Obj Box(T value) { auto box = Make("Box"); Set<T>(box, 0x10, value); return box; }
Obj List(std::vector<Obj> items) { auto list = Make("List"); lists[list] = std::move(items); return list; }
void Initialize() {
    for (const auto name : {"Box", "List", "Compositor", "Other"}) Define("", "", name);
    // Pinned 3.7.12.12 contracts (work/r74-analysis/contracts-new.json).
    Define("Core.dll", "", "esg", {{"dpzj", 0x128}, {"dqab", 0x1A0}});
    Define("Core.dll", "", "z", {{"dbrl", 0x10}, {"dbrm", 0x18}, {"dbrn", 0x20}});
    Define("Core.dll", "", "exf", {{"drih", 0x18}});
    Define("Core.dll", "", "fxc", {{"dwan", 0xB0}, {"<dwaq>k__BackingField", 0xC8}});
    Define("Core.dll", "", "fzf", {{"dwqq", 0x18}});
    Define("Core.dll", "", "fyz", {{"<dwon>k__BackingField", 0x10}, {"dwoo", 0x14}, {"dwop", 0x18}, {"dwoq", 0x1C},
        {"dwou", 0x30}, {"dwpa", 0x60}, {"dwpb", 0x68}});
    Define("Core.dll", "", "fze", {{"dwpw", 0x10}, {"dwpy", 0x20}, {"dwqa", 0x23}, {"dwqb", 0x28}, {"dwqd", 0x34}, {"dwql", 0x60}});
    Define("Core.dll", "", "fzg", {{"dwqv", 0x10}});
    Define("Core.dll", "", "dt", {{"dcbc", 0x10}, {"dcbd", 0x14}, {"dcbe", 0x18}, {"dcbf", 0x1C}, {"dcbg", 0x20}, {"dcbh", 0x24}, {"dcbi", 0x28}});
    Define("Core.dll", "", "mc");
    Define("Core.dll", "", "mg", {}, "mc");
    Define("Core.dll", "", "gau");
    Define(DataCenter, WorldSpace, "WorldPoint", {{"m_mapId", 0x10}});
    Define("Core.dll", "", "tk", {}, "WorldPoint");
    Define(DataCenter, "Core.DataCenter", "DataCenterModule");
    Define(DataCenter, WorldSpace, "DungeonsDataRoot");
    Define(DataCenter, WorldSpace, "DungeonData", {{"id", 0x10}, {"mapIds", 0x20}, {"bosses", 0x50}});
    Define(Protocol, "", "jsq", {{"ercq", 0x18}, {"ercs", 0x20}, {"ered", 0x28}, {"eree", 0x30}});
    Define(Protocol, "", "jsp", {}, {}, "jsq");
    Define(Protocol, "", "jrw", {{"eqwy", 0x20}, {"eqxe", 0x28}, {"eqxj", 0x38}, {"eqxm", 0x40}, {"eqxn", 0x48}}, {}, "jsp");
    Define(Protocol, "", "jrv", {}, {}, "jrw");
    Define(Protocol, "", "jrq", {{"eqvi", 0x18}, {"eqvk", 0x1C}}, {}, "jrv");
    Define(Protocol, "", "jrz", {{"eqyb", 0x18}, {"eqyd", 0x20}}, {}, "jsp");
    Define(Protocol, "", "jsd", {{"eqyu", 0x18}, {"eqyw", 0x1C}, {"eqyz", 0x20}, {"eqzd", 0x30}, {"eqzf", 0x38}, {"eqzh", 0x40}}, {}, "jsp");
    Define(Protocol, "", "jse", {{"eqzo", 0x20}}, {}, "jsp");
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_is_assignable_from = [](Klass expected, Klass actual) {
        for (auto type = static_cast<Class*>(actual); type; type = type->parent) if (type == expected) return true;
        return false;
    };
    api.object_unbox = [](Obj object) -> void* { return static_cast<unsigned char*>(object) + 0x10; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { handles.emplace(++nextHandle, object); return nextHandle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Unknown statistics GC handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Statistics GC handle released twice"); };
    api.class_get_field_from_name = [](Klass klass, const char* name) -> void* {
        for (auto type = static_cast<Class*>(klass); type; type = type->parent) {
            const auto found = type->layout.find(name);
            if (type->name + "." + name == fail) return nullptr;
            if (found == type->layout.end()) continue;
            layouts.insert(type->name + "." + name);
            return &found->second;
        }
        return nullptr;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
}
// The character service (esg) with its character (z) and position (tk), and the
// fight service (exf.drih = fxc) whose report owner (fzf) shows the last report.
struct World {
    Obj service, fight, history, character, info, position;
    explicit World(int64_t id = 1001) {
        service = Make("exf"); fight = Make("fxc"); history = Make("fzf"); character = Make("esg"); info = Make("z"); position = Make("tk");
        Set<Obj>(service, 0x18, fight); Set<Obj>(fight, 0xC8, history); Set<Obj>(history, 0x18, nullptr);
        Set<Obj>(character, 0x128, info); Set<Obj>(character, 0x1A0, position);
        Become(id); At(0);
        services = {{"fsl", service}, {"fng", character}};
    }
    void Become(int64_t id) { Set<int64_t>(info, 0x10, id); }
    void Show(Obj report) { Set<Obj>(history, 0x18, report); }
    void Fighting(bool on) { Set<bool>(fight, 0xB0, on); }
    void At(int64_t map) { Set<int64_t>(position, 0x10, map); }
    Obj Shown() const { return *reinterpret_cast<Obj*>(static_cast<unsigned char*>(history) + 0x18); }
};
// fzi.bipl's report (flag 2 set, 4 for a spectator) and fzi.bipi's results: id,
// outcome (1 defeat, 2 draw, 3 victory), kind (1 character, 2 monster) and loot.
Obj Item(uint32_t type, int quantity, const std::string& klass = "mc") { auto item = Make(klass); itemInfo[item] = {type, quantity}; return item; }
Obj Fighter(int64_t id, int16_t outcome = 3, int32_t kind = 1, std::vector<Obj> loot = {}) {
    auto fighter = Make("fze"), gains = Make("fzg");
    Set<int64_t>(fighter, 0x10, id); Set<int16_t>(fighter, 0x20, outcome); Set<bool>(fighter, 0x23, true);
    Set<Obj>(fighter, 0x28, gains); Set<int32_t>(fighter, 0x34, kind);
    Set<int64_t>(fighter, 0x60, 4321); // the fight's experience: never counted here
    Set<Obj>(gains, 0x10, List(std::move(loot)));
    return fighter;
}
Obj Monster(int64_t id, int16_t outcome = 1) { return Fighter(id, outcome, 2); }
Obj Statistics(int32_t dealt, int32_t taken, int32_t healed, int32_t finished) {
    auto figures = Make("dt");
    Set<int32_t>(figures, 0x10, dealt); Set<int32_t>(figures, 0x14, taken); Set<int32_t>(figures, 0x18, 70000);
    Set<int32_t>(figures, 0x1C, 80000); Set<int32_t>(figures, 0x20, healed); Set<int32_t>(figures, 0x24, 90000);
    Set<int32_t>(figures, 0x28, finished);
    return figures;
}
Obj Report(int32_t type, std::vector<Obj> fighters, int32_t ms = 90000, int32_t turns = 6, uint8_t flags = 2, Obj statistics = nullptr) {
    auto report = Make("fyz");
    Set<uint8_t>(report, 0x10, flags); Set<int32_t>(report, 0x14, type); Set<int32_t>(report, 0x18, ms); Set<int32_t>(report, 0x1C, turns);
    Set<Obj>(report, 0x30, List(std::move(fighters))); Set<Obj>(report, 0x60, statistics);
    return report;
}
// One dungeon (DungeonData) on some maps, with its bosses' MonsterData ids.
void Dungeon(int id, std::vector<int> maps, std::vector<int> bosses) {
    auto dungeon = Make("DungeonData");
    Set<int32_t>(dungeon, 0x10, id);
    std::vector<Obj> ids;
    for (const int boss : bosses) ids.push_back(Box<int32_t>(boss));
    Set<Obj>(dungeon, 0x50, List(std::move(ids)));
    for (const int map : maps) dungeonMaps[map] = dungeon;
}
// The fight actions gau.bixl receives (jsq) and their bodies.
Obj Action(int64_t source, int32_t which, Obj body, const std::string& klass = "jsq") {
    auto message = Make(klass);
    Set<int32_t>(message, 0x18, 22); Set<int64_t>(message, 0x20, source); Set<Obj>(message, 0x28, body); Set<int32_t>(message, 0x30, which);
    return message;
}
Obj Cast(int32_t spell, int32_t kind = 2) {
    auto cast = Make("jrw"), data = Make("jrq");
    Set<int64_t>(cast, 0x28, -7); Set<int32_t>(cast, 0x38, 1);
    Set<Obj>(cast, 0x40, kind == 2 ? data : Box<int32_t>(spell)); Set<int32_t>(cast, 0x48, kind);
    Set<int32_t>(data, 0x18, 9100); Set<int32_t>(data, 0x1C, spell);
    return cast;
}
Obj Death(int64_t dead) { auto death = Make("jrz"); Set<int64_t>(death, 0x18, -3); Set<int64_t>(death, 0x20, dead); return death; }
Obj Loss(int64_t target, int32_t life, int32_t element, int32_t shield = 0, bool present = false) {
    auto loss = Make("jsd");
    Set<int32_t>(loss, 0x18, present ? 1 : 0); Set<int32_t>(loss, 0x1C, 15); Set<int32_t>(loss, 0x20, shield);
    Set<int32_t>(loss, 0x30, element); Set<int64_t>(loss, 0x38, target); Set<int32_t>(loss, 0x40, life);
    return loss;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char* image, const char* space, const char* name) const {
    const auto found = fake::classes.find(name);
    fake::Check(found != fake::classes.end() && !found->second->declaring && found->second->image == image && found->second->space == space,
                "Statistics class requested from the wrong image or namespace");
    return found->second;
}
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    if (fake::fail == std::string("nested.") + name) throw std::runtime_error("Injected nested class failure");
    for (const auto& type : fake::types)
        if (type->declaring == declaringClass && type->name == name) return type.get();
    throw std::runtime_error("Nested class unavailable");
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    auto& type = *static_cast<fake::Class*>(klass);
    const auto key = type.name + "." + name;
    static const std::map<std::string, std::pair<int, uintptr_t>> expected{
        {"mc.mzb", {0, 0x2AAC720}}, {"mc.myo", {0, 0x861E70}}, {"DataCenterModule.get_dungeonsDataRoot", {0, 0x7BC7C0}},
        {"DungeonsDataRoot.GetDungeonByMapId", {1, 0x7C26B0}}, {"gau.bixl", {1, 0x1EA0910}}};
    if (key == fake::fail) throw std::runtime_error("Verified method unavailable");
    const auto found = expected.find(key);
    fake::Check(found != expected.end() && found->second == std::pair(argc, rva), "Statistics native outside the pinned contract");
    // The real lookup ends in NativeAt, which refuses an RVA without a 16-byte guard.
    fake::Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& guard) { return guard.rva == rva; }),
                "Statistics native without a signature guard");
    fake::guarded.insert(rva);
    ++fake::verifications;
    fake::methods.push_back(std::make_unique<fake::MethodInfo>(fake::MethodInfo{&type, name, argc}));
    return fake::methods.back().get();
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    using namespace fake;
    const auto& m = *static_cast<const MethodInfo*>(method);
    const std::vector<void*> args(values);
    Check(static_cast<int>(args.size()) == m.argc, "Wrong statistics method arity");
    if (m.name == fail) throw std::runtime_error("Injected native failure");
    if (m.name == "get_dungeonsDataRoot") { Check(self == nullptr, "get_dungeonsDataRoot is static"); ++rootCalls; return dungeonRoot; }
    Check(self && api.class_is_assignable_from(m.klass, api.object_get_class(self)), "Statistics method called on a foreign object");
    if (m.name == "GetDungeonByMapId") {
        Check(self == dungeonRoot, "GetDungeonByMapId on the data root");
        const auto found = dungeonMaps.find(*static_cast<int*>(args.at(0)));
        return found == dungeonMaps.end() ? nullptr : found->second;
    }
    if (m.name == "mzb") return Box<uint32_t>(itemInfo.at(self).first);
    if (m.name == "myo") return Box<int32_t>(itemInfo.at(self).second);
    throw std::runtime_error("Unexpected statistics method: " + m.name);
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> values) const {
    using namespace fake;
    const std::string call = name;
    const std::vector<void*> args(values);
    if (call == fail) throw std::runtime_error("Injected managed failure");
    Check(self && lists.contains(self), "Managed call outside a list");
    const auto& items = lists.at(self);
    if (call == "get_Count" && args.empty()) return Box<int32_t>(static_cast<int32_t>(items.size()));
    if (call == "get_Item" && args.size() == 1) return items.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected statistics call: " + call);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const {
    ++fake::lookups;
    const auto key = std::string("FindService:") + name;
    if (fake::fail == key) throw std::runtime_error("Injected service failure");
    if (fake::fail == key + ":int") throw 42; // not even a std::exception
    const auto found = fake::services.find(name);
    return found == fake::services.end() ? nullptr : found->second;
}
void Log(const std::string& line) { fake::logs.push_back(line); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    using namespace fake;
    const auto& m = *static_cast<const MethodInfo*>(method);
    Check(rva == 0x1EA0910 && m.klass->name == "gau" && m.name == "bixl" && m.argc == 1, "Only gau.bixl, through its verified method");
    if (failHook) throw std::runtime_error("Injected hook failure");
    ++hooks;
    listener = method;
    actionHook = reinterpret_cast<ActionFn>(replacement);
    *original = reinterpret_cast<void*>(&NativeAction);
}
namespace stats {
void Add(int64_t character, Counter counter, int64_t amount) {
    if (fake::storeThrows) throw std::runtime_error("Injected store failure");
    fake::Check(character > 0 && amount > 0, "The store receives positive amounts for a known character");
    fake::counters[{character, static_cast<int>(counter)}] += amount;
}
void Raise(int64_t character, Record record, int64_t value) {
    fake::Check(character > 0 && value > 0, "The store receives positive records for a known character");
    auto& best = fake::records[{character, static_cast<int>(record)}];
    best = std::max(best, value);
}
void AddTo(int64_t character, Table table, int key, int64_t amount) {
    fake::Check(character > 0 && amount > 0, "The store receives positive table amounts for a known character");
    fake::tables[{character, static_cast<int>(table), key}] += amount;
}
void Name(int64_t, const std::string&) { fake::named = true; }
bool Counting() { return fake::counting; }
}

namespace {
using namespace fake;
using stats::Counter;
using stats::Table;
void Tick(int times = 1) {
    for (int i = 0; i < times; ++i) {
        try { TickStatsFights(); }
        catch (...) { throw std::runtime_error("An exception escaped TickStatsFights towards Module::Pump"); }
    }
}
// A whole fight as the pump sees it: it starts, then ends with its report.
void Fight(World& world, Obj report) {
    world.Fighting(true); Tick();
    world.Show(report); world.Fighting(false); Tick();
}
void Send(Obj message) {
    Check(actionHook != nullptr, "The listener is hooked");
    try { actionHook(handler, message, listener); }
    catch (...) { throw std::runtime_error("An exception escaped the fight action hook"); }
}
int64_t Counted(int64_t character, Counter counter) {
    const auto found = counters.find({character, static_cast<int>(counter)});
    return found == counters.end() ? 0 : found->second;
}
int64_t Best(int64_t character) {
    const auto found = records.find({character, static_cast<int>(stats::Record::BiggestHit)});
    return found == records.end() ? 0 : found->second;
}
int64_t Entry(int64_t character, Table table, int key) {
    const auto found = tables.find({character, static_cast<int>(table), key});
    return found == tables.end() ? 0 : found->second;
}
size_t Lines(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
bool Logged(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
std::string Last(const std::string& prefix) {
    const auto found = std::find_if(logs.rbegin(), logs.rend(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
    return found == logs.rend() ? std::string{} : *found;
}
// A new scenario: nothing remembered, counting on, an empty store, fresh logs.
void Fresh() {
    RestoreStatsFights();
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    logs.clear(); counters.clear(); records.clear(); tables.clear();
    fail.clear(); storeThrows = false; counting = true;
}
void InstallContract() {
    // A moved field, an unverified native or a refused hook disables that part
    // only, without throwing; each part installs once, the hook at most once.
    fail = "z.dbrl"; InstallStatsFights();
    Check(Logged("STATS_FIGHTS_DISABLED stage=character") && hooks == 0 && verifications == 0, "Without its character nothing installs");
    { World world; world.Show(Report(4, {Fighter(1001)})); lookups = 0; Tick(2);
      Check(lookups == 0 && counters.empty(), "A disabled tracker reads nothing"); }
    fail = "fyz.dwpa"; failHook = true; InstallStatsFights();
    Check(Logged("STATS_FIGHTS_DISABLED stage=report") && Logged("STATS_FIGHTS_DISABLED stage=actions") && hooks == 0 &&
          !Logged("STATS_FIGHTS_DISABLED stage=dungeons"), "A moved report field and a refused hook disable their own part");
    fail = "DungeonData.bosses"; failHook = false; InstallStatsFights();
    Check(Logged("STATS_FIGHTS_DISABLED stage=dungeons") && hooks == 1, "Dungeons are optional; the hook is created once its contract holds");
    earlier.insert(earlier.end(), logs.begin(), logs.end()); logs.clear();
    for (const char* moved : {"jsd.eqzh", "nested.jrq", "gau.bixl", "esg.dpzj", "fyz.dwou"}) { fail = moved; InstallStatsFights(); }
    Check(logs.empty() && hooks == 1, "An installed part is never tried again");
    fail.clear(); InstallStatsFights();
    Check(logs.empty() && hooks == 1, "The pinned client contract is accepted, the hook created once");
    Check(layouts == std::set<std::string>{"esg.dpzj", "z.dbrl", "exf.drih", "fxc.dwan", "fxc.<dwaq>k__BackingField", "fzf.dwqq",
        "fyz.<dwon>k__BackingField", "fyz.dwoo", "fyz.dwop", "fyz.dwoq", "fyz.dwou", "fyz.dwpa", "fze.dwpw", "fze.dwpy", "fze.dwqb",
        "fze.dwqd", "fzg.dwqv", "dt.dcbc", "dt.dcbd", "dt.dcbg", "esg.dqab", "WorldPoint.m_mapId", "DungeonData.id",
        "DungeonData.bosses", "jsq.ercs", "jsq.ered", "jsq.eree", "jrw.eqxm", "jrw.eqxn", "jrq.eqvk", "jrz.eqyd", "jsd.eqyu", "jsd.eqyz",
        "jsd.eqzd", "jsd.eqzf", "jsd.eqzh"}, "Every field read has its pinned offset checked by name, never the fight's experience");
    Check(guarded == std::set<uintptr_t>{0x2AAC720, 0x861E70, 0x7BC7C0, 0x7C26B0, 0x1EA0910},
          "Exactly the item accessors, the dungeon lookups and gau.bixl, each behind a signature guard");
    const auto verified = verifications;
    InstallStatsFights();
    Check(verifications == verified && hooks == 1 && logs.empty(), "A second installation changes nothing");
}
void BaselineAndOnce() {
    Fresh(); World world;
    world.Show(Report(4, {Fighter(1001)})); // a fight that ended before the mod started
    Tick(3);
    Check(counters.empty() && !Lines("STATS_FIGHT"), "The report present at start is the baseline, never counted");
    const auto first = Report(4, {Fighter(1001), Monster(31), Monster(32)}, 95000, 7);
    Fight(world, first);
    Check(Counted(1001, Counter::FightsPve) == 1 && Counted(1001, Counter::WinsPve) == 1 && Counted(1001, Counter::FightMs) == 95000 &&
          Counted(1001, Counter::FightTurns) == 7, "A finished fight: its kind, its victory, its time and turns");
    Check(logs == std::vector<std::string>{"STATS_FIGHT type=4 kind=pve win=1 ms=95000 turns=7 monsters=2 souls=0 dungeon=0 casts=0 hits=0 deaths=0"},
          "One fixed line per fight");
    Tick(5);
    Fight(world, Report(4, {Fighter(1001, 1)}, 30000, 3));
    world.Show(first); Tick(2);
    Check(Counted(1001, Counter::FightsPve) == 2 && Counted(1001, Counter::WinsPve) == 1 && Counted(1001, Counter::FightMs) == 125000,
          "A report is counted once, even shown again");
    Check(Counted(1001, Counter::Kills) == 2 && !named && counters.size() == 5, "Nothing else: no name, no experience");
}
void Kinds() {
    Fresh(); World world; Tick();
    struct Case { int32_t type; Counter fights, wins; };
    const Case cases[] = {
        {0, Counter::FightsPvp, Counter::WinsPvp}, {1, Counter::FightsPvp, Counter::WinsPvp}, {5, Counter::FightsPvp, Counter::WinsPvp},
        {8, Counter::FightsPvp, Counter::WinsPvp}, {9, Counter::FightsPvp, Counter::WinsPvp},
        {7, Counter::FightsKolizeum, Counter::WinsKolizeum},
        {2, Counter::FightsPve, Counter::WinsPve}, {3, Counter::FightsPve, Counter::WinsPve}, {4, Counter::FightsPve, Counter::WinsPve},
        {6, Counter::FightsPve, Counter::WinsPve}, {10, Counter::FightsPve, Counter::WinsPve}, {-1, Counter::FightsPve, Counter::WinsPve},
        {11, Counter::FightsPve, Counter::WinsPve}};
    for (const auto& c : cases) {
        const auto fights = Counted(1001, c.fights), wins = Counted(1001, c.wins);
        Fight(world, Report(c.type, {Fighter(2002, 1), Fighter(1001, 3)}));
        Check(Counted(1001, c.fights) == fights + 1 && Counted(1001, c.wins) == wins + 1, "A victory of each fight kind");
        for (const int16_t outcome : {int16_t(1), int16_t(2), int16_t(0), int16_t(4), int16_t(5)}) {
            Fight(world, Report(c.type, {Fighter(1001, outcome), Fighter(2002, 3)}));
            Check(Counted(1001, c.wins) == wins + 1, "Only a victory (3) is a win: never a defeat, a draw or another outcome");
        }
        Check(Counted(1001, c.fights) == fights + 6, "Every finished fight counts, whatever its outcome");
    }
    Check(Counted(1001, Counter::FightsPvp) == 30 && Counted(1001, Counter::FightsKolizeum) == 6 && Counted(1001, Counter::FightsPve) == 42 &&
          Counted(1001, Counter::WinsPvp) == 5 && Counted(1001, Counter::WinsKolizeum) == 1 && Counted(1001, Counter::WinsPve) == 7,
          "Challenges, aggressions, tax collectors, AvA and prisms are JcJ, the Kolizéum its own, the rest JcM");
    Check(Logged("STATS_FIGHT type=7 kind=kolizeum win=1 ms=90000 turns=6 monsters=0 souls=0 dungeon=0 casts=0 hits=0 deaths=0") &&
          Logged("STATS_FIGHT type=0 kind=pvp win=0 ms=90000 turns=6 monsters=0 souls=0 dungeon=0 casts=0 hits=0 deaths=0"), "The kind is logged");
}
void TimeAndTurns() {
    Fresh(); World world; Tick();
    Fight(world, Report(4, {Fighter(1001)}, -5, -1));
    Fight(world, Report(4, {Fighter(1001)}, 86400001, 100001));
    Fight(world, Report(4, {Fighter(1001)}, 0, 0));
    Check(Counted(1001, Counter::FightsPve) == 3 && Counted(1001, Counter::FightMs) == 0 && Counted(1001, Counter::FightTurns) == 0,
          "A duration or a turn count outside bounds adds nothing, the fight still counts");
    Fight(world, Report(4, {Fighter(1001)}, 86400000, 100000));
    Check(Counted(1001, Counter::FightMs) == 86400000 && Counted(1001, Counter::FightTurns) == 100000, "Up to a day and 100000 turns");
}
void NotTheCharactersFight() {
    Fresh(); World world; Tick();
    Fight(world, Report(4, {Fighter(1001), Fighter(2002)}, 60000, 4, 2 | 4, Statistics(500, 0, 0, 1)));
    Fight(world, Report(4, {Fighter(2002), Monster(31)}, 60000, 4, 2, Statistics(500, 0, 0, 1)));
    auto empty = Report(9, {}); Set<Obj>(empty, 0x30, nullptr);
    Fight(world, empty);
    Check(counters.empty() && Logged("STATS_FIGHT_SKIPPED reason=spectator type=4") && Logged("STATS_FIGHT_SKIPPED reason=absent type=4") &&
          Logged("STATS_FIGHT_SKIPPED reason=absent type=9") && !Lines("STATS_FIGHT_STATISTICS"),
          "A watched fight, another character's fight or no result at all: nothing, statistics included");
}
void StartedBeforeCounting() {
    Fresh(); World world;
    world.Fighting(true); Tick(); // the mod starts during a fight
    world.Show(Report(4, {Fighter(1001)}, 1000, 1, 2, Statistics(900, 0, 0, 0))); world.Fighting(false); Tick();
    Check(counters.empty() && Logged("STATS_FIGHT_SKIPPED reason=started type=4"), "A fight running when the mod started never counts");
    Fight(world, Report(4, {Fighter(1001)}));
    Check(Counted(1001, Counter::FightsPve) == 1, "The next one does");
    // Counting switched off then on during a fight: that fight never counts either.
    world.Fighting(true); Tick(); counting = false; Tick(); counting = true; Tick();
    world.Show(Report(7, {Fighter(1001)})); world.Fighting(false); Tick();
    Check(Counted(1001, Counter::FightsKolizeum) == 0 && Lines("STATS_FIGHT_SKIPPED reason=started") == 2, "Nor one running when counting resumed");
    Fight(world, Report(7, {Fighter(1001)}));
    Check(Counted(1001, Counter::FightsKolizeum) == 1, "Counting resumes with the next fight");
    // A reconnection during a fight (a new report owner, the same character seen
    // outside before): the fight began while counting, it counts.
    world.Fighting(true); Tick();
    Set<Obj>(world.fight, 0xC8, Make("fzf")); world.history = *reinterpret_cast<Obj*>(static_cast<unsigned char*>(world.fight) + 0xC8);
    Set<Obj>(world.history, 0x18, nullptr); Tick();
    world.Show(Report(1, {Fighter(1001)})); world.Fighting(false); Tick();
    Check(Counted(1001, Counter::FightsPvp) == 1, "A fight across a reconnection counts once");
}
void CountingOff() {
    Fresh(); World world; Tick();
    counting = false; lookups = 0;
    world.Fighting(true); Tick(); world.Show(Report(4, {Fighter(1001)})); world.Fighting(false); Tick(3);
    Send(Action(1001, 2, Cast(3456))); Send(Action(-5, 3, Death(1001)));
    Check(lookups == 0 && counters.empty() && tables.empty(), "Counting off: nothing read, nothing counted");
    counting = true; Tick(2);
    Check(counters.empty(), "A fight that ended while counting was off never counts");
    Fight(world, Report(4, {Fighter(1001)}));
    Check(Counted(1001, Counter::FightsPve) == 1, "Fights count again");
}
void Characters() {
    Fresh(); World world; Tick();
    // Another character on this client: the report shown at the switch is a baseline.
    world.Become(1002); world.Show(Report(4, {Fighter(1002)})); Tick();
    Check(counters.empty(), "A character switch takes a new baseline");
    Fight(world, Report(4, {Fighter(1002), Fighter(1001, 1)}, 5000, 2, 2, Statistics(10, 20, 30, 1)));
    Check(Counted(1002, Counter::FightsPve) == 1 && Counted(1002, Counter::WinsPve) == 1 && Counted(1001, Counter::FightsPve) == 0 &&
          Counted(1002, Counter::DamageDealt) == 10, "Only the played character's own result, under its own id");
    // Back to the first character with a report already shown: a baseline too,
    // although that character was seen outside a fight before.
    world.Become(1001); world.Show(Report(4, {Fighter(1001)})); Tick();
    Check(Counted(1001, Counter::FightsPve) == 0, "The report shown at any character switch is never counted");
    // The other character again: seen outside before, its next fight counts.
    world.Become(1002); Tick(); world.Become(1001); Tick();
    Fight(world, Report(1, {Fighter(1001, 1), Fighter(1002, 3)}));
    Check(Counted(1001, Counter::FightsPvp) == 1 && Counted(1001, Counter::WinsPvp) == 0 && Counted(1002, Counter::FightsPvp) == 0,
          "Each character counts its own fights");
    // A character logging in during a fight never seen starting here: not counted.
    world.Become(1003); world.Fighting(true); Tick();
    world.Show(Report(4, {Fighter(1003)})); world.Fighting(false); Tick();
    Check(Counted(1003, Counter::FightsPve) == 0 && Logged("STATS_FIGHT_SKIPPED reason=started type=4"), "Nor a fight already running at login");
    // No character (esg.dpzj null): nothing read further, nothing counted.
    Set<Obj>(world.character, 0x128, nullptr); world.Show(Report(4, {Fighter(1003)})); lookups = 0; Tick(2);
    Check(lookups == 2 && Counted(1003, Counter::FightsPve) == 0, "Without a character the fight service is not even read");
}
void ServerStatistics() {
    Fresh(); World world; Tick();
    Fight(world, Report(4, {Fighter(1001)}, 60000, 5, 2, Statistics(1500, 300, 200, 4)));
    Check(Counted(1001, Counter::DamageDealt) == 1500 && Counted(1001, Counter::DamageTaken) == 300 && Counted(1001, Counter::Heals) == 200 &&
          Counted(1001, Counter::Kills) == 0 && Logged("STATS_FIGHT_STATISTICS dealt=1500 taken=300 heals=200"),
          "The server's statistics: damage dealt and taken, heals given (never blocked, shields, heals received or enemies finished)");
    // Arriving after the report (fzf.wd, a moment later).
    const auto later = Report(4, {Fighter(1001)});
    Fight(world, later); Tick(2);
    Check(Counted(1001, Counter::DamageDealt) == 1500, "Not before they arrive");
    Set<Obj>(later, 0x60, Statistics(1000, 0, 0, 2)); Tick();
    Check(Counted(1001, Counter::DamageDealt) == 2500 && Counted(1001, Counter::DamageTaken) == 300 && Counted(1001, Counter::Heals) == 200,
          "Added when they arrive");
    Set<Obj>(later, 0x60, Statistics(5000, 5000, 5000, 5)); Tick(3);
    Check(Counted(1001, Counter::DamageDealt) == 2500 && Counted(1001, Counter::Heals) == 200, "Once per fight, even replaced");
    // Never arriving: dropped when the next report replaces it, never added to it later.
    const auto silent = Report(4, {Fighter(1001)});
    Fight(world, silent); Tick(2);
    Fight(world, Report(4, {Fighter(1001)}, 1000, 1, 2, Statistics(10, 0, 0, 0)));
    Check(Logged("STATS_FIGHT_STATISTICS missing") && Counted(1001, Counter::DamageDealt) == 2510, "A fight without statistics adds none");
    Set<Obj>(silent, 0x60, Statistics(999999, 0, 0, 0)); Tick(2);
    Check(Counted(1001, Counter::DamageDealt) == 2510, "Statistics reaching an older report never count");
    // A character switch while they are awaited: the fight's own character.
    const auto awaited = Report(4, {Fighter(1001)});
    Fight(world, awaited); world.Become(1002); Tick();
    Set<Obj>(awaited, 0x60, Statistics(7, 0, 0, 0)); Tick();
    Check(Counted(1001, Counter::DamageDealt) == 2517 && Counted(1002, Counter::DamageDealt) == 0, "Counted for the fight's character");
    // Negative values never subtract; an unexpected object is dropped.
    world.Become(1001); Tick();
    Fight(world, Report(4, {Fighter(1001)}, 1000, 1, 2, Statistics(-5, -5, -5, -5)));
    Fight(world, Report(4, {Fighter(1001)}, 1000, 1, 2, Make("Other")));
    Check(Counted(1001, Counter::DamageDealt) == 2517 && Counted(1001, Counter::Heals) == 200 && Counted(1001, Counter::Kills) == 0 &&
          Logged("STATS_FIGHTS_ERROR stage=statistics"), "Nothing negative, nothing unexpected");
}
// « Monstres vaincus »: the monsters on the losing side of a fight the
// character's side won, each once; never the server's enemies finished.
void MonstersDefeated() {
    Fresh(); World world; Tick();
    Fight(world, Report(4, {Fighter(1001), Fighter(1002), Monster(31), Monster(32), Monster(33), Fighter(77, 3, 2), Fighter(2002, 1)},
        60000, 4, 2, Statistics(100, 0, 0, 9)));
    Check(Counted(1001, Counter::Kills) == 3 && Counted(1002, Counter::Kills) == 0 &&
          Logged("STATS_FIGHT type=4 kind=pve win=1 ms=60000 turns=4 monsters=3 souls=0 dungeon=0 casts=0 hits=0 deaths=0"),
          "Three monsters defeated: not the companion on the character's side, not a character, not the enemies finished");
    Fight(world, Report(4, {Fighter(1001, 1), Monster(31, 3), Monster(32, 1)}));   // a defeat
    Fight(world, Report(4, {Fighter(1001, 2), Monster(31, 2)}));                   // a draw
    Fight(world, Report(4, {Fighter(1001), Monster(31)}, 1000, 1, 2 | 4));         // watched
    Fight(world, Report(1, {Fighter(1001), Fighter(2002, 1)}));                    // players only
    Check(Counted(1001, Counter::Kills) == 3 && Counted(1001, Counter::FightsPve) == 3 && Counted(1001, Counter::FightsPvp) == 1,
          "Only the victories' monsters count");
    Fight(world, Report(7, {Fighter(1001), Monster(31)}));
    Check(Counted(1001, Counter::Kills) == 4, "Whatever the fight's kind");
}
void Souls() {
    Fresh(); World world; Tick();
    Fight(world, Report(4, {Fighter(1001, 3, 1, {Item(327, 1), Item(328, 2), Item(9, 5), Item(83, 1), Item(328, 0), Item(328, 101),
        Item(327, 1, "mg"), Make("Other"), nullptr}), Fighter(2002, 3, 1, {Item(328, 3)})}));
    Check(Counted(1001, Counter::Captures) == 4 && Counted(2002, Counter::Captures) == 0 &&
          Logged("STATS_FIGHT type=4 kind=pve win=1 ms=90000 turns=6 monsters=0 souls=4 dungeon=0 casts=0 hits=0 deaths=0"),
          "Boss (327) and archimonster (328) souls of this character's own loot, by quantity (1 to 100)");
    auto bare = Fighter(1001, 1); Set<Obj>(bare, 0x28, nullptr);
    Fight(world, Report(4, {bare}));
    Check(Counted(1001, Counter::Captures) == 4 && Counted(1001, Counter::FightsPve) == 2, "A fighter without loot");
}
void Dungeons() {
    Fresh(); World world; Tick();
    dungeonRoot = Make("DungeonsDataRoot");
    Dungeon(51, {1000, 1001}, {1087, 1072, 1085, 1086}); // Tynril's four variants
    Dungeon(96, {3000}, {});                              // no boss: never completed
    world.At(1000);
    Fight(world, Report(4, {Fighter(1001), Monster(1072), Monster(31)}));
    Check(Counted(1001, Counter::Dungeons) == 1 && Entry(1001, Table::Dungeons, 51) == 1 &&
          Logged("STATS_FIGHT type=4 kind=pve win=1 ms=90000 turns=6 monsters=2 souls=0 dungeon=51 casts=0 hits=0 deaths=0"),
          "A victory over one of its bosses on its map completes a dungeon");
    // The map where the fight started, even after the character moved.
    world.Fighting(true); Tick(); world.At(5555);
    world.Show(Report(4, {Fighter(1001), Monster(1087)})); world.Fighting(false); Tick();
    Check(Entry(1001, Table::Dungeons, 51) == 2, "The boss room is where the fight started");
    world.At(1000);
    Fight(world, Report(4, {Fighter(1001), Monster(31)}));           // not one of its bosses
    Fight(world, Report(4, {Fighter(1001, 1), Monster(1072, 3)}));   // a defeat
    Fight(world, Report(4, {Fighter(1001, 2), Monster(1072, 2)}));   // a draw, the boss not a winner either
    Fight(world, Report(4, {Fighter(1001), Monster(1072, 3)}));      // the boss never lost
    Fight(world, Report(4, {Fighter(1001), Fighter(1072, 3, 2)}));   // a companion carrying a boss's id, on the character's side
    Fight(world, Report(4, {Fighter(1001), Fighter(1072, 1, 1)}));   // a character carrying a boss's id
    world.At(2000); Fight(world, Report(4, {Fighter(1001), Monster(1072)}));   // not a dungeon's map
    world.At(3000); Fight(world, Report(4, {Fighter(1001), Monster(1072)}));   // a dungeon without bosses
    world.At(-1); Fight(world, Report(4, {Fighter(1001), Monster(1072)}));     // no map
    world.At(5000000000LL); Fight(world, Report(4, {Fighter(1001), Monster(1072)})); // beyond the lookup's Int32
    Check(Counted(1001, Counter::Dungeons) == 2 && Counted(1001, Counter::FightsPve) == 12, "Nothing else completes it");
    // Its start not seen: the current map.
    world.At(1001); world.Show(Report(4, {Fighter(1001), Monster(1086)})); Tick();
    Check(Entry(1001, Table::Dungeons, 51) == 3 && Counted(1001, Counter::FightsPve) == 13, "The current map when the fight's start was not seen");
    // Failing lookups: the fight still counts, one fixed line.
    fail = "GetDungeonByMapId"; Fight(world, Report(4, {Fighter(1001), Monster(1086)})); fail.clear();
    auto odd = Make("Other"); dungeonMaps[1002] = odd; world.At(1002); Fight(world, Report(4, {Fighter(1001), Monster(1086)}));
    Check(Counted(1001, Counter::Dungeons) == 3 && Counted(1001, Counter::FightsPve) == 15 && Lines("STATS_FIGHTS_ERROR stage=dungeon") == 1,
          "A failing dungeon lookup loses that dungeon only (one line every 10 s)");
    dungeonMaps.clear(); dungeonRoot = nullptr;
    world.At(1000); Fight(world, Report(4, {Fighter(1001), Monster(1072)}));
    Check(Counted(1001, Counter::Dungeons) == 3 && Counted(1001, Counter::FightsPve) == 16, "No dungeon data: no dungeon");
}
void Actions() {
    Fresh(); World world;
    Send(Action(1001, 2, Cast(3456)));
    Check(tables.empty(), "Nothing before the pump has read the character");
    Tick();
    Send(Action(1001, 2, Cast(3456)));
    Send(Action(1001, 2, Cast(3456)));
    Send(Action(1001, 2, Cast(201)));
    Send(Action(1001, 2, Cast(3456, 9)));         // a weapon hit
    auto weapon = Cast(3456); Set<int32_t>(weapon, 0x48, 9);
    Send(Action(1001, 2, weapon));                // a weapon case, whatever its body holds
    Send(Action(2002, 2, Cast(3456)));            // another fighter's spell
    Send(Action(1001, 2, Cast(0)));               // no spell
    Send(Action(1001, 2, Death(1001)));           // another body for that case
    Send(Action(1001, 25, Cast(3456)));           // a triggered spell (case 25), not a cast
    Check(Counted(1001, Counter::SpellCasts) == 3 && Entry(1001, Table::Spells, 3456) == 2 && Entry(1001, Table::Spells, 201) == 1 &&
          Entry(1001, Table::Spells, 0) == 0, "The character's own spell casts, by spell id");
    Send(Action(-12, 3, Death(1001)));
    Send(Action(1001, 3, Death(-12)));            // the character kills: not a death
    Send(Action(-12, 3, Loss(1001, 50, 1)));      // another body for that case
    Check(Counted(1001, Counter::Deaths) == 1, "The character's own deaths");
    Send(Action(1001, 37, Loss(-12, 1200, 2)));                 // fire
    Send(Action(1001, 37, Loss(-13, 300, 3, 500, true)));       // water: life and shield
    Send(Action(1001, 37, Loss(-13, 100, 4, 900, false)));      // air: a shield value without its bit is none
    Send(Action(1001, 37, Loss(-14, 2000, 6)));                 // best element: no element, still a hit
    Send(Action(1001, 37, Loss(-15, 70, 0)));                   // neutral
    Send(Action(1001, 37, Loss(1001, 5000, 1)));                // itself
    Send(Action(-12, 37, Loss(1001, 4000, 1)));                 // taken
    Send(Action(-12, 37, Loss(-13, 3000, 1)));                  // between two other fighters
    Send(Action(1001, 37, Loss(-12, -50, 1)));                  // nothing lost
    Send(Action(1001, 37, Loss(-12, 100000001, 1)));            // beyond bounds
    Send(Action(1001, 37, Death(-12)));                         // another body for that case
    Send(Action(1001, 39, Loss(-12, 100, 1)));                  // the heal case
    Check(Entry(1001, Table::Elements, 2) == 1200 && Entry(1001, Table::Elements, 3) == 800 && Entry(1001, Table::Elements, 4) == 100 &&
          Entry(1001, Table::Elements, 0) == 70 && Entry(1001, Table::Elements, 1) == 0 && Entry(1001, Table::Elements, 6) == 0,
          "Damage the character deals to others, by element (life and shield)");
    Check(Best(1001) == 2000, "The biggest hit, whatever its element");
    const auto before = handled.size();
    std::vector<Obj> sent{Action(1001, 37, nullptr), Make("Other"), nullptr, Action(1001, 37, Loss(-12, 100, 0), "Other"),
        Action(1001, 2, Cast(77))};
    for (const auto message : sent) Send(message);
    Check(handled.size() == before + sent.size() && Entry(1001, Table::Spells, 77) == 1 && Entry(1001, Table::Elements, 0) == 70,
          "Anything unexpected is left alone");
    for (size_t i = 0; i < sent.size(); ++i)
        Check(handled[before + i] == std::tuple(handler, sent[i], listener), "The game's own handler runs once, with its arguments");
    // The next counted fight logs what the hook counted meanwhile.
    Fight(world, Report(4, {Fighter(1001)}));
    Check(Logged("STATS_FIGHT type=4 kind=pve win=1 ms=90000 turns=6 monsters=0 souls=0 dungeon=0 casts=4 hits=5 deaths=1"),
          "The fight's line carries the hook's counts");
    Send(Action(1001, 3, Death(1001)));
    Fight(world, Report(4, {Fighter(1001, 1)}));
    Check(Last("STATS_FIGHT type=") == "STATS_FIGHT type=4 kind=pve win=0 ms=90000 turns=6 monsters=0 souls=0 dungeon=0 casts=0 hits=0 deaths=1",
          "Each fight's line carries only its own counts");
    // Counting off, a stopping module, no character, another thread, a failing store, a restore: nothing.
    const int64_t spells = Counted(1001, Counter::SpellCasts);
    const size_t calls = handled.size();
    counting = false; Send(Action(1001, 2, Cast(3456))); counting = true;
    module->stopped = true; Send(Action(1001, 2, Cast(3456))); module->stopped = false;
    module->stopRequested = true; Send(Action(1001, 2, Cast(3456))); module->stopRequested = false;
    Set<Obj>(world.character, 0x128, nullptr); Tick(); Send(Action(1001, 2, Cast(3456)));
    Set<Obj>(world.character, 0x128, world.info); Tick();
    for (int i = 0; i < 2; ++i) std::thread([] { Send(Action(1001, 2, Cast(3456))); }).join();
    Check(Lines("STATS_FIGHTS_THREAD") == 1, "Another thread: never counted, one line");
    storeThrows = true; Send(Action(1001, 2, Cast(3456))); storeThrows = false;
    Check(Logged("STATS_FIGHTS_ERROR stage=action"), "A failing store never escapes the hook");
    RestoreStatsFights(); Send(Action(1001, 2, Cast(3456)));
    Check(Counted(1001, Counter::SpellCasts) == spells && handled.size() == calls + 8, "The game's handler always runs, the hook counts nothing");
    Tick(); Send(Action(1001, 2, Cast(3456)));
    Check(Counted(1001, Counter::SpellCasts) == spells + 1, "Counting again once the pump has read the character");
}
void Failures() {
    // A failing read: one fixed line, never thrown; the report counts once reads work.
    for (const char* name : {"FindService:fsl", "FindService:fsl:int", "FindService:fng", "FindService:fng:int"}) {
        Fresh(); World world; Tick();
        world.Fighting(true); Tick(); world.Show(Report(4, {Fighter(1001)})); world.Fighting(false);
        fail = name; Tick(3);
        Check(counters.empty() && Lines("STATS_FIGHTS_ERROR stage=read") == 1 && logs.size() == 1, "A failing read is one fixed line");
        fail.clear(); Tick(3);
        Check(Counted(1001, Counter::FightsPve) == 1, "The report counts once reads work again");
    }
    // A failure inside a report: remembered first, never counted, never reprocessed.
    for (const char* name : {"get_Count", "get_Item", "mzb", "myo"}) {
        Fresh(); World world; Tick();
        world.Fighting(true); Tick(); world.Show(Report(4, {Fighter(1001, 3, 1, {Item(327, 1)})})); world.Fighting(false);
        fail = name; Tick(); fail.clear(); Tick(3);
        Check(counters.empty() && logs == std::vector<std::string>{"STATS_FIGHTS_ERROR stage=report"}, "A failed report is dropped");
        Fight(world, Report(4, {Fighter(1001)}));
        Check(Counted(1001, Counter::FightsPve) == 1, "The next report counts");
    }
    Fresh(); { World world; Tick();
        world.Show(Make("Other")); Tick();
        Check(Lines("STATS_FIGHTS_ERROR stage=read") == 1, "An unexpected report object is refused");
        Fight(world, Report(4, {Make("Other"), Fighter(1001)}));
        auto strange = Fighter(1001); Set<Obj>(strange, 0x28, Make("Other"));
        Fight(world, Report(4, {strange}));
        Check(counters.empty() && Lines("STATS_FIGHTS_ERROR stage=report") == 1, "Unexpected fighters or loot drop the report (one line every 10 s)");
        std::vector<Obj> crowd;
        for (int i = 0; i < 127; ++i) crowd.push_back(Monster(5000 + i));
        crowd.push_back(Fighter(1001));
        Fight(world, Report(4, crowd));
        Check(Counted(1001, Counter::FightsPve) == 1, "128 fighters are read");
        crowd.push_back(Monster(4999));
        Fight(world, Report(4, crowd));
        Check(Counted(1001, Counter::FightsPve) == 1, "129 are refused");
        services.erase("fsl"); world.Fighting(true); Tick(); world.Fighting(false); services["fsl"] = world.service;
        Set<Obj>(world.service, 0x18, Make("Other")); Tick(); Set<Obj>(world.service, 0x18, world.fight);
        Set<Obj>(world.fight, 0xC8, Make("Other")); Tick(); Set<Obj>(world.fight, 0xC8, world.history);
        Check(Lines("STATS_FIGHTS_ERROR stage=read") == 1 && Counted(1001, Counter::FightsPve) == 1, "No fight service yet: nothing decided");
    }
}
void ModuleStates() {
    Fresh(); World world; Tick();
    world.Fighting(true); Tick(); world.Show(Report(4, {Fighter(1001)})); world.Fighting(false); lookups = 0;
    module->stopRequested = true; Tick(); module->stopRequested = false;
    module->stopped = true; Tick(); module->stopped = false;
    module->servicesReady = false; Tick(); module->servicesReady = true;
    module->ready = false; Tick(); module->ready = true;
    const auto owner = module; module = nullptr; Tick(); module = owner;
    Check(lookups == 0 && counters.empty(), "Nothing is read unless the module runs");
    Tick();
    Check(Counted(1001, Counter::FightsPve) == 1, "The report counts once the module runs");
}
void SessionAndRestore() {
    Fresh(); World world; Tick();
    world.Fighting(true); Tick(); world.Show(Report(4, {Fighter(1001)})); world.Fighting(false);
    module->compositor.reset(Make("Compositor")); Tick();
    Check(counters.empty(), "A new session takes a new baseline");
    Fight(world, Report(4, {Fighter(1001)}));
    Check(Counted(1001, Counter::FightsPve) == 1, "Fights count in the new session");
    Fight(world, Report(4, {Fighter(1001)}));
    RestoreStatsFights();
    Check(handles.size() == 1, "Restore releases every reference it held (only the module's compositor remains)");
    Fight(world, Report(4, {Fighter(1001)}));
    Check(Counted(1001, Counter::FightsPve) == 2 && Logged("STATS_FIGHT_SKIPPED reason=started type=4"),
          "After a restore the shown report is a baseline, and the fight running then never counts");
}
void FixedCodeLogs() {
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    const std::regex format(R"(^(STATS_FIGHT type=-?\d+ kind=(pve|pvp|kolizeum) win=[01] ms=\d+ turns=\d+ monsters=\d+ souls=\d+ dungeon=\d+ casts=\d+ hits=\d+ deaths=\d+)"
        R"(|STATS_FIGHT_SKIPPED reason=(spectator|absent|started) type=-?\d+|STATS_FIGHT_STATISTICS (dealt=\d+ taken=\d+ heals=\d+|missing))"
        R"(|STATS_FIGHTS_ERROR stage=(read|report|statistics|dungeon|action)|STATS_FIGHTS_DISABLED stage=(character|report|dungeons|actions))"
        R"(|STATS_FIGHTS_THREAD)$)");
    Check(earlier.size() > 100, "The scenarios produced logs");
    for (const auto& line : earlier) {
        Check(std::regex_match(line, format), "Every log line is a fixed code with numbers");
        for (const char* text : {"Injected", "Exception", "unavailable", "outside"})
            Check(line.find(text) == std::string::npos, "No exception text in the log");
    }
}
}

int main() {
    using namespace fake;
    try {
        Initialize();
        Module owner{"offline-stats-fights"};
        module = &owner;
        owner.ready = true; owner.servicesReady = true;
        owner.compositor.reset(Make("Compositor"));
        handler = Make("gau");
        InstallContract();
        BaselineAndOnce();
        Kinds();
        TimeAndTurns();
        NotTheCharactersFight();
        StartedBeforeCounting();
        CountingOff();
        Characters();
        ServerStatistics();
        MonstersDefeated();
        Souls();
        Dungeons();
        Actions();
        Failures();
        ModuleStates();
        SessionAndRestore();
        FixedCodeLogs();
        RestoreStatsFights();
        owner.compositor.reset(); module = nullptr;
        Check(handles.empty(), "The fight statistics leaked GC references");
        std::cout << "PASS: fight reports (baseline, once, local character only, spectators, fights running before counting, counting off), "
                     "kinds (JcM, JcJ with challenges, Kolizéum) and victories, time and turns, monsters defeated, server statistics once, souls, "
                     "dungeons by boss, "
                     "hooked fight actions (spells, deaths, elements, biggest hit, main thread only, game handler always called), "
                     "contained failures, pinned contract, fixed-code logs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
