#include "stats/stats-window.hpp"
#include "stats/stats-format.hpp"
#include "stats/stats.hpp"
#include "ui/tools-ui.hpp"
#include "ui/native-click.hpp"
#include "souls/soul-panel.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// The statistics window (stats-window.cpp) with the real tool menus
// (tools-ui.cpp) and native clicks (native-click.cpp) against a synthetic
// managed tree. No game process, file, network or user setting is involved.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* type; std::string name; int count; };
struct Object {
    Class* type = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, float> lengths;   // inline lengths and floats as written (width, flexGrow…)
    std::map<std::string, int> enums;       // inline enums and the native setters' values (display, set_icon…)
    std::map<std::string, float> resolved;  // sizes after a layout pass; none before
    std::vector<Obj> children;
    std::array<std::byte, 32> value{};
    std::string text, name, tooltip;
    float color[4]{};
    bool active = false, enabled = true;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x57A700000000ULL;
std::vector<std::string> logs;
std::map<uintptr_t, std::string> verified;  // FindVerified: the owner and name checked at each address
Obj root = nullptr, layers = nullptr, hud = nullptr, middle = nullptr, latestWindow = nullptr, capture = nullptr;
Obj dependencies = nullptr, input = nullptr, contextService = nullptr, failOwner = nullptr;
std::string failMethod;
bool dragging = false, failInit = false;
int removedInputs = 0, snapCleanups = 0, buttonUps = 0, closedMenus = 0, windows = 0;
struct Vec2 { float x, y; };
using UpFn = void(*)(Obj, Obj, Vec2, int, Method);
using SubMenuFn = Obj(*)(Obj, Method);
using AllFn = void(*)(Obj, bool, Method);
UpFn clickHook = nullptr;
SubMenuFn subMenuHook = nullptr;
AllFn allHeadersHook = nullptr;
bool (*populate)(Obj, Obj) = nullptr;
bool (*menuCallback)(Obj, Obj) = nullptr;
// The store as the window reads it (stats.cpp is not linked here).
struct StatsFake {
    stats::Totals mine, all;
    int64_t current = 101;
    bool clearWorks = true, lastEvery = false;
    unsigned shown = 0;
    std::vector<bool> clears;
};
StatsFake statsFake;
// The window's clock (UseStatsWindowClock), moved by the tests.
unsigned long long nowMs = 5000000;
unsigned long long Clock() { return nowMs; }

void Check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
Object& Get(Obj value) { Check(value != nullptr, "Null fake object"); return *static_cast<Object*>(value); }
Class* Type(const std::string& name) {
    auto& entry = classes[name];
    if (!entry) entry = std::make_unique<Class>(Class{name});
    return entry.get();
}
Obj Make(const std::string& type) {
    auto value = std::make_unique<Object>(); value->type = Type(type);
    const auto result = value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> Obj Box(const T& value) { const auto box = Make("Box"); std::memcpy(Get(box).value.data(), &value, sizeof(T)); return box; }
Obj String(const std::string& value) { const auto result = Make("String"); Get(result).text = value; return result; }
std::string Str(void* value) { return Get(static_cast<Obj>(value)).text; }
Obj Element(const std::string& type) {
    const auto result = Make(type), style = Make("InlineStyleAccess");
    Get(style).fields["owner"] = result; Get(result).fields["style"] = style;
    return result;
}
void Add(Obj parent, Obj child) {
    if (Get(child).type->name == "MagneticDragWindow") Check(Get(child).enums["initialized"] == 1, "The native window is initialized before it is attached");
    Check(!Get(child).fields["parent"], "An element attached twice");
    Get(parent).children.push_back(child); Get(child).fields["parent"] = parent;
}
void Detach(Obj child) {
    if (const auto parent = Get(child).fields["parent"]) {
        auto& children = Get(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    Get(child).fields["parent"] = nullptr;
}
Obj Panel(Obj element) {
    if (const auto panel = Get(element).fields["panel"]) return panel;
    const auto parent = Get(element).fields["parent"];
    return parent ? Panel(parent) : nullptr;
}
bool Enabled(Obj element) {
    if (!Get(element).enabled) return false;
    const auto parent = Get(element).fields["parent"];
    return !parent || Enabled(parent);
}
// The game's button carries its own ExtendedClickable manipulator.
Obj NativeButton() {
    const auto button = Element("DofusButtonCustom"), clickable = Make("ExtendedClickable");
    Get(clickable).fields["target"] = button; Get(button).fields["m_clickable"] = clickable;
    return button;
}
bool EnumStyle(const std::string& property) {
    return property == "display" || property == "flexDirection" || property == "alignItems" || property == "justifyContent" ||
        property == "flexWrap" || property == "whiteSpace" || property == "unityTextAlign" || property == "position";
}
void DragFlag(bool value, Method) { dragging = value; }
void OriginalButtonUp(Obj, Obj, Vec2, int, Method) { ++buttonUps; }
Obj OriginalSubMenu(Obj, Method) { return nullptr; }
void OriginalAllHeaders(Obj, bool, Method) {}
void Initialize() {
    api.object_get_class = [](Obj value) -> Klass { return Get(value).type; };
    api.class_is_valuetype = [](Klass type) {
        const auto& name = static_cast<Class*>(type)->name;
        return name == "Box" || name == "Length" || name == "Color" || name.rfind("Style", 0) == 0;
    };
    api.object_new = [](Klass type) -> Obj { return Make(static_cast<Class*>(type)->name); };
    api.object_unbox = [](Obj value) -> void* { return Get(value).value.data(); };
    api.class_from_type = [](const void* type) -> Klass { return const_cast<void*>(type); };
    api.method_get_param = [](Method method, uint32_t index) -> const void* {
        const auto& name = static_cast<const MethodInfo*>(method)->name;
        Check(name == "AppendDefaultItem" && (index == 7 || index == 10), "Only the hide row's asset and colour parameters");
        return Type("Box");
    };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj value, bool) -> GCHandle { const auto handle = ++nextHandle; handles[handle] = value; return handle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "A GC handle freed twice"); };
}
Obj Invoke(Method method, Obj self, std::initializer_list<void*> arguments) {
    const auto& m = *static_cast<const MethodInfo*>(method);
    const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size()) == m.count, "Managed argument count: " + m.name);
    if (!self) {
        if (m.name == "AddManipulator") {
            Get(args[0]).fields["manipulator"] = args[1]; Get(args[1]).fields["target"] = args[0]; return nullptr;
        }
        if (m.name == "RemoveManipulator") {
            Check(Get(args[0]).fields["manipulator"] == args[1], "Only the attached manipulator is removed");
            Get(args[0]).fields["manipulator"] = nullptr; Get(args[1]).fields["target"] = nullptr; return nullptr;
        }
        if (m.name == "HasMouseCapture") return Box(capture == args[0]);
        if (m.name == "ReleaseMouse") { Check(capture == args[0], "Only the window's own capture is released"); capture = nullptr; return nullptr; }
        throw std::runtime_error("Unexpected static call " + m.type->name + "." + m.name);
    }
    if (self == failOwner && m.name == failMethod) { failOwner = nullptr; throw std::runtime_error("Injected UI failure"); }
    auto& obj = Get(self);
    const auto& type = obj.type->name;
    // Inline styles: one box per property, read and written back.
    if (type == "InlineStyleAccess") {
        auto& owner = Get(obj.fields.at("owner"));
        const std::string prefix = "UnityEngine.UIElements.IStyle.";
        Check(m.name.rfind(prefix, 0) == 0, "Inline styles through the explicit IStyle methods: " + m.name);
        const auto operation = m.name.substr(prefix.size()), property = operation.substr(4);
        const bool isEnum = EnumStyle(property), isColor = property == "color", isFloat = property == "flexGrow" || property == "flexShrink";
        if (operation.rfind("get_", 0) == 0) {
            const auto box = Make(isColor ? "StyleColor" : isEnum ? "StyleEnum" : isFloat ? "StyleFloat" : "StyleLength");
            if (isEnum) { const int current = owner.enums[property]; std::memcpy(Get(box).value.data(), &current, sizeof current); }
            return box;
        }
        Check(operation.rfind("set_", 0) == 0, "Unexpected style method " + m.name);
        if (isColor) std::memcpy(owner.color, args[0], sizeof owner.color);
        else if (isEnum) owner.enums[property] = *static_cast<int*>(args[0]);
        else owner.lengths[property] = *static_cast<float*>(args[0]);
        return nullptr;
    }
    if (type.rfind("Style", 0) == 0 && m.name == "set_value") {
        const size_t bytes = type == "StyleColor" ? 16 : type == "StyleLength" ? 8 : 4;
        std::memcpy(obj.value.data(), args[0], bytes); return nullptr;
    }
    if (type == "ResolvedStyleAccess") {
        Check(m.name == "get_width" || m.name == "get_height", "Resolved width and height only");
        const auto& sizes = Get(obj.fields.at("owner")).resolved;
        const auto found = sizes.find(m.name.substr(4));
        return Box(found == sizes.end() ? std::numeric_limits<float>::quiet_NaN() : found->second);
    }
    // The tools' ⋯ menu (tools-ui.cpp).
    if (m.name == "get_menu" || m.name == "get_menuItems" || m.name == "get_m_callback" || m.name == "get_itemData" || m.name == "get_menuData")
        return obj.fields.at(m.name.substr(4));
    if (m.name == "get_disabled") return Box(!obj.enabled);
    if (m.name == "AppendTitle" || m.name == "AppendSeparator") {
        const auto row = Make(m.name == "AppendTitle" ? "MenuTitle" : "MenuSeparator");
        if (!args.empty()) Get(row).text = Str(args[0]);
        Get(obj.fields.at("menuItems")).children.push_back(row); return nullptr;
    }
    if (m.name == "AppendDefaultItem") {
        Check(Str(args[0]) == "Masquer le module" && !args[1], "The ⋯ menu's hide row, without a native callback");
        const auto item = Make("MenuItem"), callback = Make("MenuCallback");
        Get(item).text = Str(args[0]); Get(item).tooltip = Str(args[4]); Get(item).fields["m_callback"] = callback;
        Get(obj.fields.at("menuItems")).children.push_back(item); return nullptr;
    }
    if (m.name == "RemoveMenu") { Check(!*static_cast<bool*>(args[0]), "The menu closes at once"); Detach(self); ++closedMenus; return nullptr; }
    if (m.name == "set_headerIconRight") { Check(*static_cast<int*>(args[0]) == 202, "The header's ⋯ (moreActions)"); obj.enums[m.name] = 202; return nullptr; }
    if (m.name == "set_showRightButton") { obj.enums[m.name] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr; }
    // The native window.
    if (m.name == "get_headerWidget") return obj.fields.at("header");
    if (m.name == "get_contentContainer") return obj.fields.at("content");
    if (m.name == "Init") {
        Check(args[0] == dependencies && !Panel(self), "Init with the native dependencies, before the window is attached");
        for (const auto* key : {"ydf", "ydh", "ydj", "ydl"}) {
            const auto event = Get(input).fields.at(key);
            for (const auto* field : {"m_elementDraggers", "m_resizerList"})
                for (const auto target : Get(obj.fields.at(field)).children) {
                    const auto callback = Make("Action");
                    Get(callback).fields["target"] = target; Get(callback).enums["owned"] = 1;
                    Get(Get(event).fields.at("m_input")).children.push_back(callback);
                }
            if (failInit) { failInit = false; throw std::runtime_error("Partial native Init failure"); }
        }
        obj.enums["initialized"] = 1; return nullptr;
    }
    if (m.name == "set_headerTitle") { obj.text = Str(args[0]); return nullptr; }
    if (m.name == "set_headerIcon" || m.name == "set_headerSize" || m.name == "set_dragPosition") { obj.enums[m.name] = *static_cast<int*>(args[0]); return nullptr; }
    if (m.name == "set_headerShowCloseButton" || m.name == "set_verticalResizable" || m.name == "set_horizontalResizable" ||
        m.name == "set_cornerResizable" || m.name == "set_hasBackground" || m.name == "set_isUILocked" || m.name == "SetHeaderSize") {
        obj.enums[m.name] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr;
    }
    if (m.name == "RemoveSnapPreview") { ++snapCleanups; return nullptr; }
    // Elements.
    if (m.name == "get_style") return obj.fields.at("style");
    if (m.name == "get_resolvedStyle") { const auto resolved = Make("ResolvedStyleAccess"); Get(resolved).fields["owner"] = self; return resolved; }
    if (m.name == "Add") { Add(self, args[0]); return nullptr; }
    if (m.name == "RemoveFromHierarchy") { Detach(self); return nullptr; }
    if (m.name == "BringToFront") {
        const auto parent = obj.fields["parent"]; Check(parent != nullptr, "Brought to the front of its own layer");
        Detach(self); Add(parent, self); ++obj.enums["fronts"]; return nullptr;
    }
    if (m.name == "get_parent") return obj.fields["parent"];
    if (m.name == "get_panel") return Panel(self);
    if (m.name == "get_hierarchy") return self;
    if (m.name == "get_childCount" || m.name == "get_Count") return Box(static_cast<int>(obj.children.size()));
    if (m.name == "get_Item") {
        if (type == "Dictionary") { Check(args[0] == obj.fields.at("header"), "The header's dragger, by the native header"); return obj.fields.at("dragger"); }
        return obj.children.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    }
    if (m.name == "get_name") return String(obj.name);
    if (m.name == "set_name") { obj.name = Str(args[0]); return nullptr; }
    if (m.name == "set_text" || m.name == "set_title") { obj.text = Str(args[0]); ++obj.enums["texts"]; return nullptr; }
    if (m.name == "set_tooltip") { obj.tooltip = Str(args[0]); ++obj.enums["tooltips"]; return nullptr; }
    if (m.name == "set_enableRichText") { obj.enums[m.name] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr; }
    if (m.name == "get_enabledInHierarchy") return Box(Enabled(self));
    if (m.name == "set_size" || m.name == "set_fontCase" || m.name == "set_mainStyle" || m.name == "set_icon" || m.name == "set_status" ||
        m.name == "set_componentStyle" || m.name == "set_orientation") { obj.enums[m.name] = *static_cast<int*>(args[0]); return nullptr; }
    if (m.name == "get_isOn") return Box(obj.enums["isOn"] != 0);
    if (m.name == "SetValueWithoutNotify") { obj.enums["isOn"] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr; }
    if (m.name == "set_canBeOpened") { obj.enums[m.name] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr; }
    // SectionHeader.set_isOpen (0x17462F0) keeps the previous state unless canBeOpened is on; both are off after the constructor.
    if (m.name == "set_isOpen") { if (obj.enums["set_canBeOpened"] == 1) obj.enums[m.name] = *static_cast<bool*>(args[0]) ? 1 : 0; return nullptr; }
    if (m.name == "AddSubContent") { Add(obj.fields.at("content"), args[0]); return nullptr; }
    // The session's native dependencies and input events.
    if (m.name == "bfri") return obj.fields.at("cached");
    if (m.name == "bfre") return input;
    if (m.name == "ydf" || m.name == "ydh" || m.name == "ydj" || m.name == "ydl") return obj.fields.at(m.name);
    if (m.name == "get_Target") return obj.fields.at("target");
    if (m.name == "UnSubscribe") {
        auto& callbacks = Get(obj.fields.at("m_input")).children;
        const auto found = std::find(callbacks.begin(), callbacks.end(), args[0]);
        Check(found != callbacks.end() && Get(args[0]).enums["owned"] == 1, "Only the window's own input callbacks are removed");
        callbacks.erase(found); ++removedInputs; return nullptr;
    }
    // The game's click (native-click.cpp).
    if (m.name == "get_active" || m.name == "ContainsPointer") return Box(true);
    if (m.name == "get_target") return obj.fields.at("target");
    throw std::runtime_error("Unexpected instance call " + type + "." + m.name);
}
// The exact contracts the window, the tool menus and the native clicks check.
struct Contract { const char* owner; const char* name; int count; };
const std::map<uintptr_t, Contract> Contracts = {
    {0x1712DC0, {"DofusTab", ".ctor", 0}}, {0x17136F0, {"DofusTab", "set_text", 1}}, {0x1713550, {"DofusTab", "set_size", 1}},
    {0x1712FC0, {"DofusTab", "set_fontCase", 1}}, {0x1712F50, {"DofusTab", "get_isOn", 0}}, {0x1711970, {"DofusTab", "SetValueWithoutNotify", 1}},
    {0x170D6F0, {"DofusButtonCustom", ".ctor", 0}}, {0x170DFE0, {"DofusButtonCustom", "set_icon", 1}}, {0x170E5F0, {"DofusButtonCustom", "set_text", 1}},
    {0x170E210, {"DofusButtonCustom", "set_mainStyle", 1}}, {0x170E330, {"DofusButtonCustom", "set_size", 1}}, {0x170E400, {"DofusButtonCustom", "set_status", 1}},
    {0x1745F00, {"SectionHeader", ".ctor", 5}}, {0x17467B0, {"SectionHeader", "set_title", 1}}, {0x1746200, {"SectionHeader", "set_icon", 1}},
    {0x17462F0, {"SectionHeader", "set_isOpen", 1}}, {0x1746090, {"SectionHeader", "set_canBeOpened", 1}}, {0x1744360, {"SectionHeader", "AddSubContent", 1}},
    {0x170BC10, {"Divider", ".ctor", 0}}, {0x170BDC0, {"Divider", "set_componentStyle", 1}}, {0x170C160, {"Divider", "set_orientation", 1}},
    {0x15BFB50, {"ResizableWindow", "set_headerShowCloseButton", 1}},
    {0x55F50B0, {"VisualElement", ".ctor", 0}}, {0x55DFAD0, {"VisualElement", "Add", 1}}, {0x55E7BD0, {"VisualElement", "RemoveFromHierarchy", 0}},
    {0x55E08E0, {"VisualElement", "BringToFront", 0}}, {0x55F7180, {"VisualElement", "get_parent", 0}}, {0x55F7170, {"VisualElement", "get_panel", 0}},
    {0x55F74A0, {"VisualElement", "get_style", 0}}, {0x55F72E0, {"VisualElement", "get_resolvedStyle", 0}}, {0x55F9670, {"VisualElement", "set_name", 1}},
    {0x55F9AB0, {"VisualElement", "set_tooltip", 1}}, {0x55F5EF0, {"VisualElement", "get_enabledInHierarchy", 0}},
    {0x5729760, {"Label", ".ctor", 0}}, {0x564B780, {"TextElement", "set_text", 1}}, {0x564B1E0, {"TextElement", "set_enableRichText", 1}},
    {0x55DA460, {"ResolvedStyleAccess", "get_width", 0}}, {0x20FF620, {"InputCustomEvent", "UnSubscribe", 1}},
    {0x55D1270, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_display", 0}}, {0x55D37F0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_display", 1}},
    {0x55D13C0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexDirection", 0}}, {0x55D3A80, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexDirection", 1}},
    {0x55D0C60, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_alignItems", 0}}, {0x55D2870, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_alignItems", 1}},
    {0x55D1580, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_justifyContent", 0}}, {0x55D3E00, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_justifyContent", 1}},
    {0x55D1470, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexWrap", 0}}, {0x55D3C50, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexWrap", 1}},
    {0x55D26B0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_whiteSpace", 0}}, {0x55D6370, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_whiteSpace", 1}},
    {0x55D23B0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_unityTextAlign", 0}}, {0x55D5ED0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_unityTextAlign", 1}},
    {0x55D1A30, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_position", 0}}, {0x55D48B0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_position", 1}},
    {0x55D1450, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexGrow", 0}}, {0x55D3B30, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexGrow", 1}},
    {0x55D1460, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexShrink", 0}}, {0x55D3BC0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexShrink", 1}},
    {0x55D1500, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_fontSize", 0}}, {0x55D3D00, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_fontSize", 1}},
    {0x55D11D0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_color", 0}}, {0x55D35D0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_color", 1}},
    {0x55D2740, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_width", 0}}, {0x55D63F0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_width", 1}},
    {0x55D1850, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_minWidth", 0}}, {0x55D4410, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_minWidth", 1}},
    {0x55D1810, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_minHeight", 0}}, {0x55D4370, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_minHeight", 1}},
    {0x55D1970, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingLeft", 0}}, {0x55D46D0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingLeft", 1}},
    {0x55D19B0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingRight", 0}}, {0x55D4770, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingRight", 1}},
    {0x55D19F0, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingTop", 0}}, {0x55D4810, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingTop", 1}},
    {0x55D1930, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingBottom", 0}}, {0x55D4630, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingBottom", 1}},
    // tools-ui.cpp and native-click.cpp.
    {0x5742BD0, {"ScrollView", ".ctor", 0}}, {0x5742BE0, {"ScrollView", "get_contentContainer", 0}},
    {0x16D7F70, {"DofusContextualMenu", "AppendTitle", 1}}, {0x16D76E0, {"DofusContextualMenu", "AppendSeparator", 0}},
    {0x16D6C60, {"DofusContextualMenu", "AppendDefaultItem", 13}}, {0x16D63D0, {"DofusContextualMenu", "AppendCheckboxItem", 9}},
    {0x16D72F0, {"DofusContextualMenu", "AppendRadioButtonItem", 12}},
    {0x16D8850, {"DofusContextualMenu", "InsertItem", 2}}, {0x16D8BF0, {"DofusContextualMenu", "RemoveItemAt", 1}},
    {0x56755F0, {"VisualElementExtensions", "AddManipulator", 2}}, {0x5676A90, {"VisualElementExtensions", "RemoveManipulator", 2}},
    {0x15B27B0, {"MagneticDragWindow", "SetHeaderSize", 1}}, {0x17D9B00, {"WidgetManager", "CreateHeaderSubMenu", 0}},
    {0x17DDDD0, {"WidgetManager", "SetAllHeaderState", 1}},
    {0x752840, {"Clickable", "get_active", 0}}, {0x5590600, {"Clickable", "ContainsPointer", 1}}, {0x30D6960, {"Manipulator", "get_target", 0}},
    {0x172B6A0, {"ExtendedClickable", "ProcessUpEvent", 3}}};
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass klass, const char* name, int count) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, count});
    const auto result = method.get(); fake::methods.push_back(std::move(method)); return result;
}
void* Api::Native(Method method, uintptr_t rva) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    const auto found = fake::Contracts.find(rva);
    fake::Check(found != fake::Contracts.end() && m.name == found->second.name && m.count == found->second.count &&
        m.type->name == found->second.owner, "Native contract mismatch: " + m.type->name + "." + m.name);
    return reinterpret_cast<void*>(rva);
}
Method Api::FindVerified(Klass klass, const char* name, int count, uintptr_t rva) const {
    const auto method = Find(klass, name, count);
    Native(method, rva);
    fake::verified[rva] = static_cast<fake::Class*>(klass)->name + "." + name;
    return method;
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(rva == 0x166DF30, "Only the drag flag's native helper");
    return reinterpret_cast<void*>(&fake::DragFlag);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const { return fake::Invoke(method, self, args); }
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    fake::Check(self != nullptr, std::string("Null receiver: ") + name);
    return Invoke(Find(object_get_class(self), name, static_cast<int>(args.size())), self, args);
}
Obj Api::New(Klass klass, std::initializer_list<void*> arguments) const {
    using namespace fake;
    const auto& name = static_cast<fake::Class*>(klass)->name;
    const std::vector<void*> args(arguments);
    if (name == "Length") {
        Check(args.size() == 2 && *static_cast<int*>(args[1]) == 0, "Lengths in pixels");
        const auto box = Make(name); std::memcpy(Get(box).value.data(), args[0], 4); std::memcpy(Get(box).value.data() + 4, args[1], 4); return box;
    }
    if (name == "Color") {
        Check(args.size() == 4, "A colour's four channels");
        const auto box = Make(name); for (size_t i = 0; i < 4; ++i) std::memcpy(Get(box).value.data() + i * 4, args[i], 4); return box;
    }
    if (name == "DofusContextualMenuManipulator") {
        Check(args.size() == 4 && args[0] == contextService && !args[1] && *static_cast<int*>(args[2]) == 0 && !*static_cast<bool*>(args[3]),
            "The header's native left-click menu, with a null fallback");
        return Make(name);
    }
    if (name == "SectionHeader") {
        const auto flag = [&](size_t i) { return *static_cast<bool*>(args.at(i)); };
        Check(args.size() == 5 && flag(0) && flag(1) && !flag(2) && flag(3) && !flag(4), "The native section header's constructor");
        const auto section = Element(name), content = Element("SectionContent");
        Add(section, content); Get(section).fields["content"] = content; return section;
    }
    if (name == "ScrollView") {
        const auto scroll = Element(name), content = Element("ScrollContent");
        Add(scroll, content); Get(scroll).fields["content"] = content; return scroll;
    }
    if (name == "MagneticDragWindow") {
        Check(args.empty(), "The native window's constructor");
        const auto window = Element(name); latestWindow = window; ++windows; Get(window).name = "native-default";
        const auto header = Element("HeaderWidget"), content = Element("Content"), dragger = Make("ElementDraggerWithBounds");
        const auto rightButton = NativeButton(), closeButton = NativeButton();
        Get(header).fields["rightButton"] = rightButton; Add(header, rightButton); Get(header).fields["closeButton"] = closeButton; Add(header, closeButton);
        Get(window).fields["header"] = header; Get(window).fields["content"] = content;
        Get(dragger).fields["header"] = header; Get(dragger).fields["window"] = window; Add(window, header); Add(window, content);
        const auto dictionary = Make("Dictionary"); Get(dictionary).fields["header"] = header; Get(dictionary).fields["dragger"] = dragger;
        Get(window).fields["m_registeredElementDraggers"] = dictionary;
        const auto draggers = Make("List"), resizers = Make("List");
        Get(draggers).children = {dragger, Make("ElementDraggerWithBounds")}; Get(resizers).children = {Make("ElementResizer"), Make("ElementResizer")};
        Get(window).fields["m_elementDraggers"] = draggers; Get(window).fields["m_resizerList"] = resizers;
        return window;
    }
    Check(args.empty() && (name == "VisualElement" || name == "Label" || name == "DofusTab" || name == "Divider" || name == "DofusButtonCustom"),
        "Only the game's components: " + name);
    return name == "DofusButtonCustom" ? NativeButton() : Element(name);
}
Obj Api::Field(Obj self, const char* name) const { return fake::Get(self).fields.at(name); }
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string{}; }
void* Api::FieldAddress(Obj self, const char* name) const {
    fake::Check(std::string(name) == "m_active", "Only the dragger's m_active");
    return &fake::Get(self).active;
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path) : features(nullptr), ui(nullptr) {}
managed::Obj Module::FindService(const char* name) const {
    if (std::string(name) == "fpz") return fake::contextService;
    fake::Check(std::string(name) == "fpt", std::string("Unexpected service ") + name);
    return fake::dependencies;
}
void Module::Change(Settings value, bool) { settings = std::move(value); }
void Log(const std::string& message) { fake::logs.push_back(message); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    if (rva == 0x172B6A0) {
        fake::Check(m.name == "ProcessUpEvent" && m.count == 3, "The native button click hook");
        fake::clickHook = reinterpret_cast<fake::UpFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalButtonUp); return;
    }
    if (rva == 0x17D9B00) { fake::subMenuHook = reinterpret_cast<fake::SubMenuFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalSubMenu); return; }
    if (rva == 0x17DDDD0) { fake::allHeadersHook = reinterpret_cast<fake::AllFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalAllHeaders); return; }
    throw std::runtime_error("Unexpected hook");
}
namespace soul {
managed::Obj GetUiRoot() { return fake::hud; }
managed::Obj PanelWindow() { return nullptr; }
void SetAuxiliaryPanelMenus(bool (*populate)(managed::Obj, managed::Obj), bool (*callback)(managed::Obj, managed::Obj)) {
    fake::populate = populate; fake::menuCallback = callback;
}
}
namespace stats {
int64_t Current() { return fake::statsFake.current; }
Totals Shown(bool everyCharacter) {
    ++fake::statsFake.shown; fake::statsFake.lastEvery = everyCharacter;
    return everyCharacter ? fake::statsFake.all : fake::statsFake.mine;
}
bool Clear(bool everyCharacter) { fake::statsFake.clears.push_back(everyCharacter); return fake::statsFake.clearWorks; }
std::string KeyName(Table table, int key) {
    static const char* const elements[] = {"neutre", "terre", "feu", "eau", "air"};
    if (table == Table::Elements) return key >= 0 && key < 5 ? elements[key] : "";
    if (table == Table::Spells) return key == 413 ? "Pression" : key == 12 ? "Mot curatif" : "";
    if (table == Table::Dungeons) return key == 5 ? "Donjon des Bouftous" : "";
    return key == 14 ? "Srambad" : key == 1 ? "Monde des Douze" : "";
}
}

