#include "options-ui.hpp"
#include "version.hpp"
#include "module.hpp"
#include "features.hpp"
#include "glow-mode-labels.hpp"
#include "treasure-launch.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// This executable links the actual options-ui.cpp against a small managed API
// model. No game DLL, process, injector, window, or network is used.
namespace fake {
using namespace managed;
constexpr int ModId = 0x4D4F44;
// The eight native sections of the mod, in display order: section i has the id
// ModId + 1 + i, and Restore maps the parent and every section back to « Jeu ».
enum SectionIndex : size_t { DisplaySection, GlowSection, MapsSection, TravelSection, SoulSection, LoadingSection, MetamobSection, AboutSection, SectionCount };
constexpr int SectionId(size_t index) { return ModId + 1 + static_cast<int>(index); }
constexpr int DisplayId = SectionId(DisplaySection), MetamobId = SectionId(MetamobSection);
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Choice { int id = 0; Obj label = nullptr; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> refs;
    std::map<std::string, int> integers;
    std::map<std::string, bool> booleans;
    std::vector<Obj> items;
    std::string text;
    int integer = 0, defaultInteger = 0, notifications = 0;
    bool boolean = false, defaultBoolean = false, loaded = true;
    Choice choice;
    std::map<std::string, float> styles; // inline style of an owned element
    float number = 0;                    // StyleBox / Length value
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
bool newSourcesLoaded = true;
bool rejectAutoLoad = false;
bool rejectRebuild = false;
unsigned treasureOpens = 0, notebookOpens = 0, foreignButtonClicks = 0;
std::vector<std::string> openedUrls;
unsigned changes = 0, serial = 0, enableCalls = 0;
unsigned sourceModels = 0, optionModels = 0, uiHooks = 0;
std::vector<std::string> logs;
using EnableFn = void(*)(Obj, Method);
using InitFn = void(*)(Obj, bool, bool, int, Method);
EnableFn enableHook = nullptr;
InitFn initHook = nullptr;
using TextButtonFn = void(*)(Obj, Obj, Method);
TextButtonFn textButtonHook = nullptr;
TextButtonFn textInitHook = nullptr; // TextButtonOptionElement.Init(TextButtonOption)
// The Metamob rows reach the capture sync through MetamobHooks fakes only. The
// key below is fake and no fragment of it may reach a log or a notice.
constexpr const char* FakeKey = "TEST-KEY-0123456789-NOT-REAL";
bool fakeKeyStored = false, failKeyInsert = false, failKeyRemove = false, failTextRead = false;
unsigned keyForgets = 0, textReads = 0, nativeTextInits = 0, inputsCreated = 0;
std::vector<std::string> submittedKeys, notices;
std::vector<uintptr_t> verifiedRvas;
const std::string SyncDescription = "Après chaque capture d’un archimonstre ou d’un boss (pierre d’âme remplie en combat), ajoute 1 à ce monstre dans ta quête Ocre sur Metamob, sans rien modifier d’autre. Nécessite ta clé API personnelle (metamob.fr › Paramètres › Clé API). Désactivé par défaut.";
const std::string KeyDescription = "Colle ta clé dans le champ masqué puis clique sur Enregistrer. Elle est vérifiée auprès de Metamob, chiffrée pour ton compte Windows et gardée uniquement sur ce PC ; elle n’est jamais affichée, journalisée ni incluse dans les mises à jour.";
const std::string KeyHint = "Colle ici ta clé API Metamob", StoredKeyHint = "Clé enregistrée (masquée) : colle une nouvelle clé pour la remplacer";
const std::string PasteKeyFirst = "Metamob : colle d’abord ta clé dans le champ.", ConfirmForget = "Metamob : reclique sur Supprimer pour oublier ta clé.";

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Settings Config(int mode, int animations) {
    Settings settings;
    settings.mode = mode;
    settings.animationMode = animations;
    settings.roleplayMode = 0;
    return settings;
}
Object& Get(Obj object) {
    if (!object) throw std::runtime_error("Fake API received null object");
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
    auto result = object.get();
    objects.push_back(std::move(object));
    return result;
}
Obj String(const std::string& text) { auto result = Make("String"); Get(result).text = text; return result; }
Obj Box(int value) { auto result = Make("Int32"); Get(result).integer = value; return result; }
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
Obj Category(const char* name, int id) {
    auto result = Make("CategoryData");
    auto& value = Get(result);
    value.refs["name"] = String(name);
    value.refs["uid"] = String("category-" + std::to_string(++serial));
    value.refs["subcategories"] = Make("List");
    value.refs["options"] = Make("List");
    value.integers["categoryId"] = id;
    return result;
}
Obj CloneCategory(Obj source) {
    const auto& sourceValue = Get(source);
    const auto clone = Category(Get(sourceValue.refs.at("name")).text.c_str(), sourceValue.integers.at("categoryId"));
    auto& cloneValue = Get(clone);
    cloneValue.refs["uid"] = sourceValue.refs.at("uid");
    cloneValue.booleans = sourceValue.booleans;
    // CategoryData.Clone (saved RVA 0x2D44810) copies subcategories
    // recursively, preserving uid and sharing the original option objects.
    for (const auto child : Get(sourceValue.refs.at("subcategories")).items)
        Get(cloneValue.refs.at("subcategories")).items.push_back(CloneCategory(child));
    Get(cloneValue.refs.at("options")).items = Get(sourceValue.refs.at("options")).items;
    return clone;
}
void Select(Obj page, int id) {
    auto& value = Get(page);
    const auto& displayed = Get(value.refs.at("m_optionsDisplayed")).items;
    const auto& categories = displayed.empty() ? Get(value.refs.at("m_options")).items : displayed;
    for (auto category : categories) {
        if (Get(category).integers.at("categoryId") == id) {
            value.refs["m_currentId"] = Get(category).refs.at("uid");
            value.refs["m_displayedOption"] = category;
            return;
        }
        for (auto child : Get(Get(category).refs.at("subcategories")).items) {
            if (Get(child).integers.at("categoryId") == id) {
                value.refs["m_currentId"] = Get(child).refs.at("uid");
                value.refs["m_displayedOption"] = category;
                return;
            }
        }
    }
    throw std::runtime_error("Rebuild selected an unavailable category");
}
void NativeEnable(Obj, Method) { ++enableCalls; }
void NativeTextButton(Obj, Obj, Method) { ++foreignButtonClicks; }
// Native TextButtonOptionElement.Init binds the pooled view to its model.
void NativeTextInit(Obj element, Obj option, Method) { ++nativeTextInits; Get(element).refs["option"] = option; }
void NativeInit(Obj page, bool rebuild, bool connected, int category, Method) {
    if (rejectRebuild) throw std::runtime_error("Synthetic rebuild failure");
    auto& value = Get(page);
    ++value.integers["initCalls"];
    value.integers["lastSelection"] = category;
    value.booleans["lastRebuild"] = rebuild;
    value.booleans["lastConnected"] = connected;
    const auto displayed = Make("List");
    for (const auto source : Get(value.refs.at("m_options")).items)
        Get(displayed).items.push_back(CloneCategory(source));
    value.refs["m_optionsDisplayed"] = displayed;
    const auto grid = Get(value.refs.at("dzyi")).refs.at("grid");
    Get(grid).refs["itemsSource"] = displayed;
    Select(page, category);
}
void NativeReset(Obj category, Method) {
    auto& value = Get(category);
    ++value.integers["resetCalls"];
    for (auto child : Get(value.refs.at("subcategories")).items) NativeReset(child, nullptr);
    for (auto option : Get(value.refs.at("options")).items) {
        auto source = api.Field(option, "source");
        if (source) api.Call(source, "ResetDefault");
    }
}
void InitializeApi() {
    api.class_from_type = [](const void* type) -> Klass { return const_cast<void*>(type); };
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.class_is_valuetype = [](Klass type) {
        const auto& name = static_cast<Class*>(type)->name;
        return name == "Choice" || name == "StyleBox" || name == "Length";
    };
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.klass->name == "Int32") return &value.integer;
        if (value.klass->name == "Boolean") return &value.boolean;
        if (value.klass->name == "Choice") return &value.choice;
        if (value.klass->name == "StyleBox" || value.klass->name == "Length") return &value.number;
        throw std::runtime_error("Unexpected boxed type");
    };
    api.method_get_param = [](Method method, uint32_t index) -> const void* {
        const auto& value = *static_cast<const MethodInfo*>(method);
        Check(index == 0, "Unexpected method parameter index");
        if (value.name == "set_choices") return Type("ChoiceList");
        if (value.name == "Add" && value.klass->name == "ChoiceList") return Type("Choice");
        throw std::runtime_error("Unexpected parameter reflection");
    };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gc_wbarrier_set_field = [](Obj, void** address, Obj value) { *address = value; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        const auto handle = ++nextHandle;
        handles.emplace(handle, object);
        return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        auto found = handles.find(handle);
        Check(found != handles.end(), "Unknown or truncated GC handle");
        return found->second;
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC handle freed twice"); };
}

struct Fixture {
    Module state{L"offline-test"};
    Obj container, nativeInt, nativeBool, page, game, nativeCategory;
    Fixture() {
        module = &state;
        newSourcesLoaded = true;
        rejectAutoLoad = false;
        rejectRebuild = false;
        treasureOpens = notebookOpens = foreignButtonClicks = 0; openedUrls.clear();
        changes = 0;
        sourceModels = optionModels = uiHooks = 0;
        fakeKeyStored = failKeyInsert = failKeyRemove = failTextRead = false;
        keyForgets = textReads = nativeTextInits = inputsCreated = 0;
        submittedKeys.clear(); notices.clear(); verifiedRvas.clear();
        state.servicesReady = true;
        state.ready.store(true);
        state.settings = Config(2, 1);
        state.ui = std::make_unique<OptionsUi>();
        state.ui->metamob.submitKey = [](std::string key) { submittedKeys.push_back(std::move(key)); };
        state.ui->metamob.forgetKey = [] { ++keyForgets; };
        state.ui->metamob.keyStored = [] { return fakeKeyStored; };
        state.ui->metamob.notice = [](const std::string& text) { notices.push_back(text); };
        state.ui->Install();
        container = Make("Container");
        nativeInt = Make("IntSource");
        Get(nativeInt).integer = Get(nativeInt).defaultInteger = 7;
        nativeBool = Make("BoolSource");
        Get(nativeBool).boolean = Get(nativeBool).defaultBoolean = true;
        Get(container).refs["creaturesMode"] = nativeInt;
        Get(container).refs["creatureModeFight"] = nativeBool;
        auto cached = Make("Cached");
        Get(cached).refs["container"] = container;
        state.cached.reset(cached);
        state.creatureOption.reset(nativeBool);
        page = Make("OptionGeneralUi");
        auto& value = Get(page);
        value.refs["m_options"] = Make("List");
        game = Category("Jeu", 0);
        nativeCategory = Category("Affichage", 12);
        Get(value.refs.at("m_options")).items = {game, nativeCategory};
        value.refs["m_optionsDisplayed"] = Make("List");
        value.refs["root"] = Make("Root");
        Get(value.refs.at("root")).refs["panel"] = Make("Panel");
        value.refs["tooltip"] = Make("Tooltip");
        value.refs["dzyi"] = Make("OptionTabBinding");
        auto grid = Make("Grid");
        Get(grid).refs["itemsSource"] = value.refs.at("m_optionsDisplayed");
        Get(value.refs.at("dzyi")).refs["grid"] = grid;
        value.booleans["isPlayerConnected"] = true;
        Select(page, 12);
    }
    ~Fixture() { module = nullptr; }
    OptionsUi& Ui() { return *state.ui; }
    std::vector<Obj>& Categories() { return Get(Get(page).refs.at("m_options")).items; }
    void NativePreferencesUnchanged() {
        Check(Get(nativeInt).integer == 7 && Get(nativeInt).defaultInteger == 7, "Native integer source was modified");
        Check(Get(nativeBool).boolean && Get(nativeBool).defaultBoolean, "Native creature source was modified");
    }
    void StopAndRestore() { state.stopped.store(true); Ui().Restore(); }
};
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    // Exact contracts of the pinned snapshot verified by the options UI.
    struct Contract { uintptr_t rva; const char* owner; const char* method; int argc; };
    static const Contract contracts[] = {
        {0x2D88930, "TextButtonOptionElement", "get_m_textButtonOption", 0}, {0x53ACA50, "Application", "OpenURL", 1},
        {0x2D885A0, "TextButtonOptionElement", "Init", 1},
        {0x178DC90, "TextInput", ".ctor", 0}, {0x178DDA0, "TextInput", "get_text", 0}, {0x178EC50, "TextInput", "set_text", 1},
        {0x178EAA0, "TextInput", "set_placeholderText", 1}, {0x178E9B0, "TextInput", "set_maxLength", 1},
        {0x178E8F0, "TextInput", "set_isPassword", 1},
        {0x55F7180, "VisualElement", "get_parent", 0}, {0x55E55B0, "VisualElement", "IndexOf", 1},
        {0x55E5710, "VisualElement", "Insert", 2}, {0x55E7BD0, "VisualElement", "RemoveFromHierarchy", 0},
        {0x55F74A0, "VisualElement", "get_style", 0}};
    const auto& owner = static_cast<fake::Class*>(klass)->name;
    fake::Check(std::any_of(std::begin(contracts), std::end(contracts), [&](const Contract& contract) {
        return contract.rva == rva && owner == contract.owner && std::string(name) == contract.method && argc == contract.argc;
    }), "Wrong verified options contract");
    fake::verifiedRvas.push_back(rva);
    return Find(klass, name, argc);
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc});
    auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    const auto& name = static_cast<const fake::MethodInfo*>(method)->name;
    if (name == "OpenURL") {
        fake::Check(!self && args.size() == 1, "Static Application.OpenURL(string)");
        fake::openedUrls.push_back(fake::Get(*args.begin()).text);
        return nullptr;
    }
    return Call(self, name.c_str(), args);
}
Obj Api::Call(Obj self, const char* methodName, std::initializer_list<void*> args) const {
    using namespace fake;
    auto& value = Get(self);
    const std::string name = methodName;
    const auto* arguments = args.begin();
    if (name == "get_Count") return Box(static_cast<int>(value.items.size()));
    if (name == "get_Item") return value.items.at(*static_cast<int*>(arguments[0]));
    if (name == "Contains") return Box(std::find(value.items.begin(), value.items.end(), arguments[0]) != value.items.end());
    if (name == "Clear") { value.items.clear(); return nullptr; }
    if (name == "Add") {
        if (value.klass->name == "ChoiceList") {
            auto tuple = Make("Choice");
            Get(tuple).choice = *static_cast<Choice*>(arguments[0]);
            value.items.push_back(tuple);
        } else value.items.push_back(arguments[0]);
        return nullptr;
    }
    if (name == "Remove") {
        auto found = std::find(value.items.begin(), value.items.end(), arguments[0]);
        if (found == value.items.end()) return Box(false);
        value.items.erase(found);
        return Box(true);
    }
    if (name == "IsLoaded") return Box(value.loaded);
    if (name == "ResetDefault") {
        if (value.klass->name == "IntSource") {
            if (value.integer != value.defaultInteger) ++value.notifications;
            value.integer = value.defaultInteger;
        } else if (value.klass->name == "BoolSource") {
            if (value.boolean != value.defaultBoolean) ++value.notifications;
            value.boolean = value.defaultBoolean;
        } else throw std::runtime_error("Resetting an unexpected source type");
        return nullptr;
    }
    if (name == "get_property") return value.klass->name == "IntSource" ? Box(value.integer) : Box(value.boolean);
    if (name == "set_property" || name == "SetValueWithoutNotify") {
        if (value.klass->name == "IntSource") value.integer = *static_cast<int*>(arguments[0]);
        else if (value.klass->name == "BoolSource") value.boolean = *static_cast<bool*>(arguments[0]);
        else throw std::runtime_error("Setting an unexpected source type");
        if (name == "set_property") ++value.notifications;
        return nullptr;
    }
    if (name == "set_text" || name == "set_description" || name == "set_choices") {
        value.refs[name.substr(4)] = arguments[0];
        return nullptr;
    }
    if (name == "get_dofusOptionsContainer") return value.refs.at("container");
    if (name == "get_description") return api.Field(self, "description");
    if (name == "get_m_textButtonOption") return value.refs.at("option");
    if (name == "blrl") return value.refs.at("tooltip");
    if (name == "blqu") return value.refs.at("root");
    if (name == "get_panel") return value.refs.at("panel");
    if (name == "get_gd_category") return value.refs.at("grid");
    if (name == "get_itemsSource") return value.refs.at("itemsSource");
    if (name == "Init") {
        initHook(self, *static_cast<bool*>(arguments[0]), *static_cast<bool*>(arguments[1]), *static_cast<int*>(arguments[2]), nullptr);
        return nullptr;
    }
    if (name == "Reset") { NativeReset(self, nullptr); return nullptr; }
    // Pooled TextButtonOptionElement view: binding button and row children.
    if (name == "get_btn_button") return value.refs.at("button");
    if (name == "get_parent") { const auto found = value.refs.find("parent"); return found != value.refs.end() ? found->second : nullptr; }
    if (name == "IndexOf") {
        const auto found = std::find(value.items.begin(), value.items.end(), arguments[0]);
        return Box(found == value.items.end() ? -1 : static_cast<int>(found - value.items.begin()));
    }
    if (name == "Insert") {
        if (failKeyInsert) throw std::runtime_error("Synthetic insertion failure");
        const int index = *static_cast<int*>(arguments[0]);
        const auto child = static_cast<Obj>(arguments[1]);
        Check(index >= 0 && index <= static_cast<int>(value.items.size()) && !Get(child).refs["parent"], "Owned field inserted out of range or twice");
        value.items.insert(value.items.begin() + index, child);
        Get(child).refs["parent"] = self;
        return nullptr;
    }
    if (name == "RemoveFromHierarchy") {
        if (failKeyRemove) throw std::runtime_error("Synthetic detach failure");
        if (const auto parent = value.refs["parent"]) {
            auto& siblings = Get(parent).items;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), self), siblings.end());
        }
        value.refs["parent"] = nullptr;
        return nullptr;
    }
    // Native TextInput: password mode, bounded length, placeholder, text.
    if (name == "set_isPassword") { value.booleans["isPassword"] = *static_cast<bool*>(arguments[0]); return nullptr; }
    if (name == "set_maxLength") { value.integers["maxLength"] = *static_cast<int*>(arguments[0]); return nullptr; }
    if (name == "set_placeholderText") { value.refs["placeholder"] = arguments[0]; return nullptr; }
    if (name == "get_text") {
        ++textReads;
        const auto typed = value.refs["text"] ? Get(value.refs["text"]).text : std::string{};
        // A managed exception text can quote the field: it must never be logged.
        if (failTextRead) throw std::runtime_error("Synthetic managed exception: " + typed);
        return fake::String(typed);
    }
    if (name == "get_style") {
        auto& style = value.refs["style"];
        if (!style) { style = Make("InlineStyle"); Get(style).refs["owner"] = self; }
        return style;
    }
    const std::string stylePrefix = "UnityEngine.UIElements.IStyle.";
    if (value.klass->name == "InlineStyle" && name.rfind(stylePrefix, 0) == 0) {
        auto& owner = Get(value.refs.at("owner"));
        const auto operation = name.substr(stylePrefix.size()), property = operation.substr(4);
        if (operation.rfind("get_", 0) == 0) { const auto box = Make("StyleBox"); Get(box).number = owner.styles[property]; return box; }
        Check(operation.rfind("set_", 0) == 0, "Unexpected inline style method");
        owner.styles[property] = *static_cast<float*>(arguments[0]);
        return nullptr;
    }
    if (value.klass->name == "StyleBox" && name == "set_value") { value.number = *static_cast<float*>(arguments[0]); return nullptr; }
    throw std::runtime_error("Unmodelled managed call: " + value.klass->name + "." + name);
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fake;
    const auto& name = static_cast<fake::Class*>(klass)->name;
    const auto* arguments = args.begin();
    if (name == "CategoryData") return Category("", 0);
    auto result = Make(name);
    auto& value = Get(result);
    if (name == "IntSource") {
        ++sourceModels;
        value.integer = value.defaultInteger = *static_cast<int*>(arguments[0]);
        Check(!*static_cast<bool*>(arguments[1]), "Mod source must be writable");
        value.loaded = newSourcesLoaded;
    } else if (name == "BoolSource") {
        ++sourceModels;
        value.boolean = value.defaultBoolean = *static_cast<bool*>(arguments[0]);
        Check(!*static_cast<bool*>(arguments[1]), "Mod source must be writable");
        value.loaded = newSourcesLoaded;
    } else if (name == "MultipleChoiceOption") {
        ++optionModels;
        Check(args.size() == 5, "Wrong multiple-choice constructor arity");
        value.refs["source"] = arguments[0]; value.refs["container"] = arguments[1];
    } else if (name == "BoolOption") {
        ++optionModels;
        Check(args.size() == 3, "Wrong boolean-option constructor arity");
        value.refs["source"] = arguments[1]; value.refs["container"] = arguments[2];
    } else if (name == "TextButtonOption") {
        ++optionModels;
        Check(args.size() == 2 && !arguments[0] && arguments[1], "Wrong native text button constructor");
        value.refs["source"] = nullptr;
    } else if (name == "TextInput") {
        Check(args.size() == 0, "Native TextInput default constructor");
        ++inputsCreated;
    } else if (name == "Length") {
        Check(args.size() == 2 && *static_cast<int*>(arguments[1]) == 0, "Key field lengths are in pixels");
        value.number = *static_cast<float*>(arguments[0]);
    } else if (name == "Choice") value.choice = {*static_cast<int*>(arguments[0]), arguments[1]};
    return result;
}
Obj Api::Field(Obj self, const char* name) const {
    const auto& refs = fake::Get(self).refs;
    auto found = refs.find(name);
    return found != refs.end() ? found->second : nullptr;
}
void* Api::FieldAddress(Obj self, const char* fieldName) const {
    auto& value = fake::Get(self);
    const std::string name = fieldName;
    if (name == "categoryId" || name == "icon") return &value.integers[name];
    if (name == "isSubCategory" || name == "isPlayerConnected") return &value.booleans[name];
    if (name == "dzyi" || name == "m_binding") return value.refs.at(name); // Inline binding is its own fake object.
    return &value.refs[name];
}
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string{}; }
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Module::Change(Settings next, bool) {
    if (fake::rejectAutoLoad && next.autoLoad != settings.autoLoad)
        throw std::runtime_error("Synthetic startup registration failure");
    ++fake::changes; settings = next; ui->Sync();
}
void Log(const std::string& text) { fake::logs.push_back(text); }
void OpenTreasureOverlay(const std::filesystem::path& folder) {
    fake::Check(folder == L"offline-test", "Overlay launch used another module folder");
    ++fake::treasureOpens;
}
void OpenNotebookPanel() { ++fake::notebookOpens; }
void AddHook(managed::Method, uintptr_t rva, void* replacement, void** original) {
    ++fake::uiHooks;
    if (rva == 0x2D80470) {
        fake::enableHook = reinterpret_cast<fake::EnableFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeEnable);
    } else if (rva == 0x2D84EC0) {
        fake::initHook = reinterpret_cast<fake::InitFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeInit);
    } else if (rva == 0x2D88800) {
        fake::textButtonHook = reinterpret_cast<fake::TextButtonFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeTextButton);
    } else if (rva == 0x2D885A0) {
        fake::textInitHook = reinterpret_cast<fake::TextButtonFn>(replacement);
        *original = reinterpret_cast<void*>(fake::NativeTextInit);
    } else throw std::runtime_error("Unexpected hook in options test");
}

