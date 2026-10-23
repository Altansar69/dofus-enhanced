// The Roublards' secret passage: its rules, its learning and file, the mod's run
// through Bad Aboum on a scripted game, the passive learning from the player's
// own trips, and the native NPC menu and dialog contracts on a fake runtime.
#include "travel/roublard-passage.cpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <iterator>
#include <set>
#include "core/signatures.hpp"

namespace fake {
using namespace managed;
struct Node {
    std::array<unsigned char, 256> raw{};
    std::string type;
    std::map<std::string, Obj> fields;
    std::map<std::string, int64_t> numbers;
    std::vector<Obj> items;
    std::map<int64_t, Obj> entries;
};
struct MethodData { std::string type, name; int argc = 0; uintptr_t rva = 0; };
std::vector<std::unique_ptr<Node>> nodes;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<Obj, Node*> objects;
std::map<std::string, std::unique_ptr<std::string>> classes;
std::map<GCHandle, Obj> handles;
GCHandle serial = 1;
std::map<std::string, Obj> services;
std::map<std::string, size_t> offsets;
std::map<int64_t, Obj> mapInfos;
std::vector<std::string> logs, verified;
std::string refuse;
bool combat = false;
struct Spoken { Obj view = nullptr; int entity = 0, action = 0; };
std::vector<Spoken> spoken;
struct Built { Obj data = nullptr, bus = nullptr, who = nullptr, npcs = nullptr; int entity = 0; bool rightClick = true; };
std::vector<Built> built;
std::vector<int> selected;
int closes = 0;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Obj Make(const std::string& type) {
    auto node = std::make_unique<Node>();
    node->type = type;
    Obj pointer = node->raw.data();
    objects[pointer] = node.get();
    nodes.push_back(std::move(node));
    return pointer;
}
Node& Get(Obj object) {
    const auto found = objects.find(object);
    if (found == objects.end()) throw std::runtime_error("Unknown fake object");
    return *found->second;
}
template<class T> Obj Box(T value) { auto box = Make("Box"); std::memcpy(box, &value, sizeof(value)); return box; }
bool Logged(const std::string& text) {
    return std::any_of(logs.begin(), logs.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
}
Obj Invoke(Method method, Obj self, std::initializer_list<void*> args) {
    const auto& m = *static_cast<const MethodData*>(method);
    std::vector<void*> a(args);
    const auto& name = m.name;
    if (name == "GetMapInformationById") {
        const auto map = *static_cast<int64_t*>(a[0]);
        return mapInfos.count(map) ? mapInfos[map] : nullptr;
    }
    auto& node = Get(self);
    if (name == "get_Count") return Box<int>(static_cast<int>(node.items.size()));
    if (name == "get_Item") return node.items.at(static_cast<size_t>(*static_cast<int*>(a[0])));
    if (name == "baml") { Check(node.type == "evv", "The current map's entity registry"); return node.fields["entities"]; }
    if (name == "bamm") {
        const auto id = *static_cast<int64_t*>(a[0]);
        Obj found = nullptr;
        for (auto entity : Get(node.fields["entities"]).items)
            if (Get(entity).type == "EntityInfo" && Get(entity).numbers["id"] == id) found = entity;
        *static_cast<Obj*>(a[1]) = found;
        return Box<bool>(found != nullptr);
    }
    if (name == "baks") {
        Check(*static_cast<int*>(a[0]) == 15, "The NPC id is the entity's integer property 15");
        const bool has = node.numbers.count("npc") > 0;
        if (has) *static_cast<int*>(a[1]) = static_cast<int>(node.numbers["npc"]);
        return Box<bool>(has);
    }
    if (name == "bajh") return Box<int64_t>(node.numbers["id"]);
    if (name == ".ctor" && m.type == "NpcMenuMaker") {
        Check(a.size() == 6, "NpcMenuMaker(EntityInfo, Int32, Boolean, fmz, fng, fsq)");
        Built made;
        made.data = static_cast<Obj>(a[0]); made.entity = *static_cast<int*>(a[1]); made.rightClick = *static_cast<bool*>(a[2]);
        made.bus = static_cast<Obj>(a[3]); made.who = static_cast<Obj>(a[4]); made.npcs = static_cast<Obj>(a[5]);
        built.push_back(made);
        node.fields["m_messageBus"] = made.bus;
        node.numbers["m_entityId"] = made.entity;
        return nullptr;
    }
    if (name == "OnNPCMenuClick") {
        Check(node.type == "NpcMenuMaker" && a.size() == 3, "The NPC menu row's click");
        spoken.push_back({static_cast<Obj>(a[0]), *static_cast<int*>(a[1]), *static_cast<int*>(a[2])});
        return nullptr;
    }
    if (name == "TryGetValue") {
        Check(*static_cast<int*>(a[0]) == 0, "The NPC dialog's UI key");
        const auto found = node.entries.find(0);
        *static_cast<Obj*>(a[1]) = found == node.entries.end() ? nullptr : found->second;
        return Box<bool>(found != node.entries.end());
    }
    if (name == "SelectReply") {
        Check(node.type == "NpcDialog", "The dialog's own reply click");
        selected.push_back(*static_cast<int*>(a[0]));
        node.numbers["m_lockAndWaitAnswers"] = 1;
        return nullptr;
    }
    if (name == "OnClose") { Check(node.type == "NpcDialog", "The dialog's own close"); ++closes; return nullptr; }
    if (name == "zcx") return Box<int64_t>(node.numbers["characterId"]);
    throw std::runtime_error("Unexpected call " + m.type + "." + name);
}
void Initialize() {
    api.object_get_class = [](Obj object) -> Klass { return api.Class("", "", Get(object).type.c_str()); };
    api.object_new = [](Klass type) -> Obj { return Make(*static_cast<std::string*>(type)); };
    api.object_unbox = [](Obj object) -> void* { return object; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { const auto id = serial++; handles[id] = object; return id; };
    api.gchandle_get_target = [](GCHandle id) -> Obj { return handles.at(id); };
    api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "Lost GC root"); };
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        const auto found = offsets.find(*static_cast<std::string*>(type) + "." + name);
        return found == offsets.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
}
void Layout() {
    offsets = {{"NpcMenuMaker.m_entityId", 0x14}, {"NpcMenuMaker.m_messageBus", 0x18}, {"ghd.eabq", 0x48},
        {"NpcDialog.m_npc", 0x1D0}, {"NpcDialog.m_currentMsg", 0x1E0}, {"NpcDialog.m_repliesIdFromBtn", 0x1E8},
        {"NpcDialog.m_lockAndWaitAnswers", 0x1F0}, {"NpcDialog.m_canClose", 0x244}, {"NpcDialog.m_npcEntityId", 0x290},
        {"NpcData.id", 0x10}};
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const {
    auto& value = fake::classes[name];
    if (!value) value = std::make_unique<std::string>(name);
    return value.get();
}
Method Api::Find(Klass type, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodData>();
    method->type = *static_cast<std::string*>(type); method->name = name; method->argc = argc;
    auto pointer = method.get();
    fake::methods.push_back(std::move(method));
    return pointer;
}
Method Api::FindVerified(Klass type, const char* name, int argc, uintptr_t rva) const {
    char text[160];
    std::snprintf(text, sizeof(text), "%s.%s/%d@0x%llX", static_cast<std::string*>(type)->c_str(), name, argc, static_cast<unsigned long long>(rva));
    fake::verified.push_back(text);
    if (fake::refuse == name) throw std::runtime_error(std::string("Verified method unavailable: ") + name);
    auto method = Find(type, name, argc);
    const_cast<fake::MethodData*>(static_cast<const fake::MethodData*>(method))->rva = rva;
    return method;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const { return fake::Invoke(method, self, args); }
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    return Invoke(Find(object_get_class(self), name, static_cast<int>(args.size())), self, args);
}
Obj Api::Field(Obj self, const char* name) const { return fake::Get(self).fields[name]; }
void* Api::FieldAddress(Obj self, const char* name) const { return &fake::Get(self).numbers[name]; }
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return fake::combat; }
managed::Obj Module::FindService(const char* name) const {
    const auto found = fake::services.find(name);
    return found == fake::services.end() ? nullptr : found->second;
}
void Log(const std::string& text) { fake::logs.push_back(text); }

namespace {
using namespace roublard_passage;
using fake::Check;
constexpr int64_t Crypt = AstrubCrypt, Well = AmaknaWell, Door = TempleEntrance;
constexpr int64_t TempleRoom = 67112960, Chapel = 192414728, Cemetery = 82314240, Elsewhere = 88212247, Sewer = 101716489;
// A scripted game: a map with Bad Aboum, his questions and teleports. Like the
// native layer, it shows a question only with an entity and a non-empty button.
struct Script final : Game {
    int64_t character = 77, entity = -12, closed = 0;
    std::map<int64_t, std::pair<int, int>> coords{{Crypt, {5, -15}}, {Chapel, {5, -15}}, {Door, {10, 16}}, {TempleRoom, {10, 16}},
        {Cemetery, {10, 16}}, {Well, {3, 3}}, {Elsewhere, {0, 0}}, {Sewer, {6, -12}}};
    int npcs = 1, generation = 0, marked = 0, talks = 0, replies = 0, closes = 0, finds = 0, button = -1, chosen = 0;
    bool talkOk = true, replyOk = true, failFind = false;
    Dialog dialog;  // shown: the dialog UI exists with a list of buttons
    std::function<void()> afterTalk, afterReply;
    int64_t Character() override { return character; }
    bool Locate(int64_t map, int& x, int& y) override {
        const auto found = coords.find(map);
        if (found == coords.end()) return false;
        x = found->second.first; y = found->second.second;
        return true;
    }
    int FindNpc(int npc, int64_t& found) override {
        ++finds;
        if (failFind) throw std::runtime_error("scripted registry failure");
        Check(npc == BadAboum, "Only Bad Aboum is looked for");
        found = npcs > 0 ? entity : 0;
        return npcs;
    }
    void Mark() override { marked = generation; }
    Dialog Read() override {
        auto result = dialog;
        result.shown = dialog.shown && dialog.entity != -1 && std::any_of(dialog.replies.begin(), dialog.replies.end(), [](int id) { return id != 0; });
        result.fresh = dialog.shown && generation != marked;
        return result;
    }
    bool Talk(int64_t target) override {
        ++talks;
        Check(target == entity, "Talk to the entity that was found");
        if (talkOk && afterTalk) afterTalk();
        return talkOk;
    }
    bool Reply(int index, int reply) override {
        ++replies; button = index; chosen = reply;
        Check(index >= 0 && index < static_cast<int>(dialog.replies.size()) && dialog.replies[static_cast<size_t>(index)] == reply,
            "The reply clicked is the one read on that button");
        if (!replyOk) return false;
        dialog.locked = true;
        if (afterReply) afterReply();
        return true;
    }
    bool Close(int64_t target) override { ++closes; closed = target; return true; }
    void Ask(int message, std::vector<int> ids, int npc = BadAboum, bool locked = false) {
        dialog = {};
        dialog.shown = true; dialog.npc = npc; dialog.entity = npc == BadAboum ? entity : -40; dialog.message = message;
        dialog.replies = std::move(ids); dialog.locked = locked;
        ++generation;
    }
    // NpcDialog.Open on the reused dialog: his NPC data and six empty buttons,
    // the previous question's entity, message and lock kept.
    void Open() { dialog.shown = true; dialog.npc = BadAboum; dialog.replies.assign(6, 0); ++generation; }
};
Script* script = nullptr;
// A fresh passage state on a scripted game, learning in memory.
Script& Fresh() {
    static Script instance;
    instance = Script{};
    script = &instance;
    UseGame(&instance);
    ResetKnowledge();
    passage.path.clear(); passage.storeBroken = false; passage.loaded = false; passage.syncs = 0; passage.nextSync = 0;
    fake::logs.clear();
    return instance;
}
uint64_t ticks = 1000000;
Result Tick(int64_t map, bool ready = true, uint64_t elapsed = 200) { ticks += elapsed; return Step(map, ready, ticks); }
bool Has(const std::vector<chat_route::Shortcut>& links, int id, int64_t from, int64_t to) {
    return std::any_of(links.begin(), links.end(), [&](const chat_route::Shortcut& link) {
        return link.id == id && link.from == from && link.to == to && link.cost == PassageCost;
    });
}
bool HasId(const std::vector<chat_route::Shortcut>& links, int id) {
    return std::any_of(links.begin(), links.end(), [&](const chat_route::Shortcut& link) { return link.id == id; });
}
const Knowledge::Character* Who(int64_t id) {
    for (const auto& who : Known().characters) if (who.id == id) return &who;
    return nullptr;
}
Change Made(Change::Kind kind, Side from, Side to, int64_t map, int64_t character = 0, std::vector<int> replies = {}) {
    Change change;
    change.kind = kind; change.from = from; change.to = to; change.map = map; change.character = character; change.replies = std::move(replies);
    return change;
}

void RuleTests() {
    Check(SideAt(5, -15) == Side::Astrub && SideAt(10, 16) == Side::Temple && SideAt(10, 15) == Side::Temple &&
        SideAt(3, 3) == Side::Amakna && SideAt(10, 17) == Side::None && SideAt(-15, 5) == Side::None, "Sides by their coordinates");
    Check(SideOf(67110147, 0, 0) == Side::Temple && SideOf(83101196, 10, 15) == Side::Temple && SideOf(TempleRoom, -1000, -1000) == Side::Temple &&
        SideOf(Elsewhere, 5, -15) == Side::Astrub && SideOf(Elsewhere, 0, 0) == Side::None, "The temple's own maps, then the coordinates");
    Check(QuestionSide(AstrubMessage) == Side::Astrub && QuestionSide(TempleMessage) == Side::Temple && QuestionSide(AmaknaMessage) == Side::Amakna &&
        QuestionSide(30706) == Side::None, "The side of each of his questions");
    Check(LeadsTo(8758, Side::Temple) && LeadsTo(8781, Side::Temple) && LeadsTo(8759, Side::Astrub) && LeadsTo(8782, Side::Astrub) &&
        LeadsTo(8779, Side::Amakna) && LeadsTo(8780, Side::Amakna), "Bad Aboum's travel replies by destination");
    Check(!LeadsTo(8758, Side::Astrub) && !LeadsTo(8759, Side::Temple) && !LeadsTo(8779, Side::Temple) && !LeadsTo(8758, Side::None) &&
        !LeadsTo(37075, Side::Temple) && !LeadsTo(37073, Side::Temple), "Other replies lead nowhere");
    Check(Paid(37073) && !Paid(37075) && !Paid(8758) && !Travel(37073), "The mercenary's 50 kamas visit is paid and never a trip");
    Check(Question(AstrubMessage) && Question(TempleMessage) && Question(AmaknaMessage) && !Question(30706) && !Question(0), "His three questions");
    Side from = Side::None, to = Side::None;
    Check(LinkId(Side::Astrub, Side::Temple) == 101 && LinkSides(101, from, to) && from == Side::Astrub && to == Side::Temple, "astrub>temple is 101");
    Check(LinkSides(LinkId(Side::Temple, Side::Amakna), from, to) && from == Side::Temple && to == Side::Amakna, "Ids round trip");
    for (const int invalid : {-1, 0, 99, 100, 111, 122, 103, 130, 1000}) Check(!LinkSides(invalid, from, to), "Invalid shortcut ids");
    Check(RefusedBit(Side::Astrub, Side::Temple) == 2u && RefusedBit(Side::Temple, Side::Amakna) == 32u && RefusedBit(Side::Amakna, Side::Temple) == 128u &&
        !RefusedBit(Side::Astrub, Side::Astrub) && !RefusedBit(Side::None, Side::Temple), "One refusal bit per trip");
    Check(std::string(SideName(Side::Amakna)) == "amakna" && std::string(SideName(Side::None)) == "none", "Side names");
}
void KnowledgeTests() {
    Knowledge known;
    Check(NpcMap(known, Side::Astrub) == Crypt && NpcMap(known, Side::Amakna) == Well && NpcMap(known, Side::Temple) == 0 &&
        NpcMap(known, Side::None) == 0, "Built-in maps: the Astrub crypt and the Amakna well; the temple side is learned");
    auto links = Links(known, 0);
    Check(links.size() == 4 && Has(links, 101, Crypt, Door) && Has(links, 102, Crypt, Well) && Has(links, 120, Well, Crypt) &&
        Has(links, 121, Well, Door), "Without the temple side: Astrub and Amakna trips, arrivals estimated");
    Check(ArrivalMap(known, Side::Astrub, Side::Astrub) == 0 && ArrivalMap(known, Side::None, Side::Temple) == 0, "No trip to its own side");
    Check(Apply(known, Made(Change::Kind::Npc, Side::Temple, Side::None, TempleRoom)) && !Apply(known, Made(Change::Kind::Npc, Side::Temple, Side::None, TempleRoom)) &&
        NpcMap(known, Side::Temple) == TempleRoom, "His temple map is learned once");
    links = Links(known, 0);
    Check(links.size() == 6 && Has(links, 101, Crypt, TempleRoom) && Has(links, 110, TempleRoom, Crypt) && Has(links, 112, TempleRoom, Well),
        "With the temple side: six trips, and the temple arrival estimated on his map");
    Check(Apply(known, Made(Change::Kind::Arrival, Side::Astrub, Side::Temple, Door)) && !Apply(known, Made(Change::Kind::Arrival, Side::Astrub, Side::Temple, Door)) &&
        ArrivalMap(known, Side::Astrub, Side::Temple) == Door && Has(Links(known, 0), 101, Crypt, Door), "A seen arrival replaces the estimate");
    // Not found once: only a doubt; a second time: no more looks there.
    Check(Apply(known, Made(Change::Kind::Doubt, Side::Astrub, Side::None, Crypt)) && !Apply(known, Made(Change::Kind::Doubt, Side::Astrub, Side::None, Crypt)) &&
        NpcMap(known, Side::Astrub) == Crypt && HasId(Links(known, 0), 101), "A first look in vain keeps his map");
    Check(Apply(known, Made(Change::Kind::Absent, Side::Astrub, Side::None, Crypt)) && !Apply(known, Made(Change::Kind::Absent, Side::Astrub, Side::None, Crypt)) &&
        known.doubtful.empty() && NpcMap(known, Side::Astrub) == 0 && !HasId(Links(known, 0), 101) && !HasId(Links(known, 0), 102),
        "A second look in vain removes it: no trip from Astrub");
    Check(!Apply(known, Made(Change::Kind::Doubt, Side::Astrub, Side::None, Crypt)), "An absent map is no longer doubtful");
    Check(Has(Links(known, 0), 110, TempleRoom, Crypt), "A trip to the Astrub side still arrives there (estimate kept)");
    Check(Apply(known, Made(Change::Kind::Npc, Side::Astrub, Side::None, Crypt)) && NpcMap(known, Side::Astrub) == Crypt && known.absent.empty(),
        "Seen there again: the absence is forgotten");
    Check(Apply(known, Made(Change::Kind::Absent, Side::None, Side::None, TempleRoom)) && known.npc[1] == 0 && NpcMap(known, Side::Temple) == 0,
        "A learned map found empty is forgotten");
    {   // A file merged from two clients may hold a map both learned and found empty.
        Knowledge odd;
        odd.npc[1] = TempleRoom; odd.npc[0] = Chapel; odd.absent = {TempleRoom, Chapel};
        Check(NpcMap(odd, Side::Temple) == 0 && NpcMap(odd, Side::Astrub) == Crypt && !HasId(Links(odd, 0), 110),
            "A map found empty wins over the same map learned: the built-in one, or none");
    }
    // Refusals are kept per trip: what he offers depends on where he is asked.
    Check(Apply(known, Made(Change::Kind::Refused, Side::Astrub, Side::Temple, 0, 7)) && !Apply(known, Made(Change::Kind::Refused, Side::Astrub, Side::Temple, 0, 7)),
        "A refusal is kept per character and trip");
    Check(!HasId(Links(known, 7), 101) && HasId(Links(known, 7), 121) && HasId(Links(known, 7), 102) && HasId(Links(known, 8), 101) &&
        HasId(Links(known, 0), 101), "Only that trip for that character: the temple from Amakna stays planned");
    Check(Apply(known, Made(Change::Kind::Offered, Side::Amakna, Side::None, 0, 7, {8781})) && !HasId(Links(known, 7), 101),
        "An offer asked on another side does not lift that refusal");
    Check(Apply(known, Made(Change::Kind::Offered, Side::Astrub, Side::None, 0, 7, {8758, 30706, -1})) && HasId(Links(known, 7), 101),
        "Offered where it was refused: the refusal is lifted");
    const auto* who = Person(std::as_const(known), 7);
    Check(who && who->offered == std::vector<int>{8758, 8781} && who->refused == 0, "Only travel replies are kept");
    Check(!Apply(known, Made(Change::Kind::Offered, Side::Astrub, Side::None, 0, 9, {30706, -1})) && !Person(std::as_const(known), 9),
        "A question without trips teaches nothing");
    Check(!Apply(known, Made(Change::Kind::Npc, Side::None, Side::None, Crypt)), "No side");
    Check(!Apply(known, Made(Change::Kind::Npc, Side::Astrub, Side::None, 0)), "No map");
    Check(!Apply(known, Made(Change::Kind::Npc, Side::Astrub, Side::None, int64_t(1) << 41)), "Map out of range");
    Check(!Apply(known, Made(Change::Kind::Arrival, Side::Temple, Side::Temple, Door)), "No trip to itself");
    Check(!Apply(known, Made(Change::Kind::Refused, Side::Astrub, Side::None, 0, 7)) && !Apply(known, Made(Change::Kind::Refused, Side::None, Side::Temple, 0, 7)) &&
        !Apply(known, Made(Change::Kind::Refused, Side::Temple, Side::Temple, 0, 7)), "A refusal names a trip");
    Check(!Apply(known, Made(Change::Kind::Refused, Side::Astrub, Side::Temple, 0, 0)), "Unknown character");
    Knowledge bounded;
    for (int64_t map = 1; map <= 70; ++map) {
        Apply(bounded, Made(Change::Kind::Absent, Side::None, Side::None, 1000 + map));
        Apply(bounded, Made(Change::Kind::Doubt, Side::None, Side::None, 5000 + map));
    }
    Check(bounded.absent.size() == 64 && bounded.absent.front() == 1007 && bounded.absent.back() == 1070 &&
        bounded.doubtful.size() == 64 && bounded.doubtful.front() == 5007, "At most 64 absences and 64 doubts, oldest dropped");
    for (int64_t id = 1; id <= 300; ++id) Apply(bounded, Made(Change::Kind::Refused, Side::Astrub, Side::Temple, 0, id));
    Check(bounded.characters.size() == 256 && bounded.characters.front().id == 45, "At most 256 characters, oldest dropped");
}
void StoreTests() {
    const auto root = std::filesystem::temp_directory_path() / ("DofusRoublardPassageTests-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    try {
        const auto settings = root / L"DofusMiniatures-3.7.11.11-settings.ini";
        const auto path = DataPath(settings);
        auto lockPath = path; lockPath += L".lock";
        Check(path == root / L"DofusMiniatures-roublard-passage.json", "The file sits beside the shared settings");
        Check(Read(path) == Knowledge{}, "No file: nothing learned yet");
        const std::vector<Change> first{Made(Change::Kind::Npc, Side::Temple, Side::None, TempleRoom), Made(Change::Kind::Arrival, Side::Astrub, Side::Temple, Door),
            Made(Change::Kind::Offered, Side::Astrub, Side::None, 0, 77, {8779, 8758}), Made(Change::Kind::Absent, Side::None, Side::None, Elsewhere),
            Made(Change::Kind::Doubt, Side::None, Side::None, Chapel)};
        auto merged = Update(path, first);
        Check(Read(path) == merged && merged.npc[1] == TempleRoom && merged.arrival[0][1] == Door && merged.absent == std::vector<int64_t>{Elsewhere} &&
            merged.doubtful == std::vector<int64_t>{Chapel} && merged.characters.size() == 1 && merged.characters[0].offered == std::vector<int>{8758, 8779},
            "Changes are written and read back");
        // Another client learned meanwhile: both learnings survive.
        const std::vector<Change> other{Made(Change::Kind::Refused, Side::Temple, Side::Amakna, 0, 88), Made(Change::Kind::Arrival, Side::Temple, Side::Astrub, Crypt)};
        merged = Update(path, other);
        Check(merged.npc[1] == TempleRoom && merged.arrival[1][0] == Crypt && merged.characters.size() == 2 &&
            merged.characters[1].refused == RefusedBit(Side::Temple, Side::Amakna), "Concurrent learning is merged, not overwritten");
        Check(Update(path, other) == merged, "An already known change leaves the file alone");
        Check(ToJson(merged).dump().find("\"refused\":[\"temple>amakna\"]") != std::string::npos && ToJson(merged).dump().find("\"schema\":1") != std::string::npos,
            "Readable trips in the file");
        for (const auto& entry : std::filesystem::directory_iterator(root))
            Check(entry.path().filename().wstring().find(L".tmp-") == std::wstring::npos, "No temporary file left");
        // In use by another client: refused, never taken for an invalid file.
        {
            ::Handle lock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            Check(lock.value != INVALID_HANDLE_VALUE, "Test lock");
            bool busy = false, invalid = false;
            try { Update(path, first); } catch (const InvalidFile&) { invalid = true; } catch (const std::exception&) { busy = true; }
            Check(busy && !invalid, "A held lock refuses the update as a passing error");
            // The mod starting meanwhile reads it later, and keeps learning in memory.
            fake::logs.clear();
            Install(settings);
            UseGame(script);
            Check(!passage.loaded && !passage.storeBroken && fake::Logged("file=later") && passage.known == Knowledge{}, "Read later, not given up");
            passage.nextSync = ticks + StoreRetryMs;  // the failed try, on this test's clock
            Learn(Making(Change::Kind::Npc, Side::Amakna, Side::None, Well + 1));
            Check(passage.known.npc[2] == Well + 1 && passage.pending.size() == 1 && Shortcuts(ticks).size() > 0 && !passage.loaded,
                "Learned in memory while the file is in use; no new try before the delay");
        }
        Check(Shortcuts(ticks + 31000).size() > 0 && passage.loaded && passage.pending.empty() && passage.known.npc[1] == TempleRoom &&
            Read(path).npc[2] == Well + 1, "Free again: read (with the other client's learning) and written once");
        // A failed cycle waits its delay before the next try; with nothing learned
        // meanwhile, a file in use at the start is still read once it is free.
        {
            ::Handle lock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            Check(lock.value != INVALID_HANDLE_VALUE, "Test lock");
            Install(settings);
            UseGame(script);
            passage.nextSync = 0;
            const auto tries = passage.syncs;
            Check(Shortcuts(ticks).size() > 0 && passage.syncs == tries + 1 && !passage.loaded && passage.pending.empty(), "A try while the file is in use");
            Check(Shortcuts(ticks + StoreRetryMs - 1).size() > 0 && passage.syncs == tries + 1, "No new try before the delay");
            Check(Shortcuts(ticks + StoreRetryMs).size() > 0 && passage.syncs == tries + 2 && !passage.loaded, "A new try after it");
        }
        passage.nextSync = 0;
        Check(Shortcuts(ticks).size() > 0 && passage.loaded && passage.known.npc[1] == TempleRoom && passage.known.npc[2] == Well + 1,
            "Nothing learned meanwhile: read once free");
        // One file cycle per step, however much a step learns.
        {
            auto& game = Fresh();
            passage.path = path; passage.loaded = true;
            game.afterTalk = [&game] { game.Ask(AstrubMessage, {8779, -1}); };
            Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && passage.syncs == 0, "Nothing learned, nothing written");
            Check(Tick(Crypt).outcome == Outcome::Failed && passage.syncs == 1 && passage.pending.empty(), "His map, the offer and the refusal: one write");
            const auto saved = Read(path);
            Check(saved.npc[0] == Crypt && Person(saved, 77) && Person(saved, 77)->refused == RefusedBit(Side::Astrub, Side::Temple), "All of it saved");
            // What could not be written yet goes to the file when the passage stops.
            passage.nextSync = ticks + StoreRetryMs;
            Learn(Making(Change::Kind::Arrival, Side::Amakna, Side::Astrub, Chapel));
            Check(passage.pending.size() == 1, "Waiting");
            Restore();
            Check(passage.pending.empty() && Read(path).arrival[2][0] == Chapel, "Saved when the passage stops");
        }
        // Invalid files are refused, never rewritten.
        const char* broken[] = {
            "not json",
            "{\"schema\":2,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[]}",
            "{\"schema\":1,\"npc\":{\"astrub\":-5,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[{\"from\":\"astrub\",\"to\":\"astrub\",\"map\":5}],\"characters\":[]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[{\"from\":\"astrub\",\"to\":\"mars\",\"map\":5}],\"characters\":[]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[{\"id\":5,\"offered\":[37073],\"refused\":[]}]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[{\"id\":5,\"offered\":[],\"refused\":[\"temple\"]}]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[{\"id\":5,\"offered\":[],\"refused\":[\"astrub>astrub\"]}]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[{\"id\":5,\"offered\":[],\"refused\":[]},{\"id\":5,\"offered\":[],\"refused\":[]}]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0,\"amakna\":0},\"absent\":[],\"arrival\":[],\"characters\":[]}",
            "{\"schema\":1,\"npc\":{\"astrub\":0,\"temple\":0},\"doubtful\":[],\"absent\":[],\"arrival\":[],\"characters\":[]}",
        };
        for (const char* text : broken) {
            { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << text; }
            bool invalid = false;
            try { (void)Read(path); } catch (const InvalidFile&) { invalid = true; }
            Check(invalid, "An invalid passage file is refused as not the mod's");
        }
        fake::logs.clear();
        Install(settings);
        Check(passage.storeBroken && fake::Logged("ROUBLARD_PASSAGE_STORE_ERROR passage file: invalid content"), "A file that is not the mod's: memory only");
        UseGame(script);
        Learn(Making(Change::Kind::Npc, Side::Temple, Side::None, TempleRoom));
        Restore();
        std::string kept;
        { std::ifstream file(path, std::ios::binary); kept.assign(std::istreambuf_iterator<char>(file), {}); }
        Check(kept == broken[std::size(broken) - 1] && passage.known.npc[1] == TempleRoom, "The user's file is kept as it is");
        std::filesystem::remove(path);
        Install(settings);
        Check(!passage.storeBroken && passage.loaded, "A missing file is a fresh start");
        UseGame(nullptr);
        passage.installed = false; passage.native.reset(); passage.path.clear();
    } catch (...) {
        std::error_code ignored; std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}
// The mod's run through Bad Aboum, on the scripted game.
Result Through(Script& game, int64_t arrival, int link = 101, int64_t from = Crypt) {
    Check(Start(link, from, ticks), "The run starts on his map");
    game.afterReply = [&game] { game.dialog.shown = false; };
    Check(Tick(from).outcome == Outcome::Running, "Talk");
    Check(Tick(from).outcome == Outcome::Running, "Reply");
    Check(Tick(arrival, false).outcome == Outcome::Running, "Teleport");
    Check(Tick(arrival).outcome == Outcome::Running, "One settled sample");
    return Tick(arrival);
}
// Talks to him and answers: the run then waits for the teleport.
void Replied(Script& game, std::function<void()> afterReply = {}) {
    game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, 8779, -1}); };
    game.afterReply = std::move(afterReply);
    Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && game.talks == 1, "Spoken to");
    Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 1 && game.chosen == 8758, "Replied");
}
void RunTests() {
    {   // Astrub to the temple: talk, his Astrub question, the temple reply, the arrival.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, 8779, -1}); };
        Check(Shortcuts(ticks).size() == 4 && roublard_passage::Entrance(101) == Crypt && roublard_passage::Entrance(110) == 0 &&
            roublard_passage::Entrance(7) == 0, "Planned from the built-in maps");
        Check(Describe(101) == "d’Astrub au temple Roublard" && Describe(121) == "du village d’Amakna au temple Roublard" &&
            Describe(110) == "du temple Roublard à Astrub" && Describe(102) == "d’Astrub au village d’Amakna" && Describe(3) == "?", "Chat names of the trips");
        Check(Start(101, Crypt, ticks) && Running(), "Started on the crypt");
        Check(Tick(Crypt).outcome == Outcome::Running && game.talks == 1 && game.finds == 1, "He is found and spoken to");
        Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 1 && game.button == 0 && game.chosen == 8758 && game.closes == 0, "The temple reply is clicked");
        Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 1, "Waiting for the teleport, no second click");
        Check(Tick(0, false).outcome == Outcome::Running && Tick(Door, false).outcome == Outcome::Running, "Loading");
        Check(Tick(Door).outcome == Outcome::Running, "One settled sample is not enough");
        const auto result = Tick(Door);
        Check(result.outcome == Outcome::Arrived && result.map == Door && !Running(), "Arrived in the temple");
        Check(Known().arrival[0][1] == Door && Known().npc[0] == Crypt && Who(77) && Who(77)->offered == std::vector<int>{8758, 8779},
            "Arrival, his map and the offered trips are learned");
        Check(Known().npc[1] == Door && fake::Logged("ROUBLARD_PASSAGE_SEEN map=67108864 side=temple") && HasId(Shortcuts(ticks), 110),
            "Waiting at the arrival, he is learned on the temple side: the way back is planned");
        Check(fake::Logged("ROUBLARD_PASSAGE_START trip=astrub>temple map=67372032") && fake::Logged("ROUBLARD_PASSAGE_TALK entity=-12") &&
            fake::Logged("ROUBLARD_PASSAGE_DIALOG message=8722 replies=8758,8779,-1 locked=0") && fake::Logged("ROUBLARD_PASSAGE_REPLY reply=8758 button=0") &&
            fake::Logged("ROUBLARD_PASSAGE_TELEPORTED map=67108864") && fake::Logged("ROUBLARD_PASSAGE_ARRIVED trip=astrub>temple"), "Every step is logged");
    }
    {   // Back from the temple once his temple map is known.
        auto& game = Fresh();
        passage.known.npc[1] = TempleRoom;
        game.afterTalk = [&game] { game.Ask(TempleMessage, {8780, 8759, -1}); };
        const auto result = Through(game, Crypt, 110, TempleRoom);
        Check(result.outcome == Outcome::Arrived && result.map == Crypt && game.button == 1 && game.chosen == 8759 && Known().arrival[1][0] == Crypt, "Temple to Astrub");
    }
    {   // From the Amakna well to the temple.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AmaknaMessage, {8781, 8782, -1}); game.npcs = 0; };
        Check(Through(game, TempleRoom, 121, Well).outcome == Outcome::Arrived && game.button == 0 && Known().arrival[2][1] == TempleRoom, "Amakna to the temple");
        Check(Known().npc[1] == 0 && !HasId(Shortcuts(ticks), 110), "Not at the arrival: his temple map stays unknown");
    }
    {   // The « Temple Roublard » map at [0,0] (no coordinates of its own) is the temple side.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, -1}); };
        const auto result = Through(game, 67110147);
        Check(result.outcome == Outcome::Arrived && Known().arrival[0][1] == 67110147, "Arrival on the temple's [0,0] map");
    }
    {   // Refusals to start.
        auto& game = Fresh();
        Check(!Start(101, Chapel, ticks) && !Start(110, TempleRoom, ticks) && !Start(5, Crypt, ticks) && !Running() && game.finds == 0,
            "Only on his known map, for a known trip");
        Check(Step(Crypt, true, ticks).outcome == Outcome::Aborted, "No run: nothing to step");
        UseGame(nullptr);
        Check(!Start(101, Crypt, ticks) && Shortcuts(ticks).empty(), "Unavailable: no shortcut and no run");
    }
    {   // Bad Aboum absent: a first time only pauses his trips from there; a second time removes his map.
        auto& game = Fresh();
        game.npcs = 0;
        Check(Start(101, Crypt, ticks), "Started");
        for (int i = 0; i < 19; ++i) Check(Tick(Crypt).outcome == Outcome::Running, "He may still be loading");
        auto result = Tick(Crypt, true, 400);
        Check(result.outcome == Outcome::Failed && result.notice == "Bad Aboum est introuvable ; le trajet continue sans le passage secret." &&
            game.talks == 0 && Listed(Known().doubtful, Crypt) && NpcMap(Known(), Side::Astrub) == Crypt, "Absent once: a doubt only");
        game.character = 78;
        Check(!HasId(Shortcuts(ticks), 101) && !HasId(Shortcuts(ticks), 102) && HasId(Shortcuts(ticks), 121), "His trips from there wait, for every character");
        Check(HasId(Shortcuts(ticks + PauseMs), 101), "Then they are planned again");
        Check(fake::Logged("ROUBLARD_PASSAGE_FAILED trip=astrub>temple phase=search map=67372032 reason=npc absent"), "Logged with its phase");
        ticks += PauseMs;
        Check(Start(101, Crypt, ticks), "A later try");
        for (int i = 0; i < 40 && (result = Tick(Crypt)).outcome == Outcome::Running; ++i) {}
        Check(result.outcome == Outcome::Failed && Listed(Known().absent, Crypt) && !Listed(Known().doubtful, Crypt) && NpcMap(Known(), Side::Astrub) == 0 &&
            !HasId(Shortcuts(ticks + 2 * PauseMs), 101), "Absent twice: his map is no longer planned");
    }
    {   // The map's entities never readable: no absence learned from it.
        auto& game = Fresh();
        game.npcs = -1;
        Check(Start(101, Crypt, ticks), "Started");
        for (int i = 0; i < 19; ++i) Check(Tick(Crypt).outcome == Outcome::Running, "Waiting for the registry");
        const auto result = Tick(Crypt, true, 400);
        Check(result.outcome == Outcome::Failed && result.reason == "entity registry unavailable" && Known().doubtful.empty() && Known().absent.empty() &&
            result.notice == "Bad Aboum est introuvable pour l’instant ; le trajet continue sans le passage secret.", "No absence without the registry");
        Check(!HasId(Shortcuts(ticks), 101) && HasId(Shortcuts(ticks), 102), "That trip waits for this character");
        game.character = 78;
        Check(HasId(Shortcuts(ticks), 101), "Not for another character");
    }
    {   // Several of him: no guess.
        auto& game = Fresh();
        game.npcs = 2;
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Failed && game.talks == 0 && Known().doubtful.empty(), "Ambiguous NPC");
    }
    {   // The map still loading at the start, then too long.
        auto& game = Fresh();
        Check(Start(101, Crypt, ticks) && Tick(Crypt, false).outcome == Outcome::Running && game.finds == 0, "No look before the scene is ready");
        Check(Tick(Crypt, false, 5000).outcome == Outcome::Failed, "A scene that never settles fails");
    }
    {   // The talk refused by the native layer (combat, entity gone).
        auto& game = Fresh();
        game.talkOk = false;
        Check(Start(101, Crypt, ticks), "Started");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && result.reason == "talk unavailable" && game.replies == 0, "No talk, no reply");
    }
    {   // No question within the delay: that trip waits a while for this character.
        auto& game = Fresh();
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && game.talks == 1, "Spoken to");
        Check(Tick(Crypt, true, 7000).outcome == Outcome::Running, "Still waiting");
        const auto result = Tick(Crypt, true, 1200);
        Check(result.outcome == Outcome::Failed && result.notice == "Bad Aboum ne répond pas ; le trajet continue sans le passage secret." && game.replies == 0 &&
            game.closes == 0, "Silent: failed without any click");
        Check(!HasId(Shortcuts(ticks), 101) && HasId(Shortcuts(ticks + PauseMs), 101), "Paused, then planned again");
    }
    {   // An old question (displayed before the talk) is never answered.
        auto& game = Fresh();
        game.Ask(AstrubMessage, {8758, -1});
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Talk marks the current question");
        Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 0, "The stale question is not answered");
        game.Ask(AstrubMessage, {8758, -1});
        Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 1, "The fresh one is");
    }
    {   // The game's Open before his question: six empty buttons and the previous
        // dialog's entity, message and lock. Nothing is concluded from it.
        auto& game = Fresh();
        game.Ask(30706, {37075, 37073}, 4351, true);
        game.afterTalk = [&game] { game.Open(); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        Check(Tick(Crypt).outcome == Outcome::Running && Tick(Crypt).outcome == Outcome::Running && game.closes == 0 && game.replies == 0 && !Who(77),
            "Opening: no abort, no refusal, no click");
        game.Ask(AstrubMessage, {8758, -1});
        Check(Tick(Crypt).outcome == Outcome::Running && game.replies == 1, "His question then is answered");
        auto& stale = Fresh();
        stale.Ask(AstrubMessage, {8758, -1}, BadAboum, true);
        stale.afterTalk = [&stale] { stale.Open(); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && Tick(Crypt).outcome == Outcome::Running && stale.replies == 0,
            "His previous answered question's lock is no answer of the player");
    }
    {   // The player talks to someone else meanwhile, or answers himself.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(30706, {37075, 37073}, 4351); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        Check(Tick(Crypt).outcome == Outcome::Aborted && game.replies == 0 && game.closes == 0, "Another NPC's dialog: the player's, untouched");
        auto& second = Fresh();
        second.afterTalk = [&second] { second.Ask(AstrubMessage, {8758}, BadAboum, true); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && Tick(Crypt).outcome == Outcome::Aborted && second.replies == 0,
            "Already answered by the player: aborted");
    }
    {   // An unexpected question of his.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(4242, {8758, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && result.notice == "question inattendue de Bad Aboum ; le trajet continue sans le passage secret." &&
            game.replies == 0 && game.closes == 1 && game.closed == -12, "Unknown question: his dialog closed, no reply");
    }
    {   // The trip is not offered to this character: refusal learned for that trip only.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8779, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && result.notice == "Bad Aboum ne propose pas ce trajet à ce personnage ; le trajet continue sans le passage secret." &&
            game.replies == 0 && game.closes == 1 && game.closed == -12, "Reply absent: his dialog closed, nothing clicked");
        Check(Who(77) && Who(77)->refused == RefusedBit(Side::Astrub, Side::Temple) && Who(77)->offered == std::vector<int>{8779} &&
            !HasId(Shortcuts(ticks + PauseMs), 101) && HasId(Shortcuts(ticks + PauseMs), 102) && HasId(Shortcuts(ticks + PauseMs), 121),
            "Refused from Astrub for this character: Amakna still offered, the temple from Amakna still planned");
        game.character = 78;
        Check(HasId(Shortcuts(ticks), 101), "Another character still plans it");
    }
    {   // A question offering no reply at all teaches no refusal.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {-1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && Tick(Crypt).outcome == Outcome::Failed && game.closes == 1 &&
            (!Who(77) || !Who(77)->refused), "Only the close button: failed, nothing refused");
    }
    {   // Only a paid reply: never clicked.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {37073, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && Tick(Crypt).outcome == Outcome::Failed && game.replies == 0 &&
            game.closes == 1, "The paying reply is never chosen");
    }
    {   // Two replies to the temple: no guess.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, 8781, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && game.replies == 0 && game.closes == 1 &&
            result.notice == "Bad Aboum propose plusieurs réponses vers cette destination ; le trajet continue sans le passage secret.", "Ambiguous replies");
    }
    {   // The click refused (dialog locked meanwhile).
        auto& game = Fresh();
        game.replyOk = false;
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && game.replies == 1 && game.closes == 1 &&
            result.notice == "la réponse à Bad Aboum n’a pas pu être envoyée ; le trajet continue sans le passage secret.", "Reply not sent: closed");
    }
    {   // His other question after the reply instead of the teleport: his dialog closed.
        auto& game = Fresh();
        Replied(game);
        Check(Tick(Crypt).outcome == Outcome::Running, "Locked question: waiting");
        game.Ask(TempleMessage, {8759, -1});
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && game.closes == 1 && game.closed == -12 && game.replies == 1 &&
            result.notice == "Bad Aboum pose une autre question ; le trajet continue sans le passage secret.", "A follow-up question: closed, no second reply");
    }
    {   // Another NPC's dialog while the teleport is awaited: the player's own, never closed.
        auto& game = Fresh();
        Replied(game, [&game] { game.dialog.shown = false; });
        game.Ask(30706, {37075, 37073}, 4351);
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Aborted && game.closes == 0 && result.reason == "another dialog npc=4351", "Aborted, his dialog untouched");
        auto& second = Fresh();
        Replied(second);
        second.Ask(TempleMessage, {8759, -1}, BadAboum, true);
        Check(Tick(Crypt).outcome == Outcome::Aborted && second.closes == 0, "His new question answered by the player: aborted");
    }
    {   // No teleport: only his own answered question, still shown, is closed.
        auto& game = Fresh();
        Replied(game, [&game] { game.dialog.shown = false; });
        Check(Tick(Crypt, true, 11000).outcome == Outcome::Running, "Still waiting");
        auto result = Tick(Crypt, true, 1200);
        Check(result.outcome == Outcome::Failed && result.notice == "le passage secret n’a pas eu lieu ; le trajet continue sans lui." && game.closes == 0,
            "The server already closed it: no stray close");
        auto& second = Fresh();
        Replied(second);
        Check(Tick(Crypt, true, 11000).outcome == Outcome::Running, "Still waiting");
        result = Tick(Crypt, true, 1200);
        Check(result.outcome == Outcome::Failed && second.closes == 1 && second.closed == -12, "His answered question still shown: closed");
        auto& third = Fresh();
        Replied(third);
        third.dialog.locked = false;  // his question no longer waits for an answer
        Check(Tick(Crypt, true, 11000).outcome == Outcome::Running, "Still waiting");
        result = Tick(Crypt, true, 1200);
        Check(result.outcome == Outcome::Failed && third.closes == 0, "Not waiting for the answer: not closed");
    }
    {   // The player leaves his map before or during the dialog: aborted, untouched.
        auto& game = Fresh();
        Check(Start(101, Crypt, ticks) && Tick(Chapel).outcome == Outcome::Aborted && game.talks == 0, "Left before the talk");
        game.afterTalk = [] {};
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running && Tick(Chapel).outcome == Outcome::Aborted && game.closes == 0,
            "Left during the dialog");
    }
    {   // After the reply, a map that is not his destination is the player's own move: stop, no new trip.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, -1}); };
        Check(Through(game, Chapel).outcome == Outcome::Aborted && Known().arrival[0][1] == 0, "Back on the Astrub side");
        auto& second = Fresh();
        second.afterTalk = [&second] { second.Ask(AstrubMessage, {8758, -1}); };
        auto result = Through(second, Sewer);
        Check(result.outcome == Outcome::Aborted && result.map == Sewer && result.reason == "unexpected arrival [6,-12]" &&
            result.notice == "arrivée inattendue après le passage secret : trajet arrêté." && Known().arrival[0][1] == 0, "Into the sewers: stopped, nothing learned");
        auto& third = Fresh();
        third.afterTalk = [&third] { third.Ask(AstrubMessage, {8758, -1}); };
        result = Through(third, Elsewhere);
        Check(result.outcome == Outcome::Aborted && result.reason == "unexpected arrival [0,0]", "Elsewhere: stopped");
        auto& fourth = Fresh();
        Replied(fourth);
        Check(Tick(Door, false).outcome == Outcome::Running, "Leaving");
        result = Tick(Door, false, 16000);
        Check(result.outcome == Outcome::Aborted && result.notice == "la carte d’arrivée ne se charge pas : trajet arrêté.", "An arrival never settled stops the trip");
        Check(HasId(Shortcuts(ticks), 101), "Stopping for the player's move pauses nothing");
    }
    {   // A native error ends the run as a failure (the trip goes on without it).
        auto& game = Fresh();
        game.failFind = true;
        Check(Start(101, Crypt, ticks), "Started");
        const auto result = Tick(Crypt);
        Check(result.outcome == Outcome::Failed && result.reason == "error scripted registry failure", "Native error");
    }
    {   // Stop: no game action.
        auto& game = Fresh();
        game.afterTalk = [&game] { game.Ask(AstrubMessage, {8758, -1}); };
        Check(Start(101, Crypt, ticks) && Tick(Crypt).outcome == Outcome::Running, "Spoken to");
        Stop();
        Check(!Running() && Tick(Crypt).outcome == Outcome::Aborted && game.replies == 0 && game.closes == 0, "Stopped without a click");
        Check(fake::Logged("ROUBLARD_PASSAGE_STOPPED trip=astrub>temple phase=dialog"), "Stop is logged");
    }
}
// The player's own trips: what he sees on the maps teaches the mod.
void ObserveTests() {
    {
        auto& game = Fresh();
        ticks += 10000;
        Observe(Crypt, true, ticks);
        Check(game.finds == 0, "No look before the scene settled");
        Observe(Crypt, true, ticks += 1300);
        Check(game.finds == 1 && Known().npc[0] == Crypt && fake::Logged("ROUBLARD_PASSAGE_SEEN map=67372032 side=astrub entity=-12"), "Seen in the crypt");
        game.Ask(AstrubMessage, {8758, 8779, -1});
        Observe(Crypt, true, ticks += 200);
        Check(Who(77) && Who(77)->offered == std::vector<int>{8758, 8779} && Who(77)->refused == 0 && game.replies == 0 && game.talks == 0,
            "His question: trips offered, never clicked by the mod");
        game.dialog.locked = true;
        Observe(Crypt, true, ticks += 200);
        Check(fake::Logged("ROUBLARD_PASSAGE_OBSERVED_REPLY side=astrub"), "The player's reply is seen");
        Observe(0, false, ticks += 300);
        Observe(Door, false, ticks += 300);
        Observe(Door, true, ticks += 300);
        Check(Known().arrival[0][1] == Door && fake::Logged("ROUBLARD_PASSAGE_OBSERVED_TRIP trip=astrub>temple map=67108864"),
            "The player's trip teaches its arrival");
        game.npcs = 1;
        Observe(Door, true, ticks += 1300);
        Check(Known().npc[1] == Door, "And his temple map when he stands there");
    }
    {   // On a map whose coordinates tell nothing, his question tells the side.
        auto& game = Fresh();
        constexpr int64_t Unknown = 55555;
        Observe(Unknown, true, ticks += 100);
        Observe(Unknown, true, ticks += 1300);
        Check(fake::Logged("ROUBLARD_PASSAGE_SEEN map=55555 side=none") && Known().npc[1] == 0, "Seen, side unknown: nothing learned yet");
        game.Ask(TempleMessage, {8759, 8780, -1});
        Observe(Unknown, true, ticks += 200);
        Check(Known().npc[1] == Unknown && HasId(Shortcuts(ticks), 110) && Who(77) && Who(77)->offered == std::vector<int>{8759, 8780},
            "His temple question: learned as the temple side, with the trips offered");
        game.dialog.locked = true;
        Observe(Unknown, true, ticks += 200);
        Observe(Crypt, true, ticks += 500);
        Check(Known().arrival[1][0] == Crypt && fake::Logged("ROUBLARD_PASSAGE_OBSERVED_TRIP trip=temple>astrub map=67372032"),
            "The trip starts where his question was asked");
    }
    {   // His question's side wins over the map's coordinates for what he offers.
        auto& game = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        game.Ask(TempleMessage, {8759, -1});
        Observe(Crypt, true, ticks += 200);
        Check(Who(77) && Who(77)->refused == RefusedBit(Side::Temple, Side::Amakna) && fake::Logged("ROUBLARD_PASSAGE_OBSERVED_SIDE map=67372032 coordinates=astrub question=temple"),
            "Refused from the temple side, as his question says");
    }
    {   // A question without the temple: refused for this character, from that side.
        auto& game = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        game.Ask(AstrubMessage, {8779, -1});
        Observe(Crypt, true, ticks += 200);
        Check(Who(77) && Who(77)->refused == RefusedBit(Side::Astrub, Side::Temple), "Temple refused from Astrub, Amakna offered");
        auto& closing = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        closing.Ask(AstrubMessage, {-1});
        Observe(Crypt, true, ticks += 200);
        Check(!Who(77) || !Who(77)->refused, "A question with no reply at all refuses nothing");
    }
    {   // A reply then a late map change, or a walk on the same side: nothing learned.
        auto& game = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        game.Ask(AstrubMessage, {8758, -1});
        Observe(Crypt, true, ticks += 200);
        game.dialog.locked = true;
        Observe(Crypt, true, ticks += 200);
        Observe(Chapel, true, ticks += 500);
        Observe(Door, true, ticks += 500);
        Check(Known().arrival[0][1] == 0, "Walking on the same side first is no trip");
        auto& late = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        late.Ask(AstrubMessage, {8758, -1});
        Observe(Crypt, true, ticks += 200);
        late.dialog.locked = true;
        Observe(Crypt, true, ticks += 200);
        Observe(Door, true, ticks += 21000);
        Check(Known().arrival[0][1] == 0, "A map change long after the reply is no trip");
    }
    {   // No Bad Aboum: two looks, then nothing; a stale locked question is not a reply.
        auto& game = Fresh();
        game.npcs = 0;
        Observe(Elsewhere, true, ticks += 100);
        for (int i = 0; i < 10; ++i) Observe(Elsewhere, true, ticks += 1300);
        Check(game.finds == 2, "Two looks per map");
        auto& stale = Fresh();
        stale.Ask(AstrubMessage, {8758, -1}, BadAboum, true);
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        Observe(Crypt, true, ticks += 200);
        Observe(Door, true, ticks += 300);
        Check(Known().arrival[0][1] == 0 && !Who(77), "An old locked question teaches nothing");
        // The game's Open on a reused dialog: empty buttons with the old lock.
        auto& opened = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        opened.Ask(AstrubMessage, {8758, -1}, BadAboum, true);
        opened.marked = opened.generation;
        opened.Open();
        Observe(Crypt, true, ticks += 200);
        opened.dialog.shown = false;
        Observe(Door, true, ticks += 5000);
        Check(!fake::Logged("ROUBLARD_PASSAGE_OBSERVED_REPLY") && !Who(77) && Known().arrival[0][1] == 0, "Opening teaches nothing: no reply, refusal or trip");
    }
    {   // Another NPC's dialog on his map is the player's own: nothing learned.
        auto& game = Fresh();
        Observe(Crypt, true, ticks += 100);
        Observe(Crypt, true, ticks += 1300);
        game.Ask(30706, {37075, 37073}, 4351);
        Observe(Crypt, true, ticks += 200);
        game.dialog.locked = true;
        Observe(Crypt, true, ticks += 200);
        Observe(Door, true, ticks += 500);
        Check(!fake::Logged("ROUBLARD_PASSAGE_OBSERVED_DIALOG") && !fake::Logged("ROUBLARD_PASSAGE_OBSERVED_REPLY") && !Who(77) &&
            Known().arrival[0][1] == 0, "Another NPC's dialog teaches nothing");
    }
    {   // During the mod's own run, the observer stays quiet.
        auto& game = Fresh();
        Check(Start(101, Crypt, ticks), "Run");
        Observe(Crypt, true, ticks += 5000);
        Check(game.finds == 0, "Quiet while running");
        Stop();
    }
}
// The native contracts, on a fake runtime.
struct World {
    Module owner{"offline"};
    managed::Obj registry{}, entities{}, dialogs{}, uis{}, dialog{}, npc{}, replies{}, character{}, bus{}, npcs{};
    World() {
        module = &owner; owner.ready = true; owner.servicesReady = true;
        fake::services.clear(); fake::mapInfos.clear(); fake::combat = false;
        fake::spoken.clear(); fake::built.clear(); fake::selected.clear(); fake::closes = 0;
        registry = fake::Make("evv"); entities = fake::Make("List"); fake::Get(registry).fields["entities"] = entities;
        auto player = fake::Make("EntityInfo"); fake::Get(player).numbers["id"] = 900;
        auto other = fake::Make("EntityInfo"); fake::Get(other).numbers = {{"id", -3}, {"npc", 4351}};
        auto aboum = fake::Make("EntityInfo"); fake::Get(aboum).numbers = {{"id", -12}, {"npc", BadAboum}};
        fake::Get(entities).items = {player, fake::Make("Unrelated"), other, aboum};
        character = fake::Make("esg"); fake::Get(character).fields["dpzj"] = fake::Make("z"); fake::Get(character).numbers["characterId"] = 5551;
        bus = fake::Make("MessageBus"); npcs = fake::Make("NpcService");
        dialogs = fake::Make("ghd"); uis = fake::Make("Dictionary"); fake::Get(dialogs).fields["eabq"] = uis;
        dialog = fake::Make("NpcDialog"); npc = fake::Make("NpcData"); fake::Get(npc).numbers["id"] = BadAboum;
        replies = fake::Make("List");
        fake::Get(dialog).fields = {{"m_npc", npc}, {"m_repliesIdFromBtn", replies}};
        fake::Get(dialog).numbers = {{"m_npcEntityId", -12}, {"m_currentMsg", AstrubMessage}, {"m_canClose", 1}};
        fake::Get(replies).items = {fake::Box<int>(8758), fake::Box<int>(8779), fake::Box<int>(-1)};
        fake::Get(uis).entries[0] = dialog;
        fake::services = {{"fso", registry}, {"fky", dialogs}, {"fng", character}, {"fmz", bus}, {"fsq", npcs}};
        auto info = fake::Make("MapInformationData");
        const int16_t x = 5, y = -15;
        std::memcpy(static_cast<char*>(info) + 0x18, &x, sizeof(x)); std::memcpy(static_cast<char*>(info) + 0x1A, &y, sizeof(y));
        fake::mapInfos[Crypt] = info;
    }
    ~World() { Restore(); module = nullptr; }
};
void NativeTests() {
    fake::Initialize();
    fake::Layout();
    UseGame(nullptr);
    fake::verified.clear(); fake::logs.clear();
    Install({});
    Check(Available() && fake::Logged("ROUBLARD_PASSAGE_INSTALL npc=1422 astrub=67372032 temple=0 amakna=67373056"), "Native contracts installed");
    for (const char* contract : {"evv.baml/0@0x19579E0", "evv.bamm/2@0x1957A70", "EntityInfo.bajh/0@0x1951440", "EntityInfo.baks/2@0x1952330",
            "NpcMenuMaker..ctor/6@0x16E8A40", "NpcMenuMaker.OnNPCMenuClick/3@0x16E89A0", "NpcDialog.SelectReply/1@0x2A64DB0",
            "NpcDialog.OnClose/0@0x2A61860", "esg.zcx/0@0x18BB140", "MapInformationData.GetMapInformationById/1@0x7C4200"})
        Check(std::find(fake::verified.begin(), fake::verified.end(), contract) != fake::verified.end(), "Each native method is verified at its snapshot RVA");
    for (const auto& contract : fake::verified) {
        const auto at = contract.find("@0x");
        const auto rva = std::stoull(contract.substr(at + 3), nullptr, 16);
        Check(std::any_of(std::begin(Signatures), std::end(Signatures), [rva](const NativeSignature& guard) { return guard.rva == rva; }),
            "Each verified method has its 16-byte guard");
    }
    const auto before = fake::handles.size();
    {
        World world;
        auto* game = passage.native.get();
        int64_t entity = 0;
        Check(game->FindNpc(BadAboum, entity) == 1 && entity == -12, "Bad Aboum among the map's entities, by NPC id");
        Check(game->FindNpc(4351, entity) == 1 && entity == -3 && game->FindNpc(9999, entity) == 0 && entity == 0, "Other NPCs by their own id");
        Check(game->Character() == 5551, "The local character");
        int x = 0, y = 0;
        Check(game->Locate(Crypt, x, y) && x == 5 && y == -15 && !game->Locate(Elsewhere, x, y), "Map coordinates");
        // « Parler »: the NPC menu built with the game's services, then its row's click.
        Check(game->Talk(-12) && fake::built.size() == 1 && fake::spoken.size() == 1, "Talk");
        const auto& made = fake::built[0];
        Check(made.entity == -12 && !made.rightClick && made.bus == world.bus && made.who == world.character && made.npcs == world.npcs &&
            fake::Get(made.data).numbers["id"] == -12, "NpcMenuMaker(EntityInfo, entity, false, fmz, fng, fsq)");
        Check(fake::spoken[0].view == nullptr && fake::spoken[0].entity == -12 && fake::spoken[0].action == 3, "OnNPCMenuClick(null, entity, 3: « Parler »)");
        Check(!game->Talk(-99) && fake::spoken.size() == 1, "An entity no longer there is not spoken to");
        Check(!game->Talk(int64_t(1) << 40) && fake::spoken.size() == 1, "An id outside the menu's Int32 is refused");
        fake::combat = true;
        Check(!game->Talk(-12) && !game->Reply(0, 8758) && fake::spoken.size() == 1 && fake::selected.empty(), "Never in combat");
        fake::combat = false;
        // The dialog as read, and the freshness of its question.
        auto dialog = game->Read();
        Check(dialog.shown && dialog.fresh && dialog.npc == BadAboum && dialog.entity == -12 && dialog.message == AstrubMessage &&
            dialog.replies == std::vector<int>{8758, 8779, -1} && !dialog.locked, "The dialog's question and buttons");
        game->Mark();
        Check(!game->Read().fresh, "Marked: the same question is not fresh");
        auto next = fake::Make("List");
        fake::Get(next).items = {fake::Box<int>(8759), fake::Box<int>(-1)};
        fake::Get(world.dialog).fields["m_repliesIdFromBtn"] = next;
        Check(game->Read().fresh && game->Read().replies == std::vector<int>{8759, -1}, "A new question is fresh");
        // Open on the reused dialog: six empty buttons, the previous entity: no question yet.
        auto opened = fake::Make("List");
        for (int i = 0; i < 6; ++i) fake::Get(opened).items.push_back(fake::Box<int>(0));
        fake::Get(world.dialog).fields["m_repliesIdFromBtn"] = opened;
        Check(game->Read().fresh && !game->Read().shown, "Opening, before its question: not shown");
        fake::Get(world.dialog).fields["m_repliesIdFromBtn"] = world.replies;
        fake::Get(world.dialog).numbers["m_npcEntityId"] = -1;
        Check(!game->Read().shown, "No NPC entity: not shown");
        fake::Get(world.dialog).numbers["m_npcEntityId"] = -12;
        // The reply button: in range, the reply read there, never paid, never twice.
        Check(!game->Reply(2, -1) && !game->Reply(3, 8758) && !game->Reply(-1, 8758) && fake::selected.empty(), "Only a button holding a reply");
        Check(!game->Reply(0, 8759) && !game->Reply(1, 8758) && fake::selected.empty(), "Only when the button still holds the reply chosen");
        fake::Get(world.replies).items[1] = fake::Box<int>(37073);
        Check(!game->Reply(1, 37073) && fake::selected.empty(), "Never a paid reply");
        Check(game->Reply(0, 8758) && fake::selected == std::vector<int>{0}, "SelectReply(button)");
        Check(!game->Reply(0, 8758) && fake::selected.size() == 1, "Locked: no second click");
        // The close button: only on his dialog, for that entity, when it can close.
        Check(!game->Close(-3) && fake::closes == 0, "Not another entity's dialog");
        fake::Get(world.npc).numbers["id"] = 4351;
        Check(!game->Close(-12) && fake::closes == 0, "Not another NPC's dialog");
        fake::Get(world.npc).numbers["id"] = BadAboum;
        Check(game->Close(-12) && fake::closes == 1, "OnClose on his dialog");
        fake::Get(world.dialog).numbers["m_canClose"] = 0;
        Check(!game->Close(-12) && fake::closes == 1, "Not when it cannot close");
        // No dialog UI, or another UI under the key.
        fake::Get(world.uis).entries[0] = fake::Make("OtherUi");
        Check(!game->Read().shown && !game->Reply(0, 8758) && !game->Close(-12), "Another UI is not the NPC dialog");
        fake::Get(world.uis).entries.clear();
        Check(!game->Read().shown, "No dialog UI yet");
        fake::services["fky"] = fake::Make("OtherService");
        Check(!game->Read().shown, "Another dialog service");
        fake::Get(world.character).fields["dpzj"] = nullptr;
        Check(game->Character() == 0, "No character data yet");
        fake::services["fso"] = fake::Make("OtherRegistry");
        Check(game->FindNpc(BadAboum, entity) == -1 && !game->Talk(-12), "Without the map's registry: unknown, not absent");
    }
    Restore();
    Check(fake::handles.size() == before, "Native references are released");
    // A changed layout disables only the passage.
    fake::offsets["NpcDialog.m_repliesIdFromBtn"] = 0x1F8;
    fake::logs.clear();
    Install({});
    Check(!Available() && Shortcuts(ticks).empty() && fake::Logged("ROUBLARD_PASSAGE_UNAVAILABLE Passage field mismatch: m_repliesIdFromBtn"), "Field mismatch");
    fake::Layout();
    fake::refuse = "OnNPCMenuClick";
    Install({});
    Check(!Available() && fake::Logged("ROUBLARD_PASSAGE_UNAVAILABLE Verified method unavailable: OnNPCMenuClick"), "Method mismatch");
    fake::refuse.clear();
    Install({});
    Check(Available(), "Installs again");
}
}

int main() {
    try {
        fake::Initialize();
        fake::Layout();
        RuleTests();
        KnowledgeTests();
        StoreTests();
        RunTests();
        ObserveTests();
        NativeTests();
        UseGame(nullptr);
        std::cout << "PASS: Roublard passage rules, learning and its shared file, the mod's run through Bad Aboum (every failure), "
                     "the player's own passages, native NPC menu and dialog contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