namespace fake {
constexpr int64_t Oct23 = 1792756800, Oct1 = 1790856000; // 2026-10-23 and 2026-10-01 at noon UTC: the same day in any time zone
const std::string Nb = " ";
std::string Thin(const std::string& digits) {
    std::string text;
    for (size_t i = 0; i < digits.size(); ++i) { if (i && (digits.size() - i) % 3 == 0) text += " "; text += digits[i]; }
    return text;
}
void Set(stats::Totals& totals, stats::Counter counter, int64_t value) { totals.counters[static_cast<size_t>(counter)] = value; }
// This character (Altansar, 101) and every character's sums, as the store shows them.
StatsFake DefaultStats() {
    using stats::Counter;
    StatsFake data;
    auto& mine = data.mine;
    mine.character = 101; mine.name = "Altansar"; mine.characters = 1; mine.first = Oct23; mine.last = Oct23 + 3600;
    const std::pair<Counter, int64_t> counts[] = {
        {Counter::PlayMs, 43500000}, {Counter::CellsRoleplay, 12345}, {Counter::CellsFight, 2345}, {Counter::Maps, 1234},
        {Counter::Dimensions, 12}, {Counter::Xp, 12345678}, {Counter::Levels, 3}, {Counter::KamasEarned, 1234567},
        {Counter::KamasSpent, 234567}, {Counter::Harvests, 1234}, {Counter::Crafts, 567}, {Counter::Quests, 12},
        {Counter::Achievements, 34}, {Counter::Captures, 3}, {Counter::FightsPve, 123}, {Counter::FightsPvp, 4},
        {Counter::WinsPve, 98}, {Counter::WinsPvp, 1}, {Counter::FightMs, 18720000}, {Counter::FightTurns, 1234},
        {Counter::DamageDealt, 1234567}, {Counter::DamageTaken, 234567}, {Counter::Heals, 123456}, {Counter::Kills, 2345},
        {Counter::Deaths, 12}, {Counter::Dungeons, 12}, {Counter::SpellCasts, 12345}, {Counter::TeleportZaap, 123},
        {Counter::TeleportZaapi, 45}, {Counter::TeleportPrism, 6}, {Counter::TeleportOther, 3}, {Counter::RecallPotions, 7},
        {Counter::HavenBag, 89}, {Counter::ModTrips, 12}};
    for (const auto& [counter, value] : counts) Set(mine, counter, value);
    mine.records = {3456, 10800000};
    mine.tables[static_cast<size_t>(stats::Table::Spells)] = {{413, 1234}, {4567, 987}, {12, 455}};
    mine.tables[static_cast<size_t>(stats::Table::Elements)] = {{1, 450}, {2, 300}, {4, 200}, {0, 42}, {3, 8}};
    mine.tables[static_cast<size_t>(stats::Table::Dungeons)] = {{5, 5}, {7, 2}};
    mine.tables[static_cast<size_t>(stats::Table::Dimensions)] = {{14, 8}, {1, 6}};
    auto& all = data.all;
    all.characters = 4; all.first = Oct1; all.last = Oct23 + 3600;
    Set(all, Counter::PlayMs, 4442400000); // 1 234 h
    Set(all, Counter::KamasEarned, 1000); Set(all, Counter::KamasSpent, 2000);
    return data;
}

// The window's parts, found in the synthetic tree.
Obj FindType(Obj from, const std::string& type, const std::string& text) {
    if (Get(from).type->name == type && Get(from).text == text) return from;
    for (const auto child : Get(from).children) if (const auto found = FindType(child, type, text)) return found;
    return nullptr;
}
void Collect(Obj from, const std::string& type, std::vector<Obj>& found) {
    if (Get(from).type->name == type) found.push_back(from);
    for (const auto child : Get(from).children) Collect(child, type, found);
}
std::vector<Obj> All(Obj from, const std::string& type) { std::vector<Obj> found; Collect(from, type, found); return found; }
bool Displayed(Obj element) { const auto& enums = Get(element).enums; const auto found = enums.find("display"); return found == enums.end() || found->second != 1; }
bool Open() { return latestWindow && Get(latestWindow).fields["parent"] == middle && Panel(latestWindow) != nullptr; }
// The scroll view's content, where the window lays out its parts.
Obj Body() { return Get(Get(Get(latestWindow).fields.at("content")).children.at(0)).fields.at("content"); }
Obj Header() { return Get(latestWindow).fields.at("header"); }
Obj CloseButton() { return Get(Header()).fields.at("closeButton"); }
Obj ResetButton() { const auto buttons = All(Body(), "DofusButtonCustom"); Check(buttons.size() == 1, "One button in the window's body: the reset"); return buttons[0]; }
std::vector<Obj> Tabs() { return All(Body(), "DofusTab"); }
Obj Section(const std::string& title) { const auto section = FindType(Body(), "SectionHeader", title); Check(section != nullptr, "Section " + title); return section; }
std::vector<Obj> LinesOf(Obj section) { return Get(Get(Get(section).fields.at("content")).children.at(0)).children; }
std::string Who() { return Get(Get(Body()).children.at(2)).text; }
std::string Footer() { return Get(Get(Get(ResetButton()).fields.at("parent")).children.at(0)).text; }
// The columns' box and, in a column, the titles of its sections.
Obj ColumnsBox() { return Get(Get(Section("Progression")).fields.at("parent")).fields.at("parent"); }
std::vector<std::string> Column(size_t index) {
    std::vector<std::string> titles;
    for (const auto section : Get(Get(ColumnsBox()).children.at(index)).children) titles.push_back(Get(section).text);
    return titles;
}
// What a section shows: each line « label | value », hidden ones left out.
std::vector<std::string> Shown(Obj section) {
    std::vector<std::string> shown;
    for (const auto line : LinesOf(section))
        if (Displayed(line)) shown.push_back(Get(Get(line).children.at(0)).text + " | " + Get(Get(line).children.at(1)).text);
    return shown;
}
std::vector<std::string> Expected(stats::Part part, const stats::Totals& totals) {
    std::vector<std::string> shown;
    for (const auto& line : stats::Lines(part, totals, stats::KeyName)) if (line.shown) shown.push_back(line.label + " | " + line.value);
    return shown;
}
std::string Value(const std::string& section, const std::string& label) {
    for (const auto line : LinesOf(Section(section)))
        if (Get(Get(line).children.at(0)).text == label) return Displayed(line) ? Get(Get(line).children.at(1)).text : "(hidden)";
    throw std::runtime_error("No line " + label);
}
// The game's click on a native button (ExtendedClickable.ProcessUpEvent).
void Click(Obj button) {
    Check(button != nullptr && clickHook != nullptr, "A native button and the click hook");
    clickHook(Get(button).fields.at("m_clickable"), Make("PointerUpEvent"), Vec2{}, 0, nullptr);
}
void Tick() { TickStatsWindow(); }
void Wait(unsigned long long ms) { nowMs += ms; }
// The window's ⋯ menu, as the game opens it from the header's button; a row
// is clicked through the shared native callback router.
struct Menu { Obj menu = nullptr, root = nullptr; std::vector<Obj> rows; };
Menu OpenMenu() {
    Menu m;
    m.menu = Make("Menu"); m.root = Element("MenuRoot");
    Get(m.menu).fields["menuItems"] = Make("List"); Get(m.menu).fields["dofusContextualMenuRoot"] = m.root;
    Get(m.root).fields["menuData"] = m.menu; Add(root, m.root);
    const auto event = Make("PopulateEvent"); Get(event).fields["menu"] = m.menu;
    Check(populate(Get(Get(Header()).fields.at("rightButton")).fields.at("manipulator"), event), "The ⋯ menu belongs to the tools");
    m.rows = Get(Get(m.menu).fields.at("menuItems")).children;
    return m;
}
bool Pick(const Menu& m, size_t row) {
    const auto view = Element("MenuView"); Get(view).fields["itemData"] = m.rows.at(row); Add(m.root, view);
    return menuCallback(Get(m.rows.at(row)).fields.at("m_callback"), view);
}
bool InputsReleased() {
    for (const auto* key : {"ydf", "ydh", "ydj", "ydl"})
        if (Get(Get(Get(input).fields.at(key)).fields.at("m_input")).children.size() != 1) return false;
    return true;
}
bool Shortcut() {
    const auto shortcuts = toolsUi::Shortcuts();
    return std::any_of(shortcuts.begin(), shortcuts.end(), [](const toolsUi::Shortcut& s) { return s.key == "stats" && s.title == "Statistiques" && s.icon == 77; });
}

// Installed: every native part checked against the pinned client; the
// top-bar button follows « Bouton dans la barre du haut »; nothing is built
// or read until the window is asked for.
void InstallAndTopBar() {
    for (const auto& [rva, contract] : Contracts) {
        const std::string owner = contract.owner;
        if (owner == "ScrollView" || owner == "DofusContextualMenu" || owner == "VisualElementExtensions" || owner == "WidgetManager" ||
            owner == "Clickable" || owner == "Manipulator" || owner == "ExtendedClickable" || rva == 0x15B27B0 || rva == 0x55F5EF0) continue;
        Check(verified.count(rva) && verified.at(rva) == owner + "." + contract.name, "The window checks " + owner + "." + contract.name);
    }
    Check(Shortcut() && !toolsUi::Visible("stats"), "The top-bar button « Statistiques » with the chart icon, the window closed");
    module->settings.statsButton = false;
    Check(!Shortcut(), "« Bouton dans la barre du haut » unticked: no top-bar button");
    module->settings.statsButton = true;
    Check(Shortcut(), "Ticked again: the button is back");
    for (int i = 0; i < 3; ++i) { Wait(1500); Tick(); }
    Check(!latestWindow && statsFake.shown == 0, "Nothing is built nor read until the window is asked for");
}
// Opened from the top bar: the game's window, tabs, sections in three
// columns of label / value lines, and the reset at the bottom.
void OpensAsANativeWindow() {
    Check(toolsUi::Toggle("stats") && toolsUi::Visible("stats"), "The top-bar button asks for the window");
    Tick();
    Check(Open() && windows == 1 && statsFake.shown == 1 && !statsFake.lastEvery, "One native window in the game's window layer, read once for this character");
    const auto& window = Get(latestWindow);
    Check(window.text == "Statistiques" && window.enums.at("set_headerIcon") == 77 && window.enums.at("set_headerSize") == 1 &&
        window.enums.at("set_headerShowCloseButton") == 1 && window.enums.at("set_hasBackground") == 1, "Its header: « Statistiques », the chart icon, the close button");
    Check(window.enums.at("set_verticalResizable") == 1 && window.enums.at("set_horizontalResizable") == 1 && window.enums.at("set_cornerResizable") == 1,
        "Resizable like the game's windows");
    Check(window.lengths.at("width") == 900 && window.lengths.at("minWidth") == 560 && window.lengths.at("minHeight") == 360 && window.enums.at("position") == 1,
        "As wide as three columns, never under two");
    Check(Get(latestWindow).enums.at("set_headerIconRight") == 202 && Get(Get(Header()).fields.at("rightButton")).fields["manipulator"] != nullptr,
        "The header's ⋯ opens the tools' menu");
    Check(window.enums.at("SetHeaderSize") == 0, "« Réduire les titres des modules » off: the full header");
    Check(std::find(logs.begin(), logs.end(), "STATS_WINDOW_OPEN native=true") != logs.end(), "Logged once open");
    // What is shown: the game's tabs, this character first.
    const auto tabs = Tabs();
    Check(tabs.size() == 2 && Get(tabs[0]).text == "Ce personnage" && Get(tabs[1]).text == "Tous mes personnages" && Get(tabs[0]).enums.at("isOn") == 1 &&
        Get(tabs[1]).enums.at("isOn") == 0 && Get(tabs[0]).enums.at("set_size") == 1 && Get(tabs[0]).enums.at("set_fontCase") == 1,
        "« Ce personnage » and « Tous mes personnages » as the game's capitalised tabs, this character shown");
    Check(Get(tabs[0]).tooltip == "Les statistiques du personnage connecté dans ce jeu." &&
        Get(tabs[1]).tooltip == "Les statistiques de tous tes personnages comptés sur ce PC, additionnées ; les records gardent la plus grande valeur.",
        "Each tab says what it shows on its own; the every-character one that it adds up");
    Check(All(Body(), "Divider").size() == 2 && Get(Get(Body()).children.at(1)).type->name == "Divider", "The game's dividers under the tabs and above the reset");
    Check(Who() == "Altansar, compté depuis le 23 octobre 2026" && Get(Get(Body()).children.at(2)).enums.at("set_enableRichText") == 0,
        "Whose statistics, since when (never rich text)");
    // The sections: the game's section headers, three columns in reading order.
    const char* titles[] = {"Temps et déplacements", "Progression", "Kamas", "Récolte et artisanat", "Combats", "Sorts et éléments", "Téléportations"};
    const int icons[] = {172, 134, 180, 179, 103, 284, 336};
    Check(All(Body(), "SectionHeader").size() == 7, "Seven native sections");
    for (size_t index = 0; index < stats::PartCount; ++index) {
        const auto section = Section(titles[index]);
        const auto part = static_cast<stats::Part>(index);
        Check(Get(section).enums.at("set_icon") == icons[index] && Get(section).enums.at("set_isOpen") == 1 && Get(section).enums.at("set_canBeOpened") == 0,
            std::string("Opened then locked, with its icon: ") + titles[index]);
        Check(LinesOf(section).size() == stats::PartSize(part) && Shown(section) == Expected(part, statsFake.mine),
            std::string("Every figure of the store, as stats-format writes it: ") + titles[index]);
        const auto lines = stats::Lines(part, statsFake.mine, stats::KeyName);
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto line = LinesOf(section)[i];
            if (!lines[i].shown) continue;
            const auto& label = Get(Get(line).children.at(0));
            const auto& value = Get(Get(line).children.at(1));
            Check(Get(line).tooltip == lines[i].help && Get(line).enums.at("flexDirection") == 2 && Get(line).enums.at("flexWrap") == 1 &&
                Get(line).enums.at("justifyContent") == 2 && label.lengths.at("flexGrow") == 1 && value.enums.at("unityTextAlign") == 5 &&
                label.enums.at("set_enableRichText") == 0 && value.enums.at("set_enableRichText") == 0,
                "A line: its label on the left, its value on the right (on its own line when too long), its hover text: " + lines[i].label);
        }
    }
    Check(Column(0) == std::vector<std::string>({"Temps et déplacements", "Progression", "Kamas"}) &&
        Column(1) == std::vector<std::string>({"Récolte et artisanat", "Combats"}) &&
        Column(2) == std::vector<std::string>({"Sorts et éléments", "Téléportations"}) && Displayed(Get(ColumnsBox()).children.at(2)),
        "Three columns from the window's own width");
    Check(Value("Temps et déplacements", "Temps de jeu") == "12" + Nb + "h" + Nb + "05" + Nb + "min" &&
        Value("Sorts et éléments", "Sort n° 1") == "Pression (" + Thin("1234") + " fois)" && Value("Sorts et éléments", "Sort n° 4") == "(hidden)" &&
        Value("Combats", "Donjon le plus fait") == "Donjon des Bouftous (5 fois)" && Value("Kamas", "Solde") == "+" + Thin("1000000") + Nb + "kamas",
        "Spot checks: the figures, the names, a rank not cast yet hidden");
    // The reset, at the bottom.
    const auto& reset = Get(ResetButton());
    Check(reset.text == "Réinitialiser" && reset.enums.at("set_icon") == 249 && reset.enums.at("set_mainStyle") == 1 &&
        reset.tooltip.find("Confirmer dans les 10 secondes") != std::string::npos && Footer() == "Réinitialiser les statistiques de ce personnage",
        "Réinitialiser, with the game's reset icon, says what it would erase");
}
// The figures follow the store once a second while the window is open, each
// text written only when it changes.
void FollowsTheStoreOnceASecond() {
    const auto reads = statsFake.shown;
    const auto xpValue = Get(LinesOf(Section("Progression"))[0]).children.at(1);
    const auto xpWrites = Get(xpValue).enums.at("texts");
    Set(statsFake.mine, stats::Counter::PlayMs, 43560000);
    Tick();
    Wait(999); Tick();
    Check(statsFake.shown == reads && Value("Temps et déplacements", "Temps de jeu") == "12" + Nb + "h" + Nb + "05" + Nb + "min", "Not before the second is over");
    Wait(1); Tick();
    Check(statsFake.shown == reads + 1 && Value("Temps et déplacements", "Temps de jeu") == "12" + Nb + "h" + Nb + "06" + Nb + "min" &&
        Get(xpValue).enums.at("texts") == xpWrites, "A second later the play time follows; an unchanged figure is not written again");
    statsFake.mine.tables[static_cast<size_t>(stats::Table::Spells)].push_back({7, 3});
    Wait(1000); Tick();
    Check(Value("Sorts et éléments", "Sort n° 4") == "sort 7 (3 fois)", "A fourth spell shows in its place");
    statsFake.mine.tables[static_cast<size_t>(stats::Table::Spells)].pop_back();
    Wait(1000); Tick();
    Check(Value("Sorts et éléments", "Sort n° 4") == "(hidden)", "And hides again");
}
// « Tous mes personnages »: every character's sums at once; the tab shown
// stays selected; the choice stays while the window closes and opens again.
void ScopeTabs() {
    auto tabs = Tabs();
    const auto reads = statsFake.shown;
    Get(tabs[1]).enums["isOn"] = 1;
    Tick();
    Check(statsFake.shown == reads + 1 && statsFake.lastEvery && Get(tabs[0]).enums.at("isOn") == 0 && Get(tabs[1]).enums.at("isOn") == 1 &&
        Who() == "4 personnages, comptés depuis le 1er octobre 2026" && Footer() == "Réinitialiser les statistiques de tous mes personnages" &&
        Value("Temps et déplacements", "Temps de jeu") == Thin("1234") + Nb + "h" + Nb + "00" + Nb + "min" &&
        Value("Sorts et éléments", "Dégâts par élément") == "aucun dégât compté", "Every character's sums at once");
    Get(tabs[1]).enums["isOn"] = 0;
    Tick();
    Check(Get(tabs[1]).enums.at("isOn") == 1 && Get(tabs[0]).enums.at("isOn") == 0 && statsFake.lastEvery, "A click on the tab shown keeps it selected");
    Click(CloseButton()); Tick();
    Check(!Open(), "Closed with every character shown");
    module->settings.smallHeaderStats = true;
    OpenStatsWindow(); Tick();
    tabs = Tabs();
    Check(Open() && Get(tabs[1]).enums.at("isOn") == 1 && Who() == "4 personnages, comptés depuis le 1er octobre 2026", "Opened again: still every character");
    Check(Get(latestWindow).enums.at("SetHeaderSize") == 1, "« Réduire les titres des modules » on: the reduced header at once");
    module->settings.smallHeaderStats = false;
    Get(tabs[0]).enums["isOn"] = 1;
    Tick();
    Check(!statsFake.lastEvery && Get(tabs[1]).enums.at("isOn") == 0 && Who() == "Altansar, compté depuis le 23 octobre 2026", "Back to this character");
}
// The columns follow the window's width: two under 780 px of their box,
// three from it; nothing moves before the first layout.
void ColumnsFollowTheWidth() {
    auto& box = Get(ColumnsBox());
    box.resolved["width"] = 700;
    Tick();
    Check(Column(0) == std::vector<std::string>({"Temps et déplacements", "Progression", "Kamas", "Récolte et artisanat", "Téléportations"}) &&
        Column(1) == std::vector<std::string>({"Combats", "Sorts et éléments"}) && Column(2).empty() && !Displayed(box.children.at(2)) &&
        Get(box.children.at(1)).lengths.at("paddingRight") == 0, "Two columns: the short sections, then the fights and the spells");
    box.resolved["width"] = 0;
    Tick();
    Check(Column(1).size() == 2, "No width (before a layout): the columns stay");
    box.resolved["width"] = 900;
    Tick();
    Check(Column(0).size() == 3 && Column(1) == std::vector<std::string>({"Récolte et artisanat", "Combats"}) && Column(2).size() == 2 &&
        Displayed(box.children.at(2)) && Get(box.children.at(1)).lengths.at("paddingRight") == 14, "Three again when wider");
    // A small screen: the window opens narrower, in two columns at once.
    Click(CloseButton()); Tick();
    Get(middle).resolved["width"] = 640;
    OpenStatsWindow(); Tick();
    Check(Open() && Get(latestWindow).lengths.at("width") == 608 && Column(1) == std::vector<std::string>({"Combats", "Sorts et éléments"}),
        "A narrow screen: a narrower window, two columns from the start");
    Get(middle).resolved["width"] = 400;
    Click(CloseButton()); Tick(); OpenStatsWindow(); Tick();
    Check(Get(latestWindow).lengths.at("width") == 560, "Never narrower than two columns");
    Get(middle).resolved["width"] = 1280;
    Click(CloseButton()); Tick(); OpenStatsWindow(); Tick();
    Check(Open() && Column(2).size() == 2, "A wide screen: three columns");
}
// « Réinitialiser » asks first; Confirmer within 10 s erases what was shown
// then, for the same character; a click within the first second is the same
// gesture; an outcome stays 6 s; another choice drops the question.
void ResetAsksFirst() {
    const auto question = "Effacer les statistiques d’Altansar ? Clique sur Confirmer dans les 10 secondes.";
    const auto caption = [] { return Get(ResetButton()).text; };
    const auto icon = [] { return Get(ResetButton()).enums.at("set_icon"); };
    Click(ResetButton());
    Check(Footer() == question && caption() == "Confirmer" && icon() == 316 && Get(ResetButton()).enums.at("set_status") == 1 && statsFake.clears.empty(),
        "The first click asks, naming the character; Confirmer with the trash, negative");
    Click(ResetButton()); Wait(999); Click(ResetButton());
    Check(Footer() == question && statsFake.clears.empty(), "Clicks within the second after the question erase nothing");
    Wait(1);
    const auto reads = statsFake.shown;
    Click(ResetButton());
    Check(statsFake.clears == std::vector<bool>{false} && Footer() == "Statistiques d’Altansar effacées." && caption() == "Réinitialiser" &&
        icon() == 249 && Get(ResetButton()).enums.at("set_status") == 0 && statsFake.shown == reads + 1, "Confirmer erases this character's, says so and shows at once");
    Wait(5999); Tick();
    Check(Footer() == "Statistiques d’Altansar effacées.", "The outcome stays six seconds");
    Wait(1); Tick();
    Check(Footer() == "Réinitialiser les statistiques de ce personnage", "Then goes");
    Click(ResetButton()); Wait(10000); Tick();
    Check(Footer() == "Réinitialiser les statistiques de ce personnage" && caption() == "Réinitialiser" && statsFake.clears.size() == 1,
        "Unanswered for ten seconds: the question goes, nothing erased");
    Click(ResetButton());
    Check(Footer() == question, "After it, a click asks again");
    Get(Tabs()[1]).enums["isOn"] = 1; Tick();
    Check(Footer() == "Réinitialiser les statistiques de tous mes personnages" && caption() == "Réinitialiser", "Another choice drops the question");
    Click(ResetButton());
    Check(Footer() == "Effacer les statistiques de tous tes personnages ? Clique sur Confirmer dans les 10 secondes.", "Every character: asked");
    Wait(1000); Click(ResetButton());
    Check(statsFake.clears == std::vector<bool>({false, true}) && Footer() == "Statistiques de tous tes personnages effacées.", "Every character erased");
    statsFake.clearWorks = false;
    Click(ResetButton()); Wait(1000); Click(ResetButton());
    Check(statsFake.clears.size() == 3 && Footer() == "Effacement impossible pour l’instant : réessaie dans un moment.", "A refused reset says so");
    statsFake.clearWorks = true;
    Get(Tabs()[0]).enums["isOn"] = 1; Tick();
    Click(ResetButton()); statsFake.current = 202; Wait(1000); Click(ResetButton());
    Check(statsFake.clears.size() == 3 && Footer() == "Effacement impossible pour l’instant : réessaie dans un moment.", "Never another character than the one named");
    statsFake.current = 101;
    // A question goes with the window.
    Wait(6000); Tick();
    Click(ResetButton());
    Click(CloseButton()); Tick(); OpenStatsWindow(); Tick();
    Check(Footer() == "Réinitialiser les statistiques de ce personnage" && Get(ResetButton()).text == "Réinitialiser", "Closing the window drops the question");
    // A stopping module ignores the window's buttons.
    module->stopRequested = true;
    Click(ResetButton());
    module->stopRequested = false;
    Check(Footer() == "Réinitialiser les statistiques de ce personnage" && statsFake.clears.size() == 3, "A stopping module ignores the reset");
}
// Closed by its close button (the game's own handling still runs), its ⋯
// menu and its top-bar button; opened or brought to the front by the options'
// button. Each close lets go of everything the window held.
void OpensAndClosesEachWay() {
    Check(Open(), "Open before closing");
    const auto window = latestWindow, rightButton = Get(Header()).fields.at("rightButton");
    const auto ups = buttonUps, snaps = snapCleanups;
    Click(CloseButton());
    Check(!toolsUi::Visible("stats") && buttonUps == ups + 1, "The close button: the game's handling ran, the window is no longer wanted");
    Tick();
    Check(!Get(window).fields["parent"] && InputsReleased() && !Get(rightButton).fields["manipulator"] && snapCleanups == snaps + 1 &&
        Get(window).enums.at("set_isUILocked") == 1 && Get(window).enums.at("set_dragPosition") == 0 && !StatsWindowCleanupPending(),
        "Closed: off the layer, its input callbacks, header menu and drag released");
    // The ⋯ menu: the game's title and « Masquer le module », whose help names
    // the top-bar button, or the options once « Bouton dans la barre du haut »
    // is unticked.
    OpenStatsWindow(); Tick();
    auto menu = OpenMenu();
    Check(menu.rows.size() == 3 && Get(menu.rows[0]).text == "Statistiques" && Get(menu.rows[2]).text == "Masquer le module", "The ⋯ menu: the title and the hide row");
    Check(Get(menu.rows[2]).tooltip == "Réactiver ce panneau avec son bouton à côté de Capture d’âme, en haut à droite.", "The hide row's help: the top-bar button");
    module->settings.statsButton = false;
    menu = OpenMenu();
    Check(menu.rows.size() == 3 && Get(menu.rows[2]).tooltip == "Réactiver ce panneau depuis les options du mod.",
        "Without its top-bar button, the help sends to the options");
    module->settings.statsButton = true;
    Check(Pick(menu, 2) && !toolsUi::Visible("stats") && !Panel(menu.root), "« Masquer le module » closes the menu and the window");
    Tick();
    Check(!Open(), "Hidden from its menu");
    // The top bar opens and closes it.
    Check(toolsUi::Toggle("stats"), "Top bar"); Tick();
    Check(Open(), "Opened from the top bar");
    Check(toolsUi::Toggle("stats") && !toolsUi::Visible("stats"), "Top bar again"); Tick();
    Check(!Open(), "Closed from the top bar");
    // The options' button: opens, then brings to the front.
    OpenStatsWindow(); Tick();
    const auto opened = latestWindow;
    const auto other = Element("OtherWindow"); Add(middle, other);
    Check(Get(middle).children.back() == other, "Another window above");
    OpenStatsWindow();
    Check(Get(middle).children.back() == opened && Get(opened).enums.at("fronts") == 1 && latestWindow == opened, "The options' button brings it to the front");
    Tick();
    Check(latestWindow == opened && Open(), "The same window, not a new one");
    Detach(other);
    // A logout (Module::Pump) or Stop while the window is dragged: let go at
    // once, the drag and the mouse capture released, no longer wanted.
    const auto dragger = Get(Get(opened).fields.at("m_registeredElementDraggers")).fields.at("dragger");
    capture = Header(); dragging = true; Get(dragger).active = true;
    RestoreStatsWindow();
    Check(!Open() && !toolsUi::Visible("stats") && !capture && !dragging && !Get(dragger).active && InputsReleased() && !StatsWindowCleanupPending(),
        "Logout or Stop: the window, its drag and capture released");
    Tick();
    Check(!Open() && latestWindow == opened, "Not rebuilt afterwards");
}
// Teardown failures are retried; a session change retires the window; a
// partial creation leaks nothing; nothing is read while closed.
void TeardownAndStaleCallbacks() {
    OpenStatsWindow(); Tick();
    const auto window = latestWindow, oldReset = ResetButton(), oldClose = CloseButton();
    failOwner = window; failMethod = "RemoveFromHierarchy";
    RestoreStatsWindow();
    Check(StatsWindowCleanupPending() && std::any_of(logs.begin(), logs.end(), [](const std::string& line) { return line.rfind("STATS_WINDOW_CLEANUP_ERROR", 0) == 0; }),
        "A failed detach stays pending and is logged");
    Tick();
    Check(!StatsWindowCleanupPending() && !Get(window).fields["parent"] && InputsReleased(), "Retried at the next pump");
    // Stale buttons of a closed window do nothing.
    const auto clears = statsFake.clears.size();
    const auto ups = buttonUps;
    Click(oldReset); Click(oldClose);
    Check(statsFake.clears.size() == clears && buttonUps == ups + 2 && !Open() && !toolsUi::Visible("stats"), "A closed window's buttons are no longer the mod's");
    // A session change (another settings owner) retires the window.
    OpenStatsWindow(); Tick();
    const auto cached = module->cached.get();
    module->cached.reset(Make("OtherCache"));
    Tick();
    Check(!Open() && !toolsUi::Visible("stats"), "Another session: the window is retired");
    module->cached.reset(cached);
    // A creation that fails midway (the native Init) leaves nothing behind.
    failInit = true;
    OpenStatsWindow(); Tick();
    Check(!Open() && !toolsUi::Visible("stats") && InputsReleased() && !StatsWindowCleanupPending() &&
        std::any_of(logs.begin(), logs.end(), [](const std::string& line) { return line.rfind("STATS_WINDOW_CREATE_ERROR", 0) == 0; }),
        "A partial creation is undone and logged; the window is not asked for any more");
    // Closed, nothing is read.
    const auto reads = statsFake.shown;
    for (int i = 0; i < 5; ++i) { Wait(2000); Tick(); }
    Check(statsFake.shown == reads, "Nothing is read while the window is closed");
    // A stopping module neither opens it nor keeps it.
    module->stopRequested = true;
    OpenStatsWindow(); Tick();
    Check(!Open(), "A stopping module opens nothing");
    module->stopRequested = false;
    // A foreign native button is left alone.
    const auto foreignUps = buttonUps;
    Click(NativeButton());
    Check(buttonUps == foreignUps + 1, "A foreign button's click is the game's alone");
}
}

