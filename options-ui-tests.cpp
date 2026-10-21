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
constexpr int GeneralId = ModId + 1;
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
    api.class_is_valuetype = [](Klass type) { return static_cast<Class*>(type)->name == "Choice"; };
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.klass->name == "Int32") return &value.integer;
        if (value.klass->name == "Boolean") return &value.boolean;
        if (value.klass->name == "Choice") return &value.choice;
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
        state.servicesReady = true;
        state.ready.store(true);
        state.settings = Config(2, 1);
        state.ui = std::make_unique<OptionsUi>();
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
    const std::string method = name;
    fake::Check((rva == 0x2D88930 && method == "get_m_textButtonOption" && argc == 0) ||
        (rva == 0x53ACA50 && method == "OpenURL" && argc == 1), "Wrong verified credits contract");
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
    if (name == "categoryId") return &value.integers[name];
    if (name == "isSubCategory" || name == "isPlayerConnected") return &value.booleans[name];
    if (name == "dzyi") return value.refs.at(name); // Inline binding is its own fake object.
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
    } else throw std::runtime_error("Unexpected hook in options test");
}

namespace fake {
std::vector<Obj> AllSources(OptionsUi& ui) {
    return {ui.modeSource.get(), ui.roleplayModeSource.get(),
        ui.animationSource.get(), ui.autoLoadSource.get(), ui.githubSource.get(), ui.soulSource.get(), ui.soulAutoSource.get(), ui.glowSource.get(), ui.glowNpcsSource.get(), ui.glowInteractivesSource.get(), ui.transitionsSource.get(), ui.preloadSource.get(), ui.spouseSource.get(), ui.resourcesSource.get(), ui.resourcesMiniSource.get(), ui.miniCenterSource.get()};
}
std::vector<Obj> AllOptions(OptionsUi& ui) {
    return {ui.modeOption.get(), ui.roleplayModeOption.get(),
        ui.animationOption.get(), ui.autoLoadOption.get(), ui.githubOption.get(), ui.soulOption.get(), ui.soulAutoOption.get(), ui.glowOption.get(), ui.glowNpcsOption.get(), ui.glowInteractivesOption.get(), ui.transitionsOption.get(), ui.preloadOption.get(), ui.spouseOption.get(), ui.resourcesOption.get(), ui.resourcesMiniOption.get(), ui.miniCenterOption.get(), ui.creditsOption.get()};
}
const std::vector<Obj>& Rows(Obj category) { return Get(Get(category).refs.at("options")).items; }
std::string Label(Obj option) { return Get(Get(option).refs.at("text")).text; }
std::string Description(Obj option) { return Get(Get(Get(option).refs.at("description")).refs.at("text")).text; }
Obj General(Obj parent) { return Get(Get(parent).refs.at("subcategories")).items.at(0); }
void CheckLayout(Fixture& f) {
    const auto parent = f.Categories().back();
    Check(Get(Get(parent).refs.at("name")).text == ModCategoryTitle, "Loaded revision missing from category");
    Check(!Get(parent).booleans["isSubCategory"] &&
        Get(Get(parent).refs.at("subcategories")).items.size() == 1, "Mod must retain one native section");
    const auto section = General(parent);
    Check(Rows(parent).empty() && Get(section).booleans["isSubCategory"] &&
        Get(section).integers.at("categoryId") == GeneralId &&
        Get(Get(section).refs.at("name")).text == "Général", "Missing native General section");
    Check(Get(Get(section).refs.at("subcategories")).items.empty(), "No nested settings sections expected");
    Check(Rows(section) == AllOptions(f.Ui()), "General must contain exactly seventeen rows in the requested order");
    const char* labels[] = {"En combat", "Hors combat", "Animations", "Chargement automatique", "Mises à jour GitHub", "Alerte pierre d’âme", "Équiper automatiquement la pierre d’âme", "Surbrillance", "Inclure les PNJ", "Inclure les éléments interactifs", "Transitions rapides entre cartes", "Précharger la prochaine carte (expérimental)", "Téléportation au conjoint", "Ressources sur la grande carte", "Ressources sur la mini-carte", "Bouton de recentrage sur la mini-carte", "Dofus Enhanced 1.4.0 — par Altansar"};
    Check(std::string(ModCategoryTitle) == "Dofus Enhanced" && std::string(ModSemanticVersion) == "1.4.0", "Tab name carries no revision; version is x.y.z");
    for (size_t i = 0; i != Rows(section).size(); ++i) {
        const auto option = Rows(section)[i];
        Check(Label(option) == labels[i], "Simple option label or ordering changed");
        Check(Get(option).klass->name == (i == 16 ? "TextButtonOption" : i >= 3 && i != 7 ? "BoolOption" : "MultipleChoiceOption"),
            "Simple settings must use native controls");
        if (i == 16) {
            const auto description = Description(option);
            Check(description.find("Altansar") != std::string::npos && description.find("https://github.com/Altansar69/dofus-miniatures") != std::string::npos &&
                description.find("r97") != std::string::npos, "Credits must name the author, the project page and the revision");
            Check(Get(*static_cast<Obj*>(api.FieldAddress(option, "buttonText"))).text == "Ouvrir GitHub", "Credits button opens the project page");
        }
        if (i < 3 || i == 7) {
            const auto& choices = Get(Get(option).refs.at("choices")).items;
            Check(choices.size() >= 2 && choices.size() <= 4, "Unexpected percentage selector remains");
            for (const auto choice : choices)
                Check(Get(choice).choice.id >= 0 && Get(choice).choice.id <= 3 &&
                    Get(Get(choice).choice.label).text.find('%') == std::string::npos,
                    "A percentage or minimum choice remains in native search models");
        }
    }
    Check(sourceModels == 16 && optionModels == 17,
        "The UI must create sixteen private sources and seventeen rows");
    Check(uiHooks == 3, "Only native page callbacks and the text button action must be hooked");
    const auto& displayed = Get(Get(f.page).refs.at("m_optionsDisplayed")).items;
    if (!displayed.empty()) {
        const auto clone = displayed.back();
        Check(clone != parent && Get(clone).refs.at("uid") == Get(parent).refs.at("uid"),
            "Native display must use an independent category clone with shared identity");
        Check(Rows(clone).empty() && Get(Get(clone).refs.at("subcategories")).items.size() == 1 &&
            General(clone) != section && Get(General(clone)).refs.at("uid") == Get(section).refs.at("uid") &&
            Rows(General(clone)) == Rows(section), "Native display/search clone lost its section or eleven rows");
    }
}
void ParentAndSectionSelection() {
    Fixture f;
    initHook(f.page, false, true, ModId, nullptr);
    CheckLayout(f);
    const auto displayed = Get(f.page).refs.at("m_displayedOption");
    Check(Rows(General(displayed)) == AllOptions(f.Ui()), "Displayed parent clone lost its controls");
    Select(f.page, GeneralId);
    Check(Get(f.page).refs.at("m_currentId") == Get(General(displayed)).refs.at("uid"),
        "General selection must resolve to its own stable UID");
    const auto savedSources = AllSources(f.Ui());
    initHook(f.page, false, true, GeneralId, nullptr);
    Check(Get(f.page).integers["lastSelection"] == GeneralId && AllSources(f.Ui()) == savedSources,
        "Reopening General must retain the selection and existing controls");
    CheckLayout(f);
    f.StopAndRestore();
    Check(Get(f.page).integers["lastSelection"] == 0 &&
        f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}),
        "Restore from the General child must select Jeu and remove the owned parent");
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
    initHook(f.page, false, true, 12, nullptr);
    Check(f.Categories().size() == 3 && f.Ui().pages.size() == 1 &&
        Get(f.page).booleans["lastRebuild"], "Init must append Mod and rebuild its native summary once");
    CheckLayout(f);
    Check(!f.Ui().Attach(f.page) && f.Categories().size() == 3, "Repeated attachment must be idempotent");
    CheckModeChoices(f.Ui().modeOption.get());
    CheckModeChoices(f.Ui().roleplayModeOption.get());
    const auto sources = AllSources(f.Ui());
    for (size_t i = 0; i != sources.size(); ++i) {
        const auto& source = Get(sources[i]);
        Check(sources[i] != f.nativeInt && sources[i] != f.nativeBool, "Mod must use independent sources");
        Check(i >= 3 && i != 7 ? source.defaultBoolean == (i != 8 && i != 9) : source.defaultInteger == (i == 0 ? 2 : i == 1 ? 0 : 1),
            "Persisted settings must not replace native reset defaults");
    }
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
    f.Ui().Tick();
    Check(changes == 1 && f.state.settings.mode == 1 && f.state.settings.roleplayMode == 2 &&
        f.state.settings.animationMode == 3 && !f.state.settings.autoLoad && !f.state.settings.githubUpdates && f.state.settings.soulReminder && f.state.settings.soulAutoEquip && f.state.settings.monsterGlowMode == 0 && !f.state.settings.fastMapTransitions && !f.state.settings.preloadNextMap && f.state.settings.spouseTeleport,
        "Ten native controls failed to commit independent settings together");
    f.NativePreferencesUnchanged();
    Select(f.page, 12);
    f.StopAndRestore();
    Check(f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) &&
        Get(f.page).integers["lastSelection"] == 12, "Restore must retain the selected native category");
    for (auto source : AllSources(f.Ui())) Check(!source, "Restore retained a source reference");
    for (auto option : AllOptions(f.Ui())) Check(!option, "Restore retained an option reference");
    Check(f.Ui().pages.empty() && !f.Ui().container.get(), "Restore retained owned category/container references");
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
    const auto filtered = CloneCategory(General(f.Categories().back()));
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
    const auto searchCategory = CloneCategory(General(displayedParent));
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
        Check(Get(source).klass->name == "BoolSource" ? Get(source).boolean == Get(source).defaultBoolean : Get(source).integer == Get(source).defaultInteger,
            "Whole Mod native reset must restore all eleven defaults");
    f.Ui().Tick();
    Check(f.state.settings == Config(2, 1) && Get(f.page).integers["initCalls"] == rebuilds,
        "Whole Mod reset must commit all defaults without a custom reset hook or rebuild");
    CheckLayout(f);
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
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

}

int main() {
    try {
        fake::InitializeApi();
        const auto run = [](void(*test)()) {
            test();
            fake::Check(fake::handles.empty(), "UI test leaked GC references");
        };
        run(fake::ParentAndSectionSelection);
        run(fake::CreditsLinkOpensProject);
        run(fake::RegistrationAndValues);
        run(fake::AnimationModeValuesAndDefaults);
        run(fake::GlowModeValuesAndDefaults);
        run(fake::ReducedMiniaturesDescription);
        run(fake::NativeResetAndFilteredSearchOwnership);
        run(fake::ValidationAndLoadingGate);
        run(fake::ResourceMapMasterPreservesSelection);
        run(fake::AutoLoadFailureIsRecoverable);
        run(fake::DeferredAndAccountGate);
        run(fake::CollisionAndSessionMismatch);

        std::cout << "PASS: native settings, independent map visibility, reset, session gates and retryable restoration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