namespace fake {
// Every private source, in section order (the text-button rows have none).
std::vector<Obj> AllSources(OptionsUi& ui) {
    return {ui.modeSource.get(), ui.roleplayModeSource.get(), ui.animationSource.get(),
        ui.glowSource.get(), ui.glowNpcsSource.get(), ui.glowInteractivesSource.get(),
        ui.resourcesSource.get(), ui.resourcesMiniSource.get(), ui.miniCenterSource.get(),
        ui.transitionsSource.get(), ui.preloadSource.get(), ui.spouseSource.get(),
        ui.soulSource.get(), ui.soulAutoSource.get(), ui.autoLoadSource.get(), ui.githubSource.get(),
        ui.metamobSyncSource.get()};
}
// Expected row: model, private source (none for a text button), label, native
// control and the reset default of its source (choice id, or 1/0 for a checkbox).
struct ExpectedRow { Obj option, source; std::string label, control; int reset; };
struct ExpectedSection { std::string title; std::vector<ExpectedRow> rows; };
ExpectedRow ChoiceRow(const Ref& option, const Ref& source, const char* label, int reset) {
    return {option.get(), source.get(), label, "MultipleChoiceOption", reset};
}
ExpectedRow CheckRow(const Ref& option, const Ref& source, const char* label, bool reset) {
    return {option.get(), source.get(), label, "BoolOption", reset ? 1 : 0};
}
ExpectedRow ButtonRow(const Ref& option, const char* label) { return {option.get(), nullptr, label, "TextButtonOption", 0}; }
// The sections of the options tab, indexed by SectionIndex, with their rows.
std::vector<ExpectedSection> Layout(OptionsUi& ui) {
    return {
        {"Apparence et animations", {ChoiceRow(ui.modeOption, ui.modeSource, "En combat", 2),
            ChoiceRow(ui.roleplayModeOption, ui.roleplayModeSource, "Hors combat", 0),
            ChoiceRow(ui.animationOption, ui.animationSource, "Animations", 1)}},
        {"Surbrillance", {ChoiceRow(ui.glowOption, ui.glowSource, "Surbrillance", 1),
            CheckRow(ui.glowNpcsOption, ui.glowNpcsSource, "Inclure les PNJ", false),
            CheckRow(ui.glowInteractivesOption, ui.glowInteractivesSource, "Inclure les éléments interactifs", false)}},
        {"Cartes", {CheckRow(ui.resourcesOption, ui.resourcesSource, "Ressources sur la grande carte", true),
            CheckRow(ui.resourcesMiniOption, ui.resourcesMiniSource, "Ressources sur la mini-carte", true),
            CheckRow(ui.miniCenterOption, ui.miniCenterSource, "Bouton de recentrage sur la mini-carte", true)}},
        {"Déplacements", {CheckRow(ui.transitionsOption, ui.transitionsSource, "Transitions rapides entre cartes", true),
            CheckRow(ui.preloadOption, ui.preloadSource, "Précharger la prochaine carte (expérimental)", true),
            CheckRow(ui.spouseOption, ui.spouseSource, "Téléportation au conjoint", true)}},
        {"Pierre d’âme", {CheckRow(ui.soulOption, ui.soulSource, "Alerte pierre d’âme", true),
            CheckRow(ui.soulAutoOption, ui.soulAutoSource, "Équiper automatiquement la pierre d’âme", true)}},
        {"Chargement et mises à jour", {CheckRow(ui.autoLoadOption, ui.autoLoadSource, "Chargement automatique", true),
            CheckRow(ui.githubOption, ui.githubSource, "Mises à jour GitHub", true)}},
        {"Metamob", {CheckRow(ui.metamobSyncOption, ui.metamobSyncSource, "Synchroniser les captures avec Metamob", false),
            ButtonRow(ui.metamobKeyOption, "Clé API Metamob"), ButtonRow(ui.metamobForgetOption, "Oublier la clé Metamob")}},
        {"À propos", {ButtonRow(ui.creditsOption, "Dofus Enhanced 1.9.1 — par Altansar")}}};
}
std::vector<Obj> Models(const ExpectedSection& section) {
    std::vector<Obj> models;
    for (const auto& row : section.rows) models.push_back(row.option);
    return models;
}
// Every row model, in section order.
std::vector<Obj> AllOptions(OptionsUi& ui) {
    std::vector<Obj> all;
    for (const auto& section : Layout(ui))
        for (const auto& row : section.rows) all.push_back(row.option);
    return all;
}
const std::vector<Obj>& Rows(Obj category) { return Get(Get(category).refs.at("options")).items; }
const std::vector<Obj>& Sections(Obj category) { return Get(Get(category).refs.at("subcategories")).items; }
Obj Section(Obj parent, size_t index) { return Sections(parent).at(index); }
std::string Name(Obj category) { return Get(Get(category).refs.at("name")).text; }
std::string Label(Obj option) { return Get(Get(option).refs.at("text")).text; }
std::string Description(Obj option) { return Get(Get(Get(option).refs.at("description")).refs.at("text")).text; }
bool AtDefault(Obj source) {
    const auto& value = Get(source);
    return value.klass->name == "IntSource" ? value.integer == value.defaultInteger : value.boolean == value.defaultBoolean;
}
std::string Text(Obj value) { return value ? Get(value).text : std::string{}; }
std::string ButtonText(Obj option) { return Text(*static_cast<Obj*>(api.FieldAddress(option, "buttonText"))); }
// A pooled TextButtonOptionElement view: label and button in the row container
// that holds the binding's btn_button.
struct RowView { Obj element = nullptr, row = nullptr, label = nullptr, button = nullptr; };
RowView MakeRowView() {
    RowView view;
    view.element = Make("TextButtonOptionElement");
    view.row = Make("DofusVisualElement");
    view.label = Make("DofusLabel");
    view.button = Make("DofusButtonCustom");
    Get(view.row).items = {view.label, view.button};
    Get(view.label).refs["parent"] = Get(view.button).refs["parent"] = view.row;
    const auto binding = Make("TextButtonOptionElementBinding");
    Get(binding).refs["button"] = view.button;
    Get(view.element).refs["m_binding"] = binding;
    return view;
}
std::vector<Obj> Inputs(const RowView& view) {
    std::vector<Obj> inputs;
    for (const auto child : Get(view.row).items)
        if (Get(child).klass->name == "TextInput") inputs.push_back(child);
    return inputs;
}
std::string Placeholder(Obj input) { return Text(Get(input).refs["placeholder"]); }
std::string Typed(Obj input) { return Text(Get(input).refs["text"]); }
void Paste(Obj input, const std::string& typed) { Get(input).refs["text"] = String(typed); }
void Click(const RowView& view) { textButtonHook(view.element, Make("Visual"), nullptr); }
// Any five consecutive characters of the fake key count as a leak.
bool Leaks(const std::string& text) {
    const std::string key = FakeKey;
    for (size_t start = 0; start + 5 <= key.size(); ++start)
        if (text.find(key.substr(start, 5)) != std::string::npos) return true;
    return false;
}
void CheckNoKeyTrace() {
    for (const auto& line : logs) Check(!Leaks(line), "A fragment of the Metamob key reached the module log");
    for (const auto& line : notices) Check(!Leaks(line), "A fragment of the Metamob key reached a chat notice");
}
bool Logged(size_t from, const std::string& line) { return std::find(logs.begin() + static_cast<std::ptrdiff_t>(from), logs.end(), line) != logs.end(); }
void CheckLayout(Fixture& f) {
    const auto parent = f.Categories().back();
    Check(Name(parent) == ModCategoryTitle, "Loaded revision missing from category");
    Check(std::string(ModCategoryTitle) == "Dofus Enhanced" && std::string(ModSemanticVersion) == "1.9.1" && std::string(ModVersion) == "r107",
        "Tab name carries no revision; version is x.y.z");
    const auto layout = Layout(f.Ui());
    Check(!Get(parent).booleans["isSubCategory"] && Rows(parent).empty() && Sections(parent).size() == SectionCount && layout.size() == SectionCount,
        "Mod must hold its eight native sections, from Apparence et animations to À propos, and no row of its own");
    std::vector<Obj> shownSources;
    for (size_t i = 0; i != SectionCount; ++i) {
        const auto section = Section(parent, i);
        Check(Get(section).booleans["isSubCategory"] && Get(section).integers.at("categoryId") == SectionId(i) &&
            Name(section) == layout[i].title, "A native section is missing, or its id, title or position changed");
        Check(Sections(section).empty(), "No nested settings sections expected");
        Check(Rows(section) == Models(layout[i]), "Each section must contain exactly its rows in the requested order");
        for (const auto& row : layout[i].rows) {
            const auto& option = Get(row.option);
            Check(Label(row.option) == row.label, "Option label or ordering changed");
            Check(option.klass->name == row.control, "Settings must use native controls");
            Check(option.refs.at("source") == row.source && (!row.source ||
                Get(row.source).klass->name == (row.control == "MultipleChoiceOption" ? "IntSource" : "BoolSource")),
                "Each row keeps its own private source");
            if (row.source) shownSources.push_back(row.source);
            if (row.control != "MultipleChoiceOption") continue;
            const auto& choices = Get(option.refs.at("choices")).items;
            Check(choices.size() >= 2 && choices.size() <= 4, "Unexpected percentage selector remains");
            for (const auto choice : choices)
                Check(Get(choice).choice.id >= 0 && Get(choice).choice.id <= 3 &&
                    Get(Get(choice).choice.label).text.find('%') == std::string::npos,
                    "A percentage or minimum choice remains in native search models");
        }
    }
    Check(shownSources == AllSources(f.Ui()), "Every private source must be shown once, by its own row");
    // « Metamob »: the sync preference, then the key and forget text buttons.
    const auto& metamob = Rows(Section(parent, MetamobSection));
    const auto sync = metamob.at(0), key = metamob.at(1), forget = metamob.at(2);
    Check(Description(sync) == SyncDescription, "The sync row must say what it changes, what it needs and its default");
    Check(Description(key) == KeyDescription, "The key row must explain the masked field, the check and the storage");
    Check(Description(forget).find("5 secondes") != std::string::npos, "The forget row must explain its confirmation");
    Check(ButtonText(key) == "Enregistrer" && Get(key).integers.at("icon") == 148 && ButtonText(forget) == "Supprimer" && Get(forget).integers.at("icon") == 316,
        "The key rows use native floppy and trash buttons");
    Check(!Get(key).refs.at("source") && !Get(forget).refs.at("source"), "The key rows have no source: a native reset never reaches the key");
    Check(f.Ui().metamobKeyMarker.get() == Get(key).refs.at("description") && f.Ui().metamobForgetMarker.get() == Get(forget).refs.at("description") &&
        f.Ui().metamobKeyMarker.get() != f.Ui().metamobForgetMarker.get() && f.Ui().metamobKeyMarker.get() != f.Ui().creditsMarker.get(),
        "Each owned text-button row is known by its own description reference");
    // « À propos »: the credits row alone, named after the x.y.z version.
    const auto credits = Rows(Section(parent, AboutSection)).at(0);
    const auto description = Description(credits);
    Check(Label(credits).find("1.9.1") != std::string::npos && description.find("Altansar") != std::string::npos &&
        description.find("https://github.com/Altansar69/dofus-miniatures") != std::string::npos && description.find("r107") != std::string::npos,
        "Credits must name the version, the author, the project page and the revision");
    Check(ButtonText(credits) == "Ouvrir GitHub" && f.Ui().creditsMarker.get() == Get(credits).refs.at("description"),
        "Credits button opens the project page");
    Check(sourceModels == 17 && optionModels == 20,
        "The UI must create seventeen private sources and twenty rows");
    Check(uiHooks == 4, "Only native page callbacks, the text button action and its view Init must be hooked");
    for (const uintptr_t rva : {uintptr_t{0x2D885A0}, uintptr_t{0x178E8F0}, uintptr_t{0x55E5710}, uintptr_t{0x55E7BD0}})
        Check(std::find(verifiedRvas.begin(), verifiedRvas.end(), rva) != verifiedRvas.end(), "The view Init and the masked field calls must be exact verified contracts");
    const auto& displayed = Get(Get(f.page).refs.at("m_optionsDisplayed")).items;
    if (!displayed.empty()) {
        const auto clone = displayed.back();
        Check(clone != parent && Get(clone).refs.at("uid") == Get(parent).refs.at("uid"),
            "Native display must use an independent category clone with shared identity");
        Check(Rows(clone).empty() && Sections(clone).size() == SectionCount, "Native display/search clone lost a section");
        for (size_t i = 0; i != SectionCount; ++i) {
            const auto original = Section(parent, i), copy = Section(clone, i);
            Check(copy != original && Get(copy).refs.at("uid") == Get(original).refs.at("uid") &&
                Get(copy).integers.at("categoryId") == SectionId(i) && Name(copy) == Name(original) && Rows(copy) == Rows(original),
                "Native display/search clone lost a section or its rows");
        }
    }
}
void ParentAndSectionSelection() {
    Fixture f;
    initHook(f.page, false, true, ModId, nullptr);
    CheckLayout(f);
    const auto displayed = Get(f.page).refs.at("m_displayedOption");
    const auto layout = Layout(f.Ui());
    for (size_t i = 0; i != SectionCount; ++i) {
        Check(Rows(Section(displayed, i)) == Models(layout[i]), "Displayed parent clone lost its controls");
        Select(f.page, SectionId(i));
        Check(Get(f.page).refs.at("m_currentId") == Get(Section(displayed, i)).refs.at("uid") && Get(f.page).refs.at("m_displayedOption") == displayed,
            "Each section selection must resolve to its own stable UID");
    }
    const auto savedSources = AllSources(f.Ui());
    initHook(f.page, false, true, DisplayId, nullptr);
    Check(Get(f.page).integers["lastSelection"] == DisplayId && AllSources(f.Ui()) == savedSources,
        "Reopening Apparence et animations must retain the selection and existing controls");
    CheckLayout(f);
    f.StopAndRestore();
    Check(Get(f.page).integers["lastSelection"] == 0 &&
        f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}),
        "Restore from the Apparence et animations child must select Jeu and remove the owned parent");
}

