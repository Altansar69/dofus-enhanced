// CalendarKraloveTests (offline): calendar-kralove.cpp on a fake IL2CPP
// runtime. No client binary is loaded, no process opened, no request sent:
// metamob-sync is replaced by a scripted view, metamob-data is the real one
// (local time through Windows' own time zone rules). The game's calendar
// templates (calendarLine, calendarCard, calendarFilterItem) are cloned with
// the names and components of the installed 3.7.12.12 UXML. Every native call
// the module makes must have been verified at install (FindVerified), except
// the generic List<T> and style value accessors every module uses.
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "metamob/metamob-data.hpp"
#include "metamob/metamob-sync.hpp"
#include "ui/native-click.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#pragma comment(lib, "normaliz.lib") // metamob-data.cpp (NameKey's NormalizeString), linked for LocalTime

namespace fixture {
using namespace managed;
struct Class { std::string name; };
struct MethodData { std::string type, name; int argc = 0; };
struct Rect { float x = 0, y = 0, width = 0, height = 0; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, std::array<char, 16>> styles; // inline styles written, by property
    std::vector<Obj> children, manipulators;
    std::set<Obj> keys;                                  // a cursor cache: its elements
    std::array<char, 16> bytes{};                        // a boxed value
    std::string text, name, property, rightText, color, kind;
    std::set<std::string> classes;
    Rect layout;
    bool rich = true, visible = true, value = false, selectable = true, front = true, cursor = false, nullAction = false;
    int icon = -1, iconValue = -1, origin = -1, source = -1, behavior = -1, hides = 0, constructed = 0;
    Obj service = nullptr;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle, Obj> handles;
GCHandle handleId = 100;
// Field offsets as the pinned metadata has them (the generic binding at run time).
std::map<std::string, size_t> offsets{{"m_dayCards", 0x210}, {"<dayDate>k__BackingField", 0x2F8}, {"dzyi", 0x10},
    {"s_mouseEnterCursorMap", 0x40}, {"s_mouseLeaveCursorMap", 0x48}};
std::set<std::tuple<std::string, std::string, int>> verified;
std::vector<std::tuple<std::string, std::string, int, uintptr_t>> contracts;
// The game's own class chain, for the verified-call rule.
const std::map<std::string, std::string> bases{{"DofusLabel", "TextElement"}, {"TextElement", "VisualElement"},
    {"DofusButtonCustom", "VisualElement"}, {"CalendarDayCard", "VisualElement"}, {"ListView", "VisualElement"},
    {"DofusIcon", "VisualElement"}, {"CheckboxCustom", "VisualElement"}, {"ColorSquare", "VisualElement"},
    {"DofusVisualElement", "VisualElement"}, {"Divider", "VisualElement"}, {"TooltipManipulator", "BaseTooltipManipulator"}};
std::vector<std::string> logs, urls;
using HookFn = void(*)(Obj, Method);
HookFn enableHook = nullptr, disableHook = nullptr;
int nativeEnables = 0, nativeDisables = 0;
bool (*clickHandler)(Obj) = nullptr;
int clickHandlers = 0;
Obj failDetach = nullptr;
// The scripted metamob-sync side.
metamob::KraloveView view;
int wants = 0, views = 0, changes = 0;
// Module::Change failing: 1 before it sets the settings (refused), 2 after (its refresh, sync or save).
int failChange = 0;
int64_t now = 0;
// The game's services and statics.
Obj settingsObject = nullptr, tooltipService = nullptr, enterMap = nullptr, leaveMap = nullptr;
std::string missing; // a template element left out of the clones (another client's UXML)

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake object"); return *static_cast<Object*>(obj); }
Class* Type(const std::string& name) { auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get(); }
Obj Make(const std::string& name) { auto o = std::make_unique<Object>(); o->klass = Type(name); auto p = o.get(); objects.push_back(std::move(o)); return p; }
template<class T> Obj Box(T value) {
    static_assert(sizeof(T) <= 16);
    auto o = Make("Box"); std::memcpy(Get(o).bytes.data(), &value, sizeof(value)); return o;
}
Obj String(const std::string& text) { auto o = Make("String"); Get(o).text = text; return o; }
void Add(Obj parent, Obj child) {
    Check(!Get(child).fields["parent"], "Element already attached");
    Get(parent).children.push_back(child); Get(child).fields["parent"] = parent;
}
void Insert(Obj parent, int index, Obj child) {
    Check(!Get(child).fields["parent"], "Element already attached");
    auto& children = Get(parent).children;
    Check(index >= 0 && index <= static_cast<int>(children.size()), "Insert index out of range");
    children.insert(children.begin() + index, child); Get(child).fields["parent"] = parent;
}
void Detach(Obj child) {
    if (auto parent = Get(child).fields["parent"]) { auto& v = Get(parent).children; v.erase(std::remove(v.begin(), v.end(), child), v.end()); }
    Get(child).fields["parent"] = nullptr;
}
Obj Parent(Obj o) { return Get(o).fields["parent"]; }
Obj Panel(Obj o) { if (Get(o).fields["panel"]) return Get(o).fields["panel"]; auto p = Get(o).fields["parent"]; return p ? Panel(p) : nullptr; }
size_t Size(const std::string& property) {
    static const std::set<std::string> lengths{"left", "width", "height"}, colors{"backgroundColor", "unityBackgroundImageTintColor"};
    return lengths.contains(property) ? 8 : colors.contains(property) ? 16 : 4;
}
std::string StyleKind(const std::string& property) {
    if (Size(property) == 8) return "StyleLength";
    if (Size(property) == 16) return "StyleColor";
    return property == "flexShrink" || property == "opacity" ? "StyleFloat" : "StyleEnum";
}
bool Styled(Obj o, const std::string& property) { return Get(o).styles.contains(property); }
float Pixels(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; int unit = -1;
    std::memcpy(&value, Get(o).styles.at(property).data(), 4); std::memcpy(&unit, Get(o).styles.at(property).data() + 4, 4);
    Check(unit == 0, "Pixels expected: " + property);
    return value;
}
float FloatStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; std::memcpy(&value, Get(o).styles.at(property).data(), 4); return value;
}
int EnumStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    int value = 0; std::memcpy(&value, Get(o).styles.at(property).data(), 4); return value;
}
std::array<float, 4> ColorStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    std::array<float, 4> value{}; std::memcpy(value.data(), Get(o).styles.at(property).data(), 16); return value;
}
bool Near(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    for (size_t i = 0; i < 4; ++i) if (std::abs(a[i] - b[i]) > 1e-4f) return false;
    return true;
}
void RequireVerified(const std::string& type, const std::string& name, int argc) {
    // List<T>.get_Count/get_Item and the Style* value setters: generic, called
    // through runtime_invoke by every module (never a raw native address).
    static const std::set<std::string> generic{"get_Count", "get_Item", "set_value"};
    if (generic.contains(name)) return;
    for (std::string t = type; !t.empty(); t = bases.contains(t) ? bases.at(t) : std::string())
        if (verified.contains({t, name, argc})) return;
    throw std::runtime_error("Native call not verified at install: " + type + "." + name);
}
// The game's templates, cloned as their UXML declares them. DofusButtonCustom,
// CheckboxCustom and ColorSquare register a hand cursor as they are built
// (DofusCursor.AddCursorHandler: a key in both static caches, never removed).
Obj Element(const std::string& type, const std::string& name, std::initializer_list<Obj> children = {},
    std::initializer_list<const char*> names = {}) {
    auto o = Make(type);
    Get(o).name = name == missing ? "renamed" : name;
    for (auto c : names) Get(o).classes.insert(c);
    for (auto child : children) Add(o, child);
    if (type == "DofusButtonCustom" || type == "CheckboxCustom" || type == "ColorSquare") {
        Get(o).cursor = true; Get(enterMap).keys.insert(o); Get(leaveMap).keys.insert(o);
    }
    return o;
}
void CloneInto(Obj target, const std::string& kind) {
    if (kind == "line")
        Add(target, Element("VisualElement", "calendarLine", {
            Element("VisualElement", "ctr_evenType", {Element("VisualElement", "tx_bgBanner", {Element("VisualElement", "tx_borderBanner", {
                Element("DofusIcon", "tx_eventType")})}, {"tintColor_background_medium100"})}),
            Element("VisualElement", "ctr_events")}));
    else if (kind == "card")
        Add(target, Element("VisualElement", "ctr_calendarCard", {
            Element("VisualElement", "ctr_details", {Element("VisualElement", "colorLine"), Element("VisualElement", "ctr_content", {
                Element("VisualElement", "ctr_mainInfo", {Element("DofusIcon", "tx_icon"), Element("DofusLabel", "lbl_title"),
                    Element("Divider", "offeringDivider"), Element("VisualElement", "ctr_duration", {Element("DofusLabel", "lbl_start"),
                        Element("DofusLabel", "lbl_end")})}),
                Element("DofusVisualElement", "ctr_rewards", {Element("DofusButtonCustom", "btn_left"),
                    Element("DofusVisualElement", "ctr_itemsList"), Element("DofusButtonCustom", "btn_right")}),
                Element("VisualElement", "tx_offering", {Element("DofusLabel", "lbl_offeringQty")})})}),
            Element("VisualElement", "ctr_buttons", {Element("DofusButtonCustom", "btn_ladder"), Element("DofusButtonCustom", "btn_more")})},
            {"calendarCard", "small", "start", "almanax"}));
    else if (kind == "filter")
        Add(target, Element("VisualElement", "ctr_calendarFilterItem", {Element("CheckboxCustom", "chbx_filterName"),
            Element("DofusIcon", "tx_drag"), Element("VisualElement", "", {Element("ColorSquare", "colorSquare")})}));
    else throw std::runtime_error("Unknown template");
}
// A named element under an element (depth first), for the checks.
Obj Find(Obj root, const std::string& name) {
    if (Get(root).name == name) return root;
    for (auto child : Get(root).children) if (auto found = Find(child, name)) return found;
    return nullptr;
}
Obj Manipulator(Obj element, const std::string& type) {
    Obj found = nullptr;
    for (auto m : Get(element).manipulators) if (Get(m).klass->name == type) { Check(!found, "One " + type + " per element"); found = m; }
    return found;
}
std::string TipText(Obj element) { auto tip = Manipulator(element, "TooltipManipulator"); return tip ? Get(tip).text : std::string(); }
void NativeEnable(Obj, Method) { ++nativeEnables; }
void NativeDisable(Obj, Method) { ++nativeDisables; }
void Setup() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).klass; };
    api.class_get_name = [](Klass k) -> const char* { return static_cast<Class*>(k)->name.c_str(); };
    api.class_is_valuetype = [](Klass k) {
        const auto& n = static_cast<Class*>(k)->name;
        return n == "Box" || n == "Length" || n.starts_with("Style") || n == "CalendarUIBinding";
    };
    api.object_new = [](Klass k) -> Obj { return Make(static_cast<Class*>(k)->name); };
    api.object_unbox = [](Obj o) -> void* { return Get(o).bytes.data(); };
    api.string_new = [](const char* s) -> Obj { return String(s); };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++handleId] = o; return handleId; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Invalid GC release"); };
    api.class_get_field_from_name = [](Klass, const char* name) -> void* { const auto it = offsets.find(name); return it == offsets.end() ? nullptr : &it->second; };
    api.field_get_offset = [](void* f) { return *static_cast<size_t*>(f); };
    settingsObject = Make("DofusUISettings");
    for (const auto& [getter, kind] : std::vector<std::pair<std::string, std::string>>{
        {"get_calendarLine", "line"}, {"get_calendarCard", "card"}, {"get_calendarFilterItem", "filter"}}) {
        auto asset = Make("VisualTreeAsset"); Get(asset).kind = kind; Get(settingsObject).fields[getter] = asset;
    }
    tooltipService = Make("TooltipService");
    enterMap = Make("Dictionary`2"); leaveMap = Make("Dictionary`2");
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fixture::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto m = std::make_unique<fixture::MethodData>(); m->type = static_cast<fixture::Class*>(klass)->name; m->name = name; m->argc = argc;
    auto p = m.get(); fixture::methods.push_back(std::move(m)); return p;
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    const auto type = static_cast<fixture::Class*>(klass)->name;
    fixture::Check(rva != 0, "Verified method without RVA");
    fixture::verified.insert({type, name, argc});
    fixture::contracts.push_back({type, name, argc, rva});
    return Find(klass, name, argc);
}
void* Api::FieldInfo(Klass, const char* name) const {
    const auto it = fixture::offsets.find(name);
    if (it == fixture::offsets.end()) throw std::runtime_error(std::string("Field unavailable: ") + name);
    return &it->second;
}
Obj Api::Field(Obj obj, const char* name) const { return fixture::Get(obj).fields.at(name); }
Obj Api::StaticField(Klass klass, const char* name) const {
    fixture::Check(static_cast<fixture::Class*>(klass)->name == "DofusCursor", "Cursor caches only");
    if (std::string(name) == "s_mouseEnterCursorMap") return fixture::enterMap;
    if (std::string(name) == "s_mouseLeaveCursorMap") return fixture::leaveMap;
    throw std::runtime_error("Static field unavailable");
}
std::string Api::String(Obj obj) const { return obj ? fixture::Get(obj).text : std::string(); }
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fixture;
    const auto name = static_cast<fixture::Class*>(klass)->name;
    RequireVerified(name, ".ctor", static_cast<int>(args.size()));
    auto o = Make(name);
    if (name == "Length") { size_t i = 0; for (auto arg : args) { std::memcpy(Get(o).bytes.data() + i * 4, arg, 4); ++i; } }
    return o;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    const auto& info = *static_cast<const fixture::MethodData*>(method);
    Check(info.argc == static_cast<int>(arguments.size()), "Managed invocation argument count");
    RequireVerified(info.type, info.name, info.argc);
    const std::vector<void*> args(arguments);
    const auto object = [&](size_t i) { return static_cast<Obj>(args.at(i)); };
    if (info.type == "Application" && info.name == "OpenURL") {
        Check(!self, "Static Application.OpenURL(string)");
        urls.push_back(Get(object(0)).text);
        return nullptr;
    }
    if (info.type == "DofusUISettings" && info.name == "GetSettings") { Check(!self, "Static GetSettings"); return settingsObject; }
    if (info.type == "DofusUISettings") { Check(self == settingsObject, "The game's UI settings"); return Get(self).fields.at(info.name); }
    if (info.type == "VisualTreeAsset" && info.name == "CloneTree") {
        Check(Get(object(0)).children.empty(), "Cloned into a new element");
        CloneInto(object(0), Get(self).kind);
        return nullptr;
    }
    if (info.type == "VisualElementExtensions") {
        Check(!self && object(0) && object(1), "Static manipulator extensions");
        auto& list = Get(object(0)).manipulators;
        if (info.name == "AddManipulator") {
            Check(std::find(list.begin(), list.end(), object(1)) == list.end(), "Manipulator added twice");
            list.push_back(object(1));
        } else list.erase(std::remove(list.begin(), list.end(), object(1)), list.end());
        return nullptr;
    }
    if (info.type == "TooltipManipulator" && info.name == ".ctor") {
        auto& tip = Get(self);
        tip.service = object(0); tip.text = Get(object(1)).text;
        tip.origin = *static_cast<int*>(args[2]); tip.source = *static_cast<int*>(args[3]); tip.behavior = *static_cast<int*>(args[4]);
        tip.front = *static_cast<bool*>(args[5]); ++tip.constructed;
        return nullptr;
    }
    if (info.type == "BaseTooltipManipulator" && info.name == "HideTooltip") { ++Get(self).hides; return nullptr; }
    if (info.type == "ExtendedClickable" && info.name == ".ctor") { Get(self).nullAction = !args[0]; ++Get(self).constructed; return nullptr; }
    if (info.type == "DofusCursor" && info.name == "EnableCursorHandler") {
        Check(!self && *static_cast<int64_t*>(args[1]) == 0x200000 && *static_cast<int*>(args[2]) == 0, "The game's hand cursor");
        Get(object(0)).cursor = true; Get(enterMap).keys.insert(object(0)); Get(leaveMap).keys.insert(object(0));
        return nullptr;
    }
    if (info.type == "DofusCursor" && info.name == "RemoveCursorHandler") { Get(object(0)).cursor = false; return nullptr; } // the caches keep it
    if (info.type == "Dictionary`2" && info.name == "Remove") return Box(Get(self).keys.erase(object(0)) == 1);
    if (info.type == "Dictionary`2" && info.name == "ContainsKey") return Box(Get(self).keys.contains(object(0)));
    if (info.type == "ColorSquare" && info.name == "SetColor") { Get(self).color = Get(object(0)).text; return nullptr; }
    return Call(self, info.name.c_str(), arguments);
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    auto& o = Get(self);
    const std::string method = name;
    const std::vector<void*> args(arguments);
    RequireVerified(o.klass->name, method, static_cast<int>(args.size()));
    if (method == "get_ctr_days" || method == "get_gd_events" || method == "get_gd_calendarFilters") return o.fields.at(method.substr(4));
    if (method == "get_parent") return o.fields["parent"];
    if (method == "get_panel") return Panel(self);
    if (method == "IndexOf") {
        const auto& c = o.children;
        const auto it = std::find(c.begin(), c.end(), static_cast<Obj>(args[0]));
        return Box(it == c.end() ? -1 : static_cast<int>(it - c.begin()));
    }
    if (method == "Insert") { Insert(self, *static_cast<int*>(args[0]), static_cast<Obj>(args[1])); return nullptr; }
    if (method == "Add") { Add(self, static_cast<Obj>(args[0])); return nullptr; }
    if (method == "RemoveFromHierarchy") {
        if (self == failDetach) { failDetach = nullptr; throw std::runtime_error("Temporary detach failure"); }
        Detach(self); return nullptr;
    }
    if (method == "get_layout") return Box(o.layout);
    if (method == "get_style") { auto style = Make("InlineStyleAccess"); Get(style).fields["owner"] = self; return style; }
    if (o.klass->name == "InlineStyleAccess") {
        const std::string prefix = "UnityEngine.UIElements.IStyle.";
        Check(method.starts_with(prefix), "Inline styles through their explicit IStyle methods");
        const auto operation = method.substr(prefix.size()), property = operation.substr(4);
        auto& target = Get(o.fields.at("owner"));
        if (operation.starts_with("get_")) {
            auto box = Make(StyleKind(property)); Get(box).property = property;
            if (target.styles.contains(property)) Get(box).bytes = target.styles.at(property);
            return box;
        }
        Check(operation.starts_with("set_"), "Unexpected style method");
        std::memcpy(target.styles[property].data(), args.at(0), Size(property));
        return nullptr;
    }
    if (method == "set_value") { std::memcpy(o.bytes.data(), args.at(0), Size(o.property)); return nullptr; }
    if (method == "AddToClassList") { o.classes.insert(Get(static_cast<Obj>(args[0])).text); return nullptr; }
    if (method == "RemoveFromClassList") { o.classes.erase(Get(static_cast<Obj>(args[0])).text); return nullptr; }
    if (method == "set_text") { o.text = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (method == "set_name") { o.name = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (method == "get_name") return fixture::String(o.name);
    if (method == "get_childCount") return Box(static_cast<int>(o.children.size()));
    if (method == "ElementAt") return o.children.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    if (method == "set_enableRichText") { o.rich = *static_cast<bool*>(args[0]); return nullptr; }
    if (method == "set_visible") { o.visible = *static_cast<bool*>(args[0]); return nullptr; }
    if (method == "set_icon") { o.icon = *static_cast<int*>(args[0]); return nullptr; }
    if (method == "set_iconValue") { o.iconValue = *static_cast<int*>(args[0]); return nullptr; }
    if (method == "set_rightText") { o.rightText = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (method == "SetValueWithoutNotify") { o.value = *static_cast<bool*>(args[0]); return nullptr; }
    if (method == "get_value") return Box(o.value);
    if (method == "set_selectable") { o.selectable = *static_cast<bool*>(args[0]); return nullptr; }
    if (method == "get_Count") return Box(static_cast<int>(o.children.size()));
    if (method == "get_Item") return o.children.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected fake method " + method);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const { return std::string(name) == "fou" ? fixture::tooltipService : nullptr; }
// The options tab and the settings file follow module->Change (offline: only the settings).
// As the real one: checked, then set, then refreshed, synced and saved.
void Module::Change(Settings next, bool) {
    ++fixture::changes;
    if (fixture::failChange == 1) throw std::runtime_error("Invalid display mode");
    settings = next;
    if (fixture::failChange == 2) throw std::runtime_error("Cannot save settings");
}
void Log(const std::string& message) { fixture::logs.push_back(message); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& info = *static_cast<const fixture::MethodData*>(method);
    fixture::Check(info.type == "CalendarUI" && info.argc == 0, "Calendar hook ABI");
    if (rva == 0x2E065E0) {
        fixture::Check(info.name == "OnEnable", "OnEnable hook");
        fixture::enableHook = reinterpret_cast<fixture::HookFn>(replacement); *original = reinterpret_cast<void*>(fixture::NativeEnable);
    } else if (rva == 0x2E06550) {
        fixture::Check(info.name == "OnDisable", "OnDisable hook");
        fixture::disableHook = reinterpret_cast<fixture::HookFn>(replacement); *original = reinterpret_cast<void*>(fixture::NativeDisable);
    } else throw std::runtime_error("Unexpected hook");
}
namespace nativeClick { void AddHandler(bool (*handler)(managed::Obj)) { fixture::clickHandler = handler; ++fixture::clickHandlers; } }
namespace metamob {
void WantKralove() { ++fixture::wants; }
KraloveView MetamobKralove() { ++fixture::views; return fixture::view; }
}
#include "metamob/calendar-kralove.cpp"

namespace {
using namespace fixture;
// The visible texts, written here independently of the module.
const std::string Loading = "Metamob : lecture des ouvertures…", Unreachable = "Metamob injoignable, nouvel essai automatique.",
    Busy = "Metamob : trop de demandes, nouvel essai automatique.", Refused = "Metamob refuse ta clé pour les ouvertures Kralamoure ; nouvel essai dans 10 minutes.",
    KeyUnreadable = "Clé Metamob illisible.", Unavailable = "Ouvertures Kralamoure indisponibles sur l’API Metamob.",
    Unreadable = "Réponse Metamob illisible, nouvel essai automatique.",
    NoQuest = "Serveur inconnu : aucune quête Ocre Dofus Unity sur ton compte Metamob.",
    SeveralQuests = "Serveur inconnu : plusieurs quêtes Ocre Dofus Unity, aucune n’indique ce personnage dans « Personnage à contacter ».",
    NoServer = "Serveur inconnu : ta quête Ocre sur Metamob n’indique pas de serveur.",
    PastWeek = "Metamob : les ouvertures des semaines passées ne sont pas lues.",
    Empty = "Aucune ouverture Kralamoure cette semaine sur Draconiros.",
    Banner = "Ouvertures Kralamoure planifiées sur Metamob pour Draconiros, à l’heure de ce PC. Survole une ouverture pour ses "
        "détails ; clique dessus pour l’ouvrir sur metamob.fr.",
    BannerUnknown = "Ouvertures Kralamoure planifiées sur Metamob pour le serveur de ta quête Ocre, à l’heure de ce PC. Survole une "
        "ouverture pour ses détails ; clique dessus pour l’ouvrir sur metamob.fr.",
    FilterTip = "Kralamoure (Dofus Enhanced) : les ouvertures planifiées sur Metamob pour le serveur de ta quête Ocre, en lignes sous "
        "les jours. Décoché, rien n’est affiché ni lu sur Metamob. C’est l’option « Ouvertures Kralamoure dans le calendrier ».";
const std::array<float, 4> Violet{138 / 255.f, 92 / 255.f, 246 / 255.f, 1}, VioletCard{138 / 255.f, 92 / 255.f, 246 / 255.f, .3f},
    Grey{1, 1, 1, .15f};
// Private data planted in the openings: never in a log.
const std::string Creator = "Zorglubette", Marker = "Corps-Prive-Kralove";
constexpr int64_t SecretId = 987654321;

int64_t Utc(const char* text) { const auto at = metamob::UtcSeconds(text); Check(at.has_value(), std::string("Fixture date ") + text); return *at; }
int64_t Day(int y, int m, int d) { return metamob::DayNumber(y, m, d); }
std::optional<metamob::CivilTime> Paris(int64_t at) { return metamob::LocalTime(at, L"Romance Standard Time"); }
std::optional<metamob::CivilTime> Universal(int64_t at) { return metamob::LocalTime(at, L"UTC"); }
int64_t FakeNow() { return fixture::now; }
metamob::Kralove Opening(int64_t id, const char* at, int characters, std::string creator = {}, std::string description = {}) {
    metamob::Kralove opening;
    opening.id = id; opening.at = Utc(at); opening.serverId = 11; opening.server = "Draconiros";
    opening.creator = std::move(creator); opening.description = std::move(description);
    opening.participants = 4; opening.characters = characters; opening.messages = 3;
    return opening;
}

// The calendar window as the game builds it: ctr_days (7 day cards) and
// gd_events alone in one attached container (24 px of left padding);
// gd_calendarFilters after the level filter and the « MES CALENDRIERS » title.
struct Window {
    Obj owner, binding, container, days, events, list, panel, filters, level, title, filterList;
    std::array<Obj, 7> cards;
};
Window window;
uint64_t Ticks(int64_t day) { return (static_cast<uint64_t>(day + 719162) * 864000000000ULL) | (uint64_t(2) << 62); } // Kind Local
void ShowWeek(int64_t monday) {
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).fields["<dayDate>k__BackingField"] = Box(Ticks(monday + static_cast<int64_t>(i)));
}
Window Build(int64_t monday) {
    Window w;
    w.owner = Make("CalendarUI"); w.binding = Make("CalendarUIBinding"); w.container = Make("VisualElement");
    w.days = Make("VisualElement"); w.events = Make("ListView"); w.list = Make("List"); w.panel = Make("Panel");
    w.filters = Make("VisualElement"); w.level = Make("VisualElement"); w.title = Make("VisualElement"); w.filterList = Make("ListView");
    Get(w.container).fields["panel"] = w.panel; Get(w.filters).fields["panel"] = w.panel;
    fixture::Add(w.container, w.days); fixture::Add(w.container, w.events);
    fixture::Add(w.filters, w.level); fixture::Add(w.filters, w.title); fixture::Add(w.filters, w.filterList);
    for (int i = 0; i < 5; ++i) fixture::Add(w.filterList, Make("VisualElement")); // the game's five calendars
    Get(w.owner).fields["dzyi"] = w.binding; Get(w.owner).fields["m_dayCards"] = w.list;
    Get(w.binding).fields["ctr_days"] = w.days; Get(w.binding).fields["gd_events"] = w.events;
    Get(w.binding).fields["gd_calendarFilters"] = w.filterList;
    Get(w.days).layout = {24, 0, 1096, 74};
    for (size_t i = 0; i < 7; ++i) {
        w.cards[i] = Make("CalendarDayCard");
        Get(w.cards[i]).layout = {72.f + 144.f * static_cast<float>(i), 8, 136, 58};
        fixture::Add(w.days, w.cards[i]);
        Get(w.list).children.push_back(w.cards[i]);
    }
    window = w;
    ShowWeek(monday);
    return w;
}
Obj Block() { return state.block.get(); }
Obj Filter() { return state.filter.get(); }
// The layout engine's places, once it ran: the block at the container's
// padding, its lines one under the other, their events after the 64 px banner
// and its 8 px margin. A new block or line has none until then.
void LayOut() {
    if (!Block()) return;
    Get(Block()).layout = {24, 74, 1096, 146.f * static_cast<float>(state.lines.size())};
    for (size_t i = 0; i < state.lines.size(); ++i) {
        const auto& line = state.lines[i];
        Get(line.root.get()).layout = {0, 146.f * static_cast<float>(i), 1096, 146};
        Get(line.line.get()).layout = {0, 0, 1096, 138};
        Get(line.events.get()).layout = {72, 0, 1024, 138};
    }
}
// The cards of a day, in their lines' order, and their parts.
std::vector<const Card*> On(size_t day) {
    std::vector<const Card*> found;
    for (const auto& card : state.cards) if (card.day == day) found.push_back(&card);
    return found;
}
Obj PartOf(const Card* card, const std::string& name) { auto part = Find(card->root.get(), name); Check(part != nullptr, "Card part " + name); return part; }
std::string Title(const Card* card) { return Get(PartOf(card, "lbl_title")).text; }
std::string Start(const Card* card) { return Get(PartOf(card, "lbl_start")).text; }
std::string End(const Card* card) { return Get(PartOf(card, "lbl_end")).text; }
size_t LineOf(const Card* card) {
    for (size_t i = 0; i < state.lines.size(); ++i) if (Parent(card->root.get()) == state.lines[i].events.get()) return i;
    throw std::runtime_error("Card outside the lines");
}
size_t Logged(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
// The cursor caches hold the mod's own hands only (its cards and the entry's
// checkbox), in both caches: no element of a template clone (their buttons,
// checkbox and colour square register as they are built), none of a discarded tree.
void Cached() {
    std::set<Obj> mine;
    for (const auto& card : state.cards) if (card.root.get()) mine.insert(card.root.get());
    if (state.checkbox.get()) mine.insert(state.checkbox.get());
    Check(Get(enterMap).keys == Get(leaveMap).keys, "Both cursor caches agree");
    for (auto key : Get(enterMap).keys) Check(mine.contains(key), "A cursor cache keeps an element the mod no longer owns or never shows");
}
void Tick() { TickCalendarKralove(); LayOut(); Cached(); }
void Ready(int64_t fromDay, std::vector<metamob::Kralove> openings) {
    view.usable = true; view.status = metamob::KraloveStatus::Ready; view.read = true; view.server = "Draconiros";
    view.fromDay = fromDay; view.openings = std::move(openings); ++view.version;
}
void Status(metamob::KraloveStatus status) {
    view.usable = true; view.status = status; view.read = false; view.server.clear(); view.openings.clear(); ++view.version;
}
void Untouched() {
    for (auto card : window.cards)
        Check(Get(card).styles.empty() && Get(card).classes.empty() && Get(card).manipulators.empty(), "Day cards untouched");
    for (auto element : {window.days, window.events, window.container, window.filters, window.filterList, window.level, window.title})
        Check(Get(element).styles.empty() && Get(element).classes.empty() && Get(element).manipulators.empty(), "Native elements untouched");
    Check(Get(window.days).children.size() == 7 && Get(window.filterList).children.size() == 5, "The game's lists keep their items");
}
// Nothing of the mod in the window, and no native registration left behind.
void Clean() {
    Check(Get(window.container).children == std::vector<Obj>{window.days, window.events}, "No line left");
    Check(Get(window.filters).children == std::vector<Obj>{window.level, window.title, window.filterList}, "No entry left");
    Check(Get(enterMap).keys.empty() && Get(leaveMap).keys.empty(), "Cursor caches released");
}

void Helpers() {
    Check(EventLink(SecretId) == "https://www.metamob.fr/kralove/event/987654321" && EventLink(0).empty() && EventLink(-5).empty(),
        "Event page: digits of a positive id only");
    for (const auto& [status, text] : std::vector<std::pair<metamob::KraloveStatus, std::string>>{
        {metamob::KraloveStatus::Loading, Loading}, {metamob::KraloveStatus::Ready, ""}, {metamob::KraloveStatus::Unreachable, Unreachable},
        {metamob::KraloveStatus::Busy, Busy}, {metamob::KraloveStatus::Refused, Refused}, {metamob::KraloveStatus::KeyUnreadable, KeyUnreadable},
        {metamob::KraloveStatus::Unavailable, Unavailable}, {metamob::KraloveStatus::Unreadable, Unreadable},
        {metamob::KraloveStatus::NoQuest, NoQuest}, {metamob::KraloveStatus::SeveralQuests, SeveralQuests},
        {metamob::KraloveStatus::NoServer, NoServer}})
        Check(StatusText(status) == text, "Status text " + text);
    Check(EmptyText("Draconiros") == Empty && EmptyText({}) == "Aucune ouverture Kralamoure cette semaine sur ton serveur.", "Empty week text");
    Check(MoreText(1) == "+1 ouverture" && MoreText(2) == "+2 ouvertures", "More text");

    // Bucketing by this PC's local days, around both 2026-2027 daylight saving changes in Paris.
    const std::vector<metamob::Kralove> october{
        Opening(1, "2026-10-18T21:59:00Z", 2),  // Sunday 18 23:59 CEST: the week before
        Opening(2, "2026-10-18T22:00:00Z", 2),  // Monday 19 00:00 CEST
        Opening(3, "2026-10-24T22:30:00Z", 1),  // Sunday 25 00:30 CEST
        Opening(4, "2026-10-25T00:30:00Z", 3),  // Sunday 25 02:30 CEST (the hour played twice, first time)
        Opening(5, "2026-10-25T01:30:00Z", 4),  // Sunday 25 02:30 CET (second time)
        Opening(6, "2026-10-25T22:59:00Z", 5),  // Sunday 25 23:59 CET
        Opening(7, "2026-10-25T23:00:00Z", 6)}; // Monday 26 00:00 CET: the week after
    std::array<int64_t, 7> week{};
    for (size_t i = 0; i < 7; ++i) week[i] = Day(2026, 10, 19) + static_cast<int64_t>(i);
    const auto paris = Bucket(october, week, Paris);
    Check(paris[0].size() == 1 && paris[0][0].opening->id == 2 && StartText(paris[0][0]) == "Début : 00h00" &&
        CountText(paris[0][0]) == "2 persos", "Monday 00:00 CEST");
    for (size_t day = 1; day < 6; ++day) Check(paris[day].empty(), "No opening midweek");
    Check(paris[6].size() == 4 && StartText(paris[6][0]) == "Début : 00h30" && CountText(paris[6][0]) == "1 perso" &&
        StartText(paris[6][1]) == "Début : 02h30" && paris[6][1].opening->id == 4 && StartText(paris[6][2]) == "Début : 02h30" &&
        paris[6][2].opening->id == 5 && StartText(paris[6][3]) == "Début : 23h59", "Sunday 25 October: summer then winter time");
    Check(LongDate(paris[6][1].local) == "dimanche 25 octobre 2026 à 02h30" && LongDate(paris[0][0].local) == "lundi 19 octobre 2026 à 00h00",
        "Long local dates");
    Check(LongDate(*Paris(Utc("2026-11-01T20:00:00Z"))) == "dimanche 1er novembre 2026 à 21h00" &&
        LongDate(*Paris(Utc("2026-11-02T20:00:00Z"))) == "lundi 2 novembre 2026 à 21h00", "The first of the month: « 1er »");
    const auto utc = Bucket(october, week, Universal);
    Check(utc[0].empty() && utc[6].size() == 4 && utc[6][3].opening->id == 7 && StartText(utc[6][0]) == "Début : 00h30" &&
        utc[6][0].opening->id == 4, "Another time zone, other days: the local days decide");
    const std::vector<metamob::Kralove> march{
        Opening(11, "2027-03-21T23:00:00Z", 1),  // Monday 22 00:00 CET
        Opening(12, "2027-03-28T00:30:00Z", 2),  // Sunday 28 01:30 CET
        Opening(13, "2027-03-28T01:00:00Z", 3),  // Sunday 28 03:00 CEST (02:00 skipped)
        Opening(14, "2027-03-28T21:59:00Z", 4),  // Sunday 28 23:59 CEST
        Opening(15, "2027-03-28T22:00:00Z", 5)}; // Monday 29 00:00 CEST: the week after
    for (size_t i = 0; i < 7; ++i) week[i] = Day(2027, 3, 22) + static_cast<int64_t>(i);
    const auto spring = Bucket(march, week, Paris);
    Check(spring[0].size() == 1 && StartText(spring[0][0]) == "Début : 00h00", "Monday 22 March 00:00 CET");
    Check(spring[6].size() == 3 && StartText(spring[6][0]) == "Début : 01h30" && StartText(spring[6][1]) == "Début : 03h00" &&
        StartText(spring[6][2]) == "Début : 23h59" && LongDate(spring[6][1].local) == "dimanche 28 mars 2027 à 03h00",
        "Sunday 28 March: winter then summer time");
    const auto unknown = Bucket(march, week, [](int64_t) { return std::optional<metamob::CivilTime>(); });
    for (const auto& day : unknown) Check(day.empty(), "No local time: no opening placed");
}

void Install() {
    // A field layout that is not the pinned one: disabled, the rest of the mod unaffected.
    offsets["m_dayCards"] = 0x218;
    InstallCalendarKralove();
    Check(!installed && !enableHook && !disableHook && !clickHandler && Logged("CALENDAR_KRALOVE_DISABLED") == 1,
        "A wrong contract disables the feature, before any hook");
    offsets["m_dayCards"] = 0x210;
    offsets["dzyi"] = 0x18;
    contracts.clear(); verified.clear();
    InstallCalendarKralove();
    Check(!installed && !enableHook && Logged("CALENDAR_KRALOVE_DISABLED") == 2, "The binding's offset is checked too");
    offsets["dzyi"] = 0x10;
    offsets["s_mouseLeaveCursorMap"] = 0x50;
    contracts.clear(); verified.clear();
    InstallCalendarKralove();
    Check(!installed && !enableHook && Logged("CALENDAR_KRALOVE_DISABLED") == 3, "The cursor caches' layout is checked too");
    offsets["s_mouseLeaveCursorMap"] = 0x48;
    contracts.clear(); verified.clear();
    InstallCalendarKralove();
    Check(installed && enableHook && disableHook && clickHandler && clickHandlers == 1 && Logged("CALENDAR_KRALOVE_READY") == 1, "Installed");
    const std::vector<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"CalendarUIBinding", "get_ctr_days", 0, 0x2E9E140}, {"CalendarUIBinding", "get_gd_events", 0, 0x2E9E630},
        {"CalendarUIBinding", "get_gd_calendarFilters", 0, 0x2E9E580}, {"CalendarUI", "OnEnable", 0, 0x2E065E0},
        {"CalendarUI", "OnDisable", 0, 0x2E06550}, {"DofusUISettings", "GetSettings", 0, 0x1597FD0},
        {"DofusUISettings", "get_calendarLine", 0, 0x1599AB0}, {"DofusUISettings", "get_calendarCard", 0, 0x15999F0},
        {"DofusUISettings", "get_calendarFilterItem", 0, 0x1599A70}, {"VisualTreeAsset", "CloneTree", 1, 0x567E440},
        {"VisualElement", "Insert", 2, 0x55E5710}, {"VisualElement", "IndexOf", 1, 0x55E55B0}, {"VisualElement", "AddToClassList", 1, 0x55DF820},
        {"VisualElement", "RemoveFromClassList", 1, 0x55E7AB0}, {"VisualElement", "get_childCount", 0, 0x55F5970},
        {"VisualElement", "ElementAt", 1, 0x55E2DE0}, {"VisualElement", "get_name", 0, 0x3158710}, {"VisualElement", "set_visible", 1, 0x55FA170},
        {"VisualElement", "RemoveFromHierarchy", 0, 0x55E7BD0}, {"VisualElement", "get_layout", 0, 0x55F6930},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_backgroundColor", 1, 0x55D2BA0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_backgroundColor", 0, 0x55D0D90},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_unityBackgroundImageTintColor", 1, 0x55D5530},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_unityBackgroundImageTintColor", 0, 0x55D1FC0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_left", 1, 0x55D3EB0},
        {"DofusIcon", "set_icon", 1, 0x170F480}, {"CheckboxCustom", "set_iconValue", 1, 0x1705240}, {"CheckboxCustom", "get_value", 0, 0x1705050},
        {"CheckboxCustom", "SetValueWithoutNotify", 1, 0x17044F0}, {"ColorSquare", "SetColor", 1, 0x1708AF0},
        {"ColorSquare", "set_selectable", 1, 0x17094E0}, {"TooltipManipulator", ".ctor", 6, 0x15DFAE0},
        {"BaseTooltipManipulator", "HideTooltip", 0, 0x15CD030}, {"VisualElementExtensions", "AddManipulator", 2, 0x56755F0},
        {"VisualElementExtensions", "RemoveManipulator", 2, 0x5676A90}, {"ExtendedClickable", ".ctor", 1, 0x172B830},
        {"DofusCursor", "EnableCursorHandler", 3, 0x2808CD0}, {"DofusCursor", "RemoveCursorHandler", 1, 0x2809CD0},
        {"Application", "OpenURL", 1, 0x53ACA50}, {"DofusLabel", ".ctor", 0, 0x1593940}, {"TextElement", "set_enableRichText", 1, 0x564B1E0}};
    for (const auto& contract : expected)
        Check(std::find(contracts.begin(), contracts.end(), contract) != contracts.end(), "Contract verified: " + std::get<1>(contract));
    InstallCalendarKralove();
    Check(clickHandlers == 1 && Logged("CALENDAR_KRALOVE_READY") == 1, "Installed once");
}

// No usable key: no entry, no line, no request; both come once a key is usable.
void WithoutKey() {
    view = {};
    enableHook(window.owner, nullptr);
    Check(nativeEnables == 1 && pending.handle, "OnEnable passes through, the window waits");
    Tick(); Tick();
    Check(!Filter() && !Block() && wants == 0 && views == 2, "No usable key: no entry, no line, nothing asked");
    Clean(); Untouched();
    view.usable = true;
    Tick();
    Check(Filter() && Block() && wants == 1, "A usable key: the entry and the lines");
    Check(Get(window.container).children == std::vector<Obj>{window.days, Block(), window.events}, "Lines between the day cards and the game's lines");
    Check(Get(window.filters).children == std::vector<Obj>{window.level, window.title, Filter(), window.filterList},
        "The entry right above the game's calendars");
}

void Entry() {
    const auto row = Filter();
    Check(Get(row).name == "dofusEnhancedKralamoure" && Pixels(row, "height") == 36 && FloatStyle(row, "flexShrink") == 0,
        "An entry as high as the game's (36 px), never shrunk");
    const auto item = Find(row, "ctr_calendarFilterItem"), checkbox = Find(row, "chbx_filterName"), drag = Find(row, "tx_drag"),
        square = Find(row, "colorSquare");
    Check(item && Parent(item) == row && checkbox == state.checkbox.get(), "The game's filter template");
    Check(Get(checkbox).rightText == "Kralamoure" && Get(checkbox).iconValue == 329 && Get(checkbox).value, "Kralamoure, its icon, checked");
    Check(EnumStyle(drag, "display") == 1 && !Styled(item, "display"), "No reordering handle, as the game's entries");
    Check(Get(square).color == "8A5CF6" && !Get(square).selectable, "Its colour, without the colour picker");
    Check(Get(checkbox).cursor && Get(enterMap).keys.contains(checkbox) && !Get(square).cursor && !Get(enterMap).keys.contains(square),
        "The game's hand over the checkbox only: the colour square's own registration released");
    const auto tip = Manipulator(row, "TooltipManipulator");
    Check(tip && Get(tip).text == FilterTip && Get(tip).service == tooltipService && Get(tip).origin == 7 && Get(tip).source == 1 &&
        Get(tip).behavior == 0 && !Get(tip).front && Get(tip).constructed == 1, "The game's tooltip on the entry, closed when the pointer leaves it");
    Check(Logged("CALENDAR_KRALOVE_FILTER") == 1, "Entry placed once");
}

void Loads() {
    const auto block = Block();
    Check(Get(block).name == "dofusEnhancedKralamoureLines" && FloatStyle(block, "flexShrink") == 0 && state.lines.size() == 1,
        "One line, never shrunk");
    const auto& line = state.lines[0];
    Check(Parent(line.root.get()) == block && Find(line.root.get(), "calendarLine") == line.line.get(), "The game's line template");
    Check(Near(ColorStyle(line.border.get(), "unityBackgroundImageTintColor"), Violet) && Get(line.icon.get()).icon == 329 &&
        Get(line.banner.get()).visible, "Banner tinted with the Kralamoure colour, with its icon");
    Check(Pixels(line.line.get(), "height") == 80, "A line of a status: as high as its banner");
    const auto status = state.status.get();
    Check(Get(status).klass->name == "DofusLabel" && Parent(status) == line.events.get() && Get(status).text == Loading &&
        EnumStyle(status, "display") == 0 && !Get(status).rich && Get(status).classes.contains("italicShort_medium") &&
        Get(status).classes.contains("textColor_white_white80") && EnumStyle(line.events.get(), "alignItems") == 2, "Loading text");
    Check(TipText(line.banner.get()) == BannerUnknown, "Banner tooltip");
    const auto renders = Logged("CALENDAR_KRALOVE_WEEK");
    Tick(); Tick();
    Check(wants == 3 && Logged("CALENDAR_KRALOVE_WEEK") == renders, "Asked at every pump, drawn again only on a change");
    Untouched();
}

void CheckCard(const Card* card, size_t line, size_t day, const std::string& start, const std::string& end, bool past) {
    const auto root = card->root.get(), inner = PartOf(card, "ctr_calendarCard");
    Check(LineOf(card) == line, "Card on line " + std::to_string(line));
    Check(Get(root).classes.contains("calendarCard") && EnumStyle(root, "position") == 1 && Pixels(root, "left") == 144.f * static_cast<float>(day) &&
        Pixels(root, "width") == 136, "A one-day card on its day, as the game places its own");
    Check(Get(inner).classes == std::set<std::string>{"calendarCard", "small", "start"}, "Small, starting that day, not the template's almanax");
    Check(Title(card) == "Ouverture Kralamoure" && Start(card) == start && End(card) == end && !Get(PartOf(card, "lbl_title")).rich &&
        !Get(PartOf(card, "lbl_start")).rich && !Get(PartOf(card, "lbl_end")).rich, "Card texts: " + start);
    Check(Get(PartOf(card, "tx_icon")).icon == 329, "Kralamoure icon");
    Check(Near(ColorStyle(PartOf(card, "colorLine"), "backgroundColor"), past ? Grey : Violet) &&
        Near(ColorStyle(inner, "backgroundColor"), past ? Grey : VioletCard), "Card colours");
    Check(past ? FloatStyle(root, "opacity") == .6f : !Styled(root, "opacity"), "Past openings dimmed");
    for (auto name : {"ctr_rewards", "tx_offering", "ctr_buttons"}) Check(EnumStyle(PartOf(card, name), "display") == 1, "No " + std::string(name));
    Check(EnumStyle(PartOf(card, "ctr_duration"), "flexDirection") == 0 && EnumStyle(PartOf(card, "ctr_duration"), "alignItems") == 2,
        "Start then registrants, centred");
    const auto click = Manipulator(root, "ExtendedClickable");
    Check(click && Get(click).nullAction && Get(click).constructed == 1 && Get(root).cursor && Get(enterMap).keys.contains(root) &&
        Get(leaveMap).keys.contains(root), "Clickable without action, the game's hand cursor");
}

void Openings() {
    now = Utc("2026-10-21T19:00:00Z"); // Wednesday 21:00 in Paris
    Ready(Day(2026, 10, 12), {
        Opening(SecretId, "2026-10-19T19:00:00Z", 12, Creator, "Rendez-vous au zaap " + Marker),
        Opening(108, "2026-10-18T22:00:00Z", 2),
        Opening(109, "2026-10-21T18:00:00Z", 0, Creator),
        Opening(102, "2026-10-24T22:30:00Z", 1),
        Opening(103, "2026-10-25T00:30:00Z", 3),
        Opening(104, "2026-10-25T01:30:00Z", 4),
        Opening(105, "2026-10-25T22:59:00Z", 5),
        Opening(106, "2026-10-25T23:00:00Z", 6),
        Opening(107, "2026-10-18T21:59:00Z", 7)});
    Tick();
    Check(state.lines.size() == 3 && EnumStyle(state.status.get(), "display") == 1 && TipText(state.lines[0].banner.get()) == Banner,
        "Three lines (Sunday has four openings), no status text, the server in the banner's tooltip");
    for (size_t i = 0; i < 3; ++i)
        Check(Pixels(state.lines[i].line.get(), "height") == 138 && Get(state.lines[i].banner.get()).visible == (i == 0) &&
            Near(ColorStyle(state.lines[i].border.get(), "unityBackgroundImageTintColor"), Violet), "Lines 138 px, the banner on the first only");
    Check(Get(Block()).children.size() == 3, "The lines in the block");
    const auto monday = On(0), wednesday = On(2), sunday = On(6);
    Check(monday.size() == 2 && wednesday.size() == 1 && sunday.size() == 3 && On(1).empty() && On(3).empty() && On(4).empty() && On(5).empty(),
        "Openings by local day, the weeks before and after left out");
    CheckCard(monday[0], 0, 0, "Début : 00h00", "2 persos", true);
    CheckCard(monday[1], 1, 0, "Début : 21h00", "12 persos", true);
    CheckCard(wednesday[0], 0, 2, "Début : 20h00", "0 perso", true);
    CheckCard(sunday[0], 0, 6, "Début : 00h30", "1 perso", false);
    CheckCard(sunday[1], 1, 6, "Début : 02h30", "3 persos", false);
    const auto more = sunday[2];
    Check(LineOf(more) == 2 && more->id == 0 && Title(more) == "Kralamoure" && Start(more) == "+2 ouvertures" && End(more).empty() &&
        EnumStyle(PartOf(more, "lbl_end"), "display") == 1 && Near(ColorStyle(PartOf(more, "colorLine"), "backgroundColor"), Violet),
        "The others of a crowded day: one more card");
    Check(TipText(more->root.get()) == "Autres ouvertures Kralamoure ce jour-là :\n02h30 · 4 persos\n23h59 · 5 persos\n"
        "Clique pour voir toutes les ouvertures sur metamob.fr.", "Its tooltip tells the others");
    Check(Get(more->root.get()).cursor && Get(enterMap).keys.contains(more->root.get()), "It is clickable too: the hand cursor");
    for (const auto& contract : std::vector<std::tuple<std::string, std::string, int, uintptr_t>>{
        {"Dictionary`2", "ContainsKey", 1, 0x36EEAC0}, {"Dictionary`2", "Remove", 1, 0x36F0D60}})
        Check(std::find(contracts.begin(), contracts.end(), contract) != contracts.end(), "Cursor caches' " + std::get<1>(contract) + " verified");
    // Every element of the clones that registered itself (4 buttons a card, the
    // entry's checkbox and colour square) was released at once: the caches hold
    // the 6 cards and the checkbox only.
    Check(Get(enterMap).keys.size() == 7 && Get(leaveMap).keys.size() == 7, "Cursor caches: the mod's 7 hands only");
    Check(TipText(monday[1]->root.get()) == "Ouverture Kralamoure passée · Draconiros\nlundi 19 octobre 2026 à 21h00 (heure de ce PC)\n"
        "Organisateur : " + Creator + "\nInscrits : 4 joueurs, 12 personnages · 3 messages\n« Rendez-vous au zaap " + Marker + " »\n"
        "Clique pour ouvrir l’événement sur metamob.fr.", "Tooltip of a past opening");
    Check(TipText(sunday[0]->root.get()) == "Ouverture Kralamoure · Draconiros\ndimanche 25 octobre 2026 à 00h30 (heure de ce PC)\n"
        "Organisateur : inconnu\nInscrits : 4 joueurs, 1 personnage · 3 messages\nClique pour ouvrir l’événement sur metamob.fr.",
        "Tooltip without organiser nor description");
    const auto tip = Manipulator(sunday[0]->root.get(), "TooltipManipulator");
    Check(Get(tip).origin == 7 && Get(tip).source == 1 && Get(tip).behavior == 0 && Get(tip).service == tooltipService,
        "The calendar's own tooltip placement, closed when the pointer leaves the card (not when it moves over the card's labels)");
    Check(Manipulator(state.lines[0].banner.get(), "TooltipManipulator") &&
        Get(Manipulator(state.lines[0].banner.get(), "TooltipManipulator")).behavior == 0, "The banner's too");
    // Clicks: the event's page for an opening, Metamob's list for « +2 ouvertures », the game's own elements untouched.
    Check(clickHandler(monday[1]->root.get()) && urls == std::vector<std::string>{"https://www.metamob.fr/kralove/event/987654321"}, "Opening clicked");
    Check(clickHandler(more->root.get()) && urls.size() == 2 && urls.back() == "https://www.metamob.fr/kralove",
        "« +2 ouvertures » clicked: Metamob's list of openings");
    Check(!clickHandler(Make("DofusButtonCustom")) && !clickHandler(window.cards[0]) && !clickHandler(PartOf(monday[1], "lbl_title")) &&
        urls.size() == 2, "Other elements keep their clicks");
    Check(Logged("CALENDAR_KRALOVE_OPEN") == 2 && Logged("CALENDAR_KRALOVE_WEEK openings=7 lines=3 state=1") == 1, "Logged with counts only");
    // A reading with the same openings (Metamob's view changes version at each reading): nothing drawn again.
    const auto renders = Logged("CALENDAR_KRALOVE_WEEK");
    const Obj earlier = monday[0]->root.get();
    ++view.version;
    Tick();
    view.status = metamob::KraloveStatus::Unreachable; // a passing failure keeps what was read: the same cards
    Tick();
    view.status = metamob::KraloveStatus::Ready;
    Tick();
    Check(Logged("CALENDAR_KRALOVE_WEEK") == renders && On(0)[0]->root.get() == earlier, "Unchanged openings: the same cards, nothing cloned again");
    // Time passes: greyed when its time comes.
    now = Utc("2026-10-24T22:29:59Z");
    Tick();
    Check(Logged("CALENDAR_KRALOVE_WEEK") == renders && !Styled(On(6)[0]->root.get(), "opacity"), "Not drawn again before an opening's time");
    now = Utc("2026-10-24T22:30:00Z");
    Tick();
    Check(Logged("CALENDAR_KRALOVE_WEEK") == renders + 1, "Drawn again at its time");
    CheckCard(On(6)[0], 0, 6, "Début : 00h30", "1 perso", true);
    Check(TipText(On(6)[0]->root.get()).starts_with("Ouverture Kralamoure passée · Draconiros\n"), "Its tooltip says it is past");
    Check(!Parent(earlier) && Get(earlier).manipulators.empty() && !Get(earlier).cursor && !Get(enterMap).keys.contains(earlier),
        "Previous cards detached, their tooltip, clickable and cursor released");
    size_t hidden = 0;
    for (const auto& object : objects) if (object->klass->name == "TooltipManipulator") hidden += static_cast<size_t>(object->hides);
    Check(hidden == 7, "Released tooltips hidden first (the banner's, then the six cards')");
    Untouched();
}

void Weeks() {
    // The game's arrows: the cards' days change, the lines follow.
    ShowWeek(Day(2026, 10, 26));
    Tick();
    Check(On(0).size() == 1 && Start(On(0)[0]) == "Début : 00h00" && End(On(0)[0]) == "6 persos" && state.lines.size() == 1 &&
        EnumStyle(state.status.get(), "display") == 1, "Next week: its own openings, one line");
    ShowWeek(Day(2026, 11, 2));
    Tick();
    Check(state.cards.empty() && state.lines.size() == 1 && Get(state.status.get()).text == Empty &&
        EnumStyle(state.status.get(), "display") == 0 && Pixels(state.lines[0].line.get(), "height") == 80, "Empty week: told, with the server");
    ShowWeek(Day(2026, 10, 12));
    Tick();
    Check(Get(state.status.get()).text == PastWeek && state.cards.empty(), "A week before the first day asked: not read");
    ShowWeek(Day(2026, 10, 19));
    Tick();
    Check(On(0).size() == 2 && state.lines.size() == 3 && EnumStyle(state.status.get(), "display") == 1, "Back to this week");
    // The day cards' layout: the cards follow, the block's own place included.
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).layout.x += 10;
    Tick();
    Check(Pixels(On(0)[0]->root.get(), "left") == 10 && Pixels(On(6)[1]->root.get(), "left") == 874 && Pixels(On(6)[1]->root.get(), "width") == 136,
        "Cards follow the day cards' layout");
    Get(window.cards[6]).layout.width = 120;
    Get(Block()).layout.x = 20;
    TickCalendarKralove();
    Check(Pixels(On(6)[0]->root.get(), "left") == 878 && Pixels(On(6)[0]->root.get(), "width") == 120 && Pixels(On(0)[1]->root.get(), "left") == 14,
        "Their width and the block's place too");
    Get(window.cards[3]).layout.x = NAN;
    TickCalendarKralove();
    Check(Pixels(On(0)[0]->root.get(), "left") == 14, "An unresolved layout moves nothing");
    Get(window.cards[3]).layout.x = 72.f + 144.f * 3 + 10;
    Get(window.cards[6]).layout.width = 136;
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).layout.x -= 10;
    LayOut();
    Tick();
    Check(Pixels(On(0)[0]->root.get(), "left") == 0 && Pixels(On(6)[0]->root.get(), "left") == 864, "Back on the game's places");
    // Every state of the reading, with nothing read yet.
    for (const auto& [status, text] : std::vector<std::pair<metamob::KraloveStatus, std::string>>{
        {metamob::KraloveStatus::Unreachable, Unreachable}, {metamob::KraloveStatus::Busy, Busy}, {metamob::KraloveStatus::Unavailable, Unavailable},
        {metamob::KraloveStatus::Unreadable, Unreadable}, {metamob::KraloveStatus::NoQuest, NoQuest},
        {metamob::KraloveStatus::SeveralQuests, SeveralQuests}, {metamob::KraloveStatus::NoServer, NoServer},
        {metamob::KraloveStatus::Loading, Loading}}) {
        Status(status);
        Tick();
        Check(Get(state.status.get()).text == text && EnumStyle(state.status.get(), "display") == 0 && state.cards.empty() &&
            state.lines.size() == 1 && TipText(state.lines[0].banner.get()) == BannerUnknown, "State text: " + text);
    }
    // A passing failure after a reading keeps what was read.
    Ready(Day(2026, 10, 12), {Opening(SecretId, "2026-10-22T19:00:00Z", 8, Creator, Marker)});
    view.status = metamob::KraloveStatus::Unreachable;
    Tick();
    Check(On(3).size() == 1 && EnumStyle(state.status.get(), "display") == 1, "Read openings kept through a passing failure");
    Untouched();
}

