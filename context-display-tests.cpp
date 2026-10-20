#include "context-display.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include "animation-mode-labels.hpp"
#include "glow-mode-labels.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Links the actual context-display.cpp to an isolated in-memory managed API.
// No Dofus process, DLL, window, loader, account, registry or settings file is used.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> refs;
    std::map<std::string, bool> flags;
    std::map<std::string, int> numbers;
    std::vector<Obj> items;
    std::array<unsigned char, 48> asset{};
    std::string text;
    int integer = 0;
    bool boolean = false;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<GCHandle, Obj> handles;
std::unordered_map<void*, Obj> unboxed;
GCHandle nextHandle = 0xFEDCBA980000ULL;
using CreateFn = Obj(*)(Obj, Method);
using CallbackFn = void(*)(Obj, Obj, Method);
CreateFn worldHook = nullptr, fightHook = nullptr;
CallbackFn callbackHook = nullptr;
bool combat = false;
unsigned originalCreates = 0, originalCallbacks = 0, changes = 0, nativeCloses = 0, scheduledCloses = 0;
std::string failMethod;
int failCountdown = 0;
bool failAfter = false;
std::vector<std::string> logs;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Object& Get(Obj object) {
    if (!object) throw std::runtime_error("Null fake context receiver");
    return *static_cast<Object*>(object);
}
Class* Type(const std::string& name) {
    auto& result = classes[name];
    if (!result) result = std::make_unique<Class>(Class{name});
    return result.get();
}
Obj Make(const std::string& name) {
    auto object = std::make_unique<Object>();
    object->klass = Type(name);
    const auto result = object.get();
    objects.push_back(std::move(object));
    if (name == "DofusContextualMenu") Get(result).refs["menuItems"] = Make("List");
    return result;
}
Obj String(const std::string& text) { auto result = Make("String"); Get(result).text = text; return result; }
Obj Box(int value) { auto result = Make("Int32"); Get(result).integer = value; return result; }
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
std::vector<Obj>& Items(Obj menu) { return Get(Get(menu).refs.at("menuItems")).items; }
Obj NativeCreate(Obj maker, Method) { ++originalCreates; return Get(maker).refs.at("result"); }
void NativeCallback(Obj, Obj, Method) { ++originalCallbacks; }
Obj NativeItem(Obj option, const std::string& label) {
    auto item = Make("ContextMenuItem");
    auto callback = Make(option ? "ClickCallbackHandler`1" : "ClickCallbackHandler");
    Get(callback).refs["arg1"] = option;
    Get(item).refs["m_callback"] = callback;
    Get(item).text = label;
    return item;
}
void InitializeApi() {
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_get_name = [](Klass klass) -> const char* { return static_cast<Class*>(klass)->name.c_str(); };
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.class_is_valuetype = [](Klass klass) { return static_cast<Class*>(klass)->name == "AddressableEntry"; };
    api.object_new = [](Klass klass) -> Obj { return Make(static_cast<Class*>(klass)->name); };
    api.method_get_param = [](Method method, uint32_t index) -> const void* {
        const auto& m = *static_cast<const MethodInfo*>(method);
        Check(m.name == "AppendRadioButtonItem" && m.argc == 12 && index == 4,
            "Default value type must derive from real radio method parameter metadata");
        return Type("AddressableEntry");
    };
    api.class_from_type = [](const void* type) -> Klass { return const_cast<void*>(type); };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.klass->name == "Int32") return &value.integer;
        if (value.klass->name == "Boolean") return &value.boolean;
        Check(value.klass->name == "AddressableEntry", "Unexpected context boxed value");
        unboxed[value.asset.data()] = object;
        return value.asset.data();
    };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        const auto handle = ++nextHandle;
        handles.emplace(handle, object);
        return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        const auto found = handles.find(handle);
        Check(found != handles.end(), "Invalid/truncated context GC handle");
        return found->second;
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Context GC reference freed twice"); };
}
struct Fixture {
    Module state{L"offline-context-test"};
    Obj container, maker, menu, original, grid, before, after, panel, root = nullptr, subroot = nullptr, animationRoot = nullptr, glowRoot = nullptr;
    std::vector<Obj> baseline;
    std::array<Obj, 3> views{};
    std::array<Obj, 4> animationViews{};
    std::array<Obj, 4> glowViews{};
    std::array<Obj, 2> glowCategoryViews{};
    explicit Fixture(bool inCombat = false) {
        RestoreContextDisplayMenu();
        module = &state;
        combat = inCombat;
        originalCreates = originalCallbacks = changes = nativeCloses = scheduledCloses = 0;
        failMethod.clear();
        failCountdown = 0;
        failAfter = false;
        logs.clear();
        state.servicesReady = true;
        state.cached.reset(Make("CachedDataService"));
        state.creatureOption.reset(Make("OptionBool"));
        state.roleplayOption.reset(Make("OptionBool"));
        Get(state.creatureOption.get()).boolean = true;
        Get(state.roleplayOption.get()).boolean = true;
        state.settings.mode = 1;
        state.settings.roleplayMode = 2;
        state.settings.animationMode = 3;
        state.settings.autoLoad = true;
        container = Make("DofusOptionsContainer");
        Get(container).refs["creatureModeFight"] = state.creatureOption.get();
        Get(container).refs["creatureModeRoleplay"] = state.roleplayOption.get();
        Get(state.cached.get()).refs["container"] = container;
        maker = Make(inCombat ? "FightWorldMenuMaker" : "WorldMenuMaker");
        Get(maker).refs["m_cachedDataService"] = state.cached.get();
        NewMenu();
        panel = Make("Panel");
        InstallContextDisplayMenu();
    }
    ~Fixture() { RestoreContextDisplayMenu(); module = nullptr; }
    Obj ActiveOption() const { return combat ? state.creatureOption.get() : state.roleplayOption.get(); }
    void NewMenu() {
        menu = Make("DofusContextualMenu");
        before = Make("ContextMenuSeparator");
        grid = NativeItem(Make("OtherBoolOption"), "Mode creature"); // Same apparent label, different argument identity.
        original = NativeItem(ActiveOption(), "Localized native label irrelevant to matching");
        after = NativeItem(nullptr, "Other native action");
        baseline = {before, grid, original, after};
        Items(menu) = baseline;
        for (auto item : baseline) Get(item).refs["root"] = menu;
        Get(maker).refs["result"] = menu;
        root = subroot = animationRoot = glowRoot = nullptr;
        views.fill(nullptr);
        animationViews.fill(nullptr);
        glowViews.fill(nullptr);glowCategoryViews.fill(nullptr);
    }
    Obj Create() {
        auto result = (combat ? fightHook : worldHook)(maker, nullptr);
        Check(result == menu, "CreateMenu return model identity changed");
        return result;
    }
    Obj Submenu(const char* title = "Affichage") const {
        for (auto item : Items(menu))
            if (Get(item).text == title) return Get(item).refs.at("subMenu");
        return nullptr;
    }
    Obj Choice(int index) const { auto sub = Submenu(); return sub ? Items(sub).at(index) : nullptr; }
    Obj AnimationChoice(int index) const { auto sub = Submenu("Animations"); return sub ? Items(sub).at(index) : nullptr; }
    Obj Glow() const {
        for (auto item : Items(menu)) if (Get(item).text == "Surbrillance") return item;
        return nullptr;
    }
    Obj GlowChoice(int index) const { auto sub = Submenu("Surbrillance"); return sub ? Items(sub).at(index) : nullptr; }
    Obj GlowCategory(int index) const {return Items(Submenu("Surbrillance")).at(5+index);}
    void Open() {
        Check(Submenu() != nullptr, "Three-way context submenu missing");
        root = Make("DofusContextualMenuRoot");
        Get(root).refs["menuData"] = menu;
        Get(root).refs["panel"] = panel;
        Get(menu).refs["dofusContextualMenuRoot"] = root;
        subroot = Make("DofusContextualMenuRoot");
        Get(subroot).refs["menuData"] = Submenu();
        Get(subroot).refs["panel"] = panel;
        Get(Submenu()).refs["dofusContextualMenuRoot"] = subroot;
        for (int i = 0; i < 3; ++i) {
            views[i] = Make("ContextMenuItemView");
            Get(views[i]).refs["itemData"] = Choice(i);
            Get(views[i]).refs["panel"] = panel;
            Get(views[i]).refs["parent"] = subroot;
        }
        Check(Submenu("Animations") != nullptr, "Animation context submenu missing");
        animationRoot = Make("DofusContextualMenuRoot");
        Get(animationRoot).refs["menuData"] = Submenu("Animations");
        Get(animationRoot).refs["panel"] = panel;
        Get(Submenu("Animations")).refs["dofusContextualMenuRoot"] = animationRoot;
        for (int i = 0; i < 4; ++i) {
            animationViews[i] = Make("ContextMenuItemView");
            Get(animationViews[i]).refs["itemData"] = AnimationChoice(i);
            Get(animationViews[i]).refs["panel"] = panel;
            Get(animationViews[i]).refs["parent"] = animationRoot;
        }
        Check(Submenu("Surbrillance") != nullptr, "Glow submenu missing");
        glowRoot = Make("DofusContextualMenuRoot");
        Get(glowRoot).refs["menuData"] = Submenu("Surbrillance");
        Get(glowRoot).refs["panel"] = panel;
        Get(Submenu("Surbrillance")).refs["dofusContextualMenuRoot"] = glowRoot;
        for (int i = 0; i < 4; ++i) {
            glowViews[i] = Make("ContextMenuItemView");
            Get(glowViews[i]).refs["itemData"] = GlowChoice(i);
            Get(glowViews[i]).refs["panel"] = panel;
            Get(glowViews[i]).refs["parent"] = glowRoot;
        }
        for (int i = 0; i < 2; ++i) {
            glowCategoryViews[i] = Make("ContextMenuItemView");
            Get(glowCategoryViews[i]).refs["itemData"] = GlowCategory(i);
            Get(glowCategoryViews[i]).refs["panel"] = panel;
            Get(glowCategoryViews[i]).refs["parent"] = glowRoot;
        }
        TickContextDisplayMenu();
    }
    void ClickView(Obj view, bool keyboard) {
        Check(view != nullptr, "Native selection needs a rendered view");
        auto choice = Get(view).refs.at("itemData");
        if (Get(choice).flags["disabled"]) return;
        // Both saved native input paths call SimulateClicked, which updates the
        // toggle/radio group, dispatches InvokeCallback, then honors closeOnSelect.
        auto submenu = Get(choice).refs.at("root");
        if (Get(choice).numbers["state"] == 1) Get(choice).flags["isOn"] = !Get(choice).flags["isOn"];
        else for (auto item : Items(submenu)) if (Get(item).numbers["state"] == 2) Get(item).flags["isOn"] = item == choice;
        auto parent = Get(view).refs.at("parent");
        callbackHook(Get(choice).refs.at("m_callback"), view, nullptr);
        Check(Get(view).refs.at("parent") == parent && Get(parent).refs.at("panel") == panel,
            "Callback detached a parent needed by native post-callback close");
        if (Get(choice).numbers["state"] != 1)
            Check(Get(choice).flags["closeOnSelect"] == (submenu != Submenu("Surbrillance")),
                "Only glow radios must keep the native menu open");
        if (Get(choice).flags["closeOnSelect"]) {
            ++nativeCloses;
            Get(root).flags[keyboard ? "keyboardClose" : "mouseClose"] = true;
        }
    }
    void Click(int index, bool keyboard = false) { ClickView(views[index], keyboard); }
    void ClickAnimation(int index, bool keyboard = false) { ClickView(animationViews[index], keyboard); }
    void ClickGlow(int selected, bool keyboard = false) { ClickView(glowViews[selected], keyboard); }
    void ClickGlowCategory(int index, bool keyboard = false) {ClickView(glowCategoryViews[index], keyboard);}
    void CheckUnchanged() const { Check(Items(menu) == baseline, "Native model/row order not restored exactly"); }
};
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Klass Api::NestedClass(Klass declaring, const char* name) const {
    const auto& parent = static_cast<fake::Class*>(declaring)->name;
    fake::Check((parent == "ContextMenuItem" && (std::string(name) == "ContextMenuItemView" || std::string(name) == "ClickCallbackHandler")) ||
        (parent == "DofusContextualMenu" && std::string(name) == "DofusContextualMenuRoot"), "Unexpected nested menu metadata lookup");
    return fake::Type(name);
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc});
    auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(rva != 0 && method, "Unpinned context native method");
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    if (m.name == "AppendRadioButtonItem") fake::Check(m.argc == 12 && rva == 0x16D72F0,
        "Glow must reuse the verified native radio overload");
    if (m.name == "AppendCheckboxItem") fake::Check(m.argc == 9 && rva == 0x16D63D0,"Exact native checkbox ABI");
    return reinterpret_cast<void*>(rva);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    fake::Check(m.argc == static_cast<int>(args.size()), "Wrong context native invocation arity");
    return Call(self, m.name.c_str(), args);
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    fake::Check(static_cast<fake::Class*>(klass)->name == "DofusContextualMenu" && args.size() == 0,
        "Unexpected context model construction");
    return fake::Make("DofusContextualMenu");
}
Obj Api::Field(Obj self, const char* name) const { return self ? fake::Get(self).refs[name] : nullptr; }
Obj Api::Call(Obj self, const char* methodName, std::initializer_list<void*> args) const {
    using namespace fake;
    auto& value = Get(self);
    const std::string name = methodName;
    const auto* arguments = args.begin();
    const bool fail = name == failMethod && --failCountdown == 0;
    const bool after = fail && failAfter;
    if (fail) {
        failMethod.clear();
        if (!after) throw std::runtime_error("Synthetic context operation failure");
    }
    auto done = [&](Obj result = nullptr) -> Obj {
        if (after) throw std::runtime_error("Synthetic context operation failure after mutation");
        return result;
    };
    if (name == "get_menuItems") return done(value.refs.at("menuItems"));
    if (name == "get_Count") return done(Box(static_cast<int>(value.items.size())));
    if (name == "get_Item") return done(value.items.at(*static_cast<int*>(arguments[0])));
    if (name == "get_arg1") {
        Check(value.klass->name == "ClickCallbackHandler`1", "Generic callback data accessed on an unrelated handler");
        return done(value.refs.at("arg1"));
    }
    if (name == "get_m_callback") {
        Check(value.klass->name == "ContextMenuItem", "Context item method called on separator/base item");
        return done(value.refs.at("m_callback"));
    }
    if (name == "get_creatureMode") {
        const auto container = value.refs.at("container");
        return done(Get(container).refs.at(combat ? "creatureModeFight" : "creatureModeRoleplay"));
    }
    if (name == "get_dofusOptionsContainer") return done(value.refs.at("container"));
    if (name == "get_disabled") return done(Box(value.flags["disabled"]));
    if (name == "get_property") return done(Box(value.boolean));
    if (name == "get_root") return done(value.refs.at("root"));
    if (name == "get_itemData") return done(value.refs.at("itemData"));
    if (name == "get_menuData") return done(value.refs.at("menuData"));
    if (name == "get_panel") return done(value.refs["panel"]);
    if (name == "set_isOn") { value.flags["isOn"] = *static_cast<bool*>(arguments[0]); return done(); }
    if (name == "RemoveItemAt") {
        auto& items = Items(self);
        const int index = *static_cast<int*>(arguments[0]);
        Check(index >= 0 && index < static_cast<int>(items.size()), "Invalid native context removal");
        items.erase(items.begin() + index);
        return done(self);
    }
    if (name == "InsertItem") {
        auto& items = Items(self);
        const int index = *static_cast<int*>(arguments[0]);
        Check(index >= 0 && index <= static_cast<int>(items.size()), "Invalid native context insertion");
        items.insert(items.begin() + index, arguments[1]);
        Get(arguments[1]).refs["root"] = self;
        return done(self);
    }
    if (name == "AppendSeparator") {
        Check(args.size() == 0, "Native separator ABI");Items(self).push_back(Make("ContextMenuSeparator"));return done(self);
    }
    if (name == "AppendCheckboxItem") {
        Check(args.size() == 9 && !arguments[1] && !*static_cast<bool*>(arguments[3]) &&
            !*static_cast<bool*>(arguments[4]) && *static_cast<unsigned*>(arguments[6]) == 1000 &&
            *static_cast<int*>(arguments[7]) == 0 && !Get(arguments[5]).text.empty(), "Native checkbox ABI and keep-open help");
        auto item=NativeItem(nullptr,Get(arguments[0]).text);
        Get(item).numbers["state"]=1;Get(item).flags["isOn"]=*static_cast<bool*>(arguments[2]);
        Get(item).flags["disabled"]=false;Get(item).flags["closeOnSelect"]=false;
        Get(item).refs["root"]=self;Items(self).push_back(item);return done(self);
    }
    if (name == "AppendRadioButtonItem") {
        Check(args.size() == 12 && arguments[1] == nullptr && *static_cast<int*>(arguments[3]) == 0 &&
            !*static_cast<bool*>(arguments[5]) &&
            *static_cast<unsigned*>(arguments[10]) == 1000 && !*static_cast<bool*>(arguments[11]),
            "Native radio ABI/defaults/action/group/close contract changed");
        const auto asset = unboxed.at(arguments[4]);
        Check(std::all_of(Get(asset).asset.begin(), Get(asset).asset.end(), [](auto byte) { return byte == 0; }),
            "AddressableEntry must be the real boxed zero/default value");
        const auto item = NativeItem(nullptr, Get(arguments[0]).text);
        Get(item).numbers["state"] = 2;
        Get(item).numbers["group"] = *static_cast<int*>(arguments[3]);
        Get(item).flags["isOn"] = *static_cast<bool*>(arguments[2]);
        Get(item).flags["disabled"] = *static_cast<bool*>(arguments[6]);
        Get(item).flags["closeOnSelect"] = *static_cast<bool*>(arguments[7]);
        Get(item).refs["root"] = self;
        Items(self).push_back(item);
        return done(self);
    }
    if (name == "AppendSubMenu") {
        Check(args.size() == 6 && arguments[2] == nullptr && *static_cast<int*>(arguments[4]) == 1,
            "Native submenu overload/default style ABI mismatch");
        const auto item = NativeItem(nullptr, Get(arguments[0]).text);
        Get(item).numbers["iconLeft"] = *static_cast<int*>(arguments[5]);
        Get(item).refs["subMenu"] = arguments[1];
        Get(item).flags["disabled"] = *static_cast<bool*>(arguments[3]);
        Get(item).refs["root"] = self;
        Items(self).push_back(item);
        return done(self);
    }
    if (name == "RemoveMenu") {
        Check(args.size() == 1 && !*static_cast<bool*>(arguments[0]), "Cleanup must use native immediate scheduled close");
        value.flags["scheduledClose"] = true;
        ++scheduledCloses;
        return done(); // Native scheduling preserves view parents until after dispatch.
    }
    throw std::runtime_error("Unexpected managed context method: " + value.klass->name + "." + name);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return fake::combat; }
