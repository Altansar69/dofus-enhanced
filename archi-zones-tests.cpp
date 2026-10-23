#include "archi-zones.cpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <tuple>
#include <unordered_map>

// The production « Ocre » mode and zones against an offline managed tree: no
// client, no input, no preference file, no Metamob request.
namespace fake {
using namespace managed;
struct Type { std::string name; Type* parent = nullptr; bool value = false; };
struct MethodData { Type* type; std::string name; int argc; };
struct Object {
    Type* type{};
    std::string text, name;
    std::map<std::string, Obj> fields;
    std::map<std::string, int64_t> slots; // FieldAddress storage
    std::map<std::string, std::array<unsigned char, 16>> styles;
    std::map<std::string, std::string> strings;
    std::map<std::string, int> ints;
    std::vector<Obj> children, items;
    std::vector<std::string> classes;
    std::map<int, Obj> intKeys, layers;
    std::map<std::string, Obj> stringKeys;
    std::array<unsigned char, 64> data{};
    archiZones::Color fill{}, line{};
    int paints = 0;
    bool owned = false, disposed = false, hover = true;
};
std::map<std::string, std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xA2C000000000ULL;
std::map<std::string, uintptr_t> guards;
std::map<std::pair<std::string, std::string>, size_t> offsets;
std::vector<std::string> logs;
int calls = 0, hooks = 0;
std::string failMethod;
void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Type* Class(const std::string& name) {
    auto& type = types[name];
    if (!type) type = std::make_unique<Type>(Type{name});
    return type.get();
}
Object& Get(Obj value) { Check(value != nullptr, "Null fixture object"); return *static_cast<Object*>(value); }
Obj Make(const std::string& type, bool owned = false) {
    auto value = std::make_unique<Object>();
    value->type = Class(type);
    value->owned = owned;
    auto result = value.get();
    objects.push_back(std::move(value));
    return result;
}
template<class T> Obj Box(T value, const char* type = "Box") {
    auto result = Make(type);
    static_assert(sizeof(T) <= 64);
    std::memcpy(Get(result).data.data(), &value, sizeof(value));
    return result;
}
Obj String(const std::string& text) { auto result = Make("String"); Get(result).text = text; return result; }
int64_t& Slot(Obj object, const char* name) { return Get(object).slots[name]; }
void Detach(Obj child) {
    auto parent = Get(child).fields["parent"];
    if (parent) { auto& c = Get(parent).children; c.erase(std::remove(c.begin(), c.end(), child), c.end()); }
    Get(child).fields["parent"] = nullptr;
}
void Insert(Obj parent, int index, Obj child) {
    Detach(child);
    auto& c = Get(parent).children;
    Check(index >= 0 && index <= static_cast<int>(c.size()), "Insert bounds");
    c.insert(c.begin() + index, child);
    Get(child).fields["parent"] = parent;
}
void Add(Obj parent, Obj child) { Insert(parent, static_cast<int>(Get(parent).children.size()), child); }
Obj Content(Obj element) { auto content = Get(element).fields["content"]; return content ? content : element; }
Obj Panel(Obj value) {
    while (value) { if (Get(value).fields["panel"]) return Get(value).fields["panel"]; value = Get(value).fields["parent"]; }
    return nullptr;
}
int StyleInt(Obj element, const char* name) { int value = 0; std::memcpy(&value, Get(element).styles.at(name).data(), sizeof(value)); return value; }
float StyleFloat(Obj element, const char* name) { float value = 0; std::memcpy(&value, Get(element).styles.at(name).data(), sizeof(value)); return value; }
struct LengthValue { float value; int unit; };
LengthValue StyleLength(Obj element, const char* name) { LengthValue value{}; std::memcpy(&value, Get(element).styles.at(name).data(), sizeof(value)); return value; }
archiZones::Color StyleColor(Obj element, const char* name) { archiZones::Color value{}; std::memcpy(&value, Get(element).styles.at(name).data(), sizeof(value)); return value; }
bool Shown(Obj element) { return !Get(element).styles.contains("display") || StyleInt(element, "display") == 0; }

// ------------------------------------------------------------ the game
struct Creation { Obj view; int id, fia; archiZones::Color fill, line; float thickness; std::string name; bool flag; int bonus; };
std::vector<Creation> creations, pendingCreations;
Obj character{}, service{}, viewsDictionary{}, window{}, bindingData{}, cartoRoot{}, buttons{}, worldButton{}, divider{}, anomaly{}, conquest{};
Obj bigView{}, bigRoot{}, bigWorld{}, miniView{}, miniRoot{}, miniWorld{}, subRoot{}, dungeonRoot{}, nativeShape{};
std::map<int64_t, Obj> monsters, mapInfos;
std::map<int, Obj> subareas, dungeonsByMap;
std::map<int64_t, int> monsterReads;
std::map<int, int> subareaReads;
std::vector<std::pair<int, int>> moves;
std::vector<int> leaves, worldSwitches;
Obj cartographyWindow{};
bool (*buttonHandler)(Obj) = nullptr;
bool (*rowHandler)(Obj) = nullptr;
std::map<int, SoulState> ocre, bag;
uint64_t soulRevision = 1;
int ocreReads = 0, catalogWants = 0;
std::vector<std::tuple<SoulClient, bool, bool>> wants;
std::map<int, metamob::OcreQuantity> metamobQuantities;
uint64_t clock = 5'000'000;

Obj Layer(Obj view, int fia) {
    if (fia == -1) return Get(view).fields.at("dugq");
    auto& layer = Get(view).layers[fia];
    if (!layer) { layer = Make("VisualElement"); Get(layer).name = "layer" + std::to_string(fia); Add(Get(view).fields.at("dugq"), layer); }
    return layer;
}
Obj Registry(Obj view, int world) {
    auto& inner = Get(Get(view).fields.at("<duic>k__BackingField")).intKeys[world];
    if (!inner) inner = Make("Dictionary");
    return inner;
}
// The frames between two pumps: the game's AddAreaShape finishes its work
// (one UniTask.Yield) and registers the shapes asked for.
void CompleteShapes() {
    for (const auto& c : pendingCreations) {
        auto shape = Make("fgf"), root = Make("VisualElement");
        Get(shape).fields["<dtwe>k__BackingField"] = root;
        Get(shape).fill = c.fill;
        Get(shape).line = c.line;
        Add(Layer(c.view, c.fia), root);
        const int world = static_cast<int>(Slot(Get(c.view).fields.at("<dugi>k__BackingField"), "id"));
        Get(Registry(c.view, world)).stringKeys[c.name] = shape;
    }
    pendingCreations.clear();
}

Obj Invoke(Method method, Obj self, std::initializer_list<void*> arguments) {
    const auto& m = *static_cast<const MethodData*>(method);
    const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size()) == m.argc, "Managed argument count mismatch: " + m.name);
    ++calls;
    if (!failMethod.empty() && failMethod == m.name) { failMethod.clear(); throw std::runtime_error("Injected native failure: " + m.name); }
    const auto& t = m.type->name;
    // Static data roots and lookups.
    if (t == "MonsterData" && m.name == "GetById") {
        Check(!self, "Static monster lookup");
        const auto id = *static_cast<int64_t*>(args[0]);
        ++monsterReads[id];
        const auto found = monsters.find(id);
        return found == monsters.end() ? nullptr : found->second;
    }
    if (t == "DataCenterModule") return m.name == "get_subAreasDataRoot" ? subRoot : dungeonRoot;
    if (t == "MapInformationData" && m.name == "GetMapInformationById") {
        const auto found = mapInfos.find(*static_cast<int64_t*>(args[0]));
        return found == mapInfos.end() ? nullptr : found->second;
    }
    if (t == "CartographyMapUIBinding") {
        // A value type: the receiver is the unboxed struct.
        Obj data = *static_cast<Obj*>(self);
        return Get(data).fields.at(m.name);
    }
    auto& o = Get(self);
    if (t == "fhi") {
        if (m.name == "bejv") { const int mode = *static_cast<int*>(args[0]); leaves.push_back(mode); Slot(self, "dubo") = mode == Slot(self, "dubo") ? 0 : mode; return nullptr; }
        if (m.name == "beki") { moves.emplace_back(*static_cast<int*>(args[0]), *static_cast<int*>(args[1])); return Box(uint8_t(0)); }
        if (m.name == "belb") { const int world = *static_cast<int*>(args[0]); worldSwitches.push_back(world); Slot(bigWorld, "id") = world; return nullptr; }
    }
    if (m.name == "besn") {
        Creation c{self, *static_cast<int*>(args[0]), *static_cast<int*>(args[1]), *static_cast<archiZones::Color*>(args[2]),
            *static_cast<archiZones::Color*>(args[3]), *static_cast<float*>(args[4]), Get(static_cast<Obj>(args[5])).text,
            *static_cast<bool*>(args[6]), *static_cast<int*>(args[7])};
        creations.push_back(c);
        pendingCreations.push_back(c);
        return Box(0, "UniTask");
    }
    if (m.name == "bexb") return Layer(self, *static_cast<int*>(args[0]));
    if (m.name == "befi") { o.fill = *static_cast<archiZones::Color*>(args[0]); ++o.paints; return nullptr; }
    if (m.name == "befk") { o.line = *static_cast<archiZones::Color*>(args[0]); return nullptr; }
    if (m.name == "befo") { o.hover = *static_cast<bool*>(args[0]); return nullptr; }
    if (m.name == "bege") { Add(static_cast<Obj>(args[0]), o.fields.at("<dtwe>k__BackingField")); return nullptr; }
    if (m.name == "begd") { Check(!o.disposed, "Shape disposed twice"); o.disposed = true; Detach(o.fields.at("<dtwe>k__BackingField")); return nullptr; }
    // Collections.
    if (m.name == "get_Count") return Box(static_cast<int>(o.items.size()));
    if (m.name == "get_Item") return o.items.at(*static_cast<int*>(args[0]));
    if (m.name == "TryGetValue") {
        Obj found = nullptr;
        if (Get(self).type->name == "IntDictionary") { const auto it = o.intKeys.find(*static_cast<int*>(args[0])); if (it != o.intKeys.end()) found = it->second; }
        else { const auto it = o.stringKeys.find(Get(static_cast<Obj>(args[0])).text); if (it != o.stringKeys.end()) found = it->second; }
        *static_cast<Obj*>(args[1]) = found;
        return Box(found != nullptr);
    }
    if (m.name == "Remove") return Box(o.stringKeys.erase(Get(static_cast<Obj>(args[0])).text) == 1);
    // Data.
    if (m.name == "GetSubAreaById") { const int id = *static_cast<int*>(args[0]); ++subareaReads[id]; return subareas.contains(id) ? subareas[id] : nullptr; }
    if (m.name == "GetDungeonByMapId") { const int id = *static_cast<int*>(args[0]); return dungeonsByMap.contains(id) ? dungeonsByMap[id] : nullptr; }
    if (m.name == "get_worldmapId") return Box(static_cast<int>(Slot(self, "world")));
    if (m.name == "Center" || m.name == "GetArea" || m.name == "get_subArea") return o.fields.at(m.name);
    if (m.name == "get_name") return String(o.text);
    // UI.
    if (m.name == ".ctor") {
        if (t == "ScrollView") { Check(*static_cast<int*>(args[0]) == 0, "Vertical list"); auto content = Make("VisualElement", true); Add(self, content); o.fields["content"] = content; }
        if (t == "WindowFigma") {
            auto close = Make("DofusButtonCustom", true), content = Make("VisualElement", true);
            Add(self, close); Add(self, content); o.fields["closeButton"] = close; o.fields["content"] = content;
        }
        return nullptr;
    }
    if (m.name == "Add") { Add(Content(self), static_cast<Obj>(args[0])); return nullptr; }
    if (m.name == "Insert") { Insert(self, *static_cast<int*>(args[0]), static_cast<Obj>(args[1])); return nullptr; }
    if (m.name == "IndexOf") {
        const auto& c = o.children;
        const auto it = std::find(c.begin(), c.end(), static_cast<Obj>(args[0]));
        return Box(it == c.end() ? -1 : static_cast<int>(it - c.begin()));
    }
    if (m.name == "RemoveFromHierarchy") { Check(o.owned, "Only owned elements are removed"); Detach(self); return nullptr; }
    if (m.name == "get_parent") return o.fields["parent"];
    if (m.name == "get_panel") return Panel(self);
    if (m.name == "set_pickingMode") { o.ints["picking"] = *static_cast<int*>(args[0]); return nullptr; }
    if (m.name == "set_name") { o.name = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (m.name == "AddToClassList") { o.classes.push_back(Get(static_cast<Obj>(args[0])).text); return nullptr; }
    if (m.name == "get_style") { auto style = Make("InlineStyleAccess"); Get(style).fields["owner"] = self; return style; }
    if (m.name.starts_with("UnityEngine.UIElements.IStyle.get_")) return Make("StyleValue");
    if (m.name == "set_value") { std::memcpy(o.data.data(), args[0], 16); return nullptr; }
    if (m.name.starts_with("UnityEngine.UIElements.IStyle.set_")) {
        auto& target = Get(o.fields.at("owner"));
        std::array<unsigned char, 16> bytes{};
        std::memcpy(bytes.data(), args[0], 16);
        target.styles[m.name.substr(std::string("UnityEngine.UIElements.IStyle.set_").size())] = bytes;
        return nullptr;
    }
    if (m.name == "get_contentContainer") return o.fields.at("content");
    if (m.name == "get_closeButton") return o.fields.at("closeButton");
    if (m.name == "get_text") return String(o.text);
    if (m.name == "get_state") return Box(o.ints["state"]);
    if (m.name == "get_selectedElement") return o.fields["selected"];
    if (m.name == "SelectItemIdWithoutNotify") {
        const int id = *static_cast<int*>(args[0]);
        for (auto item : o.items) if (Slot(item, "id") == id) o.fields["selected"] = item;
        return nullptr;
    }
    if (m.name == "AddListElementBasic") {
        Check(!args[1] && *static_cast<int*>(args[2]) == 0, "Choice items without managed callback");
        auto item = Make("ListElementBasic", true);
        Get(item).text = Get(static_cast<Obj>(args[0])).text;
        Slot(item, "id") = *static_cast<int*>(args[5]);
        o.items.push_back(item);
        return nullptr;
    }
    if (m.name == "set_text" || m.name == "set_headerText") { o.text = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (m.name == "set_tooltip" || m.name == "set_title" || m.name == "set_placeholderText" || m.name == "set_choiceText") {
        o.strings[m.name.substr(4)] = Get(static_cast<Obj>(args[0])).text; return nullptr;
    }
    if (m.name.starts_with("set_")) {
        int value = 0;
        std::memcpy(&value, args[0], m.name == "set_enableRichText" || m.name == "set_isNumbersOnly" || m.name == "set_isActivated" ? 1 : 4);
        o.ints[m.name.substr(4)] = value;
        if (m.name == "set_state") ++o.ints["stateWrites"];
        return nullptr;
    }
    throw std::runtime_error("Unexpected managed operation " + t + "." + m.name);
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass type, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type), name, argc});
    auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