// The entry's checkbox is the option, both ways.
void Toggle() {
    const auto asked = wants, changed = changes;
    Get(state.checkbox.get()).value = false; // unchecked in the calendar
    Tick();
    Check(changes == changed + 1 && !module->settings.calendarKralamoure && !Block() && Filter() && wants == asked &&
        Logged("CALENDAR_KRALOVE_TOGGLE off") == 1, "Unchecked: the option goes off, the lines go, nothing asked");
    Check(Get(window.container).children == std::vector<Obj>{window.days, window.events} &&
        Get(enterMap).keys == std::set<Obj>{state.checkbox.get()}, "Lines removed whole: only the entry's hand stays");
    Tick(); Tick();
    Check(wants == asked && !Block() && changes == changed + 1, "Unchecked: nothing more");
    Get(state.checkbox.get()).value = true; // checked again
    TickCalendarKralove(); // a new block: not laid out yet
    Check(changes == changed + 2 && module->settings.calendarKralamoure && Block() && wants == asked + 1 && On(3).size() == 1 &&
        Logged("CALENDAR_KRALOVE_TOGGLE on") == 1, "Checked: the option back on, lines drawn at once");
    Check(Pixels(On(3)[0]->root.get(), "left") == 432 && Pixels(On(3)[0]->root.get(), "width") == 136,
        "Before the new block's first layout: the card on the game's own place, not moved by an unresolved layout");
    LayOut(); Cached();
    Tick();
    Check(Pixels(On(3)[0]->root.get(), "left") == 432, "Laid out: still there");
    // The options tab (or another client's settings file) changes the option: the checkbox follows.
    module->settings.calendarKralamoure = false;
    Tick();
    Check(!Get(state.checkbox.get()).value && !Block() && changes == changed + 2, "Option off elsewhere: unchecked, no line, no change sent back");
    module->settings.calendarKralamoure = true;
    Tick();
    Check(Get(state.checkbox.get()).value && Block() && changes == changed + 2, "Option on elsewhere: checked, lines back");
    // A change refused before the settings are set: the checkbox shows the option again, the lines stay.
    failChange = 1;
    Get(state.checkbox.get()).value = false;
    Tick();
    Check(module->settings.calendarKralamoure && Get(state.checkbox.get()).value && Filter() && Block() &&
        Logged("CALENDAR_KRALOVE_TOGGLE_ERROR Invalid display mode") == 1 && Logged("CALENDAR_KRALOVE_TOGGLE off") == 1,
        "Refused change: checked again, nothing removed, the error told");
    // A change whose refresh, sync or save fails once the settings are set: the checkbox shows what they hold.
    failChange = 2;
    Get(state.checkbox.get()).value = false;
    Tick();
    failChange = 0;
    Check(!module->settings.calendarKralamoure && !Get(state.checkbox.get()).value && Filter() && !Block() &&
        Logged("CALENDAR_KRALOVE_TOGGLE_ERROR Cannot save settings") == 1 && Logged("CALENDAR_KRALOVE_ERROR") == 0,
        "A change failing after the settings are set: unchecked as the settings now hold, the lines gone, the entry kept");
    Get(state.checkbox.get()).value = true;
    Tick();
    Check(module->settings.calendarKralamoure && Block() && Logged("CALENDAR_KRALOVE_TOGGLE on") == 2, "Checked again");
    Untouched();
}