// Each section, from Apparence et animations to À propos, opens on its own rows and stable
// UID and keeps its controls when reopened; Restore from it selects Jeu.
void EverySectionSelectionAndRestore() {
    for (size_t index = 0; index != SectionCount; ++index) {
        Fixture f;
        const int id = SectionId(index);
        initHook(f.page, false, true, id, nullptr);
        CheckLayout(f);
        const auto displayed = Get(f.page).refs.at("m_displayedOption");
        Check(Get(f.page).integers["lastSelection"] == id &&
            Get(f.page).refs.at("m_currentId") == Get(Section(displayed, index)).refs.at("uid") &&
            Rows(Section(displayed, index)) == Models(Layout(f.Ui())[index]), "A section opens on its own rows and stable UID");
        const auto savedSources = AllSources(f.Ui());
        initHook(f.page, false, true, id, nullptr);
        Check(Get(f.page).integers["lastSelection"] == id && AllSources(f.Ui()) == savedSources && f.Categories().size() == 3,
            "Reopening a section must retain the selection and existing controls");
        f.StopAndRestore();
        Check(Get(f.page).integers["lastSelection"] == 0 && f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) && f.Ui().pages.empty(),
            "Restore from any section must select Jeu and remove the owned parent");
    }
}

// A failed rebuild keeps the page owned so that the next pump retries. The
// retry then selects Jeu from a mod section, or keeps a native selection.
void RestoreRetriesAfterFailedRebuild() {
    for (const int id : {SectionId(AboutSection), 12}) {
        Fixture f;
        initHook(f.page, false, true, id, nullptr);
        CheckLayout(f);
        rejectRebuild = true;
        bool failed = false;
        try { f.StopAndRestore(); } catch (const std::exception&) { failed = true; }
        Check(failed && f.Ui().CleanupPending() && f.Ui().pages.size() == 1 && f.Ui().pages.front().owner.get() == f.page &&
            f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) && f.Ui().modeSource.get() && f.Ui().creditsOption.get(),
            "Failed rebuild must retain ownership until retry");
        rejectRebuild = false;
        f.Ui().Restore();
        Check(!f.Ui().CleanupPending() && Get(f.page).integers["lastSelection"] == (id == 12 ? 12 : 0) &&
            f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) && !f.Ui().modeSource.get() && !f.Ui().creditsOption.get(),
            "Retried restore must release the page and select Jeu, or keep the native selection");
    }
}

