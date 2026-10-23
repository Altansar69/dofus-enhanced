#include "appearance/display-shortcut.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// The real shortcut implementation is linked against this in-memory runtime.
// No game DLL, process, window, startup helper or registry is accessed.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Value {
    float pixels = 0;
    int value = 0;
    int keyword = 0;
    std::array<float, 4> color{};
    bool operator==(const Value&) const = default;
};
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> refs;
    std::map<std::string, int> integers;
    std::map<std::string, bool> booleans;
    std::vector<Obj> children;
    std::vector<std::pair<int, std::string>> choices;
    std::string text;
    Value value;
    std::array<float, 4> bounds{0, 0, 43, 32};
    int integer = 0;
    bool boolean = false;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<GCHandle, Obj> handles;
std::unordered_map<void*, Obj> unboxed;
GCHandle nextHandle = 0xABCDEF120000ULL;
using NoArgFn = void(*)(Obj, Method);
using EventFn = void(*)(Obj, Obj, Method);
NoArgFn enableHook = nullptr, clickHook = nullptr, showHook = nullptr;
EventFn contextHook = nullptr, refreshHook = nullptr;
unsigned originalEnables = 0, originalContexts = 0, originalRefreshes = 0, originalClicks = 0;
unsigned changes = 0, dropdownsCreated = 0, originalShows = 0;
bool combat = false;
std::vector<std::string> logs;
std::vector<std::pair<Obj, std::string>> events;
Obj failOwner = nullptr;
Obj persistentHub = nullptr;
unsigned hubLookups = 0;
std::string failMethod;
bool failAfterMutation = false;
bool themeAvailable = true;
Obj themeOwner = nullptr;
std::array<float, 4> themeColor{.10f,.11f,.12f,.97f};

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Object& Get(Obj object) {
    if (!object) throw std::runtime_error("Fake shortcut API received null object");
    return *static_cast<Object*>(object);
}
Class* Type(const std::string& name) {
    auto& entry = classes[name];
    if (!entry) entry = std::make_unique<Class>(Class{name});
    return entry.get();
}
Obj Make(const std::string& name) {
    auto object = std::make_unique<Object>();
    object->klass = Type(name);
    const auto result = object.get();
    objects.push_back(std::move(object));
    return result;
}
Obj String(const std::string& text) { auto result = Make("String"); Get(result).text = text; return result; }
Obj Box(int value) { auto result = Make("Int32"); Get(result).integer = value; return result; }
Obj Box(float value) { auto result = Make("Single"); Get(result).value.pixels = value; return result; }
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
Obj Box(std::array<float, 4> value) { auto result = Make("Color"); Get(result).value.color = value; return result; }
Obj StyleValue(const char* type, Value value) {
    const auto result = Make(type);
    Get(result).value = value;
    unboxed[&Get(result).value] = result;
    return result;
}
Obj CloneValue(Obj value) { return StyleValue(Get(value).klass->name.c_str(), Get(value).value); }
Obj Element(const char* type) {
    const auto result = Make(type);
    const auto style = Make("InlineStyleAccess");
    Get(style).refs["element"] = result;
    Get(result).refs["style"] = style;
    for (const char* field : {"width", "minWidth", "maxWidth", "left", "top", "right", "bottom", "height", "minHeight", "maxHeight", "marginLeft", "marginRight", "marginTop", "marginBottom", "paddingLeft", "paddingRight", "paddingTop", "paddingBottom", "fontSize"})
        Get(style).refs[field] = StyleValue("StyleLength", {0.0f, 0, 2});
    for (const char* field : {"visibility", "display", "position", "flexDirection", "flexWrap", "alignItems", "justifyContent", "overflow"})
        Get(style).refs[field] = StyleValue("StyleEnum", {0.0f, 0, 2});
    for (const char* field : {"flexShrink", "flexGrow", "opacity"})
        Get(style).refs[field] = StyleValue("StyleFloat", {0.0f, 0, 2});
    Get(style).refs["backgroundColor"] = StyleValue("StyleColor", {0.0f, 0, 2});
    return result;
}
void Add(Obj parent, Obj child) {
    auto& children = Get(parent).children;
    Check(std::find(children.begin(), children.end(), child) == children.end(), "Duplicate hierarchy insertion");
    children.push_back(child);
    Get(child).refs["parent"] = parent;
}
void Detach(Obj child) {
    auto& value = Get(child);
    const auto parent = value.refs["parent"];
    if (parent) {
        auto& children = Get(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    value.refs["parent"] = nullptr;
}
Obj PanelOf(Obj element) {
    for (auto current = element; current; current = Get(current).refs["parent"])
        if (const auto panel = Get(current).refs["panel"]) return panel;
    return nullptr;
}
void NativeEnable(Obj, Method) { ++originalEnables; }
void NativeContext(Obj, Obj, Method) { ++originalContexts; }
void NativeRefresh(Obj, Obj, Method) { ++originalRefreshes; }
void NativeClick(Obj, Method) { ++originalClicks; }
void NativeShow(Obj, Method);

Obj Resolve(Obj object) {
    const auto found = unboxed.find(object);
    return found == unboxed.end() ? object : found->second;
}
void InitializeApi() {
    api.object_get_class = [](Obj object) -> Klass { return Get(Resolve(object)).klass; };
    api.class_is_valuetype = [](Klass klass) {
        const auto& name = static_cast<Class*>(klass)->name;
        return name == "StyleLength" || name == "StyleEnum" || name == "StyleFloat" || name == "StyleColor" || name == "Length" || name == "Hierarchy" ||
            name == "Int32" || name == "Boolean" || name == "Color" || name == "ResolvedStyleAccess";
    };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.klass->name == "Int32") return &value.integer;
        if (value.klass->name == "Boolean") return &value.boolean;
        if (value.klass->name == "Color") return &value.value.color;
        unboxed[&value.value] = object;
        return &value.value;
    };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        const auto handle = ++nextHandle;
        handles.emplace(handle, object);
        return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        const auto found = handles.find(handle);
        Check(found != handles.end(), "Unknown or truncated shortcut GC handle");
        return found->second;
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Shortcut GC handle freed twice"); };
}
using StyleSnapshot = std::map<std::pair<Obj, std::string>, Value>;
void SaveStyles(StyleSnapshot& snapshot, Obj element) {
    for (const auto& [property, value] : Get(Get(element).refs.at("style")).refs)
        if (property != "element") snapshot[{element, property}] = Get(value).value;
}
void CheckStyles(const StyleSnapshot& snapshot) {
    for (const auto& [key, saved] : snapshot) {
        const auto style = Get(key.first).refs.at("style");
        Check(Get(Get(style).refs.at(key.second)).value == saved, "Native inline style value/keyword was not restored exactly");
    }
}
Value& Style(Obj element, const char* property) {
    return Get(Get(Get(element).refs.at("style")).refs.at(property)).value;
}
void NativeShow(Obj dropdown, Method) {
    ++originalShows;
    const auto root=Get(dropdown).refs["parent"];
    if(!root) return;
    auto panel=Get(root).refs["parent"];
    while(panel&&Get(panel).refs["parent"])panel=Get(panel).refs["parent"];
    const auto popup=Get(dropdown).refs["m_listElementContent"];
    if(!panel||!popup)return;
    Get(dropdown).refs["m_panelRootVisualContainer"]=panel;
    Style(popup,"left")={Get(root).bounds[0]-Get(panel).bounds[0],0,0};
    Style(popup,"top")={Get(root).bounds[1]+Get(root).bounds[3]-Get(panel).bounds[1],0,0};
    Style(popup,"width")={Style(root,"width").pixels,0,0};
}
Obj NewOwner(Obj cached, Obj root) {
    const auto owner = Make("MapInfoUI"), binding = Make("MapInfoBinding");
    Get(owner).refs["cached"] = cached;
    Get(owner).refs["dzyi"] = binding;
    Get(binding).refs["creature"] = root;
    const auto group = Get(root).refs["parent"];
    const auto toolbar = group ? Get(group).refs["parent"] : nullptr;
    const auto wrapper = toolbar ? Get(toolbar).refs["parent"] : nullptr;
    Get(binding).refs["group"] = group;
    Get(binding).refs["toolbar"] = toolbar;
    Get(binding).refs["screen"] = wrapper ? Get(wrapper).refs["parent"] : nullptr;
    return owner;
}
struct Fixture {
    Module state{L"offline-shortcut-test"};
    Obj parent, toolbar, wrapper, screen, viewport, before, root, after, icon, highlight, owner, panel;
    StyleSnapshot originalStyles;
    Fixture() {
        RestoreDisplayShortcut();
        module = &state;
        originalEnables = originalContexts = originalRefreshes = originalClicks = 0;
        changes = dropdownsCreated = originalShows = 0;
        combat = false;
        events.clear();
        logs.clear();
        failOwner = nullptr;
        failMethod.clear();
        failAfterMutation = false;
        persistentHub = nullptr;
        hubLookups = 0;
        themeAvailable = true;
        themeColor = {.10f,.11f,.12f,.97f};
        state.servicesReady = true;
        state.ready.store(true);
        state.settings.mode = 1;
        state.settings.roleplayMode = 2;
        state.settings.ApplyLegacyAnimationMode(3);
        state.cached.reset(Make("CachedOptions"));
        viewport = Element("PanelRoot");
        themeOwner = viewport; // Theme custom declarations belong to :root, not every descendant.
        Style(viewport,"width")={800,0,0};Style(viewport,"height")={600,0,0};Get(viewport).bounds={0,0,800,600};
        screen = Element("MapInfoScreenContainer");
        Style(screen,"width")={500,0,0};Style(screen,"height")={110,0,0};
        wrapper = Element("NativeToolbarWrapper");
        toolbar = Element("ctr_buttons");
        parent = Element("ctr_buttonsToHide");
        Add(viewport, screen);Add(screen, wrapper);Add(wrapper, toolbar);Add(toolbar, parent);
        Style(toolbar,"flexDirection")={0,2,0};Style(parent,"flexDirection")={0,2,0};
        before = Element("NativeNeighbour");
        root = Element("DofusToggleButtonCustom");
        after = Element("NativeNeighbour");
        Add(parent, before);
        Add(parent, root);
        Add(parent, after);
        icon = Element("IconContainer");
        highlight = Element("NativeHighlight");
        Add(root, icon);
        Add(root, highlight);
        Add(icon, Element("NativeCreatureIcon"));
        Style(root, "width") = {43.0f, 0, 0};
        Style(root, "minWidth") = {40.0f, 0, 2};
        Style(root, "maxWidth") = {65.0f, 0, 3};
        Style(icon, "visibility") = {0.0f, 0, 1};
        Style(highlight, "visibility") = {0.0f, 1, 0};
        SaveStyles(originalStyles, parent);
        SaveStyles(originalStyles, toolbar);
        SaveStyles(originalStyles, wrapper);
        SaveStyles(originalStyles, screen);
        SaveStyles(originalStyles, viewport);
        SaveStyles(originalStyles, root);
        SaveStyles(originalStyles, icon);
        SaveStyles(originalStyles, highlight);
        owner = NewOwner(state.cached.get(), root);
        panel = Make("Panel");
        Get(parent).refs["panel"] = panel;
        InstallDisplayShortcut();
    }
    ~Fixture() { RestoreDisplayShortcut(); module = nullptr; }
    Obj Dropdown() const {
        for (const auto child : Get(root).children)
            if (Get(child).klass->name == "Dropdown") return child;
        return nullptr;
    }
    Obj Attach() {
        enableHook(owner, nullptr);
        TickDisplayShortcut();
        const auto dropdown = Dropdown();
        if (!dropdown && !logs.empty()) std::cerr << logs.back() << '\n';
        Check(dropdown != nullptr, "Captured shortcut failed to attach");
        return dropdown;
    }
    void CheckNativeTree() const {
        Check(Get(parent).children == std::vector<Obj>({before, root, after}), "Native root placement or siblings changed");
        Check(Get(root).children == std::vector<Obj>({icon, highlight}), "Native icon/highlight hierarchy was not restored");
        Check(Get(icon).children.size() == 1 && Get(Get(icon).children.front()).klass->name == "NativeCreatureIcon",
            "Native icon descendants changed");
    }
};
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc});
    const auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
