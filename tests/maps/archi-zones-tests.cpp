#include "maps/archi-zones.cpp"
#include "appearance/features.hpp"
#include "core/signatures.hpp"
#include "ui/options-ui.hpp"
#include <chrono>
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
struct Rect { float x, y, width, height; };
struct Vec3 { float x, y, z; };
struct Object {
    Type* type{};
    std::string text, name;
    std::map<std::string, Obj> fields;
    std::map<std::string, std::array<int64_t, 2>> slots; // FieldAddress storage (16 bytes each)
    std::map<std::string, std::array<unsigned char, 16>> styles;
    std::map<std::string, std::string> strings;
    std::map<std::string, int> ints;
    std::vector<Obj> children, items;
    std::vector<std::string> classes;
    std::map<int, Obj> intKeys, layers;
    std::map<std::string, Obj> stringKeys;
    std::array<unsigned char, 64> data{};
    archiZones::Color fill{}, line{};
    Rect bounds{};
    int paints = 0, repaints = 0, reveals = 0;
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
int64_t& Slot(Obj object, const char* name) { return Get(object).slots[name][0]; }
// fgf's stored fill and outline (dtvm, dtvn, written by befi and befk), which
// its vertices show.
void SetFill(Obj shape, const archiZones::Color& c) { Get(shape).fill = c; std::memcpy(Get(shape).slots["dtvm"].data(), &c, sizeof c); }
void SetLine(Obj shape, const archiZones::Color& c) { Get(shape).line = c; std::memcpy(Get(shape).slots["dtvn"].data(), &c, sizeof c); }
archiZones::Color Stored(Obj shape, const char* field) { archiZones::Color c{}; std::memcpy(&c, Get(shape).slots[field].data(), sizeof c); return c; }
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
template<class T> void SetStyle(Obj element, const char* name, T value) {
    std::array<unsigned char, 16> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(value));
    Get(element).styles[name] = bytes;
}
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
// What UI Toolkit resolved at its last style pass: (display, opacity) per element.
std::map<Obj, std::pair<int, float>> resolvedStyles;

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
// The frames between two pumps: the game's AddAreaShape finishes its work (one
// UniTask.Yield) and registers the shapes asked for. As fgf..ctor does, a hover
// shape (isFixed false) is built transparent, a fixed one opaque.
void CompleteShapes() {
    for (const auto& c : pendingCreations) {
        auto shape = Make("fgf"), root = Make("VisualElement");
        auto parent = Layer(c.view, c.fia);
        Get(shape).fields["<dtwe>k__BackingField"] = root;
        Get(shape).fields["<dtwq>k__BackingField"] = parent;
        SetFill(shape, c.fill);
        SetLine(shape, c.line);
        Get(shape).ints["fixed"] = c.flag ? 1 : 0;
        const Vec3 scale{.6f, .6f, .6f};
        std::memcpy(Get(shape).slots["dtvo"].data(), &scale, sizeof(scale));
        const float one = 1.0f; // the constructor's hover factors
        std::memcpy(Get(shape).slots["dtvk"].data(), &one, sizeof one);
        std::memcpy(Get(shape).slots["dtvl"].data(), &one, sizeof one);
        SetStyle(root, "display", 0);
        SetStyle(root, "opacity", c.flag ? 1.0f : 0.0f);
        Get(root).bounds = {-300, -200, 1600, 1200};
        Add(parent, root);
        const int world = static_cast<int>(Slot(Get(c.view).fields.at("<dugi>k__BackingField"), "id"));
        Get(Registry(c.view, world)).stringKeys[c.name] = shape;
    }
    pendingCreations.clear();
}
float FloatSlot(Obj object, const char* name) { float value = 0; std::memcpy(&value, Get(object).slots[name].data(), sizeof value); return value; }
// The pointer reaches a hover shape only (fgf..ctor registers its enter and
// leave handlers when isFixed is false), pickable and displayed with every
// ancestor.
bool Pickable(Obj shape) {
    if (Get(shape).ints["fixed"]) return false;
    const Obj root = Get(shape).fields.at("<dtwe>k__BackingField");
    if (Get(root).ints.contains("picking") && Get(root).ints.at("picking") == 1) return false;
    for (Obj node = root; node; node = Get(node).fields["parent"]) if (!Shown(node)) return false;
    return true;
}
archiZones::Color Scaled(const archiZones::Color& c, float k, float l, bool up) {
    return up ? archiZones::Color{c.r * k, c.g * k, c.b * k, c.a * l} : archiZones::Color{c.r / k, c.g / k, c.b / k, c.a / l};
}
// fgf.befs and fgf.befr as the game runs them (UI Toolkit: a leave only after
// an enter): the stored outline scaled by (k, k, k, l) on enter and divided back
// on leave; with the constructor's l = 1 the shape fades in and out (opacity),
// otherwise its stored fill is scaled too.
void Hover(Obj shape, bool enter) {
    auto& o = Get(shape);
    if (enter == (o.ints["hovered"] != 0) || (enter && !Pickable(shape))) return;
    o.ints["hovered"] = enter ? 1 : 0;
    const float k = FloatSlot(shape, "dtvk"), l = FloatSlot(shape, "dtvl");
    SetLine(shape, Scaled(Stored(shape, "dtvn"), k, l, enter));
    if (l == 1.0f) SetStyle(o.fields.at("<dtwe>k__BackingField"), "opacity", enter ? 1.0f : 0.0f);
    else SetFill(shape, Scaled(Stored(shape, "dtvm"), k, l, enter));
}
void PointerEnter(Obj shape) { Hover(shape, true); }
void PointerLeave(Obj shape) { Hover(shape, false); }
// The game's zoom (fhw.bers, then beso): the map content sized in pixels, then
// every registered shape's stored parent sized 100 % and the shape put back in it.
void Zoom(Obj view, float width, float height) {
    const Obj root = Get(view).fields.at("dugq");
    SetStyle(root, "width", LengthValue{width, 0});
    SetStyle(root, "height", LengthValue{height, 0});
    const int world = static_cast<int>(Slot(Get(view).fields.at("<dugi>k__BackingField"), "id"));
    for (const auto& [name, shape] : Get(Registry(view, world)).stringKeys) {
        const Obj parent = Get(shape).fields["<dtwq>k__BackingField"];
        if (!parent) continue;
        SetStyle(parent, "width", LengthValue{100, 1});
        SetStyle(parent, "height", LengthValue{100, 1});
        const Obj shapeRoot = Get(shape).fields.at("<dtwe>k__BackingField");
        Detach(shapeRoot);
        Add(parent, shapeRoot);
    }
}
// The frames between two pumps resolve every element's inline display and opacity.
void StylePass() {
    resolvedStyles.clear();
    for (const auto& object : objects) {
        const auto display = object->styles.find("display"), opacity = object->styles.find("opacity");
        if (display == object->styles.end() && opacity == object->styles.end()) continue;
        std::pair<int, float> value{0, 1.0f};
        if (display != object->styles.end()) std::memcpy(&value.first, display->second.data(), 4);
        if (opacity != object->styles.end()) std::memcpy(&value.second, opacity->second.data(), 4);
        resolvedStyles[object.get()] = value;
    }
}
// The game showing a world (fhw.besm): every registered shape hidden.
void HideAll(Obj view) {
    for (const auto& [world, inner] : Get(Get(view).fields.at("<duic>k__BackingField")).intKeys)
        for (const auto& [name, shape] : Get(inner).stringKeys) SetStyle(Get(shape).fields.at("<dtwe>k__BackingField"), "display", 1);
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
    if (m.name == "befi") { SetFill(self, *static_cast<archiZones::Color*>(args[0])); ++o.paints; return nullptr; }
    if (m.name == "befk") { SetLine(self, *static_cast<archiZones::Color*>(args[0])); return nullptr; }
    if (m.name == "befo") { o.hover = *static_cast<bool*>(args[0]); return nullptr; }
    if (m.name == "bege") {
        Add(static_cast<Obj>(args[0]), o.fields.at("<dtwe>k__BackingField"));
        o.fields["<dtwq>k__BackingField"] = static_cast<Obj>(args[0]);
        return nullptr;
    }
    if (m.name == "begd") { Check(!o.disposed, "Shape disposed twice"); o.disposed = true; Detach(o.fields.at("<dtwe>k__BackingField")); return nullptr; }
    if (m.name == "begr") { Check(t == "fgh", "begr is fgh's"); SetStyle(o.fields.at("<dtwe>k__BackingField"), "opacity", 1.0f); ++o.reveals; return nullptr; }
    if (m.name == "MarkDirtyRepaint") { ++o.repaints; return nullptr; }
    if (m.name == "get_worldBound") return Box(o.bounds, "Rect");
    // The resolved style (one cached ResolvedStyleAccess per element): the
    // element's display and opacity at the last style pass, UI Toolkit's
    // defaults before its first one.
    if (m.name == "get_resolvedStyle") {
        auto& access = o.fields["resolvedStyle"];
        if (!access) { access = Make("ResolvedStyleAccess"); Get(access).fields["ve"] = self; }
        return access;
    }
    if (t == "ResolvedStyleAccess" && (m.name == "get_display" || m.name == "get_opacity")) {
        const auto found = resolvedStyles.find(o.fields.at("ve"));
        const std::pair<int, float> value = found == resolvedStyles.end() ? std::pair<int, float>{0, 1.0f} : found->second;
        return m.name == "get_display" ? Box(value.first) : Box(value.second);
    }
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
    if (m.name.starts_with("UnityEngine.UIElements.IStyle.get_")) {
        // The element's current inline value, as InlineStyleAccess returns it.
        auto value = Make("StyleValue");
        const auto& target = Get(o.fields.at("owner"));
        const auto found = target.styles.find(m.name.substr(std::string("UnityEngine.UIElements.IStyle.get_").size()));
        if (found != target.styles.end()) std::memcpy(Get(value).data.data(), found->second.data(), 16);
        return value;
    }
    if (m.name == "get_value") { auto value = Make("Box"); std::memcpy(Get(value).data.data(), o.data.data(), 16); return value; }
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
    for (const auto* name : {"Box", "Length", "StyleValue", "CartographyMapUIBinding", "UniTask", "Rect"}) Class(name)->value = true;
    Class("CartographyMapView")->parent = Class("fhw");
    Class("fgf")->parent = Class("fgg");
    Class("fgg")->parent = Class("fgh");
    offsets = {
        {{"fhi", "dtzx"}, 0x408}, {{"fhi", "dtzy"}, 0x410}, {{"fhi", "dtzz"}, 0x418}, {{"fhi", "dubo"}, 0x52C}, {{"fhi", "ducm"}, 0x5D0},
        {{"fhw", "<dugf>k__BackingField"}, 0x20}, {{"fhw", "<dugh>k__BackingField"}, 0x30},
        {{"fhw", "dugq"}, 0x68}, {{"fhw", "dugv"}, 0x90}, {{"fhw", "dugy"}, 0xA8}, {{"fhw", "<dugi>k__BackingField"}, 0x38},
        {{"fhw", "<duic>k__BackingField"}, 0x160}, {{"fhk", "dueo"}, 0x98}, {{"WorldMapData", "id"}, 0x10},
        {{"fgh", "<dtwe>k__BackingField"}, 0x10}, {{"fgh", "<dtwq>k__BackingField"}, 0x58}, {{"fgf", "dtvo"}, 0xE0},
        {{"fgf", "dtvk"}, 0xB8}, {{"fgf", "dtvl"}, 0xBC}, {{"fgf", "dtvm"}, 0xC0}, {{"fgf", "dtvn"}, 0xD0},
        {{"MonsterData", "subareas"}, 0x50},
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
    Get(root).bounds = {-300, -200, 1600, 1200};
    Add(root, Make("VisualElement")); // the map picture
    world = Make("WorldMapData");
    Slot(world, "id") = worldId;
    auto registry = Make("IntDictionary");
    Get(view).fields = {{"dugq", root}, {"dugv", service}, {"dugy", character}, {"<dugi>k__BackingField", world}, {"<duic>k__BackingField", registry}};
    const double zoom = .6, base = 1.0;
    std::memcpy(&Slot(view, "<dugf>k__BackingField"), &zoom, sizeof zoom);
    std::memcpy(&Slot(view, "<dugh>k__BackingField"), &base, sizeof base);
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
        archiZones::diags = {};
        archiZones::nextLog = 0;
        resolvedStyles.clear(); objects.clear(); creations.clear(); pendingCreations.clear(); monsters.clear(); mapInfos.clear(); subareas.clear();
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
        AddZone({60, "Champs de Cania", "Cania", 70, 1, true, 0, -25, -30});
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
        AddMonster(2277, "Tofu le Dodu", {60});
        AddMonster(2278, "Larve Orange le Lent", {60});
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
    for (int i = 0; i < count; ++i) { archiZones::Tick(clock); clock += 200; CompleteShapes(); StylePass(); }
}
// Ocre states of the main case: 2271 in the bag, 2272 on Metamob (1), 2273 at 0
// on Metamob, 2270 Metamob not read yet, 2275 validated (zone 40 complete), the
// boss 147 not started; zone 60 complete: 2277 validated (and 3 on Metamob),
// 2278 in the bag.
void MainStates() {
    ocre = {{2270, SoulState::Missing}, {2271, SoulState::Missing}, {2272, SoulState::Missing}, {2273, SoulState::Missing},
            {2275, SoulState::Done}, {2276, SoulState::Missing}, {2277, SoulState::Done}, {2278, SoulState::Missing},
            {147, SoulState::NotStarted}, {85, SoulState::Missing}};
    bag.clear();
    for (const auto& goal : chat_souls::OcreGoals) bag[goal.monster] = SoulState::Missing;
    bag[2271] = SoulState::Done;
    bag[2278] = SoulState::Done;
    metamobQuantities = {{2272, {true, 1}}, {2273, {true, 0}}, {2270, {true, -1}}, {2277, {true, 3}}};
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
Obj RootOf(Obj shape) { return Get(shape).fields.at("<dtwe>k__BackingField"); }
// Seen on the map: displayed and opaque (the game builds hover shapes transparent).
bool Visible(Obj shape) { auto root = RootOf(shape); return Shown(root) && StyleFloat(root, "opacity") > .99f; }
bool Near(float a, float b) { return std::abs(a - b) < 0.002f; }
bool SameColor(const archiZones::Color& c, int band, float alpha) {
    const auto& rgb = archiZones::Scale.at(band);
    return Near(c.r, rgb.r) && Near(c.g, rgb.g) && Near(c.b, rgb.b) && Near(c.a, alpha);
}
// As the game's enter handler leaves a revealed zone's colours: (1.2, 1.2, 1.2, 1.5).
bool SameHighlight(const archiZones::Color& c, int band, float alpha) {
    const auto& rgb = archiZones::Scale.at(band);
    return Near(c.r, rgb.r * 1.2f) && Near(c.g, rgb.g * 1.2f) && Near(c.b, rgb.b * 1.2f) && Near(c.a, alpha * 1.5f);
}
Obj Find(Obj root, const std::string& type, const std::string& text = {}) {
    if (Get(root).type->name == type && (text.empty() || Get(root).text == text)) return root;
    for (auto child : Get(root).children) if (auto found = Find(child, type, text)) return found;
    return nullptr;
}
void FindAll(Obj root, const std::string& type, std::vector<Obj>& found) {
    if (Get(root).type->name == type) found.push_back(root);
    for (auto child : Get(root).children) FindAll(child, type, found);
}
int LastWant(bool& inventory, bool& quest) {
    if (wants.empty()) return -1;
    const auto& [client, i, q] = wants.back();
    inventory = i; quest = q;
    return static_cast<int>(client);
}
std::string LastLog(const std::string& prefix) {
    for (auto it = logs.rbegin(); it != logs.rend(); ++it) if (it->starts_with(prefix)) return *it;
    return {};
}
size_t CountLogs(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.starts_with(prefix); }));
}
void SetText(Obj input, const std::string& text) { Get(input).text = text; }
void Choose(Obj dropdown, int id) { for (auto item : Get(dropdown).items) if (Slot(item, "id") == id) Get(dropdown).fields["selected"] = item; }