void CheckModeChoices(Obj option) {
    const auto& choices = Get(Get(option).refs.at("choices")).items;
    const char* labels[] = {"Normal", "Créatures", "Miniatures"};
    Check(choices.size() == 3, "Both contexts need all three display modes");
    for (size_t i = 0; i != 3; ++i)
        Check(Get(choices[i]).choice.id == static_cast<int>(i) && Get(Get(choices[i]).choice.label).text == labels[i],
            "Display mode IDs or labels changed");
}

void RegistrationAndValues() {
    Fixture f;
    f.state.settings.roleplayMode = 1;
    f.state.settings.autoLoad = true;
    f.state.settings.soulReminder = false;
    f.state.settings.soulAutoEquip = false;
    f.state.settings.monsterGlowMode = 2;
    f.state.settings.fastMapTransitions = true;
    f.state.settings.preloadNextMap = true;
    f.state.settings.spouseTeleport = false;
    f.state.settings.metamobSync = true;
    initHook(f.page, false, true, 12, nullptr);
    Check(f.Categories().size() == 3 && f.Ui().pages.size() == 1 &&
        Get(f.page).booleans["lastRebuild"], "Init must append Mod and rebuild its native summary once");
    CheckLayout(f);
    Check(!f.Ui().Attach(f.page) && f.Categories().size() == 3, "Repeated attachment must be idempotent");
    CheckModeChoices(f.Ui().modeOption.get());
    CheckModeChoices(f.Ui().roleplayModeOption.get());
    for (const auto& section : Layout(f.Ui()))
        for (const auto& row : section.rows) {
            if (!row.source) continue;
            const auto& source = Get(row.source);
            Check(row.source != f.nativeInt && row.source != f.nativeBool, "Mod must use independent sources");
            Check(source.klass->name == "IntSource" ? source.defaultInteger == row.reset : source.defaultBoolean == (row.reset != 0),
                "Persisted settings must not replace native reset defaults");
        }
    const auto& sync = Get(f.Ui().metamobSyncSource.get());
    Check(f.Ui().metamobSyncSource.get() != f.nativeBool && sync.klass->name == "BoolSource" && sync.boolean && !sync.defaultBoolean,
        "Metamob sync shows its saved value on its own source and resets to off");
    Check(Get(f.Ui().modeSource.get()).integer == 2 && Get(f.Ui().roleplayModeSource.get()).integer == 1 &&
        Get(f.Ui().animationSource.get()).integer == 1 &&
        Get(f.Ui().autoLoadSource.get()).boolean && !Get(f.Ui().soulSource.get()).boolean && !Get(f.Ui().soulAutoSource.get()).boolean && Get(f.Ui().glowSource.get()).integer == 2 && Get(f.Ui().transitionsSource.get()).boolean && Get(f.Ui().preloadSource.get()).boolean && !Get(f.Ui().spouseSource.get()).boolean, "Saved settings were not applied to the eleven native controls");
    Get(f.Ui().modeSource.get()).integer = 1;
    Get(f.Ui().roleplayModeSource.get()).integer = 2;
    Get(f.Ui().animationSource.get()).integer = 3;
    Get(f.Ui().autoLoadSource.get()).boolean = false;
    Get(f.Ui().githubSource.get()).boolean = false;
    Get(f.Ui().soulSource.get()).boolean = true;
    Get(f.Ui().soulAutoSource.get()).boolean = true;
    Get(f.Ui().glowSource.get()).integer = 0;
    Get(f.Ui().transitionsSource.get()).boolean = false;
    Get(f.Ui().preloadSource.get()).boolean = false;
    Get(f.Ui().spouseSource.get()).boolean = true;
    Get(f.Ui().metamobSyncSource.get()).boolean = false;
    f.Ui().Tick();
    Check(changes == 1 && f.state.settings.mode == 1 && f.state.settings.roleplayMode == 2 &&
        f.state.settings.animationMode == 3 && !f.state.settings.autoLoad && !f.state.settings.githubUpdates && f.state.settings.soulReminder && f.state.settings.soulAutoEquip && f.state.settings.monsterGlowMode == 0 && !f.state.settings.fastMapTransitions && !f.state.settings.preloadNextMap && f.state.settings.spouseTeleport && !f.state.settings.metamobSync,
        "Ten native controls failed to commit independent settings together");
    f.NativePreferencesUnchanged();
    Select(f.page, 12);
    f.StopAndRestore();
    Check(f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) &&
        Get(f.page).integers["lastSelection"] == 12, "Restore must retain the selected native category");
    for (auto source : AllSources(f.Ui())) Check(!source, "Restore retained a source reference");
    for (auto option : AllOptions(f.Ui())) Check(!option, "Restore retained an option reference");
    Check(f.Ui().pages.empty() && !f.Ui().container.get(), "Restore retained owned category/container references");
    Check(!f.Ui().metamobKeyMarker.get() && !f.Ui().metamobForgetMarker.get() && f.Ui().keyFields.empty(), "Restore retained a Metamob row reference");
}