int main() {
    using namespace fake;
    try {
        Initialize();
        Module owner{std::filesystem::path{}};
        module = &owner;
        owner.ready = owner.servicesReady = true;
        owner.cached.reset(Make("Cache")); owner.compositor.reset(Make("Compositor"));
        contextService = Make("ContextService"); dependencies = Make("goc"); input = Make("eqo");
        Get(dependencies).fields["cached"] = owner.cached.get();
        for (const auto* key : {"ydf", "ydh", "ydj", "ydl"}) {
            const auto event = Make("InputCustomEvent"), list = Make("List"), foreign = Make("Action");
            Get(foreign).fields["target"] = Make("ForeignDragger"); Get(list).children = {foreign};
            Get(event).fields["m_input"] = list; Get(input).fields[key] = event;
        }
        root = Element("Document"); Get(root).fields["panel"] = Make("Panel");
        layers = Element("Layers"); hud = Element("Root"); middle = Element("Root");
        Get(hud).name = "RootLow"; Get(middle).name = "RootMedium";
        Add(root, layers); Add(layers, hud); Add(layers, middle);
        Get(middle).resolved = {{"width", 1280.f}, {"height", 720.f}};
        statsFake = DefaultStats();
        nativeClick::Install(); toolsUi::Install(); InstallStatsWindow(); UseStatsWindowClock(Clock);
        InstallAndTopBar();
        OpensAsANativeWindow();
        FollowsTheStoreOnceASecond();
        ScopeTabs();
        ColumnsFollowTheWidth();
        ResetAsksFirst();
        OpensAndClosesEachWay();
        TeardownAndStaleCallbacks();
        RestoreStatsWindow(); toolsUi::Restore(); UseStatsWindowClock(nullptr);
        owner.cached.reset(); owner.compositor.reset(); module = nullptr;
        Check(handles.empty(), "No GC handle leaked");
        std::cout << "PASS: statistics window (the game's window with its header, ⋯ menu, close button and resizers; this character or every character in native tabs; seven native sections in two or three columns by the window's width, label / value lines with their hover texts, following the store once a second only while open; Réinitialiser asking first, Confirmer within 10 s for the same character; opened and closed from the top bar, its close button, its menu and the options' button; teardown at logout and stop, retried, stale callbacks ignored)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fake::logs) std::cerr << line << '\n';
        return 1;
    }
}