// The calendar opened with the option off: the entry only, unchecked; checking it shows the lines.
void OptionOff() {
    disableHook(window.owner, nullptr);
    Clean();
    module->settings.calendarKralamoure = false;
    const auto asked = wants;
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(Filter() && !Get(state.checkbox.get()).value && !Block() && wants == asked, "Option off: the entry, unchecked; no line, nothing asked");
    Get(state.checkbox.get()).value = true;
    Tick();
    Check(module->settings.calendarKralamoure && Block() && wants == asked + 1, "Checked in the calendar: lines and reading");
}

void Layouts() {
    // OnDisable: everything goes before the game's own OnDisable.
    const auto block = Block(), row = Filter(), card = On(3)[0]->root.get();
    const auto disables = nativeDisables;
    disableHook(window.owner, nullptr);
    Check(!state.block.get() && !state.filter.get() && !Parent(block) && !Parent(row) && !pending.handle && nativeDisables == disables + 1,
        "OnDisable removes the entry and the lines");
    Clean();
    Check(!clickHandler(card) && urls.size() == 2, "A removed card is no longer ours");
    const auto asked = wants, read = views;
    Tick(); Tick();
    Check(wants == asked && views == read, "Closed: nothing read nor asked");
    // A layout other than ctr_days then gd_events in one container: refused with its code, nothing placed.
    auto foreign = Make("VisualElement");
    fixture::Insert(window.container, 1, foreign);
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Block() && !Filter() && Logged("CALENDAR_KRALOVE_LAYOUT code=4") == 1 && !pending.handle && Get(window.container).children.size() == 3,
        "gd_events not at index 1: refused once");
    fixture::Detach(foreign);
    fixture::Detach(window.events);
    auto elsewhere = Make("VisualElement"); fixture::Add(elsewhere, window.events);
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && Logged("CALENDAR_KRALOVE_LAYOUT code=3") == 1, "gd_events in another container: refused");
    fixture::Detach(window.events); fixture::Add(window.container, window.events);
    fixture::Detach(window.filterList);
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && !Filter() && Logged("CALENDAR_KRALOVE_LAYOUT code=3") == 2, "gd_calendarFilters without its container: refused");
    fixture::Add(window.filters, window.filterList);
    Get(window.binding).fields["gd_calendarFilters"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && Logged("CALENDAR_KRALOVE_LAYOUT code=2") == 1, "No gd_calendarFilters: refused");
    Get(window.binding).fields["gd_calendarFilters"] = window.filterList;
    Get(window.owner).fields["dzyi"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && Logged("CALENDAR_KRALOVE_LAYOUT code=1") == 1, "No binding: refused");
    Get(window.owner).fields["dzyi"] = window.binding;
    // Six days in the list, or days not in a row: the lines refused, the entry kept.
    auto last = Get(window.list).children.back();
    Get(window.list).children.pop_back();
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Block() && Filter() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 1, "Six day cards: no line (told once), the entry stays");
    Get(window.list).children.push_back(last);
    Get(window.cards[4]).fields["<dayDate>k__BackingField"] = Box(Ticks(Day(2026, 10, 30)));
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 2, "Days not in a row: refused");
    // Cards not dated yet (all on one day): waited for, then placed once dated; refused after 5 s.
    for (auto c : window.cards) Get(c).fields["<dayDate>k__BackingField"] = Box(uint64_t(0));
    enableHook(window.owner, nullptr);
    for (int i = 0; i < 25; ++i) Tick();
    Check(!Block() && Filter() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 2, "Undated cards: waited for, untold");
    Tick();
    Check(!Block() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 3, "Still undated after 5 s: refused");
    ShowWeek(Day(2026, 10, 19));
    Tick();
    Check(!Block(), "Refused until the next OnEnable");
    Get(state.checkbox.get()).value = false; Tick();
    Get(state.checkbox.get()).value = true; Tick();
    Check(Block(), "… or until checked again");
    for (auto c : window.cards) Get(c).fields["<dayDate>k__BackingField"] = Box(Ticks(Day(2026, 10, 19)));
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Block() && Filter() && pending.handle == 0, "Undated again (one day for all): waits");
    ShowWeek(Day(2026, 10, 19));
    Tick();
    Check(Block(), "Placed once dated");
    disableHook(window.owner, nullptr);
    Check(!Block() && !Filter(), "Closed");
    // Not shown yet: tried again at the next pump, untold.
    Get(window.container).fields["panel"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Block() && !Filter() && pending.handle && Logged("CALENDAR_KRALOVE_LAYOUT") == 8, "Not attached yet: waits, untold");
    Get(window.container).fields["panel"] = window.panel;
    Tick();
    Check(Block() && Filter() && Get(window.container).children == std::vector<Obj>{window.days, Block(), window.events}, "Placed once shown");
    // The game moves things around the lines or the entry: all goes, until the next OnEnable.
    auto intruder = Make("VisualElement");
    fixture::Insert(window.container, 1, intruder);
    Tick();
    Check(!Block() && !Filter() && Get(window.container).children == std::vector<Obj>{window.days, intruder, window.events},
        "A changed layout: the mod's elements go, the game's element stays");
    Tick();
    Check(!Block(), "Not placed again without an OnEnable");
    fixture::Detach(intruder);
    enableHook(window.owner, nullptr);
    Tick();
    Check(Block() && Filter(), "Placed again at the next OnEnable");
    fixture::Detach(window.title); fixture::Insert(window.filters, 2, window.title); // the title between the entry and the list
    Tick();
    Check(!Block() && !Filter(), "The entry no longer right above the game's calendars: all goes");
    fixture::Detach(window.title); fixture::Insert(window.filters, 1, window.title);
    // A day card leaving the list while shown: all goes with code 6.
    enableHook(window.owner, nullptr);
    Tick();
    Get(window.list).children.pop_back();
    Tick();
    Check(!Block() && !Filter() && Logged("CALENDAR_KRALOVE_LAYOUT code=6") == 1, "Cards changed while shown");
    Get(window.list).children.push_back(last);
    Clean();
    // Another client UXML: a template without one of its elements is refused (code 7), nothing left.
    for (const auto name : {"ctr_events", "lbl_start", "colorSquare"}) {
        missing = name;
        enableHook(window.owner, nullptr);
        Tick();
        missing.clear();
        Check(!Block() && !Filter() && !pending.handle, std::string("Template without ") + name + ": refused");
        Clean();
    }
    Check(Logged("CALENDAR_KRALOVE_LAYOUT code=7") == 3, "Template refusals told");
    // No tooltip service: nothing placed, told once.
    const auto service = tooltipService;
    tooltipService = nullptr;
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    tooltipService = service;
    Check(!Block() && !Filter() && Logged("CALENDAR_KRALOVE_ERROR Calendar tooltip service unavailable") == 1 &&
        Logged("CALENDAR_KRALOVE_ERROR") == 1, "No tooltip service: nothing placed, told once with its reason");
    Clean();
    // Another character session: all goes.
    enableHook(window.owner, nullptr);
    Tick();
    Check(Block() && Filter(), "Placed");
    module->cached.reset(Make("OtherCache"));
    Tick();
    Check(!Block() && !Filter(), "Another session: all goes");
    Clean();
    Untouched();
}

