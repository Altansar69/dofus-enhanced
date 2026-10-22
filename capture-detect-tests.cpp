#include "capture-detect.hpp"
#include "features.hpp"
#include "metamob-data.hpp"
#include "module.hpp"
#include "options-ui.hpp"
#include "signatures.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

// Real capture-detect.cpp linked against a managed fixture: no game, process,
// file, network, Metamob API or key; the sink records what would be queued.
// Only the soul gid -> monster pairs come from the pinned soul table, and the
// gids' item types and room-soul effect lists from the client's items; monster
// names and flags are labels for these tests.
namespace fake {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
struct Class { std::string image, space, name; Class* parent = nullptr; std::map<std::string, size_t> layout; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, int64_t> numbers;
    std::vector<Obj> items;
    int64_t scalar = 0;
    std::string text;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::map<GCHandle, Obj> handles;
std::map<std::string, Obj> services;
std::map<int64_t, Obj> monsters;
std::set<std::string> layouts;   // class.field offsets checked at installation
std::set<uintptr_t> guarded;     // natives verified at installation
std::vector<std::string> logs, earlier;
std::vector<metamob::Capture> captured;
std::string fail;                // member, method, "Class.member" or "FindService:name[:int]" that fails
GCHandle nextHandle = 0xCA9700000000ULL;
unsigned lookups = 0, zcxCalls = 0, verifications = 0;
bool sinkThrows = false;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Object& Get(Obj object) { Check(object != nullptr, "Null fake capture object"); return *static_cast<Object*>(object); }
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
Obj Box(int64_t value) { auto box = Make("Box"); Get(box).scalar = value; return box; }
Obj Text(const std::string& value) { auto text = Make("String"); Get(text).text = value; return text; }
Obj List(std::vector<Obj> items) { auto list = Make("List"); Get(list).items = std::move(items); return list; }
void Initialize() {
    for (const auto name : {"Box", "String", "List", "Compositor", "Other"}) Define("", "", name);
    // Pinned 3.7.12.12 contracts (work/r74-analysis/contracts-new.json).
    Define("Core.dll", "", "exf", {{"drih", 0x18}});
    Define("Core.dll", "", "fxc", {{"<dwaq>k__BackingField", 0xC8}});
    Define("Core.dll", "", "fzf", {{"dwqq", 0x18}});
    Define("Core.dll", "", "fyz", {{"dwou", 0x30}});
    Define("Core.dll", "", "fze", {{"dwpw", 0x10}, {"dwpy", 0x20}, {"dwqb", 0x28}});
    Define("Core.dll", "", "fzg", {{"dwqv", 0x10}});
    Define("Core.dll", "", "esg", {{"dpzj", 0x128}});
    Define("Core.dll", "", "z", {{"dbrl", 0x10}, {"dbrm", 0x18}});
    Define("Core.dll", "", "mc");
    Define(DataCenter, "Core.DataCenter.Metadata.Item", "ItemData", {{"possibleEffects", 0x68}});
    Define(DataCenter, "Core.DataCenter.Metadata.Effect", "EffectInstanceData", {{"actionId", 0x1A}});
    Define(DataCenter, "Core.DataCenter.Metadata.Effect.Instance", "EffectInstanceInteger", {{"value", 0x78}}, "EffectInstanceData");
    Define(DataCenter, "Core.DataCenter.Metadata.Effect.Instance", "EffectInstanceDice", {}, "EffectInstanceInteger");
    Define(DataCenter, "Core.DataCenter.Metadata.Monster", "MonsterData");
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_is_assignable_from = [](Klass expected, Klass actual) {
        for (auto type = static_cast<Class*>(actual); type; type = type->parent) if (type == expected) return true;
        return false;
    };
    api.object_unbox = [](Obj object) -> void* { return &Get(object).scalar; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { handles.emplace(++nextHandle, object); return nextHandle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Unknown capture GC handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Capture GC handle released twice"); };
    api.class_get_field_from_name = [](Klass klass, const char* name) -> void* {
        auto& type = *static_cast<Class*>(klass);
        const auto found = type.layout.find(name);
        if (type.name + "." + name == fail || found == type.layout.end()) return nullptr;
        layouts.insert(type.name + "." + name);
        return &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
}
// The character service (esg), its character (z) and the fight service
// (exf.drih = fxc) whose report owner (fzf) shows the last report (dwqq).
struct World {
    Obj service, fight, owner, character, info;
    explicit World(int64_t id = 1001, const std::string& name = "Testeur") {
        service = Make("exf"); fight = Make("fxc"); owner = Make("fzf"); character = Make("esg"); info = Make("z");
        Get(service).fields["drih"] = fight;
        Get(fight).fields["<dwaq>k__BackingField"] = owner;
        Get(owner).fields["dwqq"] = nullptr;
        Get(character).fields["dpzj"] = info;
        Become(id, name);
        services = {{"fsl", service}, {"fng", character}};
    }
    void Become(int64_t id, const std::string& name) { Get(info).fields = {{"dbrl", Box(id)}, {"dbrm", Text(name)}}; }
    void Show(Obj report) { Get(owner).fields["dwqq"] = report; }
};
Obj Effect(int action, int value, const std::string& type = "EffectInstanceDice") {
    auto effect = Make(type);
    Get(effect).fields["actionId"] = Box(action);
    if (type != "EffectInstanceData") Get(effect).fields["value"] = Box(value);
    return effect;
}
// A loot item as fzk.bipr builds it (gid, quantity, no instance effect): the
// soul's monster is in the static effects of its ItemData.
Obj Item(int gid, int type, int quantity, std::vector<Obj> effects = {}) {
    auto item = Make("mc"), data = Make("ItemData");
    Get(item).numbers = {{"gid", gid}, {"type", type}, {"quantity", quantity}};
    Get(item).fields["data"] = data;
    Get(data).fields["possibleEffects"] = List(std::move(effects));
    return item;
}
Obj Junk() { auto item = Make("mc"); Get(item).numbers = {{"gid", 311}, {"type", 9}, {"quantity", 1}}; return item; }
Obj Soul(int gid, int monster, int type = 328, int quantity = 1) {
    return Item(gid, type, quantity, {Effect(1, 5), Effect(4058, monster)});
}
Obj Fighter(int64_t id, std::vector<Obj> loot, int outcome = 3) {
    auto fighter = Make("fze"), gains = Make("fzg");
    Get(fighter).fields = {{"dwpw", Box(id)}, {"dwpy", Box(outcome)}, {"dwqb", gains}};
    Get(gains).fields["dwqv"] = List(std::move(loot));
    return fighter;
}
Obj Report(std::vector<Obj> fighters) { auto report = Make("fyz"); Get(report).fields["dwou"] = List(std::move(fighters)); return report; }
Obj Capture() { return Report({Fighter(1001, {Soul(34004, 2270)})}); }
void Monster(int64_t id, const std::string& name, bool boss, bool arch, const std::string& type = "MonsterData") {
    auto monster = Make(type);
    Get(monster).numbers = {{"boss", boss}, {"arch", arch}};
    Get(monster).fields["name"] = Text(name);
    monsters[id] = monster;
}
void Bestiary() {
    monsters.clear();
    Monster(2270, "Piradain le Pingre", false, true);  // soul 34004 (328)
    Monster(799, "Sampi l'Eternel", true, false);      // soul 33681 (327)
    Monster(797, "Mob l'Éponge", true, false);         // soul 33690 (327)
    Monster(147, "Abrakroc l'édenté", true, false);    // soul 33684 (327)
    Monster(3416, "Comte Harebourg", true, false);     // room souls 33747 to 33749 (327)
    Monster(2999, "Tofumanchou l’Empereur", false, true); // a soul newer than the table
}
void Sink(metamob::Capture capture) {
    if (sinkThrows) { sinkThrows = false; throw std::runtime_error("Injected sink failure"); }
    captured.push_back(std::move(capture));
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char* image, const char* space, const char* name) const {
    const auto found = fake::classes.find(name);
    fake::Check(found != fake::classes.end() && found->second->image == image && found->second->space == space,
                "Capture class requested from the wrong image or namespace");
    return found->second.get();
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    auto& type = *static_cast<fake::Class*>(klass);
    const auto key = type.name + "." + name;
    static const std::map<std::string, std::pair<int, uintptr_t>> expected{
        {"esg.zcx", {0, 0x18BB140}}, {"mc.myn", {0, 0xF20D40}}, {"mc.myo", {0, 0x861E70}}, {"mc.mzb", {0, 0x2AAC720}},
        {"mc.myx", {0, 0x2AAC6B0}}, {"MonsterData.GetById", {1, 0x7E76B0}}, {"MonsterData.get_isBoss", {0, 0x7C43C0}},
        {"MonsterData.get_isMiniBoss", {0, 0x7C43B0}}, {"MonsterData.get_name", {0, 0x7E7A30}}};
    if (key == fake::fail) throw std::runtime_error("Verified method unavailable");
    const auto found = expected.find(key);
    fake::Check(found != expected.end() && found->second == std::pair(argc, rva), "Capture native outside the pinned contract");
    // The real lookup ends in NativeAt, which refuses an RVA without a 16-byte guard.
    fake::Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& guard) { return guard.rva == rva; }),
                "Capture native without a signature guard");
    fake::guarded.insert(rva);
    ++fake::verifications;
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{&type, name, argc});
    const auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    using namespace fake;
    const auto& m = *static_cast<const MethodInfo*>(method);
    const std::vector<void*> args(values);
    Check(static_cast<int>(args.size()) == m.argc, "Wrong capture method arity");
    if (m.name == fail) throw std::runtime_error("Injected native failure");
    if (m.name == "GetById") {
        Check(self == nullptr, "MonsterData.GetById is static");
        const auto found = monsters.find(*static_cast<int64_t*>(args.at(0)));
        return found == monsters.end() ? nullptr : found->second;
    }
    auto& object = Get(self);
    Check(api.class_is_assignable_from(m.klass, object.klass), "Capture method called on a foreign object");
    if (m.name == "zcx") {
        // Native [esg+0x128]+0x10: a null character is a NullReferenceException.
        ++zcxCalls;
        const auto info = object.fields.at("dpzj");
        if (!info) throw std::runtime_error("NullReferenceException");
        return Box(Get(Get(info).fields.at("dbrl")).scalar);
    }
    if (m.name == "myn") return Box(object.numbers.at("gid"));
    if (m.name == "myo") return Box(object.numbers.at("quantity"));
    if (m.name == "mzb") return Box(object.numbers.at("type"));
    if (m.name == "myx") return object.fields.at("data");
    if (m.name == "get_isBoss") return Box(object.numbers.at("boss"));
    if (m.name == "get_isMiniBoss") return Box(object.numbers.at("arch"));
    if (m.name == "get_name") return object.fields.at("name");
    throw std::runtime_error("Unexpected capture method: " + m.name);
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> values) const {
    using namespace fake;
    const std::string call = name;
    const std::vector<void*> args(values);
    if (call == fail) throw std::runtime_error("Injected managed failure");
    auto& object = Get(self);
    Check(object.klass->name == "List", "Managed call outside a list");
    if (call == "get_Count" && args.empty()) return Box(static_cast<int64_t>(object.items.size()));
    if (call == "get_Item" && args.size() == 1) return object.items.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected capture call: " + call);
}
Obj Api::Field(Obj self, const char* name) const {
    if (!self) return nullptr;
    if (fake::fail == name) throw std::runtime_error("Injected field failure");
    const auto& fields = fake::Get(self).fields;
    const auto found = fields.find(name);
    if (found == fields.end()) throw std::runtime_error(std::string("Field unavailable: ") + name);
    return found->second;
}
std::string Api::String(Obj self) const {
    if (!self) return {};
    fake::Check(fake::Get(self).klass->name == "String", "Text read from a non-string object");
    return fake::Get(self).text;
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

namespace {
using namespace fake;
void Tick(int times = 1) {
    for (int i = 0; i < times; ++i) {
        try { TickCaptureDetect(); }
        catch (...) { throw std::runtime_error("An exception escaped TickCaptureDetect towards Module::Pump"); }
    }
}
size_t Lines(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
bool Logged(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
std::vector<int64_t> Monsters() { std::vector<int64_t> ids; for (const auto& capture : captured) ids.push_back(capture.monsterId); return ids; }
// A new scenario: nothing remembered, sync on, default bestiary, fresh logs.
void Fresh() {
    RestoreCaptureDetect();
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    logs.clear(); captured.clear(); fail.clear(); sinkThrows = false; zcxCalls = 0;
    module->settings.metamobSync = true; module->fight.reset();
    Bestiary();
}
void InstallContract() {
    // A moved field or an unverified native disables this feature only, without throwing.
    fail = "fzf.dwqq"; InstallCaptureDetect(Sink);
    Check(Logged("CAPTURE_DETECT_DISABLED stage=fight"), "A moved report field disables capture detection");
    fail = "MonsterData.get_name"; InstallCaptureDetect(Sink);
    Check(Logged("CAPTURE_DETECT_DISABLED stage=monster"), "An unverified monster name getter disables capture detection");
    fail.clear(); Bestiary();
    World world; world.Show(Capture()); lookups = 0; Tick(2);
    Check(lookups == 0 && captured.empty(), "A disabled detector reads nothing");
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    logs.clear(); layouts.clear(); guarded.clear(); verifications = 0;
    InstallCaptureDetect(Sink);
    Check(logs.empty(), "The pinned client contract is accepted");
    Check(layouts == std::set<std::string>{"exf.drih", "fxc.<dwaq>k__BackingField", "fzf.dwqq", "fyz.dwou", "fze.dwpw", "fze.dwpy",
        "fze.dwqb", "fzg.dwqv", "esg.dpzj", "z.dbrm", "ItemData.possibleEffects", "EffectInstanceData.actionId", "EffectInstanceInteger.value"},
        "Every field read by name has its pinned offset checked");
    Check(guarded == std::set<uintptr_t>{0x18BB140, 0xF20D40, 0x861E70, 0x2AAC720, 0x2AAC6B0, 0x7E76B0, 0x7C43C0, 0x7C43B0, 0x7E7A30},
        "Exactly the soul-inventory item accessors, esg.zcx and MonsterData, each behind a signature guard");
    InstallCaptureDetect(Sink);
    Check(verifications == 9 && logs.empty(), "A second installation only replaces the sink");
}
void BaselineAtStart() {
    Fresh(); World world;
    world.Show(Capture()); // a fight that ended before the mod started
    Tick(3);
    Check(captured.empty() && !Lines("CAPTURE_REPORT"), "The report present at start is the baseline, never processed");
    world.Show(Capture()); Tick();
    Check(captured.size() == 1, "A new report is processed");
    const auto& capture = captured[0];
    Check(capture.characterId == 1001 && capture.characterName == "Testeur" && capture.monsterId == 2270 &&
          capture.name == "Piradain le Pingre" && capture.type == metamob::TypeArchimonster && capture.quantity == 1,
          "The capture carries the character, the game monster, its French name, its kind and the quantity");
    Check(logs == std::vector<std::string>{"CAPTURE_REPORT entries=1 local=1 loot=1 souls=1 winner=1",
        "CAPTURE_SOUL monster=2270 kind=arch qty=1"}, "One report line, then one line per soul");
    Tick(5);
    Check(captured.size() == 1 && Lines("CAPTURE_REPORT") == 1, "A report is processed once");
}
void OnlyThisCharacter() {
    Fresh(); World world; Tick();
    // Every client receives the whole party's loot: allies' souls are their clients' captures.
    world.Show(Report({Fighter(2002, {Soul(34004, 2270)}), Fighter(1001, {Junk()}), Fighter(3003, {Soul(33681, 799, 327)})}));
    Tick();
    Check(captured.empty() && Logged("CAPTURE_REPORT entries=3 local=1 loot=1 souls=0 winner=1"), "Only this character's loot counts");
    world.Show(Report({Fighter(2002, {Soul(34004, 2270)}), Fighter(3003, {Soul(33681, 799, 327)})}));
    Tick();
    Check(captured.empty() && Logged("CAPTURE_REPORT entries=2 local=0 loot=0 souls=0 winner=0"), "A watched fight counts nothing");
    // Ally and character both got the archimonster's soul: each client counts its own.
    world.Show(Report({Fighter(2002, {Soul(34004, 2270)}), nullptr, Fighter(1001, {Soul(34004, 2270)}, 1)}));
    Tick();
    Check(captured.size() == 1 && captured[0].characterId == 1001 && Logged("CAPTURE_REPORT entries=3 local=1 loot=1 souls=1 winner=0"),
          "The local soul counts whatever the fight outcome (logged, not required)");
    // Another character on this client: the report shown at the switch is a baseline.
    world.Become(1002, "Seconde"); world.Show(Report({Fighter(1002, {Soul(34004, 2270)})}));
    Tick();
    Check(captured.size() == 1, "A character switch takes a new baseline");
    world.Show(Report({Fighter(1002, {Soul(33681, 799, 327)}), Fighter(1001, {Soul(34004, 2270)})}));
    Tick();
    Check(captured.size() == 2 && captured[1].characterId == 1002 && captured[1].characterName == "Seconde" &&
          captured[1].monsterId == 799 && captured[1].type == metamob::TypeBoss, "The new character's own soul only");
}
void SoulTypesOnly() {
    Fresh(); World world; Tick();
    world.Show(Report({Fighter(1001, {
        Item(9687, 83, 1),                          // empty soul stone
        Item(7010, 85, 1, {Effect(623, 2270)}),     // legacy full stone
        Item(13000, 129, 1, {Effect(4058, 2270)}),  // quest soul of another item type
        Junk(), Make("Other"), nullptr,
        Soul(33681, 799, 327), Soul(34004, 2270, 328)})}));
    Tick();
    Check(Monsters() == std::vector<int64_t>{799, 2270} && captured[0].type == metamob::TypeBoss &&
          captured[1].type == metamob::TypeArchimonster, "Only boss (327) and archimonster (328) souls");
    Check(Logged("CAPTURE_REPORT entries=1 local=1 loot=8 souls=2 winner=1") && Logged("CAPTURE_SOUL monster=799 kind=boss qty=1") &&
          Logged("CAPTURE_SOUL monster=2270 kind=arch qty=1"), "Report and soul lines");
    auto noLoot = Fighter(1001, {}); Get(noLoot).fields["dwqb"] = nullptr;
    world.Show(Report({noLoot})); Tick();
    Check(captured.size() == 2 && Logged("CAPTURE_REPORT entries=1 local=1 loot=0 souls=0 winner=1"), "A fighter without loot");
}
void MonsterIdentity() {
    Fresh(); World world; Tick();
    auto noData = Item(33681, 327, 1); Get(noData).fields["data"] = nullptr;
    world.Show(Report({Fighter(1001, {
        Soul(36000, 2999),                                            // not in the pinned table: the effect alone
        Item(33684, 327, 1),                                          // no effect 4058: table
        noData,                                                       // no ItemData: table
        Item(33690, 327, 1, {Effect(4058, 5, "EffectInstanceData")}), // effect without a value: table
        Item(33747, 327, 1, {Effect(4058, 0)}),                       // invalid value: table
        Soul(33821, 2270)})}));                                       // the live effect wins over the table (2279)
    Tick();
    Check(Monsters() == std::vector<int64_t>{2999, 147, 799, 797, 3416, 2270}, "Monster from the soul's effect 4058, else from the soul table");
    Check(captured[0].name == "Tofumanchou l’Empereur" && captured[1].name == "Abrakroc l'édenté", "French names passed through unchanged");
    // Real 3.7.12.12 dungeon-room souls list every boss of their room, their own
    // first (the table's): only that one counts. A soul unknown to both: never guessed.
    for (const auto& [id, name] : std::vector<std::pair<int64_t, std::string>>{{1087, "Tynril Ahuri"}, {1072, "Tynril Consterné"},
            {1085, "Tynril Déconcerté"}, {1086, "Tynril Perfide"}, {943, "Sphincter Cell"}, {940, "Rat Blanc"}, {939, "Rat Noir"},
            {827, "Minotot"}, {121, "Minotoror"}, {36001, "Gardien"}, {36002, "Garde"}})
        Monster(id, name, true, false);
    world.Show(Report({Fighter(1001, {
        Item(33746, 327, 1, {Effect(4057, 0), Effect(4058, 1087), Effect(4058, 1072), Effect(4058, 1085), Effect(4058, 1086)}),
        Item(33753, 327, 1, {Effect(4057, 0), Effect(4058, 943), Effect(4058, 940), Effect(4058, 939)}),
        Item(33756, 327, 1, {Effect(4057, 0), Effect(4058, 827), Effect(4058, 121)}),
        Item(39999, 328, 1)})}));
    Tick();
    Check(Monsters() == std::vector<int64_t>{2999, 147, 799, 797, 3416, 2270, 1087, 943, 827} &&
          Logged("CAPTURE_SOUL monster=1087 kind=boss qty=1") && Lines("CAPTURE_SOUL_SKIPPED") == 1 &&
          Logged("CAPTURE_SOUL_SKIPPED gid=39999 reason=unknown"), "A room soul counts its own monster only; an unknown soul is skipped");
    // Whatever the order: the table's monster when listed, else the first effect.
    world.Show(Report({Fighter(1001, {
        Item(33756, 327, 1, {Effect(4058, 121), Effect(4058, 827)}),          // the table's monster listed second
        Item(33760, 327, 1, {Effect(4058, 36002), Effect(4058, 36001)}),      // the table's (3234) not listed: the first
        Item(36010, 327, 1, {Effect(4058, 36001), Effect(4058, 36002)})})})); // not in the table: the first
    Tick();
    Check(Monsters() == std::vector<int64_t>{2999, 147, 799, 797, 3416, 2270, 1087, 943, 827, 827, 36002, 36001} &&
          Lines("CAPTURE_SOUL_SKIPPED") == 1, "A room soul holds the table's monster when listed, else its first effect's");
    for (const char* member : {"possibleEffects", "actionId", "myx"}) {
        fail = member; world.Show(Report({Fighter(1001, {Item(33684, 327, 1, {Effect(4058, 147)})})})); Tick(); fail.clear();
        Check(captured.back().monsterId == 147 && Logged("CAPTURE_DETECT_ERROR stage=effects"), "An unreadable effect falls back to the table");
    }
    Check(captured.size() == 15 && Lines("CAPTURE_DETECT_ERROR stage=effects") == 1, "The effect error is logged at most every 10 s");
}
void KindChecks() {
    Fresh(); World world; Tick();
    Monster(5001, "Gardien", true, false); Monster(5002, "Archi", false, true); Monster(5003, "", false, true);
    Monster(5004, std::string("Nom\x01") + "cassé", false, true); Monster(5005, "Autre", false, true, "Other");
    Monster(5006, std::string(257, 'a'), false, true);
    world.Show(Report({Fighter(1001, {
        Item(40001, 327, 1, {Effect(4058, 5002)}),  // boss soul of an archimonster
        Item(40002, 328, 1, {Effect(4058, 5001)}),  // archimonster soul of a boss
        Item(40003, 328, 1, {Effect(4058, 5999)}),  // unknown to MonsterData
        Item(40004, 328, 1, {Effect(4058, 5005)}),  // not a MonsterData
        Item(40005, 328, 1, {Effect(4058, 5003)}),  // no name
        Item(40006, 328, 1, {Effect(4058, 5004)}),  // control character in the name
        Item(40009, 328, 1, {Effect(4058, 5006)}),  // name beyond 256 bytes
        Item(40007, 327, 1, {Effect(4058, 5001)}),
        Item(40008, 328, 1, {Effect(4058, 5002)})})}));
    Tick();
    Check(Monsters() == std::vector<int64_t>{5001, 5002} && captured[0].type == metamob::TypeBoss &&
          captured[1].type == metamob::TypeArchimonster, "Boss souls need isBoss, archimonster souls isMiniBoss");
    for (const auto& [gid, reason] : std::vector<std::pair<int, std::string>>{{40001, "kind"}, {40002, "kind"}, {40003, "monster"},
            {40004, "monster"}, {40005, "name"}, {40006, "name"}, {40009, "name"}})
        Check(Logged("CAPTURE_SOUL_SKIPPED gid=" + std::to_string(gid) + " reason=" + reason), "Each skipped soul is logged with its reason");
}
void DisabledSync() {
    Fresh(); World world; module->settings.metamobSync = false;
    Tick(); world.Show(Capture()); Tick(3);
    Check(captured.empty() && logs.empty(), "Sync off: nothing processed, nothing logged");
    module->settings.metamobSync = true; Tick(2);
    Check(captured.empty() && logs.empty(), "A fight that ended while the sync was off never counts");
    world.Show(Capture()); Tick();
    Check(captured.size() == 1, "The next capture after enabling counts");
    // Reads failing while the sync is off: silent, and the baseline is retaken.
    module->settings.metamobSync = false; fail = "dwqq"; world.Show(Capture()); Tick(2);
    fail.clear(); module->settings.metamobSync = true; Tick();
    Check(captured.size() == 1 && !Lines("CAPTURE_DETECT_ERROR"), "No report from a sync-off period, even after failed reads");
    module->settings.metamobSync = false; services.erase("fng"); world.Show(Capture()); Tick();
    services["fng"] = world.character; module->settings.metamobSync = true; Tick();
    Check(captured.size() == 1, "Nor from a sync-off period without a character");
    world.Show(Capture()); Tick();
    Check(captured.size() == 2, "Captures count again");
}
void Quantities() {
    Fresh(); World world; Tick();
    world.Show(Report({Fighter(1001, {
        Soul(40010, 2270, 328, 0), Soul(40011, 2270, 328, -1), Soul(40012, 2270, 328, 31),
        Soul(40013, 2270, 328, 30), Soul(40014, 2270, 328, 2),
        Soul(40015, 2270, 328, 1), Soul(40015, 2270, 328, 2),     // one gid twice: queued once, 3
        Soul(40016, 2270, 328, 20), Soul(40016, 2270, 328, 15),   // merged beyond 30
        Soul(40017, 2270, 328, 0), Soul(40017, 2270, 328, 1)})}));
    Tick();
    std::vector<int> quantities;
    for (const auto& capture : captured) quantities.push_back(capture.quantity);
    Check(quantities == std::vector<int>{30, 2, 3}, "Quantities 1 to 30 only, once per gid");
    for (int gid : {40010, 40011, 40012, 40016, 40017})
        Check(Logged("CAPTURE_SOUL_SKIPPED gid=" + std::to_string(gid) + " reason=quantity"), "Out-of-bounds quantities are skipped");
    // Two soul items of one monster (real Comte Harebourg room souls): one capture
    // each (the sync groups by monster).
    world.Show(Report({Fighter(1001, {Item(33747, 327, 1, {Effect(4057, 0), Effect(4058, 3416), Effect(4058, 3384)}),
        Item(33748, 327, 2, {Effect(4057, 0), Effect(4058, 3416), Effect(4058, 3391)})})})); Tick();
    Check(captured.size() == 5 && captured[3].monsterId == 3416 && captured[3].quantity == 1 && captured[4].monsterId == 3416 &&
          captured[4].quantity == 2, "One capture per soul item");
}
void Bounds() {
    Fresh(); World world; Tick();
    std::vector<Obj> crowd;
    for (int i = 0; i < 127; ++i) crowd.push_back(Fighter(5000 + i, {}));
    crowd.push_back(Fighter(1001, {Soul(34004, 2270)}));
    world.Show(Report(crowd)); Tick();
    Check(captured.size() == 1 && Logged("CAPTURE_REPORT entries=128 local=1 loot=1 souls=1 winner=1"), "128 fighters are read");
    crowd.insert(crowd.begin(), Fighter(4999, {}));
    world.Show(Report(crowd)); Tick();
    Check(captured.size() == 1 && Logged("CAPTURE_DETECT_ERROR stage=report"), "129 fighters are refused");
    Fresh(); World next; Tick();
    std::vector<Obj> loot(511);
    for (auto& item : loot) item = Junk();
    loot.push_back(Soul(34004, 2270));
    next.Show(Report({Fighter(1001, loot)})); Tick();
    Check(captured.size() == 1 && Logged("CAPTURE_REPORT entries=1 local=1 loot=512 souls=1 winner=1"), "512 loot items are read");
    loot.push_back(Junk());
    next.Show(Report({Fighter(1001, loot)})); Tick();
    Check(captured.size() == 1 && Logged("CAPTURE_DETECT_ERROR stage=report"), "513 loot items are refused");
}
void Failures() {
    // A failing read before the report is known: logged once, kept, processed once reads work.
    for (const char* name : {"FindService:fsl", "FindService:fsl:int", "drih", "<dwaq>k__BackingField", "dwqq", "FindService:fng", "dpzj", "zcx"}) {
        Fresh(); World world; Tick();
        world.Show(Capture()); fail = name; Tick(3);
        Check(captured.empty() && Lines("CAPTURE_DETECT_ERROR stage=read") == 1 && logs.size() == 1, "A failing read is one fixed line, never thrown");
        fail.clear(); Tick(3);
        Check(captured.size() == 1, "The report is processed once reads work again");
    }
    // A failure inside a report: remembered first, so it is never processed twice.
    for (const char* name : {"dwou", "get_Count", "get_Item", "dwpw", "dwpy", "dwqb", "dwqv", "mzb", "myn", "myo"}) {
        Fresh(); World world; Tick();
        world.Show(Capture()); fail = name; Tick(); fail.clear(); Tick(3);
        Check(captured.empty() && logs == std::vector<std::string>{"CAPTURE_DETECT_ERROR stage=report"}, "A failed report is dropped, never reprocessed");
        world.Show(Capture()); Tick();
        Check(captured.size() == 1, "The next report counts");
    }
    // A failure on one soul skips that soul only.
    for (const auto& [name, kept] : std::vector<std::pair<std::string, int>>{{"GetById", 0}, {"get_isMiniBoss", 1}, {"get_name", 0}}) {
        Fresh(); World world; Tick();
        world.Show(Report({Fighter(1001, {Soul(34004, 2270), Soul(33681, 799, 327)})}));
        fail = name; Tick(); fail.clear(); Tick(2);
        Check(static_cast<int>(captured.size()) == kept && Logged("CAPTURE_SOUL_SKIPPED gid=34004 reason=error"), "A soul that cannot be read is skipped");
        Check(kept == 0 || captured[0].monsterId == 799, "The other soul still counts");
    }
    Fresh(); { World world; Tick();
        world.Show(Report({Fighter(1001, {Soul(34004, 2270), Soul(33681, 799, 327)})})); sinkThrows = true; Tick();
        Check(Monsters() == std::vector<int64_t>{799} && Logged("CAPTURE_DETECT_ERROR stage=sink"), "A failing sink loses that capture only");
        fail = "dbrm"; world.Show(Capture()); Tick(); fail.clear();
        Check(captured.size() == 2 && captured[1].characterName.empty() && Logged("CAPTURE_DETECT_ERROR stage=name"),
              "An unreadable character name leaves it empty");
        world.Show(Make("Other")); Tick();
        Check(captured.size() == 2 && Logged("CAPTURE_DETECT_ERROR stage=read"), "An unexpected report object is refused");
        world.Show(Report({Make("Other"), Fighter(1001, {Soul(34004, 2270)})})); Tick();
        auto strange = Fighter(1001, {Soul(34004, 2270)}); Get(strange).fields["dwqb"] = Make("Other");
        world.Show(Report({strange})); Tick();
        Check(captured.size() == 2 && Lines("CAPTURE_DETECT_ERROR stage=report") == 1, "Unexpected fighter or loot objects drop the report");
    }
}
void ReportOwner() {
    Fresh(); World world; Tick();
    // The game rebuilt its report owner (fzf): its current report is a baseline.
    auto rebuilt = Make("fzf"); Get(rebuilt).fields["dwqq"] = Capture();
    Get(world.fight).fields["<dwaq>k__BackingField"] = rebuilt; Tick();
    Check(captured.empty(), "A new report owner takes a new baseline");
    Get(rebuilt).fields["dwqq"] = Capture(); Tick();
    Check(captured.size() == 1, "Its next report counts");
    const auto first = Get(rebuilt).fields["dwqq"];
    Get(rebuilt).fields["dwqq"] = Report({Fighter(1001, {Soul(33681, 799, 327)})}); Tick();
    Get(rebuilt).fields["dwqq"] = first; Tick(2);
    Check(Monsters() == std::vector<int64_t>{2270, 799}, "A report shown again is never processed twice");
    // The fight service is resolved on every tick; a stale module->fight is reported once.
    module->fight.reset(world.fight); Tick();
    Check(!Lines("CAPTURE_FIGHT_CHANGED"), "Same fight service");
    auto other = Make("fxc"), otherOwner = Make("fzf");
    Get(other).fields["<dwaq>k__BackingField"] = otherOwner; Get(otherOwner).fields["dwqq"] = nullptr;
    Get(world.service).fields["drih"] = other; Tick(3);
    Check(Lines("CAPTURE_FIGHT_CHANGED") == 1 && captured.size() == 2, "A replaced fight service is logged once and read");
    Get(otherOwner).fields["dwqq"] = Capture(); Tick();
    Check(captured.size() == 3, "Captures continue on the new fight service");
}
void CharacterNames() {
    Fresh(); World world(1001, "Élodie-Brume"); Tick();
    world.Show(Capture()); Tick();
    Check(captured.back().characterName == "Élodie-Brume", "The character name picks the quest");
    for (const auto& name : {std::string("Deux\nlignes"), std::string(129, 'a')}) {
        Get(world.info).fields["dbrm"] = Text(name); world.Show(Capture()); Tick();
        Check(captured.back().characterName.empty(), "An unusable character name is left empty");
    }
    Get(world.info).fields["dbrm"] = nullptr; world.Show(Capture()); Tick();
    Check(captured.size() == 4 && captured.back().characterName.empty() && !Lines("CAPTURE_DETECT_ERROR"), "A missing name is empty");
}
void ModuleStates() {
    Fresh(); World world; Tick();
    world.Show(Capture()); lookups = 0;
    module->stopRequested = true; Tick(); module->stopRequested = false;
    module->stopped = true; Tick(); module->stopped = false;
    module->servicesReady = false; Tick(); module->servicesReady = true;
    module->ready = false; Tick(); module->ready = true;
    const auto owner = module; module = nullptr; Tick(); module = owner;
    Check(lookups == 0 && captured.empty(), "Nothing is read unless the module runs");
    Tick();
    Check(captured.size() == 1, "The report is processed once the module runs");
    // No character yet (esg.dpzj null): the native local id getter is never called.
    Get(world.character).fields["dpzj"] = nullptr; world.Show(Capture()); zcxCalls = 0; Tick(3);
    Check(zcxCalls == 0 && captured.size() == 1 && logs.size() == 2, "No character: nothing read, nothing logged");
    services.erase("fsl"); Tick(); services["fsl"] = world.service;
    Get(world.character).fields["dpzj"] = world.info; Tick();
    Check(captured.size() == 2, "The same character's report shown meanwhile is processed once readable");
}
void SessionAndRestore() {
    Fresh(); World world; Tick();
    world.Show(Capture()); module->compositor.reset(Make("Compositor")); Tick();
    Check(captured.empty(), "A new session takes a new baseline");
    world.Show(Capture()); Tick();
    Check(captured.size() == 1, "Captures count in the new session");
    RestoreCaptureDetect();
    Check(handles.size() == 1, "Restore releases every reference it held (only the module's compositor remains)");
    world.Show(Capture()); Tick();
    Check(captured.size() == 1, "After a restore the shown report is a baseline");
}
void FixedCodeLogs() {
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    const std::regex format(R"(^(CAPTURE_REPORT entries=\d+ local=\d+ loot=\d+ souls=\d+ winner=[01])"
        R"(|CAPTURE_SOUL monster=\d+ kind=(boss|arch) qty=\d+|CAPTURE_SOUL_SKIPPED gid=\d+ reason=(quantity|unknown|monster|kind|name|error))"
        R"(|CAPTURE_DETECT_ERROR stage=(read|report|effects|name|sink)|CAPTURE_DETECT_DISABLED stage=(fight|monster)|CAPTURE_FIGHT_CHANGED)$)");
    Check(earlier.size() > 50, "The scenarios produced logs");
    for (const auto& line : earlier) {
        Check(std::regex_match(line, format), "Every log line is a fixed code with numbers");
        for (const char* text : {"Testeur", "Seconde", "Brume", "Piradain", "Sampi", "Harebourg", "Tynril", "Injected", "Exception", "unavailable"})
            Check(line.find(text) == std::string::npos, "No name or exception text in the log");
    }
}
}

int main() {
    using namespace fake;
    try {
        Initialize();
        Module owner{"offline-capture"};
        module = &owner;
        owner.ready = true; owner.servicesReady = true; owner.settings.metamobSync = true;
        owner.compositor.reset(Make("Compositor"));
        InstallContract();
        BaselineAtStart();
        OnlyThisCharacter();
        SoulTypesOnly();
        MonsterIdentity();
        KindChecks();
        DisabledSync();
        Quantities();
        Bounds();
        Failures();
        ReportOwner();
        CharacterNames();
        ModuleStates();
        SessionAndRestore();
        FixedCodeLogs();
        RestoreCaptureDetect();
        owner.compositor.reset(); owner.fight.reset(); module = nullptr;
        Check(handles.empty(), "Capture detection leaked GC references");
        std::cout << "PASS: fight-report baseline and identity, local character only, 327/328 souls, effect 4058 then table, "
                     "dungeon-room souls, boss/archimonster check, sync off, quantity bounds, contained failures, fixed-code logs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