void Module::Change(Settings next, bool save) {
    fake::Check(save, "Context preferences must use the normal persistent change path");
    ++fake::changes;
    settings = next;
    fake::Get(creatureOption.get()).boolean = settings.mode != 0;
    fake::Get(roleplayOption.get()).boolean = settings.roleplayMode != 0;
}
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    if (m.name == "CreateMenu" && m.klass->name == "WorldMenuMaker" && rva == 0x1700070) {
        fake::worldHook = reinterpret_cast<fake::CreateFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeCreate);
    } else if (m.name == "CreateMenu" && m.klass->name == "FightWorldMenuMaker" && rva == 0x16DC360) {
        fake::fightHook = reinterpret_cast<fake::CreateFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeCreate);
    } else if (m.name == "InvokeCallback" && m.klass->name == "ClickCallbackHandler" && rva == 0x16CA600) {
        fake::callbackHook = reinterpret_cast<fake::CallbackFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeCallback);
    } else throw std::runtime_error("Unexpected context hook ABI");
}

namespace fake {
void ChoicesAndNativeIsolation() {
    for (const bool inCombat : {false, true}) for (int selected = 0; selected < 3; ++selected) {
        Fixture f(inCombat);
        const auto baselineSettings = f.state.settings;
        const auto gridCallback = Get(f.grid).refs.at("m_callback");
        const auto originalCallback = Get(f.original).refs.at("m_callback");
        f.Create();
        auto& rows = Items(f.menu);
        Check(rows.size() == f.baseline.size() + 2 && rows[0] == f.before && rows[1] == f.grid && rows[5] == f.after &&
            std::find(rows.begin(), rows.end(), f.original) == rows.end(), "Only native creature row must be replaced");
        Check(Get(rows[2]).text == "Affichage" && Get(rows[3]).text == "Animations" && Items(f.Submenu()).size() == 3 &&
            Items(f.Submenu("Animations")).size() == 4, "Expected adjacent Affichage and Animations submenus");
        Check(Get(rows[2]).numbers.at("iconLeft") == 136 && Get(rows[3]).numbers.at("iconLeft") == 226,
            "Display and animation submenus must use the native eye and play icons");
        Check(rows[4] == f.Glow() && Get(rows[4]).numbers.at("iconLeft") == 136,
            "Glow submenu must follow Animations with native eye icon");
        const std::array<std::string, 3> labels{"Normal", "Cr\xC3\xA9" "atures", "Miniatures"};
        for (int i = 0; i < 3; ++i) {
            auto& choice = Get(f.Choice(i));
            Check(choice.text == labels[i] && choice.numbers["state"] == 2 && choice.flags["isOn"] ==
                (i == (inCombat ? 1 : 2)), "Wrong radio labels, states or initial selection");
        }
        f.Open();
        f.Click(selected, selected % 2 == 0);
        Check(changes == 1 && (inCombat ? f.state.settings.mode : f.state.settings.roleplayMode) == selected,
            "Selected display mode not committed through Module.Change");
        Check((inCombat ? f.state.settings.roleplayMode : f.state.settings.mode) ==
            (inCombat ? baselineSettings.roleplayMode : baselineSettings.mode) &&
            f.state.settings.animationMode == baselineSettings.animationMode &&
            f.state.settings.autoLoad == baselineSettings.autoLoad,
            "Context selection changed unrelated settings");
        Check(nativeCloses == 1 && Get(f.views[selected]).refs["parent"] == f.subroot,
            "Mouse/keyboard native selection/close path was replaced");
        Check(Get(f.grid).refs["m_callback"] == gridCallback && Get(f.original).refs["m_callback"] == originalCallback,
            "Native callbacks mutated during replacement");
        RestoreContextDisplayMenu();
        f.CheckUnchanged();
        Check(Get(f.root).flags["scheduledClose"] && Get(f.subroot).flags["scheduledClose"] &&
            Get(f.animationRoot).flags["scheduledClose"] && Get(f.glowRoot).flags["scheduledClose"],
            "Stop must close all four independent native menu roots");
        Check(handles.size() == 3, "Context ownership GC handles survived explicit restoration");
    }
}
void AnimationChoicesPreserveOtherSettings() {
    for (const bool inCombat : {false, true}) for (int selected = 0; selected < 4; ++selected) {
        Fixture f(inCombat);
        f.state.settings.animationMode = (selected + 1) % 4;
        auto expected = f.state.settings;
        const int originalAnimation = expected.animationMode;
        expected.animationMode = selected;
        f.Create();
        for (int i = 0; i < 4; ++i) {
            const auto& choice = Get(f.AnimationChoice(i));
            Check(choice.text == AnimationModeLabels[i] && choice.numbers.at("state") == 2 &&
                choice.flags.at("isOn") == (i == originalAnimation), "Animation radio labels/state do not match stored modes");
        }
        f.Open();
        f.ClickAnimation(selected, selected % 2 == 0);
        Check(changes == 1 && f.state.settings == expected,
            "Animation selection must change only the global animation mode through Module.Change");
        Check(nativeCloses == 1 && Get(f.animationViews[selected]).refs.at("parent") == f.animationRoot,
            "Animation selection replaced native mouse/keyboard close behavior");
        for (int i = 0; i < 4; ++i) Check(Get(f.AnimationChoice(i)).flags["isOn"] == (i == selected),
            "Animation radio selection must remain mutually exclusive");
        for (int i = 0; i < 3; ++i) Check(Get(f.Choice(i)).flags["isOn"] == (i == (inCombat ? expected.mode : expected.roleplayMode)),
            "Animation radio group affected the independent display radio group");
        RestoreContextDisplayMenu();
        f.CheckUnchanged();
        Check(handles.size() == 3, "Animation menu ownership survived restoration");
    }
}
void GlowChoicesAndReopens() {
    for (const bool inCombat : {false, true}) for (int initial : {0, 1, 2, 3}) for (int selected : {0, 1, 2, 3}) {
        Fixture f(inCombat);
        f.state.settings.monsterGlowMode = initial;
        auto expected = f.state.settings;
        f.Create();
        for (int mode = 0; mode < 4; ++mode) {
            const auto& choice = Get(f.GlowChoice(mode));
            Check(choice.text == GlowModeLabels[mode] && choice.numbers.at("state") == 2 &&
                choice.numbers.at("group") == 0 && choice.flags.at("isOn") == (mode == initial),
                "Glow radio group must reflect the stored mode with shared option labels");
        }
        f.Open();
        f.ClickGlow(selected, selected % 2 == 0);
        expected.monsterGlowMode = selected;
        Check(changes == 1 && f.state.settings == expected, "Glow choice must only change its persistent mode");
        for (int mode = 0; mode < 4; ++mode)
            Check(Get(f.GlowChoice(mode)).flags.at("isOn") == (mode == selected), "Glow radios must remain mutually exclusive");
        Check(nativeCloses == 0 && originalCallbacks == 0 && Get(f.glowViews[selected]).refs.at("parent") == f.glowRoot,
            "Glow radio must preserve native menu ownership and keep the menu open");
        const auto retained = handles.size();
        f.NewMenu(); f.Create();
        Check(handles.size() == retained && Get(f.GlowChoice(selected)).flags.at("isOn"),
            "Reopened radios must reflect saved glow without accumulating ownership");
        f.Open(); f.ClickGlow(initial, initial % 2 == 0);
        expected.monsterGlowMode = initial;
        Check(changes == 2 && f.state.settings == expected, "Reselection must restore the original glow mode");
        RestoreContextDisplayMenu(); f.CheckUnchanged();
        Check(handles.size() == 3, "Glow submenu and callbacks must be released on cleanup");
    }
}
void GlowRepeatedChoicesKeepMenuOpen() {
    for (const bool inCombat : {false, true}) for (int initial : {0, 1, 2, 3}) {
        Fixture f(inCombat);
        f.state.settings.monsterGlowMode = initial;
        auto expected = f.state.settings;
        f.Create(); f.Open();
        const auto submenu = f.Submenu("Surbrillance");
        const auto initialRows = Items(f.menu);
        const auto retained = handles.size();
        for (int click = 0; click < 8; ++click) {
            const int selected = (initial + click + 1) % 4;
            Check(!Get(f.GlowChoice(selected)).flags.at("closeOnSelect"), "Every glow radio uses native keep-open behavior");
            f.ClickGlow(selected, click % 2 == 0);
            expected.monsterGlowMode = selected;
            TickContextDisplayMenu();
            Check(f.state.settings == expected && Get(f.GlowChoice(selected)).flags.at("isOn") &&
                changes == static_cast<unsigned>(click + 1), "Repeated selection must update exactly one persistent preference");
            Check(nativeCloses == 0 && scheduledCloses == 0 && !Get(f.root).flags["mouseClose"] &&
                !Get(f.root).flags["keyboardClose"] && Items(f.menu) == initialRows && f.Submenu("Surbrillance") == submenu &&
                handles.size() == retained, "Glow selection or following tick closed/rebuilt the live context menu");
            for (int mode = 0; mode < 4; ++mode)
                Check(Get(f.GlowChoice(mode)).flags.at("isOn") == (mode == selected), "Repeated glow selections must keep one radio checked");
        }
        Check(expected.monsterGlowMode == initial, "Round trips must restore the initial glow mode");
    }
}
void GlowCategoriesIndependentAndPersistent() {
    for(bool inCombat:{false,true})for(int initial=0;initial<4;++initial){
        Fixture f(inCombat);f.state.settings.glowNpcs=(initial&1)!=0;f.state.settings.glowInteractives=(initial&2)!=0;
        auto expected=f.state.settings;f.Create();f.Open();
        Check(Items(f.Submenu("Surbrillance")).size()==7&&Get(f.GlowCategory(0)).text=="Inclure les PNJ"&&
            Get(f.GlowCategory(1)).text=="Inclure les éléments interactifs","Two native checkboxes follow four glow modes and separator");
        Check(Get(f.GlowCategory(0)).flags["isOn"]==expected.glowNpcs&&Get(f.GlowCategory(1)).flags["isOn"]==expected.glowInteractives,"Initial checkbox state follows saved options");
        for(int i=0;i<4;++i){
            const int category=i%2;f.ClickGlowCategory(category,i%2==0);
            if(category==0)expected.glowNpcs=!expected.glowNpcs;else expected.glowInteractives=!expected.glowInteractives;
            TickContextDisplayMenu();
            Check(f.state.settings==expected&&changes==unsigned(i+1)&&nativeCloses==0&&scheduledCloses==0,"Checkbox changes exactly one preference and keeps menu open for mouse/keyboard");
            Check(Get(f.GlowCategory(0)).flags["isOn"]==expected.glowNpcs&&Get(f.GlowCategory(1)).flags["isOn"]==expected.glowInteractives,"Categories remain independent");
        }
        f.ClickGlow(0);expected.monsterGlowMode=0;
        Check(f.state.settings==expected&&Get(f.GlowCategory(0)).flags["isOn"]==expected.glowNpcs&&Get(f.GlowCategory(1)).flags["isOn"]==expected.glowInteractives,"Mode radio does not uncheck inclusion checkboxes");
        f.ClickGlowCategory(0);expected.glowNpcs=!expected.glowNpcs;
        Check(f.state.settings==expected,"Category selection can be saved while glow is disabled without enabling it");
        const auto retained=handles.size();f.NewMenu();f.Create();f.Open();
        Check(handles.size()==retained&&Get(f.GlowCategory(0)).flags["isOn"]==expected.glowNpcs&&Get(f.GlowCategory(1)).flags["isOn"]==expected.glowInteractives,"Reopening restores persisted selections without retaining old callbacks");
    }
}
void GlowCategoryCallbackGuards(){
    for(int reason=0;reason<6;++reason){
        Fixture f;f.Create();f.Open();auto item=f.GlowCategory(0),view=f.glowCategoryViews[0];
        auto callback=Get(item).refs.at("m_callback");
        if(reason==0)view=f.glowCategoryViews[1];
        if(reason==1)Get(view).refs["panel"]=nullptr;
        if(reason==2)Get(item).flags["disabled"]=true;
        if(reason==3)Get(item).refs["root"]=f.menu;
        if(reason==4)f.state.stopRequested=true;
        if(reason==5)Get(f.maker).refs["m_cachedDataService"]=Make("OtherSession");
        callbackHook(callback,view,nullptr);Check(changes==0,"Invalid checkbox callback never changes preferences");
    }
}
void CallbackIdentityAndDisabledRows() {
    Fixture f;
    f.Create();
    f.Open();
    const auto ownHandler = Get(f.Choice(0)).refs.at("m_callback");
    const auto otherHandler = Make("ClickCallbackHandler");
    callbackHook(otherHandler, f.views[0], nullptr);
    Check(originalCallbacks == 1 && changes == 0, "Unrelated callback must be forwarded untouched");
    callbackHook(ownHandler, f.views[1], nullptr);
    Check(originalCallbacks == 1 && changes == 0, "Owned handler must require exact view.itemData identity");
    const auto animationHandler = Get(f.AnimationChoice(0)).refs.at("m_callback");
    callbackHook(ownHandler, f.animationViews[0], nullptr);
    callbackHook(animationHandler, f.views[0], nullptr);
    callbackHook(animationHandler, f.animationViews[1], nullptr);
    Check(originalCallbacks == 1 && changes == 0, "Handlers must never cross submenu or radio item identities");
    Get(f.Choice(0)).flags["disabled"] = true;
    callbackHook(ownHandler, f.views[0], nullptr);
    Check(changes == 0, "Disabled owned choice must not change settings");
    Get(f.Choice(0)).flags["disabled"] = false;
    Get(f.views[0]).refs["panel"] = nullptr;
    callbackHook(ownHandler, f.views[0], nullptr);
    Check(changes == 0, "Detached view must not change settings");
    Get(f.AnimationChoice(0)).flags["disabled"] = true;
    callbackHook(animationHandler, f.animationViews[0], nullptr);
    Check(changes == 0, "Disabled animation choice must not change settings");
    Get(f.AnimationChoice(0)).flags["disabled"] = false;
    Get(f.animationViews[0]).refs["panel"] = nullptr;
    callbackHook(animationHandler, f.animationViews[0], nullptr);
    Check(changes == 0, "Detached animation view must not change settings");
    const auto glowHandler = Get(f.GlowChoice(0)).refs.at("m_callback");
    callbackHook(glowHandler, f.views[1], nullptr);
    callbackHook(ownHandler, f.glowViews[0], nullptr);
    callbackHook(glowHandler, f.glowViews[1], nullptr);
    Check(changes == 0, "Glow radio handler must require exact item identity");
    Get(f.GlowChoice(0)).flags["disabled"] = true;
    callbackHook(glowHandler, f.glowViews[0], nullptr);
    Check(changes == 0, "Disabled glow radio must not commit");
    Get(f.GlowChoice(0)).flags["disabled"] = false;
    Get(f.glowViews[0]).refs["panel"] = nullptr;
    callbackHook(glowHandler, f.glowViews[0], nullptr);
    Check(changes == 0, "Detached glow radio must not commit");
    Get(f.glowViews[0]).refs["panel"] = f.panel;
    Get(f.GlowChoice(0)).refs["root"] = f.menu;
    callbackHook(glowHandler, f.glowViews[0], nullptr);
    Check(changes == 0, "Glow radio on a wrong menu root must not commit");
}
void NativeDisabledStateIsPreserved() {
    Fixture f;
    Get(f.original).flags["disabled"] = true;
    f.Create();
    Check(Get(Items(f.menu)[2]).flags["disabled"], "Native disabled entry became an enabled submenu");
    for (int i = 0; i < 3; ++i) Check(Get(f.Choice(i)).flags["disabled"], "Disabled parent must keep all owned choices disabled");
    Check(!Get(Items(f.menu)[3]).flags["disabled"], "Native display availability must not disable the independent animation setting");
    for (int i = 0; i < 4; ++i) Check(!Get(f.AnimationChoice(i)).flags["disabled"], "Animation choices inherited unrelated display disabling");
    f.Open();
    auto expected = f.state.settings;
    expected.animationMode = 0;
    f.ClickAnimation(0);
    Check(changes == 1 && f.state.settings == expected, "Animation selection must work while native creature display is disabled");
    Check(!Get(f.Glow()).flags["disabled"], "Creature availability must not disable glow");
    expected.monsterGlowMode = 3;
    f.ClickGlow(3);
    Check(changes == 2 && f.state.settings == expected, "Independent glow must work while native display is disabled");
}
void ReopenPruneAndDuplicateCreation() {
    Fixture f;
    f.Create();
    const auto sub = f.Submenu();
    const auto animationSub = f.Submenu("Animations");
    const auto retained = handles.size();
    f.Create();
    TickContextDisplayMenu();
    Check(f.Submenu() == sub && f.Submenu("Animations") == animationSub && Items(f.menu).size() == 6 && handles.size() == retained,
        "Duplicate creation duplicated rows or GC ownership");
    f.Open();
    const auto oldMenu = f.menu;
    const auto oldRows = f.baseline;
    const auto oldRoot = f.root;
    const auto oldDisplayRoot = f.subroot;
    const auto oldAnimationRoot = f.animationRoot;
    const auto oldGlowRoot = f.glowRoot;
    f.NewMenu();
    f.Create();
    Check(Items(oldMenu) == oldRows && Get(oldRoot).flags["scheduledClose"] && Get(oldDisplayRoot).flags["scheduledClose"] &&
        Get(oldAnimationRoot).flags["scheduledClose"] && Get(oldGlowRoot).flags["scheduledClose"], "Reopening must retire previous model and all roots");
    Check(handles.size() == retained, "Reopening must not accumulate retained menus/handlers");
    f.Open();
    Get(f.root).refs["panel"] = nullptr;
    TickContextDisplayMenu();
    f.CheckUnchanged();
    Check(Get(f.subroot).flags["scheduledClose"] && Get(f.animationRoot).flags["scheduledClose"] && Get(f.glowRoot).flags["scheduledClose"],
        "Parent detach must also close all independent submenu roots");
    Check(handles.size() == 3, "Detached native root was not pruned");
}
void StaleContextAndSessionCannotCommit() {
    for (int mutation = 0; mutation < 4; ++mutation) {
        Fixture f;
        f.Create();
        f.Open();
        auto handler = Get(f.Choice(0)).refs.at("m_callback");
        auto animationHandler = Get(f.AnimationChoice(0)).refs.at("m_callback");
        if (mutation == 0) combat = true;
        if (mutation == 1) Get(f.container).refs["creatureModeRoleplay"] = Make("ReplacementOption");
        if (mutation == 2) Get(f.maker).refs["m_cachedDataService"] = Make("OtherSession");
        if (mutation == 3) f.state.stopRequested.store(true);
        callbackHook(handler, f.views[0], nullptr);
        callbackHook(animationHandler, f.animationViews[0], nullptr);
        for (int mode = 0; mode < 4; ++mode)
            callbackHook(Get(f.GlowChoice(mode)).refs.at("m_callback"), f.glowViews[mode], nullptr);
        Check(changes == 0, "Stale context/session/stop callback committed preferences");
        TickContextDisplayMenu();
        f.CheckUnchanged();
        Check(Get(f.root).flags["scheduledClose"] && handles.size() == 3, "Invalid session/context ownership not retired");
    }
}
void NativeAndOtherMenuIsolation() {
    Fixture f;
    Get(f.maker).refs["m_cachedDataService"] = Make("OtherAccount");
    f.Create();
    f.CheckUnchanged();
    Get(f.maker).refs["m_cachedDataService"] = f.state.cached.get();
    Get(Get(f.original).refs.at("m_callback")).refs["arg1"] = Make("UnrelatedOption");
    f.Create();
    f.CheckUnchanged();
    Check(changes == 0 && handles.size() == 3, "Menus without exact native creature argument should remain untouched");
}
void ExternalChoiceClosesStaleMenu() {
    for (int setting = 0; setting < 5; ++setting) {
        Fixture f;
        f.Create();
        f.Open();
        if (setting == 0) f.state.settings.roleplayMode = 0;
        else if (setting == 1) f.state.settings.animationMode = 0;
        else if (setting == 2) f.state.settings.monsterGlowMode = 3;
        else if (setting == 3) f.state.settings.glowNpcs = true;
        else f.state.settings.glowInteractives = true;
        TickContextDisplayMenu();
        f.CheckUnchanged();
        Check(changes == 0 && Get(f.root).flags["scheduledClose"] && Get(f.subroot).flags["scheduledClose"] &&
            Get(f.animationRoot).flags["scheduledClose"] && Get(f.glowRoot).flags["scheduledClose"], "External display, animation or glow change must retire all stale checked views");
    }
}
void FailureRollback() {
    struct Failure { const char* method; int occurrence; bool after; };
    const Failure failures[] = {
        {"AppendRadioButtonItem", 3, false}, {"AppendRadioButtonItem", 6, false}, {"AppendSubMenu", 2, true},
        {"AppendRadioButtonItem", 8, false}, {"AppendRadioButtonItem", 8, true},
        {"AppendRadioButtonItem", 9, false}, {"AppendRadioButtonItem", 10, true}, {"AppendRadioButtonItem", 11, false},
        {"AppendCheckboxItem", 1, false}, {"AppendCheckboxItem", 1, true},
        {"AppendCheckboxItem", 2, false}, {"AppendCheckboxItem", 2, true}, {"AppendSeparator", 1, true},
        {"AppendSubMenu", 3, false}, {"AppendSubMenu", 3, true},
        {"InsertItem", 1, false}, {"InsertItem", 1, true}, {"InsertItem", 2, false}, {"InsertItem", 2, true},
        {"InsertItem", 3, false}, {"InsertItem", 3, true}
    };
    for (const auto failure : failures) {
        Fixture f;
        failMethod = failure.method;
        failCountdown = failure.occurrence;
        failAfter = failure.after;
        f.Create();
        f.CheckUnchanged();
        Check(changes == 0 && handles.size() == 3, "Failed creation did not release partial submenu/restore native row");
        Check(std::any_of(logs.begin(), logs.end(), [](const auto& log) { return log.starts_with("CONTEXT_DISPLAY_DISABLED "); }),
            "Failure should be reported and isolated from other features");
        Check(!f.state.stopped.load() && !f.state.stopRequested.load(), "Context-menu failure stopped unrelated feature module");
        f.Create();
        f.CheckUnchanged();
    }
}
void StopRestoresOnlyOwnedRowAndInertsStaleHandler() {
    Fixture f;
    f.Create();
    f.Open();
    const auto ownedHandler = Get(f.Choice(0)).refs.at("m_callback");
    const auto animationHandler = Get(f.AnimationChoice(0)).refs.at("m_callback");
    const auto glowHandler = Get(f.GlowChoice(0)).refs.at("m_callback");
    auto extra = NativeItem(nullptr, "Late native action");
    Items(f.menu).insert(Items(f.menu).begin() + 2, extra);
    RestoreContextDisplayMenu();
    const std::vector<Obj> expected{f.before, f.grid, extra, f.original, f.after};
    Check(Items(f.menu) == expected, "Restoration removed/reordered a late native row");
    callbackHook(ownedHandler, f.views[0], nullptr); // Null native action remains harmless after ownership was released.
    callbackHook(animationHandler, f.animationViews[0], nullptr);
    callbackHook(glowHandler, f.glowViews[0], nullptr);
    Check(changes == 0 && handles.size() == 3 && Get(f.root).flags["scheduledClose"],
        "Stopped module retained ownership or applied a queued native callback");
}
void SelectionFailureKeepsNativeDispatchParents() {
    Fixture f;
    f.Create();
    f.Open();
    failMethod = "set_isOn";
    failCountdown = 1;
    f.Click(0, true); // The setting succeeds; the subsequent visual Sync fails.
    f.CheckUnchanged();
    Check(changes == 1 && f.state.settings.roleplayMode == 0 && nativeCloses == 1,
        "A visual Sync failure must preserve accepted settings and native keyboard dispatch");
    Check(Get(f.root).flags["scheduledClose"] && Get(f.subroot).flags["scheduledClose"] &&
        Get(f.animationRoot).flags["scheduledClose"] && Get(f.glowRoot).flags["scheduledClose"] && handles.size() == 3,
        "Callback exception did not retire all four independent roots and GC ownership");
    Check(!f.state.stopped.load() && !f.state.stopRequested.load(), "Visual selection failure stopped unrelated module features");
    Check(std::any_of(logs.begin(), logs.end(), [](const auto& log) { return log.starts_with("CONTEXT_DISPLAY_DISABLED "); }),
        "Selection exception must be reported");
}
void RestoreFailureStillClosesOtherRootAndReleasesReferences() {
    for (int failingRoot = 1; failingRoot <= 4; ++failingRoot) {
        Fixture f;
        f.Create();
        f.Open();
        failMethod = "RemoveMenu";
        failCountdown = failingRoot;
        RestoreContextDisplayMenu();
        f.CheckUnchanged();
        Check(Get(f.subroot).flags["scheduledClose"] == (failingRoot != 1) &&
            Get(f.animationRoot).flags["scheduledClose"] == (failingRoot != 2) &&
            Get(f.glowRoot).flags["scheduledClose"] == (failingRoot != 3) &&
            Get(f.root).flags["scheduledClose"] == (failingRoot != 4) && handles.size() == 3,
            "A failed close must not skip other independent roots or retain ownership");
        Check(std::any_of(logs.begin(), logs.end(), [](const auto& log) { return log.starts_with("CONTEXT_DISPLAY_RESTORE_ERROR "); }),
            "Best-effort restoration must expose its partial cleanup failure");
    }
}
}
int main() {
    try {
        fake::InitializeApi();
        const auto run = [](void(*test)()) {
            test();
            fake::Check(fake::handles.empty(), "Context test leaked managed 64-bit GC references");
        };
        run(fake::ChoicesAndNativeIsolation);
        run(fake::AnimationChoicesPreserveOtherSettings);
        run(fake::GlowChoicesAndReopens);
        run(fake::GlowRepeatedChoicesKeepMenuOpen);
        run(fake::GlowCategoriesIndependentAndPersistent);
        run(fake::GlowCategoryCallbackGuards);
        run(fake::CallbackIdentityAndDisabledRows);
        run(fake::NativeDisabledStateIsPreserved);
        run(fake::ReopenPruneAndDuplicateCreation);
        run(fake::StaleContextAndSessionCannotCommit);
        run(fake::NativeAndOtherMenuIsolation);
        run(fake::ExternalChoiceClosesStaleMenu);
        run(fake::FailureRollback);
        run(fake::StopRestoresOnlyOwnedRowAndInertsStaleHandler);
        run(fake::SelectionFailureKeepsNativeDispatchParents);
        run(fake::RestoreFailureStillClosesOtherRootAndReleasesReferences);
        std::cout << "PASS: real context menu integration, display3 + animation4 + glow4 in both contexts, persistence dispatch, native close/keyboard, rollback, stale sessions and four-root GC cleanup\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

bool HandleSoulGroupMenuCallback(managed::Obj, managed::Obj) { return false; }
bool HandleMapTravelCallback(managed::Obj, managed::Obj) { return false; }
bool HandleSpousePartyMenuCallback(managed::Obj, managed::Obj) { return false; }

namespace soul { bool HandlePanelMenuCallback(managed::Obj, managed::Obj) { return false; } }