void AnimationModeValuesAndDefaults() {
    Fixture f;
    f.state.settings.animationMode = 3;
    initHook(f.page, false, true, ModId, nullptr);
    auto& source = Get(f.Ui().animationSource.get());
    Check(source.klass->name == "IntSource" && source.integer == 3 && source.defaultInteger == 1,
        "Persisted instant mode must keep the shortened reset default");
    const auto& choices = Get(Get(f.Ui().animationOption.get()).refs.at("choices")).items;
    const char* labels[] = {"Normal", "Raccourcies", "Sans animations des combattants (expérimental)", "Instantané (expérimental)"};
    Check(choices.size() == 4, "Simplification must retain all four animation modes");
    for (size_t i = 0; i != 4; ++i)
        Check(Get(choices[i]).choice.id == static_cast<int>(i) && Get(Get(choices[i]).choice.label).text == labels[i],
            "Animation choices lost stable IDs or labels");
    const auto description = Description(f.Ui().animationOption.get());
    for (const char* limit : {"expérimentaux", "déplacements", "attentes visuelles", "délais serveur", "certaines attentes", "ne supprime pas toutes"})
        Check(description.find(limit) != std::string::npos, "Animation tooltip must retain practical limits");
    for (int mode : {0, 1, 2, 3}) {
        source.integer = mode;
        f.Ui().Tick();
        Check(f.state.settings.animationMode == mode, "Animation transition was not committed");
    }
    Check(changes == 4, "Animation transitions must commit once each");
    api.Call(f.Categories().back(), "Reset");
    Check(source.integer == 1 && Get(f.Categories().back()).integers["resetCalls"] == 1,
        "Native reset must restore shortened animations");
    f.Ui().Tick();
    Check(f.state.settings.animationMode == 1, "Reset animation choice was not persisted");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(Get(f.page).integers["lastSelection"] == 0, "Restore from Mod must select Jeu");
}

void GlowModeValuesAndDefaults() {
    Fixture f;
    f.state.settings.monsterGlowMode = 3;
    initHook(f.page, false, true, ModId, nullptr);
    auto& source = Get(f.Ui().glowSource.get());
    Check(source.klass->name == "IntSource" && source.integer == 3 && source.defaultInteger == 1,
        "Saved always-glow must use native integer source and preserve UI-only reset default");
    const auto& choices = Get(Get(f.Ui().glowOption.get()).refs.at("choices")).items;
    Check(choices.size() == GlowModeLabels.size(), "Glow must expose off and all three levels");
    for (size_t index = 0; index < choices.size(); ++index)
        Check(Get(choices[index]).choice.id == static_cast<int>(index) &&
            Get(Get(choices[index]).choice.label).text == GlowModeLabels[index], "Glow mode IDs and labels must match context menu");
    const auto description = Description(f.Ui().glowOption.get());
    for (const char* term : {"grande carte", "inventaire", "sorts", "tous les modes"})
        Check(description.find(term) != std::string::npos, "Glow description must preserve foreground window exclusions");
    auto expected = f.state.settings;
    for (int mode : {0, 1, 2, 3}) {
        source.integer = mode;
        f.Ui().Tick();
        expected.monsterGlowMode = mode;
        Check(f.state.settings == expected, "Glow dropdown must change only its own preference");
    }
    Check(changes == 4, "Each changed glow selection commits exactly once");
    f.state.settings.monsterGlowMode = 2;
    f.Ui().Sync();
    Check(source.integer == 2, "External context choice must update the native dropdown");
    const auto filtered = CloneCategory(Section(f.Categories().back(), GlowSection));
    Get(Get(filtered).refs.at("options")).items = {f.Ui().glowOption.get()};
    api.Call(filtered, "Reset");
    Check(source.integer == 1 && f.state.settings.monsterGlowMode == 2,
        "Native filtered reset restores UI default and leaves persistence to the normal pump");
    f.Ui().Tick();
    expected.monsterGlowMode = 1;
    Check(f.state.settings == expected, "Filtered glow reset must preserve unrelated preferences");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
}

void GlowCategories() {
    Fixture f; f.state.settings.glowNpcs=true;
    initHook(f.page, false, true, ModId, nullptr);
    auto& npc=Get(f.Ui().glowNpcsSource.get()); auto& interactive=Get(f.Ui().glowInteractivesSource.get());
    Check(npc.boolean && !npc.defaultBoolean && !interactive.boolean && !interactive.defaultBoolean,"Independent category preferences and opt-in defaults");
    auto expected=f.state.settings; interactive.boolean=true; f.Ui().Tick(); expected.glowInteractives=true;
    Check(f.state.settings==expected,"Interactive option must preserve mode and NPC preference");
    npc.boolean=false; f.Ui().Tick(); expected.glowNpcs=false;
    Check(f.state.settings==expected,"NPC option must preserve interactive preference");
    f.state.settings.glowNpcs=true; f.state.settings.glowInteractives=false; f.Ui().Sync();
    Check(npc.boolean && !interactive.boolean,"External preferences update both category checkboxes");
    f.StopAndRestore();
}
void ReducedMiniaturesDescription() {
    Fixture f;
    initHook(f.page, false, true, ModId, nullptr);
    CheckLayout(f);
    for (auto option : {f.Ui().modeOption.get(), f.Ui().roleplayModeOption.get()}) {
        const auto text=Description(option);
        Check(text.find("60 %")!=std::string::npos && text.find("50 %")!=std::string::npos,
            "Both display contexts must explain reduction and the measured monster floor");
    }
    for (auto option : AllOptions(f.Ui())) {
        Check(Label(option)!="Miniatures" && Description(option).find("Uniformiser")==std::string::npos,
            "Removed size selector must not remain in native category or search models");
    }
    f.StopAndRestore();
}