void KeyAndCleanup() {
    enableHook(window.owner, nullptr);
    Tick();
    Check(Block() && Filter() && !state.cards.empty(), "Placed with its cards");
    // The key no longer usable (forgotten, refused, unreadable): no entry, no line, nothing asked; usable again: back without reopening.
    view.usable = false;
    const auto asked = wants;
    Tick(); Tick();
    Check(!Block() && !Filter() && wants == asked && state.owner.handle, "No usable key: all goes, the calendar waits");
    Clean();
    view.usable = true;
    Tick();
    Check(Block() && Filter() && wants == asked + 1, "Usable again: back");
    // A detach that fails: the cleanup stays pending (clicks consumed, no page), then completes.
    const auto block = Block(), row = Filter(), card = On(3)[0]->root.get();
    failDetach = card;
    disableHook(window.owner, nullptr);
    Check(CalendarKraloveCleanupPending() && Parent(card) && Parent(block) && Parent(row), "Detach failure: cleanup pending");
    Check(Get(card).manipulators.empty() && !Get(card).cursor && !Get(enterMap).keys.contains(card), "Its registrations already released");
    Check(clickHandler(card) && urls.size() == 2, "While cleaning up: the click is consumed, no page");
    Tick();
    Check(!CalendarKraloveCleanupPending() && !Parent(card) && !Parent(block) && !Parent(row) && !state.block.get() &&
        Logged("CALENDAR_KRALOVE_CLEANUP_ERROR") == 1, "Cleanup completed at the next pump");
    Check(!clickHandler(card), "Then no longer ours");
    Clean();
    // Stop: Restore releases everything.
    enableHook(window.owner, nullptr);
    Tick();
    Check(Block() && Filter(), "Placed");
    RestoreCalendarKralove();
    Check(!Block() && !Filter() && !pending.handle && !CalendarKraloveCleanupPending(), "Restored");
    Clean();
    for (const auto& object : objects)
        if (object->klass->name == "TooltipManipulator" || object->klass->name == "ExtendedClickable")
            Check(object->constructed == 1, "Each native registration made once");
    Untouched();
}