void* Api::Native(Method method, uintptr_t rva) const {
    const auto& info = *static_cast<const fake::MethodInfo*>(method);
    const auto& name = info.name;
    fake::Check(rva != 0 && !name.empty(), "Native shortcut method must be pinned");
    if (name=="bfsu") fake::Check(info.klass->name=="ghz" && info.argc==2 && rva==0x1FBBC70,
        "Persistent UI lookup must match the saved native contract");
    return reinterpret_cast<void*>(rva);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    fake::Check(std::string(name)=="TryGetValue" && argc==2 && rva==0x55FCCB0, "Theme lookup must use the Color overload");
    auto method=Find(klass,name,argc); Native(method,rva); return method;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    return Call(fake::Resolve(self), static_cast<const fake::MethodInfo*>(method)->name.c_str(), args);
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fake;
    const auto& name = static_cast<fake::Class*>(klass)->name;
    if (name == "Dropdown") {
        Check(args.size() == 0, "Wrong native Dropdown constructor arity");
        ++dropdownsCreated;
        const auto result = Element("Dropdown");
        Get(result).integers["selectedIndex"] = -1;
        for(const char* field:{"m_dropdown","m_content","captionIconContainer","m_listElementContent","m_captionLabel","m_choiceLabel","m_chevronIcon"}) Get(result).refs[field]=Element("NativeDropdownChild");
        Add(result, Get(result).refs.at("m_dropdown"));
        for(const char* field:{"m_captionLabel","m_choiceLabel","m_chevronIcon"})Get(Get(result).refs.at(field)).refs["parent"]=Element("NativeDropdownContainer");
        const auto popup=Get(result).refs.at("m_listElementContent");Style(popup,"height")={90,0,0};
        return result;
    }
    if (name == "Length") {
        Check(args.size() == 2, "Wrong native Length constructor arity");
        const auto* arguments = args.begin();
        return StyleValue("Length", {*static_cast<float*>(arguments[0]), *static_cast<int*>(arguments[1]), 0});
    }
    throw std::runtime_error("Unexpected shortcut construction: " + name);
}
Obj Api::Call(Obj self, const char* methodName, std::initializer_list<void*> args) const {
    using namespace fake;
    self = Resolve(self);
    auto& value = Get(self);
    const std::string name = methodName;
    const auto* arguments = args.begin();
    const bool fail = self == failOwner && name == failMethod;
    const bool after = fail && failAfterMutation;
    if (fail) {
        failOwner = nullptr;
        failMethod.clear();
        if (!after) throw std::runtime_error("Synthetic shortcut operation failure");
    }
    const auto done = [&](Obj result = nullptr) -> Obj {
        if (after) throw std::runtime_error("Synthetic shortcut operation failure after mutation");
        return result;
    };
    if (name=="bfsu") {
        Check(value.klass->name=="ghz" && args.size()==2 && *static_cast<int*>(arguments[0])==4,
            "Only the existing MapInfo window may be requested");
        ++hubLookups;
        const auto result=value.refs["mapInfo"];
        *static_cast<Obj*>(arguments[1])=result;
        return done(Box(result!=nullptr));
    }
    if (name == "blrn") return done(value.refs.at("cached"));
    if (name == "get_tg_creature") return done(value.refs.at("creature"));
    if (name == "get_ctr_buttonsToHide") return done(value.refs.at("group"));
    if (name == "get_ctr_buttons") return done(value.refs.at("toolbar"));
    if (name == "get_MapInfoScreenContainer") return done(value.refs.at("screen"));
    if (name == "get_style") return done(value.refs.at("style"));
    if (name == "get_customStyle") {
        const auto custom = Make("CustomStyleAccess");
        Get(custom).refs["element"] = self;
        return done(custom);
    }
    if (name == "TryGetValue") {
        Check(value.klass->name=="CustomStyleAccess" && Get(*static_cast<Obj*>(arguments[0])).text=="--background_dark97",
            "Closed face must use the native open-state theme variable");
        const bool available = themeAvailable && value.refs.at("element") == themeOwner;
        if (available) *static_cast<std::array<float,4>*>(arguments[1])=themeColor;
        return done(Box(available));
    }
    if (name == "set_hasShadow") { value.booleans["shadow"] = *static_cast<bool*>(arguments[0]); return done(); }
    if (name == "get_panel") return done(PanelOf(self));
    if (name == "get_parent") return done(value.refs["parent"]);
    if (name == "get_resolvedStyle") {
        const auto resolved = Make("ResolvedStyleAccess");
        Get(resolved).refs["element"] = self;
        return done(resolved);
    }
    // Saved Unity metadata exposes a public getter on the boxed concrete type,
    // not an explicit-interface method with the fully qualified interface name.
    if (value.klass->name == "ResolvedStyleAccess" && name == "get_display")
        return done(Box(Style(value.refs.at("element"), "display").value));
    if (value.klass->name == "ResolvedStyleAccess" && (name=="get_width"||name=="get_height")) {
        const auto element=value.refs.at("element");const auto prop=name.substr(4);const auto& style=Style(element,prop.c_str());
        return done(Box(style.keyword==0?style.pixels:Get(element).bounds[prop=="width"?2:3]));
    }
    if(name=="get_worldBound"){auto rect=Make("Rect");Get(rect).bounds=value.bounds;Get(rect).bounds[2]=Style(self,"width").keyword==0?Style(self,"width").pixels:value.bounds[2];return done(rect);}
    if(value.klass->name=="Rect"){
        const std::map<std::string,size_t> indices={{"get_x",0},{"get_y",1},{"get_width",2},{"get_height",3}};
        return done(Box(value.bounds.at(indices.at(name))));
    }
    if(value.klass->name=="StyleLength"&&name=="get_value")return done(StyleValue("Length",value.value));
    if(value.klass->name=="StyleEnum"&&name=="get_value")return done(Box(value.value.value));
    if(value.klass->name=="StyleFloat"&&name=="get_value")return done(Box(value.value.pixels));
    if(value.klass->name=="StyleColor"&&name=="get_value")return done(Box(value.value.color));
    if((value.klass->name=="StyleLength"||value.klass->name=="StyleEnum"||value.klass->name=="StyleFloat"||value.klass->name=="StyleColor")&&name=="get_keyword")return done(Box(value.value.keyword));
    if(value.klass->name=="Length"&&name=="get_unit")return done(Box(value.value.value));
    if(value.klass->name=="Length"&&name=="get_value")return done(Box(value.value.pixels));
    if (name == "get_enabledInHierarchy") return done(Box(!value.booleans["disabled"]));
    const std::string styleGet = "UnityEngine.UIElements.IStyle.get_";
    const std::string styleSet = "UnityEngine.UIElements.IStyle.set_";
    if (name.starts_with(styleGet)) return done(CloneValue(value.refs.at(name.substr(styleGet.size()))));
    if (name.starts_with(styleSet)) {
        const auto source = unboxed.at(arguments[0]);
        value.refs[name.substr(styleSet.size())] = CloneValue(source);
        events.emplace_back(value.refs.at("element"), name);
        return done();
    }
    if (name == "set_value") {
        if (value.klass->name == "StyleLength") value.value = Get(unboxed.at(arguments[0])).value;
        else if (value.klass->name == "StyleEnum") value.value = {0, *static_cast<int*>(arguments[0]), 0};
        else if (value.klass->name == "StyleFloat") value.value = {*static_cast<float*>(arguments[0]), 0, 0};
        else if (value.klass->name == "StyleColor") { value.value.color=*static_cast<std::array<float,4>*>(arguments[0]); value.value.keyword=0; }
        else throw std::runtime_error("Setting unexpected boxed shortcut style");
        return done();
    }
    if (name == "get_hierarchy") {
        const auto hierarchy = Make("Hierarchy");
        Get(hierarchy).refs["element"] = self;
        return done(hierarchy);
    }
    if (name == "get_childCount") return done(Box(static_cast<int>(Get(value.refs.at("element")).children.size())));
    if (name == "get_Item") return done(Get(value.refs.at("element")).children.at(*static_cast<int*>(arguments[0])));
    if (name == "Add") {
        Add(value.refs.at("element"), arguments[0]);
        events.emplace_back(arguments[0], "add");
        return done();
    }
    if (name == "set_name" || name == "set_captionText" || name == "set_choiceText" || name == "set_tooltip") { value.refs[name.substr(4)] = arguments[0]; return done(); }
    if (name == "set_dropdownSize") { value.integers["dropdownSize"] = *static_cast<int*>(arguments[0]); return done(); }
    if (name == "set_dropdownType") { value.integers["dropdownType"] = *static_cast<int*>(arguments[0]); return done(); }
    if (name == "AddListElementBasic") {
        Check(args.size() == 6 && arguments[1] == nullptr && *static_cast<int*>(arguments[2]) == 0 &&
            *static_cast<int*>(arguments[3]) == 0 && *static_cast<int*>(arguments[4]) == 0,
            "Native shortcut choices must use basic rows without synthetic callbacks");
        value.choices.emplace_back(*static_cast<int*>(arguments[5]), Get(arguments[0]).text);
        return done();
    }
    if (name == "SelectItemWithoutNotify") {
        value.integers["selectedIndex"] = *static_cast<int*>(arguments[0]);
        value.refs["choiceText"]=fake::String(value.choices.at(value.integers["selectedIndex"]).second);
        ++value.integers["syncCalls"];
        return done();
    }
    if (name == "SetValueWithoutNotify") { value.boolean = *static_cast<bool*>(arguments[0]); return done(); }
    if (name == "set_activated") {
        const bool changed=value.booleans["activated"] != *static_cast<bool*>(arguments[0]);
        value.booleans["activated"] = *static_cast<bool*>(arguments[0]);
        if(changed&&value.booleans["activated"])showHook(self,nullptr);
        events.emplace_back(self, value.booleans["activated"] ? "open" : "close");
        return done();
    }
    if (name == "RemoveFromHierarchy") {
        Check(!value.booleans["activated"], "Shortcut popup must close before its anchor is detached");
        Detach(self);
        events.emplace_back(self, "remove");
        return done();
    }
    throw std::runtime_error("Unmodelled shortcut call: " + value.klass->name + "." + name);
}
Obj Api::Field(Obj self, const char* name) const {
    const auto& refs = fake::Get(fake::Resolve(self)).refs;
    const auto found = refs.find(name);
    return found == refs.end() ? nullptr : found->second;
}
void* Api::FieldAddress(Obj self, const char* fieldName) const {
    auto& value = fake::Get(fake::Resolve(self));
    const std::string name = fieldName;
    if (name == "dzyi") return value.refs.at("dzyi");
    if (name == "selectedIndex") return &value.integers[name];
    if (name == "m_activated") return &value.booleans["activated"];
    if (name == "hasMouseWheel" || name == "closeOnSelection") return &value.booleans[name];
    throw std::runtime_error("Unexpected shortcut field address: " + name);
}
std::string Api::String(Obj self) const { return self ? fake::Get(fake::Resolve(self)).text : std::string{}; }
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return fake::combat; }
void Module::Change(Settings next, bool) { ++fake::changes; settings = next; }
managed::Obj Module::FindService(const char* name) const {
    fake::Check(std::string(name)=="fqw","Shortcut must query only the persistent UI hub");
    return fake::persistentHub;
}
void Log(const std::string& message) { fake::logs.push_back(message); }
void AddHook(managed::Method, uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x1726AE0) {
        fake::showHook=reinterpret_cast<fake::NoArgFn>(replacement);*original=reinterpret_cast<void*>(fake::NativeShow);
    } else if (rva == 0x2C63890) {
        fake::enableHook = reinterpret_cast<fake::NoArgFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeEnable);
    } else if (rva == 0x2C63540) {
        fake::contextHook = reinterpret_cast<fake::EventFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeContext);
    } else if (rva == 0x2C65410) {
        fake::refreshHook = reinterpret_cast<fake::EventFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeRefresh);
    } else if (rva == 0x2C63270) {
        fake::clickHook = reinterpret_cast<fake::NoArgFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeClick);
    } else throw std::runtime_error("Unexpected shortcut hook address");
}