void NativeResetAndFilteredSearchOwnership() {
    Fixture f;
    f.state.settings.roleplayMode = 2;
    f.state.settings.animationMode = 3;
    f.state.settings.autoLoad = true;
    initHook(f.page, false, true, ModId, nullptr);
    f.state.settings.spouseTeleport = false;
    f.Ui().Sync();
    const auto beforeReset = f.state.settings;
    const auto nativeSource = Make("IntSource");
    Get(nativeSource).integer = 90;
    Get(nativeSource).defaultInteger = 40;
    const auto nativeOption = Make("MultipleChoiceOption");
    Get(nativeOption).refs["source"] = nativeSource;
    const auto sameIdForeign = Category("Other extension", ModId);
    Get(Get(sameIdForeign).refs.at("options")).items.push_back(nativeOption);
    api.Call(sameIdForeign, "Reset");
    Check(Get(sameIdForeign).integers["resetCalls"] == 1 && Get(nativeSource).integer == 40,
        "Foreign category must retain original native reset behavior");
    api.Call(f.nativeCategory, "Reset");
    Check(f.state.settings == beforeReset && Get(f.Ui().animationSource.get()).integer == 3,
        "Unrelated native reset must not affect Mod models");
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    const auto searchCategory = CloneCategory(Section(displayedParent, DisplaySection));
    Get(searchCategory).integers["categoryId"] = 999;
    Get(Get(searchCategory).refs.at("options")).items = {f.Ui().animationOption.get()};
    const auto rebuilds = Get(f.page).integers["initCalls"];
    api.Call(searchCategory, "Reset");
    Check(Get(searchCategory).integers["resetCalls"] == 1 &&
        Get(f.Ui().animationSource.get()).integer == 1 &&
        Get(f.Ui().modeSource.get()).integer == 2 &&
        Get(f.Ui().roleplayModeSource.get()).integer == 2 &&
        Get(f.Ui().autoLoadSource.get()).boolean && !Get(f.Ui().spouseSource.get()).boolean,
        "Filtered native reset must touch only its contained option, not the other Mod settings");
    Check(f.state.settings == beforeReset, "Native reset must defer persistence until the normal pump");
    f.Ui().Tick();
    auto resetMethodOnly = beforeReset;
    resetMethodOnly.animationMode = 1;
    Check(f.state.settings == resetMethodOnly && Get(f.page).integers["initCalls"] == rebuilds,
        "Filtered reset must persist only its row without rebuilding native categories");
    api.Call(displayedParent, "Reset");
    for (const auto source : AllSources(f.Ui()))
        Check(AtDefault(source), "Whole Mod native reset must restore every default");
    f.Ui().Tick();
    Check(f.state.settings == Config(2, 1) && Get(f.page).integers["initCalls"] == rebuilds,
        "Whole Mod reset must commit all defaults without a custom reset hook or rebuild");
    CheckLayout(f);
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
}

// Every preference shown in the tab, away from its reset default.
Settings AwayFromDefaults() {
    Settings settings = Config(1, 3);
    settings.roleplayMode = 1;
    settings.monsterGlowMode = 3;
    settings.glowNpcs = settings.glowInteractives = true;
    settings.resourceMapEnabled = settings.resourceMiniMapEnabled = settings.miniMapCenterButton = false;
    settings.fastMapTransitions = settings.preloadNextMap = settings.spouseTeleport = false;
    settings.soulReminder = settings.soulAutoEquip = false;
    settings.autoLoad = settings.githubUpdates = false;
    settings.metamobSync = true;
    return settings;
}
// The preferences behind one section's rows, back to their defaults.
Settings WithSectionDefaults(Settings settings, size_t index) {
    const Settings defaults;
    if (index == DisplaySection) {
        settings.mode = defaults.mode; settings.roleplayMode = defaults.roleplayMode; settings.animationMode = defaults.animationMode;
    } else if (index == GlowSection) {
        settings.monsterGlowMode = defaults.monsterGlowMode; settings.glowNpcs = defaults.glowNpcs; settings.glowInteractives = defaults.glowInteractives;
    } else if (index == MapsSection) {
        settings.resourceMapEnabled = defaults.resourceMapEnabled; settings.resourceMiniMapEnabled = defaults.resourceMiniMapEnabled;
        settings.miniMapCenterButton = defaults.miniMapCenterButton;
    } else if (index == TravelSection) {
        settings.fastMapTransitions = defaults.fastMapTransitions; settings.preloadNextMap = defaults.preloadNextMap; settings.spouseTeleport = defaults.spouseTeleport;
    } else if (index == SoulSection) {
        settings.soulReminder = defaults.soulReminder; settings.soulAutoEquip = defaults.soulAutoEquip;
    } else if (index == LoadingSection) {
        settings.autoLoad = defaults.autoLoad; settings.githubUpdates = defaults.githubUpdates;
    } else if (index == MetamobSection) settings.metamobSync = defaults.metamobSync;
    return settings;
}
// Resetting one section (its displayed clone) restores its own rows only; the
// pump then persists exactly those preferences, once and without a rebuild.
void SectionResetOwnsItsRows() {
    for (size_t index = 0; index != SectionCount; ++index) {
        Fixture f;
        f.state.settings = AwayFromDefaults();
        initHook(f.page, false, true, SectionId(index), nullptr);
        const auto before = f.state.settings;
        for (const auto source : AllSources(f.Ui())) Check(!AtDefault(source), "Every row starts away from its reset default");
        const auto layout = Layout(f.Ui());
        std::vector<Obj> own;
        for (const auto& row : layout[index].rows)
            if (row.source) own.push_back(row.source);
        const auto onlyOwnRowsAtDefault = [&] {
            for (const auto source : AllSources(f.Ui()))
                if (AtDefault(source) != (std::find(own.begin(), own.end(), source) != own.end())) return false;
            return true;
        };
        const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
        const auto section = Section(displayedParent, index);
        const auto rebuilds = Get(f.page).integers["initCalls"];
        api.Call(section, "Reset");
        Check(Get(section).integers["resetCalls"] == 1 && onlyOwnRowsAtDefault() && f.state.settings == before,
            "A section reset restores its own rows only and leaves persistence to the normal pump");
        f.Ui().Tick();
        Check(onlyOwnRowsAtDefault() && f.state.settings == WithSectionDefaults(before, index) &&
            changes == (own.empty() ? 0u : 1u) && Get(f.page).integers["initCalls"] == rebuilds,
            "The pump persists only the reset section's preferences, once and without a rebuild");
        f.NativePreferencesUnchanged();
        f.StopAndRestore();
    }
}

void ResourceMapMasterPreservesSelection() {
    Fixture f;
    f.state.settings.resourceMapItems = {303, 421};
    f.state.settings.resourceMapEnabled = false;
    Check(f.Ui().Attach(f.page), "Resource option fixture failed to attach");
    Check(!Get(f.Ui().resourcesSource.get()).boolean, "Saved resource opt-out must reach native checkbox");
    const auto before = f.state.settings;
    Get(f.Ui().resourcesSource.get()).boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.resourceMapEnabled && f.state.settings.resourceMapItems == before.resourceMapItems,
        "Enabling annotations must preserve multiple resource selections");
    Get(f.Ui().resourcesSource.get()).boolean = false;
    f.Ui().Tick();
    Check(f.state.settings == before, "Disabling annotations must not clear selections or unrelated settings");
    Get(f.Ui().resourcesMiniSource.get()).boolean = false;
    f.Ui().Tick();
    Check(!f.state.settings.resourceMiniMapEnabled && !f.state.settings.resourceMapEnabled && f.state.settings.resourceMapItems == before.resourceMapItems,
        "Mini-map visibility is independent and preserves selected resources");
    Get(f.Ui().resourcesSource.get()).boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.resourceMapEnabled && !f.state.settings.resourceMiniMapEnabled,
        "Enabling full map must not enable mini-map");
    f.StopAndRestore();
}

void ValidationAndLoadingGate() {
    Fixture f;
    Check(f.Ui().Attach(f.page), "Validation fixture failed to attach");
    for (auto source : AllSources(f.Ui())) {
        Get(source).loaded = false;
        Get(f.Ui().modeSource.get()).integer = 1;
        f.Ui().Tick();
        f.Ui().Sync();
        Check(changes == 0 && Get(f.Ui().modeSource.get()).integer == 1, "Unloaded models must gate polling and Sync");
        Get(source).loaded = true;
        f.Ui().Sync();
    }
    for (int invalid : {-1, 3, 99}) {
        for (const auto source : {f.Ui().modeSource.get(), f.Ui().roleplayModeSource.get()}) {
            Get(source).integer = invalid;
            f.Ui().Tick();
            Check(changes == 0 && Get(f.Ui().modeSource.get()).integer == 2 &&
                Get(f.Ui().roleplayModeSource.get()).integer == 0, "Invalid context mode must revert");
        }
    }
    for (int invalid : {-1, 4, 99}) {
        Get(f.Ui().animationSource.get()).integer = invalid;
        f.Ui().Tick();
        Check(changes == 0 && Get(f.Ui().animationSource.get()).integer == 1, "Invalid animation mode must revert");
        Get(f.Ui().glowSource.get()).integer = invalid;
        f.Ui().Tick();
        Check(changes == 0 && Get(f.Ui().glowSource.get()).integer == 1, "Invalid glow mode must revert without persistence");
    }
    CheckLayout(f);
    f.StopAndRestore();
}