Method Api::FindVerified(Klass type, const char* name, int argc, uintptr_t rva) const {
    if (fake::failMethod == name) { fake::failMethod.clear(); throw std::runtime_error(std::string("Verified method unavailable: ") + name); }
    fake::guards[static_cast<fake::Type*>(type)->name + "." + name] = rva;
    return Find(type, name, argc);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const { return fake::Invoke(method, self, args); }
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    return Invoke(Find(object_get_class(self), name, static_cast<int>(args.size())), self, args);
}
Obj Api::New(Klass type, std::initializer_list<void*> args) const {
    fake::Check(static_cast<fake::Type*>(type)->name == "Length" && args.size() == 2, "Only the native Length constructor");
    const std::vector<void*> values(args);
    return fake::Box(fake::LengthValue{*static_cast<float*>(values[0]), *static_cast<int*>(values[1])}, "Length");
}
Obj Api::Field(Obj self, const char* name) const { ++fake::calls; return self ? fake::Get(self).fields[name] : nullptr; }
void* Api::FieldAddress(Obj self, const char* name) const { ++fake::calls; return &fake::Slot(self, name); }
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string{}; }
}
Module* module = nullptr;
Module::Module(std::filesystem::path) : features(nullptr), ui(nullptr) {}
managed::Obj Module::FindService(const char* name) const {
    const std::string service(name);
    return service == "fng" ? fake::character : service == "fkc" ? fake::service : nullptr;
}
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method, uintptr_t, void*, void**) { ++fake::hooks; throw std::runtime_error("The Ocre zones must never hook a native method"); }
namespace nativeClick { void AddHandler(bool (*handler)(managed::Obj)) { fake::buttonHandler = handler; } }
namespace soul { void SetAuxiliaryPanelAction(bool (*handler)(managed::Obj)) { fake::rowHandler = handler; } }
namespace resourceMap { managed::Obj CartographyWindow() { return fake::cartographyWindow; } }
namespace metamob {
OcreQuantity MetamobOcreQuantity(int monster) {
    const auto found = fake::metamobQuantities.find(monster);
    return found == fake::metamobQuantities.end() ? OcreQuantity{} : found->second;
}
void WantMetamobCatalog() { ++fake::catalogWants; }
}
void WantSoulStatus(SoulClient client, bool inventory, bool quest) { fake::wants.emplace_back(client, inventory, quest); }
SoulState OcreProgress(int monster) { ++fake::ocreReads; const auto f = fake::ocre.find(monster); return f == fake::ocre.end() ? SoulState::Unknown : f->second; }
SoulState BagProgress(int monster) { const auto f = fake::bag.find(monster); return f == fake::bag.end() ? SoulState::Unknown : f->second; }
uint64_t SoulStatusRevision() { return fake::soulRevision; }