namespace fake {
void FaceThemeReadsAncestorDeclaration() {
    Fixture f;
    enableHook(f.owner,nullptr); TickDisplayShortcut(1);
    const auto face=Get(f.Dropdown()).refs.at("m_dropdown");
    Check(Style(face,"backgroundColor").keyword==0 && Style(face,"backgroundColor").color==themeColor,
        "Theme defined only on panel root must darken the closed selector");
}
void FaceThemeWaitsForStylesAndTracksThemeChanges() {
    Fixture f;
    themeAvailable=false;
    enableHook(f.owner,nullptr); TickDisplayShortcut(1);
    const auto dropdown=f.Dropdown(); Check(dropdown!=nullptr,"Theme not ready must not remove selector");
    const auto face=Get(dropdown).refs.at("m_dropdown");
    const std::array<float,4> defaultDark{27.0f/255,29.0f/255,50.0f/255,.97f};
    Check(Style(face,"backgroundColor").keyword==0 && Style(face,"backgroundColor").color==defaultDark,
        "Unresolved theme must use native dark fallback, never retain bright closed fill");
    themeAvailable=true; TickDisplayShortcut(501);
    Check(Style(face,"backgroundColor").color==themeColor && !Get(face).booleans.at("shadow"),
        "Closed face must use dark open-state color without highlighted inner shadow");
    Check(Style(face,"opacity").keyword==2,"Caption and caret must retain native opacity");
    themeColor={.2f,.21f,.22f,.97f}; TickDisplayShortcut(1001);
    Check(Style(face,"backgroundColor").color==themeColor,"Changing theme must update the owned face");
    Check(Style(Get(dropdown).refs.at("m_listElementContent"),"backgroundColor").keyword==2 &&
        Style(f.before,"backgroundColor").keyword==2 && Style(f.root,"backgroundColor").keyword==2,
        "Face theme must not modify native popup, neighbours or root");
    clickHook(f.owner,nullptr); TickDisplayShortcut(1002);
    Check(Get(dropdown).booleans["activated"] && changes==0,"Theme adaptation must preserve open/selection behavior");
    // An unchanged theme must still repair a later native reset of the inline fill.
    Style(face,"backgroundColor").keyword=2;
    TickDisplayShortcut(1501);
    Check(Style(face,"backgroundColor").keyword==0 && Style(face,"backgroundColor").color==themeColor,
        "Native style reset must not restore bright face when cached theme is unchanged");
    const auto eventCount=events.size(); TickDisplayShortcut(2001);
    Check(std::none_of(events.begin()+eventCount,events.end(),[&](const auto& e){
        return e.first==face && e.second=="UnityEngine.UIElements.IStyle.set_backgroundColor";
    }),"Stable face must not cause repeated style invalidation");
}
void FaceThemeTracksScopeAndRejectsInvalidColor() {
    Fixture f;
    enableHook(f.owner,nullptr); TickDisplayShortcut(1);
    const auto face=Get(f.Dropdown()).refs.at("m_dropdown");
    // A theme scope can change without rebuilding this map toolbar.
    themeOwner=f.wrapper; themeColor={.13f,.14f,.15f,.97f}; TickDisplayShortcut(501);
    Check(Style(face,"backgroundColor").color==themeColor,"Nearest current theme scope must be read again");
    themeColor[0]=std::numeric_limits<float>::quiet_NaN(); TickDisplayShortcut(1001);
    Check(Style(face,"backgroundColor").color==std::array<float,4>{27.0f/255,29.0f/255,50.0f/255,.97f},
        "Invalid theme values must use the dark fallback");
    themeColor={.12f,.11f,.10f,.97f}; TickDisplayShortcut(1501);
    Check(Style(face,"backgroundColor").color==themeColor,"Valid theme must recover after an invalid value");
}
void AlreadyOpenToolbarNeedsNoClick() {
    Fixture f;
    persistentHub=Make("ghz"); Get(persistentHub).refs["mapInfo"]=f.owner;
    TickDisplayShortcut(1);
    const auto dropdown=f.Dropdown();
    Check(dropdown && dropdownsCreated==1 && changes==0 && originalClicks==0 && originalEnables==0,
        "An already-open toolbar must get its selector without a click or lifecycle event");
    Check(!Get(dropdown).booleans["activated"] && Style(f.icon,"visibility").value==1,
        "Initial discovery must show the collapsed combobox and hide the native creature icon");
    for (uint64_t now=2; now<1000; now+=25) TickDisplayShortcut(now);
    Check(hubLookups==1 && dropdownsCreated==1 && f.Dropdown()==dropdown,
        "Attached selector must not keep searching or create duplicates");
}
void DiscoveryWaitsForServiceAndAttachment() {
    Fixture f;
    f.state.servicesReady=false;
    TickDisplayShortcut(1);
    Check(hubLookups==0 && !f.Dropdown(),"Discovery must wait for current services");
    f.state.servicesReady=true;
    persistentHub=Make("ghz");
    TickDisplayShortcut(2);
    Get(persistentHub).refs["mapInfo"]=f.owner;
    Get(f.parent).refs["panel"]=nullptr;
    TickDisplayShortcut(100);
    Check(hubLookups==1 && !f.Dropdown(),"Missing UI lookup must be throttled");
    TickDisplayShortcut(252);
    Check(hubLookups==2 && !f.Dropdown(),"Known window must wait for its panel without opening it");
    Get(f.parent).refs["panel"]=f.panel;
    TickDisplayShortcut(253);
    Check(f.Dropdown() && originalEnables==0 && originalClicks==0 && changes==0,
        "Window attaching later must receive its selector without another event");
}
void DiscoveryIsolationAndReplacement() {
    Fixture f;
    persistentHub=Make("OtherService");
    TickDisplayShortcut(1);
    Check(hubLookups==0 && !f.Dropdown(),"Unexpected service implementation must not be queried");
    persistentHub=Make("ghz"); Get(persistentHub).refs["mapInfo"]=Make("OtherWindow");
    TickDisplayShortcut(251);
    Check(!f.Dropdown(),"Unrelated window must not be treated as MapInfoUI");
    Get(persistentHub).refs["mapInfo"]=NewOwner(Make("OtherAccount"),f.root);
    TickDisplayShortcut(501);
    Check(!f.Dropdown(),"Discovered owner must belong to the current session");
    Get(persistentHub).refs["mapInfo"]=f.owner;
    TickDisplayShortcut(751);
    auto oldDropdown=f.Dropdown();
    Check(oldDropdown!=nullptr,"Valid owner not recovered after transient unavailable data");
    Detach(oldDropdown);
    TickDisplayShortcut(1001);
    Check(f.Dropdown() && f.Dropdown()!=oldDropdown && dropdownsCreated==2,
        "A removed combobox must be restored automatically on the existing toolbar");
    oldDropdown=f.Dropdown();
    const auto replacement=Element("DofusToggleButtonCustom");
    const auto icon=Element("ReplacementNativeIcon");
    Add(f.parent,replacement); Add(replacement,icon);
    const auto replacementOwner=NewOwner(f.state.cached.get(),replacement);
    Get(persistentHub).refs["mapInfo"]=replacementOwner;
    Get(oldDropdown).integers["selectedIndex"]=0;
    Detach(f.root);
    TickDisplayShortcut(1251);
    Check(dropdownsCreated==3 && Get(oldDropdown).refs["parent"]==nullptr && changes==0 &&
        Get(replacement).children.size()==2 && Style(icon,"visibility").value==1,
        "A recreated toolbar must get a combobox and discard the detached toolbar's queued choice");
    const auto queries=hubLookups;
    f.state.stopped=true;
    TickDisplayShortcut(2001);
    Check(hubLookups==queries && changes==0,"Stopped module must not discover or mutate UI");
}
void UnattachedPendingOwnerCanBeReplaced() {
    Fixture f;
    persistentHub=Make("ghz"); Get(persistentHub).refs["mapInfo"]=f.owner;
    Detach(f.root);
    TickDisplayShortcut(1);
    Check(dropdownsCreated==0,"Detached pending window must not be modified");
    const auto replacement=Element("DofusToggleButtonCustom");
    Add(f.parent,replacement); Add(replacement,Element("ReplacementIcon"));
    Get(persistentHub).refs["mapInfo"]=NewOwner(f.state.cached.get(),replacement);
    TickDisplayShortcut(251);
    Check(dropdownsCreated==1 && Get(replacement).children.size()==2 && changes==0 && originalClicks==0,
        "A stale unattached pending owner must not prevent discovering the replacement toolbar");
}
void NativeAppearanceAndRestoration() {
    Fixture f;
    const auto dropdown = f.Attach();
    Check(originalEnables == 1 && changes == 0 && dropdownsCreated == 1, "Capture must preserve native lifecycle without changing settings");
    Check(Get(f.parent).children == std::vector<Obj>({f.before, f.root, f.after}), "Shortcut moved the native button or its siblings");
    Check(Get(f.root).children == std::vector<Obj>({f.icon, f.highlight, dropdown}), "Dropdown must overlay the preserved native children");
    Check(Style(f.icon, "visibility").value == 1 && Style(f.highlight, "visibility").value == 1,
        "Original icon/highlight children must be hidden while retaining their layout");
    Check(Style(f.root, "display").value == 0 && Style(f.root, "visibility").value == 0 &&
        Style(f.parent, "display").value == 0, "Replacement must not hide or remove the native root or toolbar");
    Check(Style(f.root,"width").pixels==128.0f&&Style(f.root,"minWidth").pixels==128.0f&&Style(f.root,"maxWidth").pixels==128.0f&&Style(f.root,"flexShrink").pixels==0,
        "Selector must reserve a readable nonshrinking cell in native layout");
    for(auto group:{f.parent,f.toolbar})
        Check(Style(group,"flexWrap").value==1&&Style(group,"flexShrink").pixels==1&&Style(group,"maxWidth").pixels==100&&Style(group,"maxWidth").value==1&&Style(group,"minWidth").pixels==0,
            "Both native rows must reflow within their parent's width");
    Check(Style(f.wrapper,"maxWidth").pixels==100&&Style(f.wrapper,"maxWidth").value==1&&Style(f.wrapper,"flexShrink").pixels==0,
        "Intrinsic toolbar wrapper must be constrained and retain the reflowed row height");
    Check(Style(dropdown,"width").pixels==100&&Style(dropdown,"width").value==1&&Style(dropdown,"minWidth").pixels==0&&Style(dropdown,"overflow").value==1,
        "Owned selector must be percentage-bounded and clipped to its reserved cell");
    Check(Get(Get(dropdown).refs.at("choiceText")).text=="Miniatures"&&Get(Get(dropdown).refs.at("tooltip")).text=="Affichage : Miniatures",
        "Current mode must remain fully readable without needing a tooltip");
    Check(Get(dropdown).integers["dropdownSize"]==0&&Style(Get(dropdown).refs.at("m_dropdown"),"paddingLeft").pixels==8&&Style(Get(dropdown).refs.at("m_dropdown"),"paddingRight").pixels==10&&Style(Get(dropdown).refs.at("m_chevronIcon"),"width").pixels==16&&Style(Get(dropdown).refs.at("m_choiceLabel"),"fontSize").pixels==14,"Native selector must reserve legible text, spacing and caret");
    Check(Style(dropdown, "position").value == 1, "Dropdown must use absolute overlay positioning");
    Check(Get(dropdown).choices == std::vector<std::pair<int, std::string>>({{0, "Normal"}, {1, "Créatures"}, {2, "Miniatures"}}),
        "Shortcut must expose the three native text choices with stable IDs");
    Check(Get(dropdown).integers["dropdownType"] == 0 && !Get(dropdown).booleans["hasMouseWheel"] &&
        Get(dropdown).booleans["closeOnSelection"] && Get(dropdown).integers["selectedIndex"] == 2,
        "Dropdown configuration or initial context selection is wrong");
    clickHook(f.owner, nullptr);
    TickDisplayShortcut();
    Check(Get(dropdown).booleans["activated"] && originalClicks == 0, "Click must open the selector without dispatching the native toggle bus");
    events.clear();
    f.state.stopped.store(true);
    RestoreDisplayShortcut();
    const auto close = std::find(events.begin(), events.end(), std::make_pair(dropdown, std::string("close")));
    const auto remove = std::find(events.begin(), events.end(), std::make_pair(dropdown, std::string("remove")));
    Check(close != events.end() && remove != events.end() && close < remove, "Stop must close its popup before removing the dropdown");
    Check(!Get(dropdown).refs["parent"] && !Get(dropdown).booleans["activated"], "Stop left its dropdown or popup attached");
    f.CheckNativeTree();
    CheckStyles(f.originalStyles);
}

void BoundedLayoutResizeAndReopen() {
    Fixture f;auto dropdown=f.Attach();
    auto popup=Get(dropdown).refs.at("m_listElementContent");
    Get(f.root).bounds={760,560,43,32};
    clickHook(f.owner,nullptr);TickDisplayShortcut();
    Check(Get(dropdown).booleans["activated"]&&Style(popup,"width").pixels==160&&Style(popup,"left").pixels==640&&Style(popup,"top").pixels==510,
        "Readable popup must fit right/bottom bounds independently of toolbar anchor");
    Style(f.root,"width")={120,0,0};TickDisplayShortcut();
    Check(!Get(dropdown).booleans["activated"]&&Get(Get(dropdown).refs.at("choiceText")).text=="Miniatures",
        "Resize must close stale popup and retain the full caption");
    Style(f.root,"width")={132,0,0};Style(f.viewport,"width")={90,0,0};Style(f.viewport,"height")={60,0,0};Get(f.root).bounds={70,40,132,20};
    TickDisplayShortcut();clickHook(f.owner,nullptr);TickDisplayShortcut();
    Check(Style(popup,"width").pixels==90&&Style(popup,"left").pixels==0&&Style(popup,"top").pixels==0&&Style(popup,"maxHeight").pixels==60,
        "Small viewport must bound native popup and allow scroll instead of overflow");
    Check(Get(Get(dropdown).refs.at("choiceText")).text=="Miniatures"&&Style(f.root,"width").pixels==132,
        "Full caption and later external size changes must be preserved");
    Get(f.root).bounds[0]=20;TickDisplayShortcut();Check(!Get(dropdown).booleans["activated"],"Moved map toolbar anchor must close stale popup");
    clickHook(f.owner,nullptr);TickDisplayShortcut();Check(Get(dropdown).booleans["activated"]&&originalShows==3,"Reopen must refresh native popup position");
    auto foreign=Element("Dropdown");const auto shows=originalShows;showHook(foreign,nullptr);Check(originalShows==shows+1,"Foreign native Show must remain untouched");
    RestoreDisplayShortcut();Check(Style(f.root,"width").pixels==132&&Style(f.viewport,"width").pixels==90,"Cleanup must preserve native resize performed while mod active");
}

void ReadableChoicesAndReflowContract() {
    Fixture f;const auto dropdown=f.Attach();
    for(int mode=0;mode<3;++mode){
        Get(dropdown).integers["selectedIndex"]=mode;TickDisplayShortcut();
        Check(Get(Get(dropdown).refs.at("choiceText")).text==Get(dropdown).choices.at(mode).second,
            "Every selected mode must show the same complete label as its popup choice");
    }
    // Exercise the production-provided native flow contract at roomy and narrow
    // widths. This is a packing fixture, not a Unity renderer or font screenshot.
    for(float available:{500.0f,200.0f,140.0f}){
        const auto group=f.parent;
        Check(Style(group,"flexDirection").value==2&&Style(group,"flexWrap").value==1,"Native group must remain a wrapping row");
        float x=0,y=0;std::vector<std::array<float,4>> cells;
        for(auto item:Get(group).children){
            const float width=item==f.root?Style(item,"minWidth").pixels:43.0f;
            if(x>0&&x+width>available){x=0;y+=36;}
            Check(width<=available,"Readable cell does not fit the constrained native flow");
            cells.push_back({x,y,width,32});x+=width+4;
        }
        for(size_t a=0;a<cells.size();++a)for(size_t b=a+1;b<cells.size();++b)
            Check(cells[a][1]!=cells[b][1]||cells[a][0]+cells[a][2]<=cells[b][0],"Reflowed native neighbours overlap");
        Check(cells.at(1)[2]==128,"Selector lost its readable width during reflow");
    }
}
void DeferredCaptureAndIdempotence() {
    Fixture f;
    Get(f.parent).refs["panel"] = nullptr;
    clickHook(f.owner, nullptr);
    Check(originalClicks == 0 && Get(f.root).boolean, "Pending native click must only restore its boolean, never dispatch a toggle");
    TickDisplayShortcut();
    TickDisplayShortcut();
    Check(dropdownsCreated == 0 && !f.Dropdown(), "Shortcut must wait until native root has a panel");
    Get(f.parent).refs["panel"] = f.panel;
    TickDisplayShortcut();
    const auto dropdown = f.Dropdown();
    Check(dropdown && dropdownsCreated == 1 && Get(dropdown).booleans["activated"], "Deferred capture lost its requested popup");
    enableHook(f.owner, nullptr);
    contextHook(f.owner, nullptr, nullptr);
    refreshHook(f.owner, nullptr, nullptr);
    TickDisplayShortcut();
    TickDisplayShortcut();
    Check(f.Dropdown() == dropdown && dropdownsCreated == 1 && Get(f.root).children.size() == 3,
        "Repeated native events must not duplicate the dropdown or native children");
    Check(originalContexts == 1 && originalRefreshes == 1, "Capture must preserve native context/refresh handlers");
    const auto newChild = Element("LateNativeChild");
    Style(newChild, "visibility") = {0, 0, 3};
    StyleSnapshot saved;
    SaveStyles(saved, newChild);
    Add(f.root, newChild);
    TickDisplayShortcut();
    Check(Style(newChild, "visibility").value == 1, "Native children added after capture must also be hidden");
    RestoreDisplayShortcut();
    CheckStyles(saved);
    CheckStyles(f.originalStyles);
    Check(Get(f.root).children == std::vector<Obj>({f.icon, f.highlight, newChild}), "Restore must preserve native children added later");
}

void ContextSelectionAndExternalSync() {
    Fixture f;
    const auto dropdown = f.Attach();
    const auto baseline = f.state.settings;
    Get(dropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(changes == 1 && f.state.settings.roleplayMode == 0 && f.state.settings.mode == baseline.mode &&
        [&] { for (auto field : AnimationToggles) if (f.state.settings.*field != baseline.*field) return false; return true; }(),
        "Outside-combat choice must update only its display context");
    Check(!Get(f.root).boolean, "Native root boolean must follow Normal selection");
    combat = true;
    contextHook(f.owner, nullptr, nullptr);
    TickDisplayShortcut();
    Check(changes == 1 && Get(dropdown).integers["selectedIndex"] == 1, "Combat transition must reflect combat settings without committing a choice");
    Get(dropdown).integers["selectedIndex"] = 2;
    TickDisplayShortcut();
    Check(changes == 2 && f.state.settings.mode == 2 && f.state.settings.roleplayMode == 0,
        "Combat choice must update combat mode independently");
    f.state.settings.mode = 0;
    refreshHook(f.owner, nullptr, nullptr);
    TickDisplayShortcut();
    Check(changes == 2 && Get(dropdown).integers["selectedIndex"] == 0 && !Get(f.root).boolean,
        "Options/native preference changes must resync the shortcut without being overwritten");
    for (const int invalid : {-1, 3, 99}) {
        Get(dropdown).integers["selectedIndex"] = invalid;
        TickDisplayShortcut();
        Check(changes == 2 && Get(dropdown).integers["selectedIndex"] == 0, "Invalid shortcut index must revert without committing");
    }
}

void QueuedSelectionCannotCrossContextOrExternalChange() {
    Fixture f;
    const auto dropdown = f.Attach();
    Get(dropdown).integers["selectedIndex"] = 0;
    combat = true;
    TickDisplayShortcut();
    Check(changes == 0 && f.state.settings.mode == 1 && f.state.settings.roleplayMode == 2 &&
        Get(dropdown).integers["selectedIndex"] == 1, "Queued roleplay choice leaked into combat");
    Get(dropdown).integers["selectedIndex"] = 0;
    f.state.settings.mode = 2;
    TickDisplayShortcut();
    Check(changes == 0 && Get(dropdown).integers["selectedIndex"] == 2 && f.state.settings.mode == 2,
        "External mode changes must take precedence over an unpolled selection");
    Get(dropdown).integers["selectedIndex"] = 1;
    combat = false;
    TickDisplayShortcut();
    Check(changes == 0 && Get(dropdown).integers["selectedIndex"] == 2 && f.state.settings.roleplayMode == 2,
        "Queued combat choice leaked into roleplay");
}

void UnrelatedDropdownAndSessionAreIsolated() {
    Fixture f;
    const auto dropdown = f.Attach();
    const auto otherDropdown = Element("Dropdown");
    Get(otherDropdown).integers["selectedIndex"] = 0;
    Add(f.parent, otherDropdown);
    const auto otherOwner = NewOwner(Make("OtherAccount"), Element("OtherCreatureButton"));
    enableHook(otherOwner, nullptr);
    clickHook(otherOwner, nullptr);
    TickDisplayShortcut();
    Check(changes == 0 && originalClicks == 1 && f.Dropdown() == dropdown &&
        Get(otherDropdown).integers["selectedIndex"] == 0 && Get(otherDropdown).refs["parent"] == f.parent,
        "Unrelated native UI or account owner must remain untouched");
    Get(f.owner).refs["cached"] = Make("NewAccount");
    Get(dropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(changes == 0 && !f.Dropdown(), "Session owner change must restore before reading a queued selection");
    CheckStyles(f.originalStyles);
    Check(Get(otherDropdown).refs["parent"] == f.parent, "Restoration removed an unrelated dropdown");
}

void HiddenDisabledOrDetachedAnchorClosesPopup() {
    Fixture f;
    const auto dropdown = f.Attach();
    const auto open = [&] {
        clickHook(f.owner, nullptr);
        TickDisplayShortcut();
        Check(Get(dropdown).booleans["activated"], "Shortcut popup did not open before visibility test");
    };
    open();
    Style(f.parent, "display").value = 1;
    Get(dropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(!Get(dropdown).booleans["activated"] && changes == 0, "Hidden ancestor must close popup and suppress stale selection");
    Style(f.parent, "display").value = 0;
    TickDisplayShortcut();
    Check(Get(dropdown).integers["selectedIndex"] == 2 && changes == 0, "Showing ancestor must restore context value before polling");
    open();
    Get(f.root).booleans["disabled"] = true;
    Get(dropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(!Get(dropdown).booleans["activated"] && changes == 0, "Disabled native root must close popup and suppress selection");
    Get(f.root).booleans["disabled"] = false;
    TickDisplayShortcut();
    open();
    Get(f.parent).refs["panel"] = nullptr;
    Get(dropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(!Get(dropdown).booleans["activated"] && changes == 0, "Detached panel must close popup and suppress selection");
    Get(f.parent).refs["panel"] = f.panel;
    TickDisplayShortcut();
    Check(Get(dropdown).integers["selectedIndex"] == 2 && changes == 0, "Panel reattachment must discard stale selection");
}

void ReplacementBindingCannotUseOldSelection() {
    Fixture f;
    const auto oldDropdown = f.Attach();
    const auto replacement = Element("DofusToggleButtonCustom");
    Add(f.parent, replacement);
    const auto nativeChild = Element("NewNativeIcon");
    Add(replacement, nativeChild);
    StyleSnapshot replacementStyles;
    SaveStyles(replacementStyles, replacement);
    SaveStyles(replacementStyles, nativeChild);
    const auto binding = Get(f.owner).refs.at("dzyi");
    Get(binding).refs["creature"] = replacement;
    Get(oldDropdown).integers["selectedIndex"] = 0;
    TickDisplayShortcut();
    Check(changes == 0 && !f.Dropdown() && !Get(oldDropdown).refs["parent"], "Replacement binding must discard the old queued selection immediately");
    CheckStyles(f.originalStyles);
    TickDisplayShortcut();
    Obj newDropdown = nullptr;
    for (const auto child : Get(replacement).children)
        if (Get(child).klass->name == "Dropdown") newDropdown = child;
    Check(newDropdown && newDropdown != oldDropdown && dropdownsCreated == 2 &&
        Get(newDropdown).integers["selectedIndex"] == 2 && changes == 0,
        "Replacement native root must receive one fresh, synced shortcut");
    RestoreDisplayShortcut();
    CheckStyles(replacementStyles);
    Check(Get(replacement).children == std::vector<Obj>({nativeChild}), "Replacement root did not restore its native icon");
}

void AttachmentFailureRollsBackPartialStyles() {
    Fixture f;
    failOwner = Get(f.highlight).refs.at("style");
    failMethod = "UnityEngine.UIElements.IStyle.set_visibility";
    failAfterMutation = true;
    enableHook(f.owner, nullptr);
    TickDisplayShortcut();
    Check(dropdownsCreated == 1 && !f.Dropdown() && changes == 0, "Failed attachment retained its dropdown or committed settings");
    f.CheckNativeTree();
    CheckStyles(f.originalStyles);
    Check(!f.state.stopped.load() && !f.state.stopRequested.load(), "Shortcut failure must not stop the combat module");
    Check(std::any_of(logs.begin(), logs.end(), [](const std::string& message) {
        return message.starts_with("DISPLAY_SHORTCUT_DISABLED ");
    }), "Shortcut rollback must report its failure");
    enableHook(f.owner, nullptr);
    TickDisplayShortcut();
    Check(dropdownsCreated == 1, "Failed shortcut must not retry repeated mutations every tick");
    RestoreDisplayShortcut();
    Check(handles.size() == 1, "Failed shortcut retained GC handles beyond the module's cached service");
}
void LayoutFailureAndForeignStyleIsolation() {
    for(bool after:{false,true}){
        Fixture f;
        failOwner=Get(f.parent).refs.at("style");
        failMethod="UnityEngine.UIElements.IStyle.set_flexWrap";
        failAfterMutation=after;
        enableHook(f.owner,nullptr);TickDisplayShortcut();
        Check(!f.Dropdown()&&changes==0,"Partial layout failure must not publish a selector or a setting change");
        f.CheckNativeTree();CheckStyles(f.originalStyles);
        Check(handles.size()==1,"Partial layout failure leaked saved style references");
    }
    {
        Fixture f;f.Attach();
        Style(f.parent,"flexWrap")={0,2,0};
        Style(f.wrapper,"maxWidth")={350,0,0};
        Style(f.root,"flexShrink")={0.5f,0,0};
        f.originalStyles[{f.parent,"flexWrap"}]=Style(f.parent,"flexWrap");
        f.originalStyles[{f.wrapper,"maxWidth"}]=Style(f.wrapper,"maxWidth");
        f.originalStyles[{f.root,"flexShrink"}]=Style(f.root,"flexShrink");
        RestoreDisplayShortcut();CheckStyles(f.originalStyles);
    }
    {
        Fixture f;
        Get(Get(f.owner).refs.at("dzyi")).refs["toolbar"]=f.viewport;
        enableHook(f.owner,nullptr);TickDisplayShortcut();
        Check(!f.Dropdown(),"Unexpected native hierarchy must fail without modifying unrelated containers");
        f.CheckNativeTree();CheckStyles(f.originalStyles);
    }
}
}

int main() {
    try {
        fake::InitializeApi();
        const auto run = [](void(*test)()) {
            test();
            fake::Check(fake::handles.empty(), "Shortcut test leaked high 64-bit GC references");
        };
        run(fake::AlreadyOpenToolbarNeedsNoClick);
        run(fake::FaceThemeReadsAncestorDeclaration);
        run(fake::FaceThemeWaitsForStylesAndTracksThemeChanges);
        run(fake::FaceThemeTracksScopeAndRejectsInvalidColor);
        run(fake::DiscoveryWaitsForServiceAndAttachment);
        run(fake::DiscoveryIsolationAndReplacement);
        run(fake::UnattachedPendingOwnerCanBeReplaced);
        run(fake::NativeAppearanceAndRestoration);
        run(fake::BoundedLayoutResizeAndReopen);
        run(fake::ReadableChoicesAndReflowContract);
        run(fake::DeferredCaptureAndIdempotence);
        run(fake::ContextSelectionAndExternalSync);
        run(fake::QueuedSelectionCannotCrossContextOrExternalChange);
        run(fake::UnrelatedDropdownAndSessionAreIsolated);
        run(fake::HiddenDisabledOrDetachedAnchorClosesPopup);
        run(fake::ReplacementBindingCannotUseOldSelection);
        run(fake::AttachmentFailureRollsBackPartialStyles);
        run(fake::LayoutFailureAndForeignStyleIsolation);
        std::cout << "PASS: readable native toolbar flow, complete choices, isolated contexts, stale choices, popup lifecycle and layout rollback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