void AutoLoadFailureIsRecoverable() {
    Fixture f;
    f.state.settings.autoLoad = false;
    Check(f.Ui().Attach(f.page), "Auto-load fixture failed to attach");
    rejectAutoLoad = true;
    Get(f.Ui().autoLoadSource.get()).boolean = true;
    f.Ui().Tick();
    Check(!f.state.settings.autoLoad && !Get(f.Ui().autoLoadSource.get()).boolean && changes == 0,
        "Failed startup registration must restore the checkbox");
    Check(!f.state.stopRequested.load() && !f.state.stopped.load(), "Recoverable startup failure must not stop the module");
    Check(!logs.empty() && logs.back().rfind("UI_CHANGE_REJECTED autoLoad:", 0) == 0, "Startup failure must be logged");
    rejectAutoLoad = false;
    Get(f.Ui().autoLoadSource.get()).boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.autoLoad && changes == 1, "Checkbox must remain usable after failure");
    Get(f.Ui().autoLoadSource.get()).boolean = false;
    Get(f.Ui().githubSource.get()).boolean = false;
    f.Ui().Tick();
    Check(!f.state.settings.autoLoad, "Manual auto-load opt-out stays usable");
    api.Call(f.Categories().back(), "Reset");
    f.Ui().Tick();
    Check(f.state.settings.autoLoad, "Whole Mod reset must enable auto-load");
    f.StopAndRestore();
}

void DeferredAndAccountGate() {
    Fixture f;
    newSourcesLoaded = false;
    Check(!f.Ui().Attach(f.page) && f.Categories().size() == 2, "Unloaded account models must block registration");
    Check(!f.Ui().container.get() && !f.Ui().modeSource.get(), "Unloaded models must not be retained");
    newSourcesLoaded = true;
    enableHook(f.page, nullptr);
    Check(enableCalls != 0, "OnEnable must call its original method");
    f.Ui().Tick();
    Check(f.Categories().size() == 2, "UI registered before native list settled");
    Select(f.page, 0);
    f.Ui().Tick();
    Check(f.Categories().size() == 2, "Selection change must restart stability wait");
    f.Ui().Tick();
    Check(f.Categories().size() == 3 && Get(f.page).integers["lastSelection"] == 0, "Settled UI failed to preserve selection");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
}

void CollisionAndSessionMismatch() {
    Fixture f;
    auto foreign = Category("Other extension", ModId);
    f.Categories().push_back(foreign);
    bool rejected = false;
    try { f.Ui().Attach(f.page); }
    catch (const std::exception&) { rejected = true; }
    Check(rejected && f.Categories().size() == 3 && f.Ui().pages.empty(), "Category collision must be rejected without ownership");
    f.StopAndRestore();
    Check(f.Categories().back() == foreign, "Restore removed an unowned category");
    f.state.stopped.store(false);
    Get(f.container).refs["creatureModeFight"] = Make("BoolSource");
    Check(!f.Ui().Attach(f.page), "Changed account preference owner must block registration");
}

void CreditsLinkOpensProject() {
    Fixture f;
    initHook(f.page, false, true, ModId, nullptr);
    auto element = Make("TextButtonOptionElement");
    Get(element).refs["option"] = f.Ui().creditsOption.get();
    textButtonHook(element, Make("Visual"), nullptr);
    Check(openedUrls == std::vector<std::string>{"https://github.com/Altansar69/dofus-miniatures"} && foreignButtonClicks == 0,
        "Credits button opens the project page without running the null native action");
    auto foreign = Make("TextButtonOptionElement"), other = Make("TextButtonOption");
    Get(other).refs["description"] = Make("Description");
    Get(foreign).refs["option"] = other;
    textButtonHook(foreign, Make("Visual"), nullptr);
    Check(foreignButtonClicks == 1 && openedUrls.size() == 1, "Foreign text buttons keep their native action");
    f.state.stopRequested.store(true);
    textButtonHook(element, Make("Visual"), nullptr);
    Check(openedUrls.size() == 1 && foreignButtonClicks == 1, "A stopping module ignores the credits button without falling through");
    f.state.stopRequested.store(false);
    f.StopAndRestore();
    Check(!f.Ui().creditsOption.get() && !f.Ui().creditsMarker.get(), "Restore releases the credits row");
    textButtonHook(element, Make("Visual"), nullptr);
    Check(openedUrls.size() == 1 && foreignButtonClicks == 2, "After restore the stale element runs the native action");
}