namespace fake {
using namespace managed;
using archiZones::Show;
void Init() {
    api.object_get_class = [](Obj value) -> Klass { return Get(value).type; };
    api.class_is_valuetype = [](Klass type) { return static_cast<Type*>(type)->value; };
    api.class_is_assignable_from = [](Klass base, Klass type) {
        for (auto t = static_cast<Type*>(type); t; t = t->parent) if (t == base) return true;
        return false;
    };
    api.object_unbox = [](Obj value) -> void* { return Get(value).data.data(); };
    api.object_new = [](Klass type) -> Obj { return Make(static_cast<Type*>(type)->name, true); };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj value, bool) -> GCHandle { const auto handle = ++nextHandle; handles.emplace(handle, value); return handle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC reference double free"); };
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        const auto found = offsets.find({static_cast<Type*>(type)->name, name});
        return found == offsets.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
    for (const auto* name : {"Box", "Length", "StyleValue", "CartographyMapUIBinding", "UniTask"}) Class(name)->value = true;
    Class("CartographyMapView")->parent = Class("fhw");
    Class("fgf")->parent = Class("fgg");
    Class("fgg")->parent = Class("fgh");
    offsets = {
        {{"fhi", "dtzx"}, 0x408}, {{"fhi", "dtzy"}, 0x410}, {{"fhi", "dtzz"}, 0x418}, {{"fhi", "dubo"}, 0x52C}, {{"fhi", "ducm"}, 0x5D0},
        {{"fhw", "dugq"}, 0x68}, {{"fhw", "dugv"}, 0x90}, {{"fhw", "dugy"}, 0xA8}, {{"fhw", "<dugi>k__BackingField"}, 0x38},
        {{"fhw", "<duic>k__BackingField"}, 0x160}, {{"fhk", "dueo"}, 0x98}, {{"WorldMapData", "id"}, 0x10},
        {{"fgh", "<dtwe>k__BackingField"}, 0x10}, {{"MonsterData", "subareas"}, 0x50},
        {{"SubAreaData", "mapIds"}, 0x20}, {{"SubAreaData", "shape"}, 0x30}, {{"SubAreaData", "level"}, 0x40},
        {{"SubAreaData", "displayOnWorldMap"}, 0x46}, {{"SubAreaData", "dungeonId"}, 0x98}, {{"DungeonData", "entranceMapId"}, 0x28},
        {{"MapInformationData", "posX"}, 0x18}, {{"MapInformationData", "posY"}, 0x1A}, {{"Point", "x"}, 0x10}, {{"Point", "y"}, 0x14},
        {{"ListElementBase", "id"}, 0x310}, {{"Dropdown", "closeOnSelection"}, 0x3D8},
    };
}
Obj List(std::vector<uint32_t> values) {
    auto list = Make("List");
    for (auto value : values) Get(list).items.push_back(Box(value));
    return list;
}
Obj MakeView(const char* type, Obj& root, Obj& world, int worldId) {
    auto view = Make(type);
    root = Make("VisualElement");
    Get(root).fields["panel"] = Make("Panel");
    Add(root, Make("VisualElement")); // the map picture
    world = Make("WorldMapData");
    Slot(world, "id") = worldId;
    auto registry = Make("IntDictionary");
    Get(view).fields = {{"dugq", root}, {"dugv", service}, {"dugy", character}, {"<dugi>k__BackingField", world}, {"<duic>k__BackingField", registry}};
    Layer(view, 0); Layer(view, 6); Layer(view, 24); // native layers, map icons above the first
    return view;
}
struct ZoneSpec { int id; const char* name; const char* area; int level, world; bool display; int dungeon; int x, y; };
void AddZone(const ZoneSpec& spec) {
    auto sub = Make("SubAreaData");
    Get(sub).text = spec.name;
    Slot(sub, "level") = spec.level;
    Slot(sub, "world") = spec.world;
    Slot(sub, "displayOnWorldMap") = spec.display;
    Slot(sub, "dungeonId") = spec.dungeon;
    Get(sub).fields["shape"] = List({10001, 10002, 10003});
    Get(sub).fields["mapIds"] = List({static_cast<uint32_t>(spec.id * 100 + 1)});
    auto point = Make("Point"); Slot(point, "x") = spec.x; Slot(point, "y") = spec.y;
    auto area = Make("AreaData"); Get(area).text = spec.area;
    Get(sub).fields["Center"] = point;
    Get(sub).fields["GetArea"] = area;
    subareas[spec.id] = sub;
}
void AddMonster(int id, const char* name, std::vector<uint32_t> zones) {
    auto monster = Make("MonsterData");
    Get(monster).text = name;
    Get(monster).fields["subareas"] = List(std::move(zones));
    monsters[id] = monster;
}
struct Scenario {
    Module owner{std::filesystem::path{}};
    size_t baseline = 0;
    Scenario() {
        // Production state from a previous scenario never leaks into this one.
        archiZones::square = {};
        archiZones::panel.reset();
        archiZones::views = {};
        archiZones::nextLog = 0;
        objects.clear(); creations.clear(); pendingCreations.clear(); monsters.clear(); mapInfos.clear(); subareas.clear();
        dungeonsByMap.clear(); monsterReads.clear(); subareaReads.clear(); moves.clear(); leaves.clear(); worldSwitches.clear();
        ocre.clear(); bag.clear(); wants.clear(); metamobQuantities.clear(); logs.clear();
        calls = hooks = ocreReads = catalogWants = 0; failMethod.clear(); soulRevision = 1;
        module = &owner;
        owner.ready = true; owner.servicesReady = true;
        owner.compositor.reset(Make("Compositor"));
        owner.settings.archiZonesMap = false; owner.settings.archiZonesMiniMap = false;
        character = Make("Character");
        service = Make("fhk");
        viewsDictionary = Make("IntDictionary");
        Get(service).fields["dueo"] = viewsDictionary;
        bigView = MakeView("CartographyMapView", bigRoot, bigWorld, 1);
        miniView = MakeView("fhw", miniRoot, miniWorld, 1);
        Get(viewsDictionary).intKeys = {{0, miniView}, {1, bigView}};
        // A native anomaly shape already registered: never touched.
        nativeShape = Make("fgf");
        Get(nativeShape).fields["<dtwe>k__BackingField"] = Make("VisualElement");
        Get(Registry(bigView, 1)).stringKeys["anomaly_shape99"] = nativeShape;
        // The map window, its root and its mode column (CartographyMapUI).
        window = Make("fhi");
        cartoRoot = Make("VisualElement");
        Get(cartoRoot).fields["panel"] = Make("Panel");
        buttons = Make("VisualElement");
        Add(cartoRoot, buttons);
        worldButton = Make("DofusButtonCustom"); divider = Make("Divider"); anomaly = Make("DofusButtonCustom"); conquest = Make("DofusButtonCustom");
        for (auto child : {worldButton, divider, anomaly, conquest}) Add(buttons, child);
        bindingData = Make("BindingData");
        Get(bindingData).fields = {{"get_ctr_worldmapButtons", buttons}, {"get_btn_conquest", conquest}};
        auto binding = Make("CartographyMapUIBinding");
        std::memcpy(Get(binding).data.data(), &bindingData, sizeof(Obj));
        Get(window).fields = {{"dzyi", binding}, {"dzyj", cartoRoot}, {"dtzy", character}, {"dtzz", service}, {"ducm", bigView}};
        Slot(window, "dtzx") = 1;
        Slot(window, "dubo") = 0;
        cartographyWindow = window;
        // The game's data.
        subRoot = Make("SubAreasDataRoot");
        dungeonRoot = Make("DungeonsDataRoot");
        AddZone({10, "Coin des Bouftous", "Amakna", 10, 1, true, 0, 4, -2});
        AddZone({20, "Plaine des Scarafeuilles", "Amakna", 40, 1, true, 0, 2, 7});
        AddZone({30, "Antre du Dragon Cochon", "Amakna", 60, 1, false, 7, 9, 9});
        AddZone({31, "Plaines de Cania", "Cania", 100, 1, true, 0, -20, -20});
        AddZone({40, "Île de Frimousse", "Frigost", 150, 3, true, 0, -75, -40});
        AddZone({50, "Grotte Hésitante", "Otomaï", 80, 1, false, 0, -50, 10});
        auto dungeon = Make("DungeonData"); Slot(dungeon, "entranceMapId") = 8001;
        dungeonsByMap[3001] = dungeon;
        auto entrance = Make("MapInformationData"); Slot(entrance, "posX") = -1; Slot(entrance, "posY") = -10;
        Get(entrance).fields["get_subArea"] = subareas.at(20);
        mapInfos[8001] = entrance;
        AddMonster(2270, "Bouftonnaire l’Ancestral", {10, 20});
        AddMonster(2271, "Craqueleur le Grand", {10});
        AddMonster(2272, "Scarafeuille le Rouge", {10, 10});
        AddMonster(2273, "Pétrifié l’Ébène", {10});
        AddMonster(2275, "Givrefoux l’Hivernal", {40});
        AddMonster(2276, "Kitsou l’Ermite", {50});
        AddMonster(147, "Dragon Cochon", {30});
        AddMonster(85, "Corailleur Magistral", {31});
        archiZones::active = archiZones::needsAsked = false;
        archiZones::pendingClicks = {};
        archiZones::retryUi = archiZones::retryMap = 0;
        archiZones::filters = {};
        archiZones::sort = {};
        archiZones::model = {};
        archiZones::build = {};
        baseline = handles.size();
    }
    ~Scenario() {
        archiZones::RestoreAll(clock + 60000);
        owner.compositor.reset();
        module = nullptr;
    }
};
void Pump(int count = 1) {
    for (int i = 0; i < count; ++i) { archiZones::Tick(clock); clock += 200; CompleteShapes(); }
}
// Ocre states of the main case: 2271 in the bag, 2272 on Metamob (1), 2273 at 0
// on Metamob, 2270 Metamob not read yet, 2275 validated, the boss 147 not started.
void MainStates() {
    ocre = {{2270, SoulState::Missing}, {2271, SoulState::Missing}, {2272, SoulState::Missing}, {2273, SoulState::Missing},
            {2275, SoulState::Done}, {2276, SoulState::Missing}, {147, SoulState::NotStarted}, {85, SoulState::Missing}};
    bag.clear();
    for (const auto& goal : chat_souls::OcreGoals) bag[goal.monster] = SoulState::Missing;
    bag[2271] = SoulState::Done;
    metamobQuantities = {{2272, {true, 1}}, {2273, {true, 0}}, {2270, {true, -1}}};
    ++soulRevision;
}
Obj Square() {
    for (auto child : Get(buttons).children) if (Get(child).owned && Get(child).type->name == "DofusButtonCustom") return child;
    return nullptr;
}
Obj PanelWindow() {
    for (auto child : Get(cartoRoot).children) if (Get(child).owned && Get(child).type->name == "WindowFigma") return child;
    return nullptr;
}
Obj OurLayer(Obj root) {
    for (auto child : Get(root).children) if (Get(child).name == "dofus_miniatures_ocre") return child;
    return nullptr;
}
// The zone ids of the panel's shown rows, in order.
std::vector<int> Rows() {
    std::vector<int> ids;
    Check(archiZones::panel != nullptr, "Panel expected");
    for (const auto& row : archiZones::panel->rows) if (row.shown) ids.push_back(archiZones::model.zones.at(row.zone).id);
    return ids;
}
const archiZones::Row& RowOf(int zone) {
    for (const auto& row : archiZones::panel->rows) if (row.shown && archiZones::model.zones.at(row.zone).id == zone) return row;
    throw std::runtime_error("No row for zone " + std::to_string(zone));
}
// Our shapes registered in a view's registry for its world: zone id -> shape.
std::map<int, Obj> Shapes(Obj view) {
    std::map<int, Obj> shapes;
    const int world = static_cast<int>(Slot(Get(view).fields.at("<dugi>k__BackingField"), "id"));
    for (const auto& [name, shape] : Get(Registry(view, world)).stringKeys)
        if (name.starts_with("dofus_miniatures_ocre_")) shapes[std::stoi(name.substr(22))] = shape;
    return shapes;
}
std::set<int> Ids(const std::map<int, Obj>& shapes) { std::set<int> ids; for (const auto& [id, _] : shapes) ids.insert(id); return ids; }
bool Near(float a, float b) { return std::abs(a - b) < 0.002f; }
bool SameColor(const archiZones::Color& c, int band, float alpha) {
    const auto& rgb = archiZones::Scale.at(band);
    return Near(c.r, rgb.r) && Near(c.g, rgb.g) && Near(c.b, rgb.b) && Near(c.a, alpha);
}
Obj Find(Obj root, const std::string& type, const std::string& text = {}) {
    if (Get(root).type->name == type && (text.empty() || Get(root).text == text)) return root;
    for (auto child : Get(root).children) if (auto found = Find(child, type, text)) return found;
    return nullptr;
}
int LastWant(bool& inventory, bool& quest) {
    if (wants.empty()) return -1;
    const auto& [client, i, q] = wants.back();
    inventory = i; quest = q;
    return static_cast<int>(client);
}

// ------------------------------------------------------------ the tests
void ModelAndColours() {
    using namespace archiZones;
    Check(Fold("Île de Frimousse") == "ile de frimousse" && Fold("Pétrifié l’Ébène") == "petrifie l'ebene" && Fold("ŒIL") == "oeil",
        "Search folds case and French accents");
    Check(Level(" 120 ") == 120 && Level("") == 0 && Level("12a") == 0 && Level("123456") == 999, "Level fields: digits only, bounded");
    Check(Band(0) == -1 && Band(1) == 0 && Band(2) == 1 && Band(3) == 2 && Band(4) == 3 && Band(5) == 3 && Band(6) == 4 && Band(10) == 4,
        "Colour scale by missing count: 1, 2, 3, 4-5, 6+");
    Check(SameColor(Fill(0, false), 0, .35f) && SameColor(Fill(4, true), 4, .25f) && SameColor(Line(2), 2, .8f),
        "Fill 0.35 on the big map, 0.25 on the mini-map, outline 0.8");
    Check(Near(Scale[0].r, 1.0f) && Near(Scale[0].g, 213 / 255.0f) && Near(Scale[0].b, 74 / 255.0f) &&
        Near(Scale[4].r, 142 / 255.0f) && Near(Scale[4].g, 27 / 255.0f) && Near(Scale[4].b, 90 / 255.0f), "Colours #FFD54A to #8E1B5A");
    std::cout << "PASS: model folding, levels and colour scale\n";
}
void InstallNoHooks() {
    Scenario s;
    archiZones::installed = false;
    InstallArchiZones();
    Check(archiZones::installed && hooks == 0, "Installed without any hook of its own");
    Check(buttonHandler == archiZones::ButtonClick && rowHandler == archiZones::RowClick, "Clicks come from the shared native click handlers");
    Check(guards.at("fhw.besn") == 0x1BF0FE0 && guards.at("fhi.bejv") == 0x1BB4090 && guards.at("fhi.beki") == 0x1BB5E70 &&
        guards.at("fhi.belb") == 0x1BB8D40 && guards.at("CartographyMapUIBinding.get_btn_conquest") == 0x2EA0A00 &&
        guards.at("CartographyMapUIBinding.get_ctr_worldmapButtons") == 0x2EA2830 && guards.at("fgg.bege") == 0x1BAE1D0 &&
        guards.at("fgg.begd") == 0x1BAE0A0 && guards.at("fgf.befi") == 0x1BAC490 && guards.at("fgf.befk") == 0x1BAC570 &&
        guards.at("fgf.befo") == 0x1BAC7A0 && guards.at("SubAreasDataRoot.GetSubAreaById") == 0x7D1060 &&
        guards.at("MonsterData.GetById") == 0x7E76B0 && guards.at("WindowFigma.get_closeButton") == 0x1798CF0 &&
        guards.at("TableColumnHeader.set_state") == 0x1785650, "Exact verified native contracts");
    std::cout << "PASS: installation verifies its natives, hooks nothing, shares the click handlers\n";
}
void OffIsInert() {
    Scenario s;
    calls = 0;
    Pump(5);
    Check(calls == 0 && !Square() && !PanelWindow() && wants.empty() && catalogWants == 0 && creations.empty() && !OurLayer(bigRoot) &&
        !OurLayer(miniRoot) && !ArchiZonesCleanupPending(), "Both options off: no square, no zone, no reading, no native call at all");
    Check(!buttonHandler(conquest) && !rowHandler(Make("ListElementBasic")), "Foreign controls are never ours");
    std::cout << "PASS: options off are fully inert\n";
}
void SquareFollowsOption() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    Pump(2);
    Check(!Square(), "The square needs the big map option");
    s.owner.settings.archiZonesMiniMap = false;
    s.owner.settings.archiZonesMap = true;
    Pump(1);
    auto square = Square();
    const auto& c = Get(buttons).children;
    Check(square && c.size() == 5 && c[0] == worldButton && c[1] == divider && c[2] == anomaly && c[3] == conquest && c[4] == square,
        "The Ocre square sits right under Conquête, native squares untouched and in order");
    Check(Get(square).ints.at("icon") == archiZones::IconArchimonster && Get(square).ints.at("mainStyle") == 1 &&
        Get(square).ints.at("enableRichText") == 0 && StyleLength(square, "marginTop").value == 4,
        "The game's own mode square look: secondary style, archimonster icon, no rich text, 4 px apart");
    Check(Get(square).strings.at("tooltip").find("Ocre") == 0 && !PanelWindow() && creations.empty(), "Tooltip; the mode starts off");
    Pump(3);
    Check(Square() == square && Get(buttons).children.size() == 5, "Ticks keep one square");
    s.owner.settings.archiZonesMap = false;
    Pump(1);
    Check(!Square() && Get(buttons).children.size() == 4 && !ArchiZonesCleanupPending(), "Option off: the square leaves");
    s.owner.settings.archiZonesMap = true;
    Pump(1);
    Check(Square() && Square() != square, "Option on again: the square returns");
    cartographyWindow = nullptr;
    Pump(1);
    Check(!Square(), "Closed map: no square kept");
    std::cout << "PASS: mode square only with the option, under Conquête, native look\n";
}
void ModePanelAndData() {
    Scenario s;
    s.owner.settings.archiZonesMap = true;
    Slot(window, "dubo") = 1; // the game's anomaly mode is on
    Pump(1);
    Check(buttonHandler(Square()), "The square's native click is ours");
    Pump(1);
    Check(archiZones::active && leaves == std::vector<int>{0} && Slot(window, "dubo") == 0,
        "Entering Ocre leaves the anomaly mode the way its window's close button does");
    auto panel = PanelWindow();
    Check(panel && Get(panel).strings.at("title") == "Ocre" && Get(panel).ints.at("headerStyle") == 1, "Native window titled Ocre, secondary header");
    Check(StyleInt(panel, "position") == 1 && StyleLength(panel, "right").value == 16 && StyleLength(panel, "top").value == 10 &&
        StyleLength(panel, "top").unit == 1 && StyleLength(panel, "width").value == 560 && StyleLength(panel, "height").value == 700,
        "The anomaly panel's place and size");
    Check(archiZones::panel->statusText == archiZones::LoadingText, "Loading state while the zones are read");
    bool inventory = false, quest = false;
    Check(LastWant(inventory, quest) == static_cast<int>(SoulClient::Map) && inventory && quest && catalogWants > 0,
        "The map asks the shared readers (bag, quest) and the Metamob list while shown");
    auto search = Find(panel, "TextInput");
    Check(search && Get(search).ints.at("icon") == archiZones::IconMagnifier && Get(search).strings.at("placeholderText") == "Rechercher" &&
        StyleLength(search, "width").value == 254, "Search field with the anomaly panel's magnifier");
    Pump(12);
    Check(archiZones::build.ready, "Zones read from the game within a few pumps");
    Check(monsterReads.size() == chat_souls::OcreGoals.size(), "Every Ocre goal read from MonsterData");
    for (const auto& [id, count] : monsterReads) Check(count == 1, "Each monster read once");
    for (const auto& [id, count] : subareaReads) Check(count == 1, "Each zone read once");
    Check(archiZones::model.zones.size() == 6, "Six zones from the game's MonsterData.subareas");
    const auto& zone10 = archiZones::model.zones.at(archiZones::build.zoneIndex.at(10));
    Check(zone10.name == "Coin des Bouftous" && zone10.area == "Amakna" && zone10.level == 10 && zone10.world == 1 && zone10.shape &&
        zone10.goals.size() == 4, "Zone fields and its four archimonsters (a duplicate subarea counted once)");
    const auto& zone30 = archiZones::model.zones.at(archiZones::build.zoneIndex.at(30));
    Check(!zone30.shape && zone30.dungeon == 7, "A boss's dungeon zone, hidden on the world map");
    std::vector<std::string> areas;
    for (auto item : Get(archiZones::panel->areas.get()).items) areas.push_back(Get(item).text);
    Check(areas == std::vector<std::string>{"Tous les territoires", "Amakna", "Cania", "Frigost", "Otomaï"}, "Territories, sorted");
    std::vector<std::string> choices;
    for (auto item : Get(archiZones::panel->type.get()).items) choices.push_back(Get(item).text);
    Check(choices == std::vector<std::string>{"Tous", "Archimonstres", "Boss de donjon"}, "Type choice: all, archimonsters, dungeon bosses");
    // Ocre still unknown: nothing coloured, the hint instead.
    Check(Rows().empty() && Shown(archiZones::panel->noResult.get()) && !Shown(archiZones::panel->content.get()) &&
        archiZones::panel->noResultText == archiZones::OcreUnknownText && archiZones::panel->statusText == archiZones::OcreUnknownText &&
        creations.empty(), "Unknown Ocre progress colours nothing and invites to open the quest journal");
    std::cout << "PASS: Ocre mode, native panel, runtime data, unknown Ocre hint\n";
}
void Activate(Scenario& s) {
    s.owner.settings.archiZonesMap = true;
    Pump(1);
    buttonHandler(Square());
    Pump(14);
    Check(archiZones::build.ready && archiZones::panel, "Ocre mode ready");
}
void MissingAndShapes() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    Check(Rows() == std::vector<int>{10, 20, 30, 50, 31}, "Missing zones sorted by missing count, then level");
    const auto& row10 = RowOf(10);
    Check(row10.countText == "2" && row10.levelText == "10" && row10.icon == archiZones::IconArchimonster && row10.name == "Coin des Bouftous",
        "Zone row: name, level, missing count (2271 in the bag, 2272 on Metamob: not missing)");
    Check(row10.tooltip.find("Bouftonnaire l’Ancestral") != std::string::npos && row10.tooltip.find("Pétrifié l’Ébène") != std::string::npos &&
        row10.tooltip.find("Craqueleur") == std::string::npos && row10.tooltip.find("Scarafeuille le Rouge") == std::string::npos,
        "The row's tooltip names the missing monsters only");
    Check(RowOf(30).icon == archiZones::IconBoss && RowOf(30).tooltip.find("Dragon Cochon (boss)") != std::string::npos &&
        RowOf(30).tooltip.find("Donjon sans contour") != std::string::npos, "A not started boss's dungeon zone is listed even without a shape");
    Check(SameColor(StyleColor(row10.swatch.get(), "backgroundColor"), 1, .9f), "Row swatch in the zone's colour");
    Check(archiZones::panel->statusText ==
        "3 archimonstres et 2 boss de donjon manquants, dans 5 zones.\nOcre : lu · Sac : lu · Metamob : ? (pas encore lu)",
        "Totals and sources in the panel's footer");
    // The big map: outlined zones of world 1 among the rows.
    auto shapes = Shapes(bigView);
    Check(Ids(shapes) == std::set<int>{10, 20, 31}, "Coloured shapes for the outlined zones of the map's world only");
    for (const auto& c : creations) Check(c.view == bigView && c.fia == -1 && Near(c.thickness, 2) && !c.flag && c.bonus == -1,
        "The game's AddAreaShape with the anomalies' outline");
    Check(SameColor(Get(shapes[10]).fill, 1, .35f) && SameColor(Get(shapes[10]).line, 1, .8f) && SameColor(Get(shapes[20]).fill, 0, .35f),
        "Zone colour by missing count");
    auto layer = OurLayer(bigRoot);
    Check(layer && Get(layer).ints.at("picking") == 1 && StyleInt(layer, "position") == 1 && StyleLength(layer, "width").unit == 1,
        "The mod's own full-size, unpickable layer");
    const auto& rootChildren = Get(bigRoot).children;
    const auto layerAt = std::find(rootChildren.begin(), rootChildren.end(), layer) - rootChildren.begin();
    const auto firstAt = std::find(rootChildren.begin(), rootChildren.end(), Get(bigView).layers.at(0)) - rootChildren.begin();
    Check(layerAt == 1 && firstAt == 2, "Above the map picture, just under the first native layer");
    for (const auto& [id, shape] : shapes)
        Check(Get(Get(shape).fields.at("<dtwe>k__BackingField")).fields["parent"] == layer && Get(shape).hover, "Shapes in the mod's layer; hover kept on the big map");
    // The game's world refresh hides every registered shape: ours come back.
    for (const auto& [id, shape] : shapes) {
        auto root = Get(shape).fields.at("<dtwe>k__BackingField");
        Check(Shown(root), "Adopted shapes are shown");
        Get(root).styles["display"] = {1};
    }
    Pump(11);
    for (const auto& [id, shape] : shapes) Check(Shown(Get(shape).fields.at("<dtwe>k__BackingField")), "Shapes shown again after the game hid them");
    // Bag unknown: counted as not in the bag; only the changed zone is repainted.
    const int paints20 = Get(shapes[20]).paints, paints10 = Get(shapes[10]).paints;
    bag.clear();
    ++soulRevision;
    Pump(6);
    Check(RowOf(10).countText == "3" && SameColor(Get(shapes[10]).fill, 2, .35f) && Get(shapes[10]).paints == paints10 + 1 &&
        Get(shapes[20]).paints == paints20 && creations.size() == 3, "Unknown bag counts as absent; only the changed zone is recoloured");
    Check(archiZones::panel->statusText.find("Sac : ? (compté comme absent)") != std::string::npos, "Unknown bag told in the legend");
    // The Metamob list's state in the legend: why it gives no quantity.
    const auto metamobStates = metamobQuantities;
    const std::pair<metamob::OcreList, const char*> listStates[] = {
        {metamob::OcreList::NoQuest, "Metamob : aucune quête Ocre Dofus Unity sur ton compte"},
        {metamob::OcreList::SeveralQuests, "Metamob : plusieurs quêtes Ocre, aucune n’indique ce personnage"},
        {metamob::OcreList::Failed, "Metamob : ? (lecture impossible, nouvel essai automatique)"}};
    for (const auto& [list, text] : listStates) {
        for (auto& [id, value] : metamobQuantities) value = metamob::OcreQuantity{true, -1, list};
        Pump(12);
        Check(archiZones::panel->statusText.find(text) != std::string::npos, "The legend says why Metamob gives nothing");
    }
    metamobQuantities = metamobStates;
    Pump(12);
    Check(archiZones::panel->statusText.find("Metamob : ? (pas encore lu)") != std::string::npos, "Back to not read yet");
    // No usable Metamob key: Metamob ignored.
    metamobQuantities.clear();
    Pump(12);
    Check(RowOf(10).countText == "4" && SameColor(Get(shapes[10]).fill, 3, .35f) &&
        archiZones::panel->statusText.find("Metamob : non utilisé") != std::string::npos, "Without a usable Metamob key, Metamob is ignored");
    // A zone no longer missing loses its shape; the native anomaly shape stays.
    ocre[2270] = SoulState::Done;
    ++soulRevision;
    Pump(6);
    shapes = Shapes(bigView);
    Check(Ids(shapes) == std::set<int>{10, 31} && Get(Get(Registry(bigView, 1)).stringKeys.at("anomaly_shape99")).disposed == false,
        "Zone 20 complete: its shape is disposed and unregistered; native shapes untouched");
    Check(std::count_if(objects.begin(), objects.end(), [](const auto& o) { return o->type->name == "fgf" && o->disposed; }) == 1,
        "Exactly our completed zone disposed");
    // Ocre unknown again: nothing coloured.
    ocre.clear();
    ++soulRevision;
    Pump(6);
    Check(Shapes(bigView).empty() && Rows().empty() && archiZones::panel->noResultText == archiZones::OcreUnknownText,
        "Ocre unknown: every shape removed, the hint shown");
    std::cout << "PASS: missing per Ocre, bag and Metamob state, colours, shapes, recolour only changed zones\n";
}
void SetText(Obj input, const std::string& text) { Get(input).text = text; }
void Choose(Obj dropdown, int id) { for (auto item : Get(dropdown).items) if (Slot(item, "id") == id) Get(dropdown).fields["selected"] = item; }
void FiltersAndSort() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    auto& p = *archiZones::panel;
    Choose(p.type.get(), 1);
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20, 50} && Ids(Shapes(bigView)) == std::set<int>{10, 20} &&
        Get(p.type.get()).strings.at("choiceText") == "Archimonstres", "Archimonsters only, on the list and the map");
    Choose(p.type.get(), 2);
    Pump(2);
    Check(Rows() == std::vector<int>{30, 31} && Ids(Shapes(bigView)) == std::set<int>{31} &&
        p.statusText.find("2 boss de donjon manquants, dans 2 zones.") == 0, "Dungeon bosses only (the dungeon has no outline)");
    Choose(p.type.get(), 0);
    SetText(p.search.get(), "bouf");
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20}, "Search by zone name or by a missing monster's name");
    SetText(p.search.get(), "EBENE");
    Pump(2);
    Check(Rows() == std::vector<int>{10}, "Search without case or accents");
    SetText(p.search.get(), " plaine ");
    Pump(2);
    Check(Rows() == std::vector<int>{20, 31} && Ids(Shapes(bigView)) == std::set<int>{20, 31}, "The map follows the panel's filters");
    SetText(p.search.get(), "zzz");
    Pump(2);
    Check(Rows().empty() && p.noResultText == archiZones::NoMatchText && Shown(p.noResult.get()) && Shapes(bigView).empty(), "No match told");
    SetText(p.search.get(), "");
    Choose(p.areas.get(), 2);
    Pump(2);
    Check(Rows() == std::vector<int>{31} && Get(p.areas.get()).strings.at("choiceText") == "Cania", "Territory choice");
    Choose(p.areas.get(), 0);
    SetText(p.minLevel.get(), "50");
    SetText(p.maxLevel.get(), "90");
    Pump(2);
    Check(Rows() == std::vector<int>{30, 50}, "Level range");
    SetText(p.minLevel.get(), "");
    SetText(p.maxLevel.get(), "");
    Pump(2);
    // Sortable native columns: the clicked one wins, the others go inactive.
    Check(Get(p.headers[2].get()).ints.at("state") == 2 && p.headers[0].get() && Get(p.headers[1].get()).text == "Niveau",
        "Missing column sorted descending by default");
    Get(p.headers[1].get()).ints["state"] = 1; // a native click on « Niveau »
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20, 30, 50, 31} && Get(p.headers[2].get()).ints.at("state") == 0, "Level ascending, Missing reset");
    Get(p.headers[1].get()).ints["state"] = 2;
    Pump(2);
    Check(Rows() == std::vector<int>{31, 50, 30, 20, 10}, "Level descending");
    Get(p.headers[0].get()).ints["state"] = 1;
    Pump(2);
    Check(Rows() == std::vector<int>{30, 10, 50, 20, 31} && Get(p.headers[1].get()).ints.at("state") == 0, "Names ascending");
    std::cout << "PASS: panel search, territory, level range, type and sortable columns\n";
}
void RowClicks() {
    Scenario s;
    Activate(s);
    MainStates();
    ocre[2275] = SoulState::Missing; // zone 40, in another world
    ++soulRevision;
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{10, 20, 31}, "A listed zone of another world is not drawn on this one");
    Check(rowHandler(RowOf(10).element.get()), "A zone row's native click is ours");
    Pump(1);
    Check(moves == std::vector<std::pair<int, int>>{{4, -2}} && worldSwitches.empty() && RowOf(10).activated, "Centred on the zone's centre");
    rowHandler(RowOf(30).element.get());
    Pump(1);
    Check(moves.back() == std::pair<int, int>{-1, -10} && worldSwitches.empty() && !RowOf(10).activated && RowOf(30).activated,
        "A dungeon zone centres on the dungeon's entrance");
    rowHandler(RowOf(40).element.get());
    Pump(1);
    Check(worldSwitches == std::vector<int>{3} && moves.back() == std::pair<int, int>{-75, -40}, "Another world: switched first, as the anomaly rows do");
    Check(!rowHandler(Make("ListElementBasic", true)), "Foreign rows are not ours");
    Pump(3);
    Check(Ids(Shapes(bigView)) == std::set<int>{40}, "The map now on world 3: only its zone is coloured");
    std::cout << "PASS: row click centres the map (zone, dungeon entrance, other world)\n";
}
void MiniMap() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    MainStates();
    Pump(16);
    Check(!Square() && !PanelWindow() && Ids(Shapes(miniView)) == std::set<int>{10, 20, 31} && Shapes(bigView).empty(),
        "Mini-map zones without the big map or its panel");
    for (const auto& [id, shape] : Shapes(miniView)) Check(!Get(shape).hover && Near(Get(shape).fill.a, .25f), "No map info hover, lighter fill");
    Check(OurLayer(miniRoot) != nullptr, "The mini-map's own layer");
    bool inventory = false, quest = false;
    Check(LastWant(inventory, quest) == static_cast<int>(SoulClient::Map) && inventory && quest, "Readers wanted for the mini-map");
    s.owner.settings.archiZonesMap = true;
    Pump(1);
    buttonHandler(Square());
    Pump(2);
    Choose(archiZones::panel->type.get(), 2);
    Pump(2);
    Check(Ids(Shapes(miniView)) == std::set<int>{31}, "The mini-map follows the panel's type choice");
    s.owner.settings.archiZonesMiniMap = false;
    Pump(2);
    Check(Shapes(miniView).empty() && !OurLayer(miniRoot) && Ids(Shapes(bigView)) == std::set<int>{31}, "Mini-map option off: its zones and layer leave");
    std::cout << "PASS: mini-map zones toggle and follow the type choice\n";
}
void LeavingTheMode() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    Check(!Shapes(bigView).empty(), "Zones shown");
    Check(buttonHandler(Get(archiZones::panel->window.get()).fields.at("closeButton")), "The panel's close button is ours");
    Pump(2);
    Check(!archiZones::active && !PanelWindow() && Shapes(bigView).empty() && !OurLayer(bigRoot) && Square(), "Closing leaves the mode");
    bool inventory = true, quest = true;
    Check(LastWant(inventory, quest) == static_cast<int>(SoulClient::Map) && !inventory && !quest, "Nothing shown: the map releases its readers");
    buttonHandler(Square());
    Pump(3);
    Check(archiZones::active && PanelWindow() && !Shapes(bigView).empty(), "The square enters it again");
    Slot(window, "dubo") = 2; // the game's conquest mode chosen
    Pump(2);
    Check(!archiZones::active && !PanelWindow() && Shapes(bigView).empty(), "Choosing Conquête leaves the Ocre mode");
    buttonHandler(Square());
    Pump(3);
    buttonHandler(Square());
    Pump(2);
    Check(!archiZones::active && !PanelWindow(), "A second click on the square leaves it too");
    buttonHandler(Square());
    Pump(3);
    cartographyWindow = nullptr;
    Pump(2);
    Check(!Square() && !PanelWindow() && Shapes(bigView).empty() && !archiZones::active,
        "Map closed: UI and zones leave, and the Ocre mode ends, as the game's own modes do");
    cartographyWindow = window;
    Pump(3);
    Check(Square() && !PanelWindow() && Shapes(bigView).empty(), "Reopened: the square only, the Ocre mode off");
    std::cout << "PASS: leaving the mode (close, Conquête, square, map closed)\n";
}
void BudgetsAndCadence() {
    Scenario s;
    // 24 more archimonsters, each alone in an outlined zone of world 1.
    int added = 0;
    for (const auto& goal : chat_souls::OcreGoals) {
        if (goal.step < archiZones::FirstArchimonsterStep || monsters.contains(goal.monster) || added == 24) continue;
        const int zone = 1000 + added++;
        AddZone({zone, "Zone d’essai", "Amakna", 20, 1, true, 0, 0, 0});
        AddMonster(goal.monster, "Essai", {static_cast<uint32_t>(zone)});
    }
    MainStates();
    for (const auto& [id, monster] : monsters) if (id != 2271 && id != 2272 && id != 2275) ocre[static_cast<int>(id)] = SoulState::Missing;
    Activate(s);
    Pump(4);
    buttonHandler(Square());
    Pump(1);
    Check(!archiZones::active && Shapes(bigView).empty(), "Restart from no shape");
    creations.clear();
    buttonHandler(Square());
    Pump(1);
    Check(creations.size() <= archiZones::CreateBudget && !creations.empty(), "At most twelve shapes asked per pump");
    Pump(1);
    Check(creations.size() <= 2 * archiZones::CreateBudget, "Creation continues within the budget");
    Pump(4);
    Check(Shapes(bigView).size() == 27, "Every outlined missing zone eventually coloured");
    // Recount cadence, with explicit pump times.
    const uint64_t t = clock;
    archiZones::build.counted = false;
    archiZones::Count(t);
    const int counted = ocreReads;
    archiZones::Count(t + 200);
    Check(ocreReads == counted, "No recount without a change within two seconds");
    ++soulRevision;
    archiZones::Count(t + 400);
    Check(ocreReads == counted, "At most one recount per second, even after a change");
    archiZones::Count(t + 1000);
    const int changed = ocreReads;
    Check(changed > counted, "The change is counted once the second is over");
    archiZones::Count(t + 1200);
    Check(ocreReads == changed, "No recount without another change");
    archiZones::Count(t + 3001);
    Check(ocreReads > changed, "Every two seconds anyway (Metamob list, bag validity)");
    std::cout << "PASS: budgeted shape creation, recount at most once per second\n";
}
void CleanupAndRestore() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    Activate(s);
    MainStates();
    ocre[2270] = SoulState::Done;
    ++soulRevision;
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{10, 31} && Ids(Shapes(miniView)) == std::set<int>{10, 31}, "Both maps coloured");
    // A shape asked for but not registered yet is never left behind.
    ocre[2270] = SoulState::Missing;
    ++soulRevision;
    archiZones::Tick(clock); clock += 1200;
    archiZones::Tick(clock); clock += 200;
    Check(!pendingCreations.empty(), "A shape is still being created");
    archiZones::RestoreAll(clock);
    Check(ArchiZonesCleanupPending() && !Square() && !PanelWindow(), "Cleanup waits for the shape still being created");
    CompleteShapes();
    archiZones::RestoreAll(clock + 200);
    Check(!ArchiZonesCleanupPending() && Shapes(bigView).empty() && Shapes(miniView).empty() && !OurLayer(bigRoot) && !OurLayer(miniRoot),
        "Every shape, layer and control removed");
    Check(Get(Registry(bigView, 1)).stringKeys.contains("anomaly_shape99"), "Native shapes kept");
    bool inventory = true, quest = true;
    Check(LastWant(inventory, quest) == static_cast<int>(SoulClient::Map) && !inventory && !quest, "Readers released");
    Check(handles.size() == s.baseline, "No managed reference kept");
    s.owner.settings.archiZonesMap = s.owner.settings.archiZonesMiniMap = false;
    calls = 0;
    Pump(3);
    Check(calls == 0, "Inert again after cleanup");
    s.owner.settings.archiZonesMap = true;
    s.owner.stopRequested = true;
    Pump(1);
    Check(!Square() && !ArchiZonesCleanupPending(), "A stopping module shows nothing");
    std::cout << "PASS: cleanup and restore (no shape left behind, readers released, references freed)\n";
}
// A shape asked for just before the map changes world is registered by the game
// under the new one (AddAreaShape reads the world when it resumes): disposed
// there at once, never left drawn.
void WorldChangeWhileCreating() {
    Scenario s;
    Activate(s);
    MainStates();
    ocre[2270] = SoulState::Done;
    ++soulRevision;
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{10, 31}, "Coloured");
    ocre[2270] = SoulState::Missing;
    ++soulRevision;
    archiZones::Tick(clock); clock += 1200;
    archiZones::Tick(clock); clock += 200;
    Check(!pendingCreations.empty(), "A shape is still being created");
    Slot(bigWorld, "id") = 3; // another world shown before the game resumes it
    CompleteShapes();
    const auto landed = Shapes(bigView);
    Check(Ids(landed) == std::set<int>{20}, "The game registered it under the new world");
    Pump(1);
    Check(Shapes(bigView).empty() && Get(landed.at(20)).disposed, "Disposed under the world it landed in, at once");
    bool left = false;
    for (const auto& [name, shape] : Get(Registry(bigView, 1)).stringKeys) left |= name.starts_with("dofus_miniatures_ocre_");
    Check(!left && Get(Registry(bigView, 1)).stringKeys.contains("anomaly_shape99"), "Nothing of ours left under the old world");
    Pump(20);
    Check(Shapes(bigView).empty(), "The new world has no missing zone: nothing drawn there");
    std::cout << "PASS: a shape registered under another world by a world change is disposed there\n";
}
void ContractMismatchDisablesOnlyThis() {
    Scenario s;
    archiZones::installed = false;
    offsets[{"fhi", "dubo"}] = 0x530;
    InstallArchiZones();
    offsets[{"fhi", "dubo"}] = 0x52C;
    Check(!archiZones::installed && !logs.empty() && logs.back().starts_with("ARCHI_ZONES_DISABLED"), "A field mismatch disables only the Ocre zones");
    s.owner.settings.archiZonesMap = s.owner.settings.archiZonesMiniMap = true;
    calls = 0;
    TickArchiZones();
    RestoreArchiZones();
    Check(calls == 0 && !ArchiZonesCleanupPending(), "Disabled: inert");
    failMethod = "besn";
    InstallArchiZones();
    Check(!archiZones::installed && logs.back().find("besn") != std::string::npos, "A missing verified method disables it too");
    InstallArchiZones();
    Check(archiZones::installed, "Installs once the contracts match");
    std::cout << "PASS: a contract mismatch disables only the Ocre zones\n";
}
void PanelFailureRecovers() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    archiZones::nextLog = 0;
    failMethod = "set_state";
    Get(archiZones::panel->headers[1].get()).ints["state"] = 1;
    Pump(1);
    Check(!PanelWindow() && !logs.empty() && logs.back().starts_with("ARCHI_ZONES_ERROR panel"), "A native failure in the panel removes it and is logged");
    Pump(30);
    Check(PanelWindow() && !Rows().empty(), "The panel comes back after a pause");
    std::cout << "PASS: panel failure recovery\n";
}
}

int main() {
    try {
        fake::Init();
        fake::ModelAndColours();
        fake::InstallNoHooks();
        fake::OffIsInert();
        fake::SquareFollowsOption();
        fake::ModePanelAndData();
        fake::MissingAndShapes();
        fake::FiltersAndSort();
        fake::RowClicks();
        fake::MiniMap();
        fake::LeavingTheMode();
        fake::BudgetsAndCadence();
        fake::CleanupAndRestore();
        fake::WorldChangeWhileCreating();
        fake::PanelFailureRecovers();
        fake::ContractMismatchDisablesOnlyThis();
        std::cout << "PASS: Ocre map mode and zones (big map panel, mini-map, shared readers, cleanup)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fake::logs) std::cerr << line << '\n';
        return 1;
    }
}