// ------------------------------------------------------------ the tests
void ModelAndColours() {
    using namespace archiZones;
    Check(Fold("Île de Frimousse") == "ile de frimousse" && Fold("Pétrifié l’Ébène") == "petrifie l'ebene" && Fold("ŒIL") == "oeil",
        "Search folds case and French accents");
    Check(Level(" 120 ") == 120 && Level("") == 0 && Level("12a") == 0 && Level("123456") == 999, "Level fields: digits only, bounded");
    Check(Band(0) == -1 && Band(1) == 0 && Band(2) == 1 && Band(3) == 2 && Band(4) == 3 && Band(5) == 3 && Band(6) == 4 && Band(10) == 4,
        "Colour scale by missing count: 1, 2, 3, 4-5, 6+");
    Check(PaintBand(2, 4, 0) == 1 && PaintBand(0, 3, 0) == CompleteBand && PaintBand(0, 0, 0) == -1 && PaintBand(0, 3, 1) == -1 &&
        PaintBand(1, 3, 1) == 0, "A zone missing none of its monsters is Complet; unknown progress or no monster: no colour");
    Check(SameColor(Fill(0, false), 0, .35f) && SameColor(Fill(4, true), 4, .25f) && SameColor(Line(2), 2, .8f),
        "Fill 0.35 on the big map, 0.25 on the mini-map, outline 0.8");
    Check(SameColor(Fill(CompleteBand, false), CompleteBand, .28f) && SameColor(Fill(CompleteBand, true), CompleteBand, .2f) &&
        SameColor(Line(CompleteBand), CompleteBand, .7f), "Complete zones lighter: fill 0.28 and 0.2, outline 0.7");
    Check(Near(Scale[0].r, 1.0f) && Near(Scale[0].g, 213 / 255.0f) && Near(Scale[0].b, 74 / 255.0f) &&
        Near(Scale[4].r, 142 / 255.0f) && Near(Scale[4].g, 27 / 255.0f) && Near(Scale[4].b, 90 / 255.0f), "Colours #FFD54A to #8E1B5A");
    Check(Near(Scale[CompleteBand].r, 46 / 255.0f) && Near(Scale[CompleteBand].g, 134 / 255.0f) && Near(Scale[CompleteBand].b, 222 / 255.0f),
        "Complet #2E86DE");
    // Distinct from the scale and from the game's anomaly colours (#8811AD, #10BA70, #999999).
    const std::array<Rgb, 3> anomalyColours{{{136 / 255.0f, 17 / 255.0f, 173 / 255.0f}, {16 / 255.0f, 186 / 255.0f, 112 / 255.0f}, {.6f, .6f, .6f}}};
    const auto distance = [](const Rgb& a, const Rgb& b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); };
    for (int band = 0; band < CompleteBand; ++band) Check(distance(Scale[CompleteBand], Scale[band]) > .6f, "Complet apart from the scale");
    for (const auto& colour : anomalyColours) Check(distance(Scale[CompleteBand], colour) > .5f, "Complet apart from the anomaly colours");
    Check(ScaleLabels[CompleteBand] == std::string("Complet") && ScaleLabels.size() == Scale.size(), "The legend names Complet");
    Check(TintOf(Fill(1, false), Line(1), 1, false) == Tint::Painted && TintOf(Fill(2, true), Line(2), 2, true) == Tint::Painted &&
        TintOf(Scaled(Fill(1, false), 1.2f, 1.5f, true), Scaled(Line(1), 1.2f, 1.5f, true), 1, false) == Tint::Highlighted &&
        TintOf(Scaled(Fill(1, false), 1.2f, 1.5f, false), Scaled(Line(1), 1.2f, 1.5f, false), 1, false) == Tint::Other &&
        TintOf(Fill(1, false), Line(1), 2, false) == Tint::Other && TintOf(Fill(1, true), Line(1), 1, false) == Tint::Other,
        "Stored colours: as painted, highlighted by the game's enter handler, or neither");
    // A zone whose monsters are all had (bag, Metamob) stays uncoloured while the
    // Ocre progress itself is unknown; known, it is Complet.
    Model m;
    m.goals.resize(2);
    m.zones.resize(1);
    m.zones[0].goals = {0, 1};
    m.Totalize();
    m.states = {{false, HadBag, -1}, {false, HadMetamob, 2}};
    m.counts.assign(1, {});
    m.unknown.assign(1, {});
    m.sources.ocreKnown = false;
    Check(m.Paint(0, Show::All) == -1 && m.Rows({}, {}).empty(), "Unknown Ocre: no colour, no row, whatever the bag and Metamob say");
    m.sources.ocreKnown = true;
    Check(m.Paint(0, Show::All) == CompleteBand && m.Rows({}, {}) == std::vector<size_t>{0}, "Known: complete");
    Check(HadReason(m.states[0]) == "dans le sac" && HadReason(m.states[1]) == "sur Metamob (×2)" &&
        HadReason({false, HadOcre | HadBag | HadMetamob, 1}) == "validé dans l’Ocre · dans le sac · sur Metamob", "Why each one is had");
    std::cout << "PASS: model folding, levels, colour scale and Complet\n";
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
    Check(guards.at("fgh.begr") == 0x1C3D3C0 && guards.at("VisualElement.MarkDirtyRepaint") == 0x55E5BA0 &&
        guards.at("VisualElement.get_worldBound") == 0x55F7CC0 && !guards.contains("VisualElement.get_childCount") &&
        guards.at("DofusIcon..ctor") == 0x170EBD0 && guards.at("DofusIcon.set_icon") == 0x170F480 &&
        guards.at("VisualElement.get_resolvedStyle") == 0x55F72E0 && guards.at("ResolvedStyleAccess.get_display") == 0x55D9890 &&
        guards.at("ResolvedStyleAccess.get_opacity") == 0x55D9CD0,
        "The reveal, the repaint, the probe and the detail's icons are verified too");
    // Every RVA verified here has a 16-byte guard in the shared table.
    for (const auto& [name, rva] : guards)
        Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& g) { return g.rva == rva; }),
            "Guard missing for " + name);
    std::cout << "PASS: installation verifies its natives (all guarded), hooks nothing, shares the click handlers\n";
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
    Check(LastLog("ARCHI_ZONES_MODE") == "ARCHI_ZONES_MODE on", "Mode logged");
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
        StyleLength(search, "width").value == 254 && Get(search).strings.at("tooltip") == archiZones::SearchTooltip,
        "Search field with the anomaly panel's magnifier");
    // The legend: the missing-count scale and Complet.
    std::vector<std::string> legend;
    std::vector<Obj> labels;
    FindAll(panel, "DofusLabel", labels);
    for (auto label : labels) legend.push_back(Get(label).text);
    Check(std::find(legend.begin(), legend.end(), "Complet") != legend.end() &&
        std::find(legend.begin(), legend.end(), "Manquants par zone :") != legend.end() &&
        std::find(legend.begin(), legend.end(), "6+") != legend.end(), "Legend: the scale, then « Complet »");
    Pump(12);
    Check(archiZones::build.ready, "Zones read from the game within a few pumps");
    Check(monsterReads.size() == chat_souls::OcreGoals.size(), "Every Ocre goal read from MonsterData");
    for (const auto& [id, count] : monsterReads) Check(count == 1, "Each monster read once");
    for (const auto& [id, count] : subareaReads) Check(count == 1, "Each zone read once");
    Check(archiZones::model.zones.size() == 7, "Seven zones from the game's MonsterData.subareas");
    const auto& zone10 = archiZones::model.zones.at(archiZones::build.zoneIndex.at(10));
    Check(zone10.name == "Coin des Bouftous" && zone10.area == "Amakna" && zone10.level == 10 && zone10.world == 1 && zone10.shape &&
        zone10.goals.size() == 4 && zone10.sortName == "coin des bouftous", "Zone fields and its four archimonsters (a duplicate subarea counted once)");
    const auto& zone30 = archiZones::model.zones.at(archiZones::build.zoneIndex.at(30));
    Check(!zone30.shape && zone30.dungeon == 7, "A boss's dungeon zone, hidden on the world map");
    std::vector<std::string> areas;
    for (auto item : Get(archiZones::panel->areas.get()).items) areas.push_back(Get(item).text);
    Check(areas == std::vector<std::string>{"Tous les territoires", "Amakna", "Cania", "Frigost", "Otomaï"}, "Territories, sorted");
    std::vector<std::string> choices;
    for (auto item : Get(archiZones::panel->type.get()).items) choices.push_back(Get(item).text);
    Check(choices == std::vector<std::string>{"Tous", "Archimonstres", "Boss de donjon"}, "Type choice: all, archimonsters, dungeon bosses");
    // Ocre still unknown: nothing coloured, not even « complete », the hint instead.
    Check(Rows().empty() && Shown(archiZones::panel->noResult.get()) && !Shown(archiZones::panel->content.get()) &&
        archiZones::panel->noResultText == archiZones::OcreUnknownText && archiZones::panel->statusText == archiZones::OcreUnknownText &&
        creations.empty(), "Unknown Ocre progress colours nothing and invites to open the quest journal");
    std::cout << "PASS: Ocre mode, native panel, legend with Complet, runtime data, unknown Ocre hint\n";
}
void Activate(Scenario& s) {
    s.owner.settings.archiZonesMap = true;
    Pump(1);
    buttonHandler(Square());
    Pump(14);
    Check(archiZones::build.ready && archiZones::panel, "Ocre mode ready");
}
// The r111 failure: every shape created, registered, adopted and displayed, but
// left transparent by the game's constructor (isFixed false). Now each one is
// revealed the game's way (fgh.begr) and checked again at every pump.
void ShapesAreVisible() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    auto shapes = Shapes(bigView);
    Check(Ids(shapes) == std::set<int>{10, 20, 31, 60}, "Coloured shapes for the outlined zones of the map's world, complete one included");
    for (const auto& c : creations) Check(c.view == bigView && !c.flag, "Big map: the game's hover shape, as its anomaly territories");
    Check(CountLogs("ARCHI_ZONES_RESHOW") == 0,
        "A shape just revealed is not taken for hidden (its resolved style follows a frame later)");
    for (const auto& [id, shape] : shapes) {
        Check(Visible(shape), "Zone " + std::to_string(id) + " seen: displayed and opaque");
        Check(Get(shape).reveals >= 1 && Get(shape).hover, "Revealed with fgh.begr; map info hover kept on the big map");
        Check(Get(RootOf(shape)).repaints >= 1, "Repainted after its colours were set");
        Check(Near(FloatSlot(shape, "dtvk"), 1.2f) && Near(FloatSlot(shape, "dtvl"), 1.5f),
            "The hover factors of the game's revealed zones (anomaly and search continuations)");
        const auto fill = Get(shape).fill, line = Get(shape).line;
        PointerEnter(shape);
        Check(Visible(shape) && Get(shape).fill.a > fill.a + .1f, "The mouse on a zone highlights it");
        PointerLeave(shape);
        Check(Visible(shape) && archiZones::Same(Get(shape).fill, fill) && archiZones::Same(Get(shape).line, line),
            "The mouse passing over a zone leaves it shown, its colours back (a highlight, not a fade out)");
    }
    // The game hides every shape when it shows a world (fhw.besm): back at the next pump.
    HideAll(bigView);
    StylePass();
    for (const auto& [id, shape] : shapes) Check(!Visible(shape), "Hidden by the game");
    Pump(1);
    for (const auto& [id, shape] : shapes) Check(Visible(shape), "Every shape back after the game hid them");
    Check(LastLog("ARCHI_ZONES_RESHOW") == "ARCHI_ZONES_RESHOW view=big reason=display shapes=4", "The reshow logged");
    // An opacity taken back (fgh.begs on our shapes) is restored too, the reshow log rate-limited.
    for (const auto& [id, shape] : shapes) SetStyle(RootOf(shape), "opacity", 0.0f);
    StylePass();
    Pump(1);
    for (const auto& [id, shape] : shapes) Check(Visible(shape), "Opacity given back");
    Check(CountLogs("ARCHI_ZONES_RESHOW") == 1, "At most one reshow line every 30 s");
    std::cout << "PASS: shapes revealed the game's way (begr), shown again when the game hides them\n";
}
void MissingAndShapes() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    Check(Rows() == std::vector<int>{10, 20, 30, 50, 31, 60, 40}, "Missing zones sorted by missing count, then level; complete zones after them");
    const auto& row10 = RowOf(10);
    Check(row10.countText == "2" && row10.levelText == "10" && row10.icon == archiZones::IconArchimonster && row10.name == "Coin des Bouftous",
        "Zone row: name, level, missing count (2271 in the bag, 2272 on Metamob: not missing)");
    Check(row10.tooltip.find("Bouftonnaire l’Ancestral") != std::string::npos && row10.tooltip.find("Pétrifié l’Ébène") != std::string::npos &&
        row10.tooltip.find("Craqueleur") == std::string::npos && row10.tooltip.find("Scarafeuille le Rouge") == std::string::npos &&
        row10.tooltip.find("Un clic affiche aussi") != std::string::npos, "The row's tooltip names the missing monsters only, and the click");
    Check(RowOf(30).icon == archiZones::IconBoss && RowOf(30).tooltip.find("Dragon Cochon (boss)") != std::string::npos &&
        RowOf(30).tooltip.find("Donjon sans contour") != std::string::npos, "A not started boss's dungeon zone is listed even without a shape");
    Check(RowOf(60).countText == "0" && RowOf(60).band == archiZones::CompleteBand && RowOf(60).icon == archiZones::IconArchimonster &&
        RowOf(60).tooltip.find("Complet : tout y est validé") != std::string::npos, "A complete zone listed: 0 missing, blue swatch, told complete");
    Check(SameColor(StyleColor(row10.swatch.get(), "backgroundColor"), 1, .9f) &&
        SameColor(StyleColor(RowOf(60).swatch.get(), "backgroundColor"), archiZones::CompleteBand, .9f), "Row swatches in the zones' colours");
    Check(archiZones::panel->statusText ==
        "3 archimonstres et 2 boss de donjon manquants, dans 5 zones. 2 zones complètes.\nOcre : lu · Sac : lu · Metamob : ? (pas encore lu)",
        "Totals, complete zones and sources in the panel's footer");
    // The big map: outlined zones of world 1 among the rows, complete ones blue.
    auto shapes = Shapes(bigView);
    Check(Ids(shapes) == std::set<int>{10, 20, 31, 60}, "Coloured shapes for the outlined zones of the map's world only");
    for (const auto& c : creations) Check(c.view == bigView && c.fia == 27 && Near(c.thickness, 2) && !c.flag && c.bonus == -1,
        "The game's AddAreaShape with the anomalies' outline, in the waiting layer (fia 27)");
    Check(SameColor(Get(shapes[10]).fill, 1, .35f) && SameColor(Get(shapes[10]).line, 1, .8f) && SameColor(Get(shapes[20]).fill, 0, .35f) &&
        SameColor(Get(shapes[60]).fill, archiZones::CompleteBand, .28f) && SameColor(Get(shapes[60]).line, archiZones::CompleteBand, .7f),
        "Zone colour by missing count, Complet in blue");
    auto layer = OurLayer(bigRoot);
    Check(layer && Get(layer).ints.at("picking") == 1 && StyleInt(layer, "position") == 1 && StyleLength(layer, "width").unit == 1,
        "The mod's own full-size, unpickable layer");
    const auto& rootChildren = Get(bigRoot).children;
    const auto layerAt = std::find(rootChildren.begin(), rootChildren.end(), layer) - rootChildren.begin();
    const auto firstAt = std::find(rootChildren.begin(), rootChildren.end(), Get(bigView).layers.at(0)) - rootChildren.begin();
    Check(layerAt == 1 && firstAt == 2, "Above the map picture, just under the first native layer");
    for (const auto& [id, shape] : shapes)
        Check(Get(RootOf(shape)).fields["parent"] == layer && Get(shape).fields.at("<dtwq>k__BackingField") == layer,
            "Shapes in the mod's layer (the game's zoom rescale keeps them there)");
    // Bag unknown: counted as not in the bag; only the changed zones are repainted.
    const int paints20 = Get(shapes[20]).paints, paints10 = Get(shapes[10]).paints, paints60 = Get(shapes[60]).paints;
    const int repaints10 = Get(RootOf(shapes[10])).repaints;
    bag.clear();
    ++soulRevision;
    Pump(6);
    Check(RowOf(10).countText == "3" && SameColor(Get(shapes[10]).fill, 2, .35f) && Get(shapes[10]).paints == paints10 + 1 &&
        Get(RootOf(shapes[10])).repaints == repaints10 + 1 && Get(shapes[20]).paints == paints20 && Get(shapes[60]).paints == paints60 + 1 &&
        SameColor(Get(shapes[60]).fill, 0, .35f) && creations.size() == 4,
        "Unknown bag counts as absent; only the changed zones are recoloured and redrawn (zone 60 no longer complete)");
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
    // A zone with nothing missing any more turns Complet blue, its shape kept.
    ocre[2270] = SoulState::Done;
    ++soulRevision;
    Pump(6);
    shapes = Shapes(bigView);
    Check(Ids(shapes) == std::set<int>{10, 20, 31, 60} && SameColor(Get(shapes[20]).fill, archiZones::CompleteBand, .28f) &&
        RowOf(20).countText == "0" && Rows().back() == 40 && Get(Get(Registry(bigView, 1)).stringKeys.at("anomaly_shape99")).disposed == false,
        "Zone 20 complete: recoloured Complet and listed with the complete zones; native shapes untouched");
    Check(std::count_if(objects.begin(), objects.end(), [](const auto& o) { return o->type->name == "fgf" && o->disposed; }) == 0,
        "Nothing disposed: every zone stays coloured");
    // Ocre unknown again: nothing coloured at all.
    ocre.clear();
    ++soulRevision;
    Pump(6);
    Check(Shapes(bigView).empty() && Rows().empty() && archiZones::panel->noResultText == archiZones::OcreUnknownText,
        "Ocre unknown: every shape removed, the hint shown");
    std::cout << "PASS: missing per Ocre, bag and Metamob state, colours incl. Complet, shapes, recolour only changed zones\n";
}
void FiltersAndSort() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    auto& p = *archiZones::panel;
    Choose(p.type.get(), 1);
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20, 50, 60, 40} && Ids(Shapes(bigView)) == std::set<int>{10, 20, 60} &&
        Get(p.type.get()).strings.at("choiceText") == "Archimonstres", "Archimonsters only, on the list and the map, complete zones last");
    Choose(p.type.get(), 2);
    Pump(2);
    Check(Rows() == std::vector<int>{30, 31} && Ids(Shapes(bigView)) == std::set<int>{31} &&
        p.statusText.find("2 boss de donjon manquants, dans 2 zones.") == 0, "Dungeon bosses only (the dungeon has no outline)");
    Choose(p.type.get(), 0);
    SetText(p.search.get(), "bouf");
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20}, "Search by zone name or by a monster's name");
    SetText(p.search.get(), "EBENE");
    Pump(2);
    Check(Rows() == std::vector<int>{10}, "Search without case or accents");
    SetText(p.search.get(), "dodu");
    Pump(2);
    Check(Rows() == std::vector<int>{60} && Ids(Shapes(bigView)) == std::set<int>{60}, "A monster you have finds its zones too");
    SetText(p.search.get(), " plaine ");
    Pump(2);
    Check(Rows() == std::vector<int>{20, 31} && Ids(Shapes(bigView)) == std::set<int>{20, 31}, "The map follows the panel's filters");
    SetText(p.search.get(), "zzz");
    Pump(2);
    Check(Rows().empty() && p.noResultText == archiZones::NoMatchText && Shown(p.noResult.get()) && Shapes(bigView).empty(), "No match told");
    SetText(p.search.get(), "");
    Choose(p.areas.get(), 2);
    Pump(2);
    Check(Rows() == std::vector<int>{31, 60} && Get(p.areas.get()).strings.at("choiceText") == "Cania", "Territory choice");
    Choose(p.areas.get(), 0);
    SetText(p.minLevel.get(), "50");
    SetText(p.maxLevel.get(), "90");
    Pump(2);
    Check(Rows() == std::vector<int>{30, 50, 60}, "Level range");
    SetText(p.minLevel.get(), "");
    SetText(p.maxLevel.get(), "");
    Pump(2);
    // Sortable native columns: the clicked one wins, the others go inactive;
    // the complete zones stay after the others whatever the order.
    Check(Get(p.headers[2].get()).ints.at("state") == 2 && p.headers[0].get() && Get(p.headers[1].get()).text == "Niveau",
        "Missing column sorted descending by default");
    Get(p.headers[1].get()).ints["state"] = 1; // a native click on « Niveau »
    Pump(2);
    Check(Rows() == std::vector<int>{10, 20, 30, 50, 31, 60, 40} && Get(p.headers[2].get()).ints.at("state") == 0, "Level ascending, Missing reset");
    Get(p.headers[1].get()).ints["state"] = 2;
    Pump(2);
    Check(Rows() == std::vector<int>{31, 50, 30, 20, 10, 40, 60}, "Level descending");
    Get(p.headers[0].get()).ints["state"] = 1;
    Pump(2);
    Check(Rows() == std::vector<int>{30, 10, 50, 20, 31, 60, 40} && Get(p.headers[1].get()).ints.at("state") == 0, "Names ascending");
    Get(p.headers[2].get()).ints["state"] = 1;
    Pump(2);
    Check(Rows() == std::vector<int>{20, 30, 50, 31, 10, 60, 40}, "Missing ascending: still the complete zones last");
    std::cout << "PASS: panel search, territory, level range, type and sortable columns, complete zones last\n";
}
// The detail block under a clicked row: the zone's monsters you lack and those
// you have, with their kind icons and why they are had; a second click closes it.
std::vector<std::string> Texts(Obj root) {
    std::vector<std::string> texts;
    std::vector<Obj> labels;
    FindAll(root, "DofusLabel", labels);
    for (auto label : labels) {
        bool shown = true;
        for (Obj node = label; node && node != root; node = Get(node).fields["parent"]) shown &= Shown(node);
        if (shown) texts.push_back(Get(label).text);
    }
    return texts;
}
std::vector<int> Icons(Obj root) {
    std::vector<int> icons;
    std::vector<Obj> found;
    FindAll(root, "DofusIcon", found);
    for (auto icon : found) {
        bool shown = true;
        for (Obj node = icon; node && node != root; node = Get(node).fields["parent"]) shown &= Shown(node);
        if (shown) icons.push_back(Get(icon).ints.at("icon"));
    }
    return icons;
}
void DetailBlock() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    auto& p = *archiZones::panel;
    Check(!p.detail, "No detail before a click");
    Check(rowHandler(RowOf(10).element.get()), "A zone row's native click is ours");
    Pump(1);
    Check(moves == std::vector<std::pair<int, int>>{{4, -2}} && RowOf(10).activated, "The click still centres the map on the zone");
    Check(p.detail && p.detail->shown, "The click opens the zone's detail");
    auto detail = p.detail->root.get();
    auto content = Get(p.list.get()).fields.at("content");
    const auto& children = Get(content).children;
    const auto rowAt = std::find(children.begin(), children.end(), RowOf(10).element.get()) - children.begin();
    Check(rowAt + 1 < static_cast<long long>(children.size()) && children[rowAt + 1] == detail, "Right under its row, in the list");
    Check(Get(detail).classes == std::vector<std::string>{"backgroundColor_black_black25"} && Get(detail).ints.at("picking") == 1,
        "The game's own background class, not clickable");
    Check(Texts(detail) == std::vector<std::string>{"Il te manque (2)", "Bouftonnaire l’Ancestral", "Pétrifié l’Ébène",
        "Tu as (2)", "Craqueleur le Grand", "dans le sac", "Scarafeuille le Rouge", "sur Metamob"},
        "« Il te manque » and « Tu as », each monster named, and why it is had");
    Check(Icons(detail) == std::vector<int>{archiZones::IconArchimonster, archiZones::IconArchimonster, archiZones::IconArchimonster,
        archiZones::IconArchimonster}, "Each monster with its kind icon");
    std::vector<Obj> lines;
    FindAll(detail, "DofusLabel", lines);
    Check(Get(lines.front()).classes == std::vector<std::string>{"textShort_smallBold", "textColor_highlight_secondary100"},
        "Group titles in the game's section style");
    // Another zone: the detail moves under its row and shows it.
    rowHandler(RowOf(60).element.get());
    Pump(1);
    const auto row60 = std::find(children.begin(), children.end(), RowOf(60).element.get()) - children.begin();
    Check(children[row60 + 1] == detail && RowOf(60).activated && !RowOf(10).activated && moves.back() == std::pair<int, int>{-25, -30},
        "Another row: the detail follows, the map centres there");
    Check(Texts(detail) == std::vector<std::string>{"Il te manque (0)", "Rien : la zone est complète.", "Tu as (2)",
        "Larve Orange le Lent", "dans le sac", "Tofu le Dodu", "validé dans l’Ocre · sur Metamob (×3)"},
        "A complete zone: nothing missing, every reason told (validated, bag, Metamob quantity)");
    // A dungeon boss zone.
    rowHandler(RowOf(30).element.get());
    Pump(1);
    Check(Texts(detail) == std::vector<std::string>{"Il te manque (1)", "Dragon Cochon", "Tu as (0)", "Aucun pour l’instant."} &&
        Icons(detail) == std::vector<int>{archiZones::IconBoss}, "A dungeon boss: the boss icon");
    // A second click on the open zone closes it; the map does not move.
    const auto movesBefore = moves.size();
    rowHandler(RowOf(30).element.get());
    Pump(1);
    Check(!p.detail->shown && !Shown(detail) && !RowOf(30).activated && moves.size() == movesBefore, "A second click closes the detail");
    // Open again, then filtered out: hidden; back when listed again.
    rowHandler(RowOf(10).element.get());
    Pump(1);
    Check(p.detail->shown, "Open again");
    SetText(p.search.get(), "cania");
    Pump(2);
    Check(!p.detail->shown, "Hidden while its zone is not listed");
    SetText(p.search.get(), "");
    Pump(2);
    Check(p.detail->shown && Texts(detail).front() == "Il te manque (2)", "Back with its zone");
    // The type choice and the readers' results change its content.
    Choose(p.type.get(), 2);
    Pump(2);
    Check(!p.detail->shown, "Zone 10 holds no boss: not listed with « Boss de donjon »");
    Choose(p.type.get(), 0);
    ocre[2273] = SoulState::Done;
    ++soulRevision;
    Pump(6);
    Check(Texts(detail) == std::vector<std::string>{"Il te manque (1)", "Bouftonnaire l’Ancestral", "Tu as (3)", "Craqueleur le Grand",
        "dans le sac", "Pétrifié l’Ébène", "validé dans l’Ocre", "Scarafeuille le Rouge", "sur Metamob"}, "Validated: moves to « Tu as »");
    // Nothing missing, but a monster of unknown progress: not told complete.
    ocre.erase(2277);
    metamobQuantities.erase(2277);
    ++soulRevision;
    Pump(6);
    rowHandler(RowOf(60).element.get());
    Pump(1);
    Check(Texts(detail) == std::vector<std::string>{"Il te manque (0)", "Aucun connu pour l’instant.", "Tu as (1)", "Larve Orange le Lent",
        "dans le sac", "1 monstre à l’avancement inconnu."}, "Nothing known missing and one monster of unknown progress: not told complete");
    Check(RowOf(60).tooltip.find("\n1 monstre à l’avancement inconnu.") != std::string::npos && RowOf(60).tooltip.find("Complet") == std::string::npos &&
        RowOf(60).band == -1, "The row's tooltip tells the unknown progress too");
    Check(!rowHandler(Make("ListElementBasic", true)), "Foreign rows are not ours");
    std::cout << "PASS: zone detail under its row (missing, had and why, kind icons), toggled by a second click\n";
}
void RowClicksAndWorlds() {
    Scenario s;
    Activate(s);
    MainStates();
    ocre[2275] = SoulState::Missing; // zone 40, in another world
    ++soulRevision;
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{10, 20, 31, 60}, "A listed zone of another world is not drawn on this one");
    rowHandler(RowOf(30).element.get());
    Pump(1);
    Check(moves.back() == std::pair<int, int>{-1, -10} && worldSwitches.empty() && RowOf(30).activated,
        "A dungeon zone centres on the dungeon's entrance");
    rowHandler(RowOf(40).element.get());
    Pump(1);
    Check(worldSwitches == std::vector<int>{3} && moves.back() == std::pair<int, int>{-75, -40}, "Another world: switched first, as the anomaly rows do");
    Pump(3);
    Check(Ids(Shapes(bigView)) == std::set<int>{40}, "The map now on world 3: only its zone is coloured");
    std::cout << "PASS: row click centres the map (dungeon entrance, other world)\n";
}
void MiniMap() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    MainStates();
    Pump(16);
    Check(!Square() && !PanelWindow() && Ids(Shapes(miniView)) == std::set<int>{10, 20, 31, 60} && Shapes(bigView).empty(),
        "Mini-map zones without the big map or its panel, complete one included");
    for (const auto& c : creations) Check(c.view == miniView && !c.flag && c.fia == 27,
        "Mini-map: a hover shape too (built transparent in the waiting layer, revealed once adopted)");
    for (const auto& [id, shape] : Shapes(miniView)) {
        Check(Visible(shape) && !Get(shape).hover && Get(RootOf(shape)).ints.at("picking") == 1 && !Pickable(shape),
            "Seen, no map info hover, no pointer event on the mini-map");
        Check(Near(Get(shape).fill.a, id == 60 ? .2f : .25f), "Lighter fills on the mini-map");
    }
    Check(SameColor(Get(Shapes(miniView)[60]).fill, archiZones::CompleteBand, .2f), "Complet blue on the mini-map too");
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
    Choose(archiZones::panel->type.get(), 1);
    Pump(3);
    Check(Ids(Shapes(miniView)) == std::set<int>{10, 20, 60}, "Archimonsters only, complete zone included");
    HideAll(miniView);
    StylePass();
    Pump(1);
    for (const auto& [id, shape] : Shapes(miniView)) Check(Visible(shape), "Shown again after the game hid them");
    s.owner.settings.archiZonesMiniMap = false;
    Pump(2);
    Check(Shapes(miniView).empty() && !OurLayer(miniRoot) && Ids(Shapes(bigView)) == std::set<int>{10, 20, 60},
        "Mini-map option off: its zones and layer leave");
    std::cout << "PASS: mini-map zones (Complet included), revealed, unpickable, follow the type choice\n";
}
void Diagnostics() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(30);
    Check(LastLog("ARCHI_ZONES_SHAPES") == "ARCHI_ZONES_SHAPES view=big world=1 listed=7 wanted=4 missing=3 complete=1 otherWorld=1 "
        "noOutline=2 requested=4 shown=4 pending=0 failed=0 layer=1 firstLayer=2", "The view's shape counts, world and layer place logged");
    const auto probe = LastLog("ARCHI_ZONES_PROBE");
    Check(probe.starts_with("ARCHI_ZONES_PROBE view=big zone=") && probe.find(" parent=layer stored=layer display=0 opacity=1.00 ") != std::string::npos &&
        probe.find("scale=0.600 zoom=0.600/1.000 shape=1600x1200@-300,-200") != std::string::npos &&
        probe.find("root=1600x1200@-300,-200") != std::string::npos && CountLogs("ARCHI_ZONES_PROBE") == 1,
        "One probe per layer: where the first shape lives, its styles, scale and bounds");
    const size_t lines = CountLogs("ARCHI_ZONES_SHAPES");
    Pump(40);
    Check(CountLogs("ARCHI_ZONES_SHAPES") == lines, "Nothing new: no new line");
    // The view no longer the session's: the reason is logged once, the shapes leave.
    Get(bigView).fields["dugv"] = Make("OtherService");
    Pump(3);
    Check(LastLog("ARCHI_ZONES_FRAME") == "ARCHI_ZONES_FRAME view=big reason=services" && CountLogs("ARCHI_ZONES_FRAME") == 1 &&
        Shapes(bigView).empty(), "Frame failure logged once with its reason");
    Get(bigView).fields["dugv"] = service;
    Pump(30);
    Check(LastLog("ARCHI_ZONES_SHAPES").find("shown=4") != std::string::npos &&
        LastLog("ARCHI_ZONES_SHAPES").find("first=frame:services") != std::string::npos, "Back, with the first failure kept");
    // A shape the game never registers.
    archiZones::RestoreAll(clock);
    logs.clear();
    archiZones::diags = {};
    Pump(1);
    Check(buttonHandler(Square()), "The square back, the mode off");
    archiZones::Tick(clock); clock += 200;
    Check(pendingCreations.size() == 4, "The four zones asked for again");
    pendingCreations.clear(); // lost by the game
    Pump(20);
    Check(LastLog("ARCHI_ZONES_SHAPE_MISSING").starts_with("ARCHI_ZONES_SHAPE_MISSING view=big zone=") &&
        CountLogs("ARCHI_ZONES_SHAPE_MISSING") == 1, "A missing shape logged, rate-limited");
    Pump(30);
    Check(LastLog("ARCHI_ZONES_SHAPES").find("failed=4") != std::string::npos &&
        LastLog("ARCHI_ZONES_SHAPES").find("first=missing:") != std::string::npos, "Failures counted in the shapes line");
    std::cout << "PASS: rate-limited diagnostics (counts, world, layer, probe, frame failure, missing shapes)\n";
}
// The game's hover handlers scale a revealed zone's stored colours when the
// mouse enters and divide them back when it leaves: a zone recoloured under the
// mouse keeps its new colours once the mouse leaves; colours left wrong are
// painted again; a shape the mouse was on before the mod adopted it is not
// darkened when it leaves.
void HighlightAndRecolour() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(6);
    auto shapes = Shapes(bigView);
    const Obj z10 = shapes.at(10), z20 = shapes.at(20);
    PointerEnter(z10);
    Check(SameHighlight(Get(z10).fill, 1, .35f) && SameHighlight(Get(z10).line, 1, .8f), "The mouse on zone 10: highlighted");
    const int paints = Get(z10).paints;
    Pump(20);
    Check(Get(z10).paints == paints && SameHighlight(Get(z10).fill, 1, .35f) && CountLogs("ARCHI_ZONES_RECOLOUR") == 0,
        "A highlighted zone is left as the game made it");
    bag.clear(); // zone 10 goes from 2 to 3 missing under the mouse
    ++soulRevision;
    Pump(6);
    Check(RowOf(10).countText == "3" && SameHighlight(Get(z10).fill, 2, .35f) && SameHighlight(Get(z10).line, 2, .8f) &&
        SameColor(Get(shapes.at(60)).fill, 0, .35f), "Recoloured under the mouse: highlighted as the enter handler leaves them; others plain");
    PointerLeave(z10);
    Check(SameColor(Get(z10).fill, 2, .35f) && SameColor(Get(z10).line, 2, .8f), "The mouse leaves: exactly the new colours");
    Pump(10);
    Check(SameColor(Get(z10).fill, 2, .35f) && CountLogs("ARCHI_ZONES_RECOLOUR") == 0, "Nothing to repair");
    // Colours a hover handler divided without scaling them first: painted again.
    SetFill(z20, Scaled(Stored(z20, "dtvm"), 1.2f, 1.5f, false));
    SetLine(z20, Scaled(Stored(z20, "dtvn"), 1.2f, 1.5f, false));
    Pump(1);
    Check(SameColor(Get(z20).fill, 0, .35f) && SameColor(Get(z20).line, 0, .8f) &&
        LastLog("ARCHI_ZONES_RECOLOUR") == "ARCHI_ZONES_RECOLOUR view=big zone=20 shapes=1", "Wrong colours painted again, logged");
    // Asked for again with the mouse on the zones: out of its reach until adopted.
    buttonHandler(Square());
    Pump(2);
    Check(Shapes(bigView).empty(), "Mode left");
    buttonHandler(Square());
    archiZones::Tick(clock);
    clock += 200;
    CompleteShapes();
    StylePass();
    const auto waiting = Shapes(bigView);
    Check(waiting.size() == 4, "Registered by the game, not adopted yet");
    for (const auto& [id, shape] : waiting) {
        Check(!Pickable(shape), "A waiting shape is out of the pointer's reach");
        PointerEnter(shape);
    }
    Pump(1);
    for (const auto& [id, shape] : Shapes(bigView)) {
        const auto fill = Get(shape).fill, line = Get(shape).line;
        const int band = archiZones::views[archiZones::BigView].shapes.at(id).band;
        Check(Visible(shape) && Pickable(shape) && archiZones::TintOf(fill, line, band, false) == archiZones::Tint::Painted,
            "Adopted: shown, under the pointer, plain colours");
        PointerLeave(shape);
        Check(archiZones::Same(Get(shape).fill, fill) && archiZones::Same(Get(shape).line, line) && Visible(shape),
            "The mouse that was on the zone while it waited leaves: nothing scaled back");
        PointerEnter(shape);
        PointerLeave(shape);
        Check(archiZones::Same(Get(shape).fill, fill) && archiZones::Same(Get(shape).line, line), "A later highlight is scaled back exactly");
    }
    std::cout << "PASS: highlighted zones recoloured under the mouse keep their colours; wrong colours painted again\n";
}
// AddAreaShape builds the shapes in the native layer fia 27, hidden by the mod
// while shapes wait there: never drawn nor pickable over the map icons, and the
// game's zoom rescale, which sizes each shape's stored parent to 100 %, sizes
// that layer, never the map content.
void WaitingLayerHidden() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    Activate(s);
    MainStates();
    Pump(6);
    Check(!creations.empty(), "Shapes asked for");
    for (const auto& c : creations) Check(c.fia == archiZones::WaitingLayer && !c.flag, "Both maps: hover shapes built in the waiting layer");
    const Obj waitingLayer = Get(bigView).layers.at(27), miniWaiting = Get(miniView).layers.at(27);
    for (const Obj layer : {waitingLayer, miniWaiting}) Check(Shown(layer) && Get(layer).children.empty(), "Shown again, empty, once no shape waits");
    // Asked for again, and a zoom before the mod adopts them.
    buttonHandler(Square());
    Pump(2);
    buttonHandler(Square());
    archiZones::Tick(clock);
    clock += 200;
    Check(!Shown(waitingLayer) && Shown(miniWaiting), "Hidden before the game builds the shapes (the mini-map's waits for nothing)");
    CompleteShapes();
    StylePass();
    Check(Shapes(bigView).size() == 4, "Registered");
    for (const auto& [id, shape] : Shapes(bigView))
        Check(Get(RootOf(shape)).fields["parent"] == waitingLayer && !Visible(shape) && !Pickable(shape),
            "Waiting: in the hidden layer, transparent, out of the pointer's reach");
    Zoom(bigView, 2400, 1800);
    Check(StyleLength(bigRoot, "width").unit == 0 && Near(StyleLength(bigRoot, "width").value, 2400) && StyleLength(bigRoot, "height").unit == 0,
        "A zoom meanwhile keeps the map content's size in pixels");
    Pump(1);
    for (const auto& [id, shape] : Shapes(bigView))
        Check(Visible(shape) && Get(RootOf(shape)).fields["parent"] == OurLayer(bigRoot), "Adopted into the mod's layer, shown");
    Check(Shown(waitingLayer) && Get(waitingLayer).children.empty(), "The waiting layer shown again, empty");
    Zoom(bigView, 1600, 1200);
    Check(StyleLength(bigRoot, "width").unit == 0 && Near(StyleLength(bigRoot, "width").value, 1600), "Adopted shapes keep the zoom harmless");
    for (const auto& [id, shape] : Shapes(bigView)) Check(Get(RootOf(shape)).fields["parent"] == OurLayer(bigRoot), "Still in the mod's layer");
    // Cleanup while shapes wait: the layer shown again once they are gone.
    buttonHandler(Square());
    Pump(2);
    buttonHandler(Square());
    archiZones::Tick(clock);
    clock += 200;
    Check(!Shown(waitingLayer), "Hidden again for new shapes");
    archiZones::RestoreAll(clock);
    CompleteShapes();
    archiZones::RestoreAll(clock + 200);
    Check(!ArchiZonesCleanupPending() && Shown(waitingLayer) && Shown(miniWaiting) && Get(waitingLayer).children.empty() &&
        Get(miniWaiting).children.empty(), "Cleanup: nothing left there, both waiting layers shown");
    std::cout << "PASS: shapes wait in a hidden native layer (not drawn, not pickable, zoom-safe), shown again once adopted\n";
}
// A failing probe or shapes line is reported, never taken for a map failure
// (which would retire every shape every 5 s).
void DiagnosticsNeverRetire() {
    Scenario s;
    Activate(s);
    MainStates();
    Pump(30);
    const auto shapes = Shapes(bigView);
    Check(shapes.size() == 4 && CountLogs("ARCHI_ZONES_PROBE") == 1, "Shown and probed");
    archiZones::views[archiZones::BigView].probed = false; // as for a new layer
    archiZones::nextLog = 0;
    failMethod = "get_worldBound"; // the probe's bounds only
    Pump(1);
    Check(failMethod.empty() && LastLog("ARCHI_ZONES_ERROR").starts_with("ARCHI_ZONES_ERROR diagnostics: ") &&
        !archiZones::views[archiZones::BigView].retiring && Shapes(bigView) == shapes, "A failing probe is reported; the shapes stay");
    for (const auto& [id, shape] : shapes) Check(Visible(shape) && !Get(shape).disposed, "Still shown");
    Pump(5);
    Check(CountLogs("ARCHI_ZONES_PROBE") == 1 && CountLogs("ARCHI_ZONES_ERROR") == 1, "Probed once per layer, even when it fails");
    archiZones::nextLog = 0;
    failMethod = "IndexOf"; // the shapes line's layer place only (no detail open)
    bag.clear();
    ++soulRevision;
    Pump(30);
    Check(failMethod.empty() && CountLogs("ARCHI_ZONES_ERROR diagnostics") == 2 && !archiZones::views[archiZones::BigView].retiring &&
        Shapes(bigView) == shapes && SameColor(Get(shapes.at(10)).fill, 2, .35f), "A failing shapes line is reported; the shapes stay and follow");
    std::cout << "PASS: a diagnostics failure is reported without taking the shapes down\n";
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
    Check(LastLog("ARCHI_ZONES_MODE") == "ARCHI_ZONES_MODE off reason=close", "Leaving logged with its reason");
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
    Check(!archiZones::active && !PanelWindow() && LastLog("ARCHI_ZONES_MODE") == "ARCHI_ZONES_MODE off reason=square",
        "A second click on the square leaves it too");
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
    for (const auto& [id, monster] : monsters) if (id != 2271 && id != 2272 && id != 2275 && id != 2277) ocre[static_cast<int>(id)] = SoulState::Missing;
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
    Check(Shapes(bigView).size() == 28, "Every outlined zone eventually coloured");
    for (const auto& [id, shape] : Shapes(bigView)) Check(Visible(shape), "All of them seen");
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
// The cost of a pump once everything is shown: native calls and time.
void PumpCost() {
    Scenario s;
    int added = 0;
    for (const auto& goal : chat_souls::OcreGoals) {
        if (goal.step < archiZones::FirstArchimonsterStep || monsters.contains(goal.monster) || added == 100) continue;
        const int zone = 2000 + added++;
        const auto name = "Zone d’essai " + std::to_string(added);
        AddZone({zone, name.c_str(), "Amakna", 20 + added % 150, 1, true, 0, 0, 0});
        AddMonster(goal.monster, "Essai", {static_cast<uint32_t>(zone)});
    }
    MainStates();
    for (const auto& [id, monster] : monsters) if (!ocre.contains(static_cast<int>(id))) ocre[static_cast<int>(id)] = id % 3 ? SoulState::Missing : SoulState::Done;
    s.owner.settings.archiZonesMap = s.owner.settings.archiZonesMiniMap = true;
    const auto busiest = [](int pumps) {
        int peak = 0;
        for (int i = 0; i < pumps; ++i) { calls = 0; Pump(1); peak = std::max(peak, calls); }
        return peak;
    };
    // Opening: the data read, the rows built and written, the shapes asked for
    // and adopted on both maps, from the very first pump.
    busiest(1);
    buttonHandler(Square());
    const int opening = busiest(45);
    const auto big = Shapes(bigView).size(), mini = Shapes(miniView).size();
    const size_t listed = Rows().size();
    Check(big >= 100 && mini >= 100 && listed >= 100, "About a hundred zones on each map and in the list");
    // Another type choice: every row written again, a few per pump from the top;
    // shapes removed and asked for.
    Choose(archiZones::panel->type.get(), 1);
    const int first = busiest(1);
    const auto stale = [] {
        const auto& p = *archiZones::panel;
        size_t count = 0;
        for (size_t i = 0; i < p.zones.size() && i < p.rows.size(); ++i) count += p.rows[i].zone != p.zones[i];
        return count;
    };
    Check(stale() > 50 && archiZones::panel->rows[0].zone == archiZones::panel->zones[0], "One pump later: the top rows written, most still to do");
    const int switching = std::max(first, busiest(20));
    Check(stale() == 0, "A few pumps later: every row written");
    Check(Rows().size() + 2 == listed, "Archimonsters only: the two boss zones left the list");
    std::cout << "INFO: busiest pump while opening: " << opening << " native calls; after another type choice: " << switching << "\n";
    Check(opening < 1500, "Opening stays within its budgets (rows built and written, shapes asked and adopted)");
    Check(switching < 400, "A new type choice rewrites the rows a few per pump");
    rowHandler(RowOf(10).element.get());
    Pump(10);
    calls = 0;
    constexpr int Pumps = 50;
    double elapsed = 0; // the production tick only, not the fixture's frames
    for (int i = 0; i < Pumps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        archiZones::Tick(clock);
        elapsed += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / Pumps;
        clock += 200;
        CompleteShapes();
        StylePass();
    }
    const double perPump = static_cast<double>(calls) / Pumps;
    std::cout << "INFO: steady pump with " << big << " big-map and " << mini << " mini-map zones, panel and detail open: "
              << perPump << " native calls, " << elapsed << " us in the fake runtime\n";
    Check(perPump < 120, "A steady pump stays cheap (one shape checked per view, rows not rewritten)");
    std::cout << "PASS: per-pump cost measured\n";
}
void CleanupAndRestore() {
    Scenario s;
    s.owner.settings.archiZonesMiniMap = true;
    Activate(s);
    MainStates();
    Choose(archiZones::panel->type.get(), 2);
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{31} && Ids(Shapes(miniView)) == std::set<int>{31}, "Both maps coloured");
    // Shapes asked for but not registered yet are never left behind.
    Choose(archiZones::panel->type.get(), 0);
    archiZones::Tick(clock); clock += 1200;
    archiZones::Tick(clock); clock += 200;
    Check(!pendingCreations.empty(), "Shapes are still being created");
    archiZones::RestoreAll(clock);
    Check(ArchiZonesCleanupPending() && !Square() && !PanelWindow(), "Cleanup waits for the shapes still being created");
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
// Shapes asked for just before the map changes world are registered by the game
// under the new one (AddAreaShape reads the world when it resumes): disposed
// there at once, never left drawn.
void WorldChangeWhileCreating() {
    Scenario s;
    Activate(s);
    MainStates();
    Choose(archiZones::panel->type.get(), 2);
    Pump(6);
    Check(Ids(Shapes(bigView)) == std::set<int>{31}, "Coloured");
    Choose(archiZones::panel->type.get(), 0);
    archiZones::Tick(clock); clock += 1200;
    archiZones::Tick(clock); clock += 200;
    Check(!pendingCreations.empty(), "Shapes are still being created");
    Slot(bigWorld, "id") = 3; // another world shown before the game resumes it
    CompleteShapes();
    const auto landed = Shapes(bigView);
    Check(Ids(landed) == std::set<int>{10, 20, 60}, "The game registered them under the new world");
    Pump(1);
    const auto now3 = Shapes(bigView);
    Check(!now3.contains(10) && !now3.contains(20) && !now3.contains(60) && Get(landed.at(10)).disposed && Get(landed.at(20)).disposed &&
        Get(landed.at(60)).disposed, "Disposed under the world they landed in, at once");
    bool left = false;
    for (const auto& [name, shape] : Get(Registry(bigView, 1)).stringKeys) left |= name.starts_with("dofus_miniatures_ocre_");
    Check(!left && Get(Registry(bigView, 1)).stringKeys.contains("anomaly_shape99"), "Nothing of ours left under the old world");
    Pump(20);
    Check(Ids(Shapes(bigView)) == std::set<int>{40}, "The new world's complete zone drawn there");
    std::cout << "PASS: shapes registered under another world by a world change are disposed there\n";
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
    failMethod = "begr";
    InstallArchiZones();
    Check(!archiZones::installed && logs.back().find("begr") != std::string::npos, "A missing verified method disables it too");
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
        fake::ShapesAreVisible();
        fake::MissingAndShapes();
        fake::FiltersAndSort();
        fake::DetailBlock();
        fake::RowClicksAndWorlds();
        fake::MiniMap();
        fake::Diagnostics();
        fake::HighlightAndRecolour();
        fake::WaitingLayerHidden();
        fake::DiagnosticsNeverRetire();
        fake::LeavingTheMode();
        fake::BudgetsAndCadence();
        fake::PumpCost();
        fake::CleanupAndRestore();
        fake::WorldChangeWhileCreating();
        fake::PanelFailureRecovers();
        fake::ContractMismatchDisablesOnlyThis();
        std::cout << "PASS: Ocre map mode and zones (big map panel and detail, mini-map, visible shapes, diagnostics, cleanup)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fake::logs) std::cerr << line << '\n';
        return 1;
    }
}