// Row views are pooled: the masked field follows the key row from view to view.
void MetamobKeyFieldFollowsPooledViews() {
    Fixture f;
    initHook(f.page, false, true, MetamobId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    auto view = MakeRowView();
    textInitHook(view.element, keyModel, nullptr);
    const auto inputs = Inputs(view);
    Check(nativeTextInits == 1 && Get(view.element).refs.at("option") == keyModel, "The view's native Init runs first, unchanged");
    Check(inputs.size() == 1 && inputsCreated == 1 && Get(view.row).items == std::vector<Obj>({view.label, inputs[0], view.button}),
        "One owned input sits between the row label and its button");
    const auto field = inputs[0];
    Check(Get(field).booleans.at("isPassword") && Get(field).integers.at("maxLength") == 256 && Placeholder(field) == KeyHint,
        "The key field is the native masked input, bounded, with the empty-key hint");
    Check(Get(field).styles.at("flexGrow") == 1.f && Get(field).styles.at("minWidth") == 0.f && Get(field).styles.at("minHeight") == 32.f,
        "The key field takes the free width of the row, like the notebook inputs");
    // Re-Init of the same view, also with the native search clone of the row:
    // still one field and no new GC reference.
    const auto references = handles.size();
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(keyModel).refs.at("description");
    for (const auto model : {keyModel, clone, keyModel}) textInitHook(view.element, model, nullptr);
    Check(Inputs(view) == inputs && inputsCreated == 1 && f.Ui().keyFields.size() == 1 && handles.size() == references,
        "Re-Init keeps exactly one field without new references");
    // A key saved meanwhile: a second view shows the stored-key hint, Tick updates the first.
    fakeKeyStored = true;
    auto second = MakeRowView();
    textInitHook(second.element, clone, nullptr);
    Check(Inputs(second).size() == 1 && Placeholder(Inputs(second)[0]) == StoredKeyHint && f.Ui().keyFields.size() == 2,
        "A search clone of the key row gets its own field with the stored-key hint");
    f.Ui().nextKeyFieldRefresh = 0;
    f.Ui().Tick();
    Check(Placeholder(field) == StoredKeyHint, "A key saved while the row is shown changes its hint");
    fakeKeyStored = false;
    f.Ui().Tick();
    Check(Placeholder(field) == StoredKeyHint, "The hint refresh runs at most once a second");
    f.Ui().nextKeyFieldRefresh = 0;
    f.Ui().Tick();
    Check(Placeholder(field) == KeyHint && Placeholder(Inputs(second)[0]) == KeyHint, "A forgotten key brings the empty-key hint back");
    // The view recycled for another row loses the field, emptied first.
    Paste(field, FakeKey);
    textInitHook(view.element, f.Ui().creditsOption.get(), nullptr);
    Check(Inputs(view).empty() && Get(view.row).items == std::vector<Obj>({view.label, view.button}) &&
        Typed(field).empty() && !Get(field).refs.at("parent") && f.Ui().keyFields.size() == 1,
        "A view recycled for another row loses the field, emptied first");
    const auto foreign = Make("TextButtonOption");
    Get(foreign).refs["description"] = Make("DescriptionData");
    for (const auto model : {f.Ui().metamobForgetOption.get(), foreign}) {
        textInitHook(view.element, model, nullptr);
        Check(Inputs(view).empty(), "Only the key row has a field");
    }
    textInitHook(view.element, keyModel, nullptr);
    Check(Inputs(view).size() == 1 && Inputs(view)[0] != field && inputsCreated == 3 && f.Ui().keyFields.size() == 2,
        "Back on the key row, the view gets a fresh field");
    // A stopping module adds no field.
    auto third = MakeRowView();
    f.state.stopRequested.store(true);
    textInitHook(third.element, keyModel, nullptr);
    Check(Inputs(third).empty() && f.Ui().keyFields.size() == 2 && nativeTextInits == 10, "A stopping module adds no field");
    f.state.stopRequested.store(false);
    // Restore removes every field and releases every Metamob reference.
    f.StopAndRestore();
    Check(f.Ui().keyFields.empty() && Inputs(view).empty() && Inputs(second).empty(), "Restore removes the owned fields from the pooled views");
    Check(!f.Ui().metamobSyncSource.get() && !f.Ui().metamobSyncOption.get() && !f.Ui().metamobKeyOption.get() &&
        !f.Ui().metamobKeyMarker.get() && !f.Ui().metamobForgetOption.get() && !f.Ui().metamobForgetMarker.get(), "Restore releases the Metamob rows");
    textInitHook(view.element, keyModel, nullptr);
    Check(Inputs(view).empty() && f.Ui().keyFields.empty(), "After restore the stale key row gets no field");
    CheckNoKeyTrace();
}

// Enregistrer: the field is read once, emptied at once, the text goes to the sync.
void MetamobKeySaveReadsOnceAndClears() {
    Fixture f;
    initHook(f.page, false, true, MetamobId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    const auto field = Inputs(view).at(0);
    Click(view);
    Check(notices == std::vector<std::string>{PasteKeyFirst} && submittedKeys.empty() && textReads == 1 && foreignButtonClicks == 0,
        "An empty field asks for the key in the chat");
    Paste(field, FakeKey);
    Click(view);
    Check(textReads == 2 && submittedKeys == std::vector<std::string>{FakeKey} && Typed(field).empty() &&
        notices.size() == 1 && foreignButtonClicks == 0 && openedUrls.empty(),
        "Enregistrer reads the field once, empties it and submits the key, never the native action");
    // The native search clone of the key row in another view uses its own field;
    // the text goes as typed (the capture sync trims and validates it).
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(f.Ui().metamobKeyOption.get()).refs.at("description");
    auto other = MakeRowView();
    textInitHook(other.element, clone, nullptr);
    const std::string spaced = std::string(" ") + FakeKey + " ";
    Paste(Inputs(other).at(0), spaced);
    Click(other);
    Check(submittedKeys.size() == 2 && submittedKeys.back() == spaced && Typed(Inputs(other).at(0)).empty() && textReads == 3,
        "A search clone of the key row submits its own field");
    // A view of the key row whose field never appeared: only the chat hint.
    const auto bare = Make("TextButtonOptionElement");
    Get(bare).refs["option"] = f.Ui().metamobKeyOption.get();
    textButtonHook(bare, Make("Visual"), nullptr);
    Check(notices.size() == 2 && notices.back() == PasteKeyFirst && submittedKeys.size() == 2 && foreignButtonClicks == 0,
        "Without a field the button only asks for the key");
    // A stopping module ignores the button: the field stays, no native action.
    Paste(field, FakeKey);
    f.state.stopRequested.store(true);
    Click(view);
    Check(Typed(field) == FakeKey && submittedKeys.size() == 2 && textReads == 3 && foreignButtonClicks == 0, "A stopping module ignores the key button");
    f.state.stopRequested.store(false);
    // Capture sync not wired: the field is still emptied, nothing is called.
    f.Ui().metamob = MetamobHooks{};
    Click(view);
    Click(view);
    Check(Typed(field).empty() && submittedKeys.size() == 2 && notices.size() == 2 && foreignButtonClicks == 0 && textReads == 5,
        "Without capture sync hooks a click only empties the field");
    Check(!f.state.stopRequested.load() && !f.state.stopped.load(), "Key handling never stops the mod");
    CheckNoKeyTrace();
    f.StopAndRestore();
}

// Supprimer: a first click asks for confirmation, a second one within 5 s forgets.
void MetamobForgetNeedsTwoClicks() {
    Fixture f;
    fakeKeyStored = true;
    initHook(f.page, false, true, MetamobId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobForgetOption.get(), nullptr);
    Check(Inputs(view).empty(), "The forget row has no field");
    Click(view);
    Check(keyForgets == 0 && notices == std::vector<std::string>{ConfirmForget} && f.Ui().forgetDeadline != 0, "A first click only asks for confirmation");
    Click(view);
    Check(keyForgets == 1 && notices.size() == 1 && f.Ui().forgetDeadline == 0, "A second click within five seconds forgets the key");
    Click(view);
    Check(keyForgets == 1 && notices.size() == 2, "A third click starts a new confirmation");
    f.Ui().forgetDeadline = 1; // armed long ago: expired
    Click(view);
    Check(keyForgets == 1 && notices.size() == 3 && notices.back() == ConfirmForget, "An expired confirmation asks again");
    Click(view);
    Check(keyForgets == 2 && foreignButtonClicks == 0 && openedUrls.empty() && submittedKeys.empty(),
        "Supprimer acts through forgetKey only, never the native action");
    Click(view);
    Check(f.Ui().forgetDeadline != 0, "Armed before stopping");
    f.StopAndRestore();
    Check(f.Ui().forgetDeadline == 0, "Restore disarms the confirmation");
    CheckNoKeyTrace();
}

// The native reset turns the sync off and never touches the key.
void MetamobNativeResetStopsSyncKeepsKey() {
    Fixture f;
    f.state.settings.metamobSync = true;
    fakeKeyStored = true;
    initHook(f.page, false, true, MetamobId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    Paste(Inputs(view).at(0), FakeKey);
    const auto before = f.state.settings;
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    api.Call(Section(displayedParent, MetamobSection), "Reset");
    Check(!Get(f.Ui().metamobSyncSource.get()).boolean && f.state.settings == before,
        "The section reset restores the off default and leaves persistence to the pump");
    f.Ui().Tick();
    auto expected = before;
    expected.metamobSync = false;
    Check(f.state.settings == expected && changes == 1, "The Metamob section reset turns only the sync off");
    Get(f.Ui().metamobSyncSource.get()).boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.metamobSync && changes == 2, "The sync can be turned on again");
    api.Call(displayedParent, "Reset");
    f.Ui().Tick();
    Check(f.state.settings == Config(2, 1), "The whole Mod reset turns the sync off with every other default");
    Check(keyForgets == 0 && submittedKeys.empty() && notices.empty() && fakeKeyStored && Typed(Inputs(view).at(0)) == FakeKey,
        "A native reset never forgets, submits or announces the key");
    f.state.settings.metamobSync = true;
    f.Ui().Sync();
    Check(Get(f.Ui().metamobSyncSource.get()).boolean, "An external change updates the native checkbox");
    f.StopAndRestore();
    CheckNoKeyTrace();
}

// Key field failures: fixed codes, rate limited, never what(), never a stop.
void MetamobKeyFieldErrorsStayLocal() {
    Fixture f;
    initHook(f.page, false, true, MetamobId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    auto view = MakeRowView();
    failKeyInsert = true;
    const auto lines = logs.size();
    textInitHook(view.element, keyModel, nullptr);
    Check(nativeTextInits == 1 && Inputs(view).empty() && Get(view.row).items.size() == 2 && f.Ui().keyFields.empty(),
        "A failed insertion leaves the native row as it was");
    Check(logs.size() == lines + 1 && logs.back() == "METAMOB_KEY_FIELD_ERROR step=bind", "Key field failures log a fixed code");
    textInitHook(view.element, keyModel, nullptr);
    Check(logs.size() == lines + 1, "Key field failures are logged at most once per ten seconds");
    failKeyInsert = false;
    textInitHook(view.element, keyModel, nullptr);
    Check(Inputs(view).size() == 1 && f.Ui().keyFields.size() == 1, "The next Init places the field");
    // A view whose binding has no button (layout change): no field.
    const auto broken = Make("TextButtonOptionElement");
    Get(broken).refs["m_binding"] = Make("TextButtonOptionElementBinding");
    f.Ui().nextKeyFieldLog = 0;
    textInitHook(broken, keyModel, nullptr);
    Check(f.Ui().keyFields.size() == 1 && logs.back() == "METAMOB_KEY_FIELD_ERROR step=bind", "A row without its button gets no field");
    // A managed exception quoting the typed key while the field is read.
    Paste(Inputs(view).at(0), FakeKey);
    failTextRead = true;
    f.Ui().nextKeyFieldLog = 0;
    Click(view);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=save" && submittedKeys.empty() && notices.empty() && foreignButtonClicks == 0,
        "A failed read submits nothing and logs a fixed code, never the exception text");
    failTextRead = false;
    // A throwing capture sync hook stays inside the options code.
    f.Ui().metamob.submitKey = [](std::string) { throw std::runtime_error(std::string("Synthetic sync failure ") + FakeKey); };
    f.Ui().nextKeyFieldLog = 0;
    Click(view);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=save" && Typed(Inputs(view).at(0)).empty() && foreignButtonClicks == 0,
        "A throwing submit hook is contained and the field is still emptied");
    f.Ui().metamob.keyStored = []() -> bool { throw std::runtime_error("Synthetic key file failure"); };
    f.Ui().nextKeyFieldLog = 0;
    f.Ui().nextKeyFieldRefresh = 0;
    f.Ui().Tick();
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=stored" && Placeholder(Inputs(view).at(0)) == KeyHint,
        "A failing key check in Tick logs a fixed code and keeps the empty-key hint");
    Check(!f.state.stopRequested.load() && !f.state.stopped.load(), "Key field failures never stop the mod");
    // Restore when a field cannot be detached: its references are still released.
    failKeyRemove = true;
    f.Ui().nextKeyFieldLog = 0;
    const auto beforeRestore = logs.size();
    f.StopAndRestore();
    Check(f.Ui().keyFields.empty() && Logged(beforeRestore, "METAMOB_KEY_FIELD_ERROR step=remove") &&
        f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}), "A field that cannot be detached never blocks Restore");
    CheckNoKeyTrace();
}

// Logout: the game tore the options page down; the owned fields still leave.
void MetamobFieldsLeaveOnAbandon() {
    Fixture f;
    initHook(f.page, false, true, MetamobId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    Check(Inputs(view).size() == 1, "Abandon fixture failed to place the field");
    f.state.stopped.store(true);
    f.Ui().Abandon();
    Check(f.Ui().pages.empty() && f.Ui().keyFields.empty() && Inputs(view).empty() && !f.Ui().CleanupPending(),
        "Abandon drops the torn-down page and removes the owned key fields");
    f.Ui().Restore();
}

}

int main() {
    try {
        fake::InitializeApi();
        const auto run = [](void(*test)()) {
            test();
            fake::Check(fake::handles.empty(), "UI test leaked GC references");
        };
        run(fake::ParentAndSectionSelection);
        run(fake::EverySectionSelectionAndRestore);
        run(fake::RestoreRetriesAfterFailedRebuild);
        run(fake::CreditsLinkOpensProject);
        run(fake::RegistrationAndValues);
        run(fake::AnimationModeValuesAndDefaults);
        run(fake::GlowModeValuesAndDefaults);
        run(fake::GlowCategories);
        run(fake::ReducedMiniaturesDescription);
        run(fake::NativeResetAndFilteredSearchOwnership);
        run(fake::SectionResetOwnsItsRows);
        run(fake::ValidationAndLoadingGate);
        run(fake::ResourceMapMasterPreservesSelection);
        run(fake::AutoLoadFailureIsRecoverable);
        run(fake::DeferredAndAccountGate);
        run(fake::CollisionAndSessionMismatch);
        run(fake::MetamobKeyFieldFollowsPooledViews);
        run(fake::MetamobKeySaveReadsOnceAndClears);
        run(fake::MetamobForgetNeedsTwoClicks);
        run(fake::MetamobNativeResetStopsSyncKeepsKey);
        run(fake::MetamobKeyFieldErrorsStayLocal);
        run(fake::MetamobFieldsLeaveOnAbandon);
        fake::CheckNoKeyTrace();

        std::cout << "PASS: eight native sections (layout, display clones, selection, section resets, restore to Jeu), native settings, independent map visibility, reset, session gates, retryable restoration and the Metamob section (masked key field, two-click forget, no key trace)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