void Private() {
    for (const auto& line : logs) {
        Check(line.find("987654321") == std::string::npos && line.find("Zorglub") == std::string::npos && line.find("Corps-Prive") == std::string::npos &&
            line.find("Draconiros") == std::string::npos && line.find("metamob.fr") == std::string::npos, "A log quotes an opening: " + line);
    }
}
}

int main() {
    try {
        Setup();
        clockNow = FakeNow;
        localTime = Paris;
        Module mod({});
        module = &mod;
        mod.ready = mod.servicesReady = true;
        mod.cached.reset(Make("Cache"));
        mod.compositor.reset(Make("Compositor"));
        Helpers();
        Build(Day(2026, 10, 19));
        Install();
        WithoutKey();
        Entry();
        Loads();
        Openings();
        Weeks();
        Toggle();
        OptionOff();
        Layouts();
        mod.cached.reset(Make("Cache"));
        KeyAndCleanup();
        Private();
        mod.cached.reset(); mod.compositor.reset();
        module = nullptr;
        Check(handles.empty(), "No managed reference kept after Restore");
        std::cout << "PASS: Kralamoure calendar: a « Kralamoure » entry of « Mes calendriers » from the game's filter template (icon, "
            "colour, tooltip) whose checkbox is the option both ways; native lines from the game's line and card templates (tinted "
            "banner, icon, « Début : 21h00 », « 12 persos », past ones greyed, extra lines for a crowded day then « +N ouvertures »); "
            "native tooltips (Leave), clickable and hand cursor; event page on click (digits only), Metamob's list for « +N »; "
            "cursor caches holding the mod's own hands only (the clones' self-registered buttons, checkbox and colour square "
            "released at once); cards drawn again only when what they show changes; no move before the first layout; local days "
            "(DST 2026-10-25 and 2027-03-28, « 1er »); week changes and layout followed; state texts; no key: nothing; layout "
            "codes 1-7; a failed option change mirrors the settings; removed on OnDisable / layout change / another session / no "
            "key / option off; retryable cleanup; every native call verified; no private data in logs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fixture::logs) std::cerr << "  log: " << line << '\n';
        return 1;
    }
}
