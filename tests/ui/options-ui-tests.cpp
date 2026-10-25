#include "ui/options-ui.hpp"
#include "core/version.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "glow/glow-mode-labels.hpp"
#include "animations/animation-mode-labels.hpp"
#include "appearance/miniature-category-labels.hpp"
#include "treasure/treasure-launch.hpp"
#include "stats/stats-window.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// This executable links the actual options-ui.cpp against a small managed API
// model. No game DLL, process, injector, window, or network is used.
namespace fake {
using namespace managed;
constexpr int ModId = 0x4D4F44;
// The sixteen native sections of the mod, in display order: section i has the
// id ModId + 1 + i, and Restore maps the parent and every section back to « Jeu ».
// « Chasse au trésor » (1.16.1) holds its panel's top-bar button alone;
// « Curseur » (1.17.0) follows « Surbrillance », « Jauges » (1.18.0) « Curseur ».
enum SectionIndex : size_t { AppearanceSection, AnimationSection, GlowSection, CursorSection, GaugeSection, MapsSection, NotebookSection, TreasureSection,
    TravelSection, SoulSection, ChatSection, CalendarSection, PerformanceSection, StatsSection, LoadingSection, AboutSection, SectionCount };
// « Statistiques »: the counting checkbox, « Voir mes statistiques » (an owned
// text button opening the window: OpenStatsWindow below counts the opens) and
// the checkbox of the window's top-bar button.
unsigned statsOpens = 0;
// Settings' ExtraToggles, by row (options-ui.cpp ExtraRow).
enum ExtraIndex : size_t { MiniMapBarExtra, MiniMapZoneNameExtra, NotebookPingExtra, NotebookCopyExtra, NotebookTravelExtra, MapAddPositionExtra,
    PerfOverlayExtra, PerfGpuExtra, PerfDiskExtra, PerfDragExtra, PerfButtonExtra, NotebookSendExtra, GaugeVerticalExtra, PartyTravelExtra,
    CursorGameExtra };
constexpr int SectionId(size_t index) { return ModId + 1 + static_cast<int>(index); }
// The Metamob rows sit in « Pierre d’âme », after the two soul rows and Capture d’âme's top-bar button.
constexpr int DisplayId = SectionId(AppearanceSection), SoulId = SectionId(SoulSection);
// « Apparence » checkbox titles, in Settings' category order.
constexpr const char* CombatCategoryTitles[] = {"Moi", "Joueurs alliés", "Invocations alliées", "Mes invocations", "Ennemis", "Invocations ennemies"};
constexpr const char* RoleplayCategoryTitles[] = {"Moi", "Membres du groupe", "Autres joueurs", "Monstres", "PNJ"};
// « Surbrillance » checkbox titles, in the order of Settings' GlowCategoryToggles.
constexpr const char* GlowCategoryTitles[] = {"Monstres", "PNJ", "Éléments interactifs", "Moi", "Membres du groupe", "Autres joueurs"};
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
    std::map<std::string, int> enumStyles; // inline StyleEnum values (display, flexDirection, alignItems, whiteSpace)
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
// The fake capture sync's key status: fakeStatus for a key file there
// (fakeKeyStored), None without one or while a forget runs (fakeForgetting).
bool fakeKeyStored = false, fakeForgetting = false, failKeyInsert = false, failKeyRemove = false, failTextRead = false;
metamob::KeyStatus fakeStatus = metamob::KeyStatus::Verified;
std::vector<bool> keyChecks; // checkKey calls: false the silent check of a key shown, true « Clé non vérifiée » clicked
// keyCopies: characters of a field copied out (api.String on a get_text result); lengthReads: lengths only.
unsigned keyForgets = 0, keyCopies = 0, lengthReads = 0, nativeTextInits = 0, inputsCreated = 0;
std::vector<std::string> submittedKeys, notices;
std::vector<uintptr_t> verifiedRvas;
// « Défaut (<thème>) » (1.18.1): set_needRebuild's contract missing at Install.
bool failRebuildContract = false;
bool (*clickHandler)(Obj) = nullptr; // nativeClick handler registered by the options UI
const std::string SyncDescription = "Après chaque capture d’un archimonstre ou d’un boss (pierre d’âme remplie en combat), ajoute 1 à ce monstre dans ta quête Ocre sur Metamob, sans rien modifier d’autre. Nécessite ta clé API personnelle (metamob.fr › Paramètres › Clé API). Désactivé par défaut.";
const std::string KeyDescription = "Colle ta clé dans le champ masqué puis clique sur Enregistrer. Elle est vérifiée auprès de Metamob, chiffrée pour ton compte Windows et gardée uniquement sur ce PC ; elle n’est jamais affichée, journalisée ni incluse dans les mises à jour. Une fois enregistrée, elle reste masquée et la corbeille la supprime de ce PC, après confirmation. Elle reste valable sur Metamob : régénère-la sur le site pour la révoquer.";
const std::string KeyHint = "Colle ici ta clé API Metamob", KeyStars = "************", ValidKey = "Clé valide";
const std::string ConfirmQuestion = "Voulez-vous supprimer la clé Metamob ?";
const std::string PasteKeyFirst = "Metamob : colle d’abord ta clé dans le champ.";
const std::string ValidKeyNotice = "Metamob : une clé valide est enregistrée sur ce PC. Pour en changer, supprime-la avec la corbeille puis colle la nouvelle.";
// The other statuses of a saved key: captions and click notices.
const std::string CheckingKey = "Vérification…", UnverifiedKey = "Clé non vérifiée", RefusedKey = "Clé refusée", UnreadableKey = "Clé illisible";
const std::string CheckingNotice = "Metamob : vérification de la clé enregistrée en cours.",
    UnverifiedNotice = "Metamob : la clé enregistrée n’a pas pu être vérifiée auprès de Metamob ; nouvelle vérification en cours.",
    RefusedNotice = "Metamob : clé refusée par Metamob (révoquée ou régénérée ?). Supprime-la avec la corbeille, puis enregistre une nouvelle clé.",
    UnreadableNotice = "Metamob : clé illisible sur ce compte Windows. Supprime-la avec la corbeille, puis enregistre-la à nouveau.";
// The native Enregistrer button's own display in the fake row views, put back exactly whenever it is shown.
constexpr int NativeDisplay = 7;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Settings Config(int mode, int animations) {
    Settings settings;
    settings.mode = mode;
    settings.ApplyLegacyAnimationMode(animations);
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
        return name == "Choice" || name == "StyleBox" || name == "StyleEnum" || name == "Length";
    };
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.klass->name == "Int32") return &value.integer;
        if (value.klass->name == "Boolean") return &value.boolean;
        if (value.klass->name == "Choice") return &value.choice;
        if (value.klass->name == "StyleBox" || value.klass->name == "Length") return &value.number;
        if (value.klass->name == "StyleEnum") return &value.integer;
        throw std::runtime_error("Unexpected boxed type");
    };
    // il2cpp_string_length: the length only, no character copied.
    api.string_length = [](Obj object) -> int32_t { ++lengthReads; return static_cast<int32_t>(Get(object).text.size()); };
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
    // DofusOptionsContainer.currentTheme: the game's theme, Standard (1) unless a test changes it.
    Obj theme;
    Fixture() {
        module = &state;
        newSourcesLoaded = true;
        rejectAutoLoad = false;
        rejectRebuild = false;
        treasureOpens = notebookOpens = foreignButtonClicks = 0; openedUrls.clear();
        changes = 0;
        sourceModels = optionModels = uiHooks = 0;
        fakeKeyStored = fakeForgetting = failKeyInsert = failKeyRemove = failTextRead = false;
        fakeStatus = metamob::KeyStatus::Verified;
        keyForgets = keyCopies = lengthReads = nativeTextInits = inputsCreated = 0;
        submittedKeys.clear(); notices.clear(); verifiedRvas.clear(); keyChecks.clear();
        statsOpens = 0;
        state.servicesReady = true;
        state.ready.store(true);
        state.settings = Config(2, 1);
        state.ui = std::make_unique<OptionsUi>();
        state.ui->metamob.submitKey = [](std::string key) { submittedKeys.push_back(std::move(key)); };
        state.ui->metamob.forgetKey = [] { ++keyForgets; fakeForgetting = true; };
        state.ui->metamob.keyStatus = [] { return fakeKeyStored && !fakeForgetting ? fakeStatus : metamob::KeyStatus::None; };
        state.ui->metamob.checkKey = [](bool again) {
            keyChecks.push_back(again);
            if (again && fakeStatus == metamob::KeyStatus::Unverified) fakeStatus = metamob::KeyStatus::Saved; // as the capture sync does
        };
        state.ui->metamob.notice = [](const std::string& text) { notices.push_back(text); };
        state.ui->Install();
        container = Make("Container");
        nativeInt = Make("IntSource");
        Get(nativeInt).integer = Get(nativeInt).defaultInteger = 7;
        nativeBool = Make("BoolSource");
        Get(nativeBool).boolean = Get(nativeBool).defaultBoolean = true;
        Get(container).refs["creaturesMode"] = nativeInt;
        Get(container).refs["creatureModeFight"] = nativeBool;
        theme = Make("IntSource");
        Get(theme).integer = Get(theme).defaultInteger = 1;
        Get(theme).loaded = true;
        Get(container).refs["currentTheme"] = theme;
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
        {0x55F74A0, "VisualElement", "get_style", 0}, {0x55F50B0, "VisualElement", ".ctor", 0}, {0x55DFAD0, "VisualElement", "Add", 1},
        {0x1721860, "DofusVisualElement", "SetEnabled", 1},
        {0x55D1270, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_display", 0}, {0x55D37F0, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_display", 1},
        {0x55D13C0, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexDirection", 0}, {0x55D3A80, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexDirection", 1},
        {0x55D0C60, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_alignItems", 0}, {0x55D2870, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_alignItems", 1},
        {0x55D26B0, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_whiteSpace", 0}, {0x55D6370, "InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_whiteSpace", 1},
        {0x170D6F0, "DofusButtonCustom", ".ctor", 0}, {0x170E330, "DofusButtonCustom", "set_size", 1}, {0x170E210, "DofusButtonCustom", "set_mainStyle", 1},
        {0x170E400, "DofusButtonCustom", "set_status", 1}, {0x170DFE0, "DofusButtonCustom", "set_icon", 1}, {0x170E5F0, "DofusButtonCustom", "set_text", 1},
        {0x1593940, "DofusLabel", ".ctor", 0}, {0x1594570, "DofusLabel", "set_text", 1},
        {0x2DA3A00, "MultipleChoiceOption", "set_needRebuild", 1}};
    const auto& owner = static_cast<fake::Class*>(klass)->name;
    fake::Check(std::any_of(std::begin(contracts), std::end(contracts), [&](const Contract& contract) {
        return contract.rva == rva && owner == contract.owner && std::string(name) == contract.method && argc == contract.argc;
    }), "Wrong verified options contract");
    if (rva == 0x2DA3A00 && fake::failRebuildContract) throw std::runtime_error("Synthetic missing contract: set_needRebuild");
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
        } else if (value.klass->name == "VisualElement") { // the key row's owned container
            const auto child = static_cast<Obj>(arguments[0]);
            Check(!Get(child).refs["parent"], "Owned control added twice");
            value.items.push_back(child);
            Get(child).refs["parent"] = self;
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
    // MultipleChoiceOption.set_needRebuild: its element's RebuildDropdown (the argument is ignored by the game; always true here).
    if (name == "set_needRebuild") {
        Check(value.klass->name == "MultipleChoiceOption" && *static_cast<bool*>(arguments[0]), "Only a choice row is rebuilt");
        ++value.integers["rebuilds"];
        return nullptr;
    }
    if (name == "get_description") return api.Field(self, "description");
    if (name == "get_m_textButtonOption") return value.refs.at("option");
    if (name == "blrl") return value.refs.at("tooltip");
    if (name == "blqu") return value.refs.at("root");
    if (name == "get_panel") { const auto found = value.refs.find("panel"); return found != value.refs.end() ? found->second : nullptr; }
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
        Check(value.klass->name == "TextInput", "Only a field is read");
        const auto typed = value.refs["text"] ? Get(value.refs["text"]).text : std::string{};
        // A managed exception text can quote the field: it must never be logged.
        if (failTextRead) throw std::runtime_error("Synthetic managed exception: " + typed);
        const auto text = fake::String(typed);
        Get(text).booleans["typed"] = true; // its characters are the field's: see Api::String
        return text;
    }
    // The game's DofusButtonCustom (size, style, status, icon) and DofusVisualElement.SetEnabled.
    if (name == "set_size" || name == "set_mainStyle" || name == "set_status" || name == "set_icon") {
        Check(value.klass->name == "DofusButtonCustom", "Button setters on the game's button only");
        value.integers[name.substr(4)] = *static_cast<int*>(arguments[0]);
        return nullptr;
    }
    if (name == "SetEnabled") { value.booleans["enabled"] = *static_cast<bool*>(arguments[0]); return nullptr; }
    if (name == "get_style") {
        auto& style = value.refs["style"];
        if (!style) { style = Make("InlineStyle"); Get(style).refs["owner"] = self; }
        return style;
    }
    const std::string stylePrefix = "UnityEngine.UIElements.IStyle.";
    if (value.klass->name == "InlineStyle" && name.rfind(stylePrefix, 0) == 0) {
        auto& owner = Get(value.refs.at("owner"));
        const auto operation = name.substr(stylePrefix.size()), property = operation.substr(4);
        // StyleEnum properties hold an int, the others (StyleFloat, StyleLength) a float.
        const bool enumeration = property == "display" || property == "flexDirection" || property == "alignItems" || property == "whiteSpace";
        if (operation.rfind("get_", 0) == 0) {
            if (enumeration) { const auto box = Make("StyleEnum"); Get(box).integer = owner.enumStyles[property]; return box; }
            const auto box = Make("StyleBox"); Get(box).number = owner.styles[property]; return box;
        }
        Check(operation.rfind("set_", 0) == 0, "Unexpected inline style method");
        if (enumeration) owner.enumStyles[property] = *static_cast<int*>(arguments[0]);
        else owner.styles[property] = *static_cast<float*>(arguments[0]);
        return nullptr;
    }
    if (value.klass->name == "StyleBox" && name == "set_value") { value.number = *static_cast<float*>(arguments[0]); return nullptr; }
    if (value.klass->name == "StyleEnum" && name == "set_value") { value.integer = *static_cast<int*>(arguments[0]); return nullptr; }
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
std::string Api::String(Obj self) const {
    if (!self) return {};
    const auto& value = fake::Get(self);
    if (value.booleans.count("typed") && !value.text.empty()) ++fake::keyCopies; // characters of a field, copied out
    return value.text;
}
}
// The game's click on owned DofusButtonCustom buttons (native-click.cpp is not linked here).
namespace nativeClick {
void AddHandler(bool (*handler)(managed::Obj)) { fake::clickHandler = handler; }
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
// « Voir mes statistiques » opens the statistics window (stats-window.cpp is not linked here).
void OpenStatsWindow() { ++fake::statsOpens; }
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
    std::vector<Obj> appearance{ui.modeSource.get()};
    for (const auto& source : ui.combatMiniatureSources) appearance.push_back(source.get());
    appearance.push_back(ui.roleplayModeSource.get());
    for (const auto& source : ui.roleplayMiniatureSources) appearance.push_back(source.get());
    for (const auto& source : ui.animationSources) appearance.push_back(source.get());
    appearance.push_back(ui.glowSource.get());
    for (const auto& source : ui.glowCategorySources) appearance.push_back(source.get());
    appearance.insert(appearance.end(), {ui.customCursorSource.get(), ui.extraSources[CursorGameExtra].get(), ui.customCursorColorSource.get(),
        ui.customCursorSizeSource.get(), ui.extraSources[GaugeVerticalExtra].get()});
    std::vector<Obj> rest{
        ui.resourcesSource.get(), ui.resourcesMiniSource.get(), ui.extraSources[MiniMapBarExtra].get(), ui.miniCenterSource.get(),
        ui.extraSources[MiniMapZoneNameExtra].get(), ui.mapCoordsSource.get(), ui.miniMapCoordsSource.get(),
        ui.archiZonesSource.get(), ui.archiZonesMiniSource.get(), ui.mapMenuSource.get(),
        ui.extraSources[NotebookPingExtra].get(), ui.extraSources[NotebookCopyExtra].get(), ui.extraSources[NotebookTravelExtra].get(),
        ui.extraSources[NotebookSendExtra].get(), ui.extraSources[MapAddPositionExtra].get(), ui.notebookButtonSource.get(), ui.treasureButtonSource.get(),
        ui.transitionsSource.get(), ui.preloadSource.get(), ui.spouseSource.get(), ui.extraSources[PartyTravelExtra].get(),
        ui.travelPlannerSource.get(), ui.templeSource.get(),
        ui.soulSource.get(), ui.soulAutoSource.get(), ui.soulButtonSource.get(), ui.metamobSyncSource.get(),
        ui.chatTravelSource.get(), ui.chatSoulSource.get(), ui.chatOcreSource.get(), ui.chatMetamobSource.get(), ui.chatCoordsSource.get(),
        ui.calendarKraloveSource.get(), ui.extraSources[PerfOverlayExtra].get(), ui.extraSources[PerfGpuExtra].get(),
        ui.extraSources[PerfDiskExtra].get(), ui.extraSources[PerfDragExtra].get(), ui.extraSources[PerfButtonExtra].get(),
        ui.statsSource.get(), ui.statsButtonSource.get(),
        ui.autoLoadSource.get(), ui.githubSource.get()};
    appearance.insert(appearance.end(), rest.begin(), rest.end());
    return appearance;
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
    // « Apparence »: each context's choice, then its category checkboxes (all
    // ticked by default); « Animations »: one checkbox per toggle.
    const Settings defaults;
    ExpectedSection appearance{"Apparence", {ChoiceRow(ui.modeOption, ui.modeSource, "En combat", 2)}}, animations{"Animations", {}};
    for (size_t i = 0; i < ui.combatMiniatureOptions.size(); ++i)
        appearance.rows.push_back(CheckRow(ui.combatMiniatureOptions[i], ui.combatMiniatureSources[i], CombatCategoryTitles[i], true));
    appearance.rows.push_back(ChoiceRow(ui.roleplayModeOption, ui.roleplayModeSource, "Hors combat", 0));
    for (size_t i = 0; i < ui.roleplayMiniatureOptions.size(); ++i)
        appearance.rows.push_back(CheckRow(ui.roleplayMiniatureOptions[i], ui.roleplayMiniatureSources[i], RoleplayCategoryTitles[i],
            defaults.*RoleplayMiniatureToggles[i]));
    for (size_t i = 0; i < ui.animationOptions.size(); ++i)
        animations.rows.push_back(CheckRow(ui.animationOptions[i], ui.animationSources[i], AnimationToggleLabels[i], defaults.*AnimationToggles[i]));
    // « Statistiques »: the counting checkbox, « Voir mes statistiques » (an
    // owned text button opening the window, see StatsSectionRows), then the
    // checkbox of the window's top-bar button; both checkboxes ticked by default.
    ExpectedSection statistics{"Statistiques", {CheckRow(ui.statsOption, ui.statsSource, "Compter mes statistiques", true),
        ButtonRow(ui.statsOpenOption, "Voir mes statistiques"), CheckRow(ui.statsButtonOption, ui.statsButtonSource, "Bouton dans la barre du haut", true)}};
    // « Surbrillance »: the level, then the six categories (Monstres alone ticked by default).
    ExpectedSection glow{"Surbrillance", {ChoiceRow(ui.glowOption, ui.glowSource, "Surbrillance", 1)}};
    for (size_t i = 0; i < ui.glowCategoryOptions.size(); ++i)
        glow.rows.push_back(CheckRow(ui.glowCategoryOptions[i], ui.glowCategorySources[i], GlowCategoryTitles[i], defaults.*GlowCategoryToggles[i]));
    return {
        appearance, animations, glow,
        // « Curseur » (1.17.0): the mod's cursors, off by default, then « Or »
        // and the Windows pointer's size; 1.19.0: « Curseurs du jeu aussi », on
        // by default, under « Curseur Dofus ».
        {"Curseur", {CheckRow(ui.customCursorOption, ui.customCursorSource, "Curseur Dofus", false),
            CheckRow(ui.extraOptions[CursorGameExtra], ui.extraSources[CursorGameExtra], "Curseurs du jeu aussi", true),
            ChoiceRow(ui.customCursorColorOption, ui.customCursorColorSource, "Couleur du curseur", 0),
            ChoiceRow(ui.customCursorSizeOption, ui.customCursorSizeSource, "Taille du curseur", 0)}},
        // « Jauges » (1.18.0): the gauges' « Afficher à la verticale » row, on by default.
        {"Jauges", {CheckRow(ui.extraOptions[GaugeVerticalExtra], ui.extraSources[GaugeVerticalExtra], "Affichage vertical dans le menu … des jauges", true)}},
        {"Cartes", {CheckRow(ui.resourcesOption, ui.resourcesSource, "Ressources sur la grande carte", true),
            CheckRow(ui.resourcesMiniOption, ui.resourcesMiniSource, "Ressources sur la mini-carte", true),
            CheckRow(ui.extraOptions[MiniMapBarExtra], ui.extraSources[MiniMapBarExtra], "Barre des ressources sur la mini-carte", true),
            CheckRow(ui.miniCenterOption, ui.miniCenterSource, "Bouton de recentrage sur la mini-carte", true),
            CheckRow(ui.extraOptions[MiniMapZoneNameExtra], ui.extraSources[MiniMapZoneNameExtra], "Nom de la zone au survol de la mini-carte", true),
            ChoiceRow(ui.mapCoordsOption, ui.mapCoordsSource, "Dimension avec les coordonnées sur la grande carte", 1),
            ChoiceRow(ui.miniMapCoordsOption, ui.miniMapCoordsSource, "Dimension avec les coordonnées sur la mini-carte", 1),
            CheckRow(ui.archiZonesOption, ui.archiZonesSource, "Archimonstres manquants sur la grande carte", true),
            CheckRow(ui.archiZonesMiniOption, ui.archiZonesMiniSource, "Archimonstres manquants sur la mini-carte", false),
            CheckRow(ui.mapMenuOption, ui.mapMenuSource, "Réglages des cartes au clic droit", true)}},
        {"Carnet", {CheckRow(ui.extraOptions[NotebookPingExtra], ui.extraSources[NotebookPingExtra], "Bouton de repère à côté des positions", true),
            CheckRow(ui.extraOptions[NotebookCopyExtra], ui.extraSources[NotebookCopyExtra], "Bouton de copie à côté des entrées", true),
            CheckRow(ui.extraOptions[NotebookTravelExtra], ui.extraSources[NotebookTravelExtra], "Bouton de voyage à côté des positions", true),
            CheckRow(ui.extraOptions[NotebookSendExtra], ui.extraSources[NotebookSendExtra], "Boutons d’envoi à côté des messages", true),
            CheckRow(ui.extraOptions[MapAddPositionExtra], ui.extraSources[MapAddPositionExtra], "« Ajouter aux positions » au clic droit sur les cartes", true),
            CheckRow(ui.notebookButtonOption, ui.notebookButtonSource, "Bouton dans la barre du haut", true)}},
        // « Chasse au trésor » (1.16.1): its panel's top-bar button alone, ticked by default.
        {"Chasse au trésor", {CheckRow(ui.treasureButtonOption, ui.treasureButtonSource, "Bouton dans la barre du haut", true)}},
        {"Déplacements", {CheckRow(ui.transitionsOption, ui.transitionsSource, "Transitions rapides entre cartes", true),
            CheckRow(ui.preloadOption, ui.preloadSource, "Précharger la prochaine carte (expérimental)", true),
            CheckRow(ui.spouseOption, ui.spouseSource, "Téléportation au conjoint", true),
            CheckRow(ui.extraOptions[PartyTravelExtra], ui.extraSources[PartyTravelExtra], "Bouton de trajet sur les membres du groupe", true),
            CheckRow(ui.travelPlannerOption, ui.travelPlannerSource, "Remplacer l’autopilote du jeu", true),
            CheckRow(ui.templeOption, ui.templeSource, "Passage secret et temple des Roublards", true)}},
        {"Pierre d’âme", {CheckRow(ui.soulOption, ui.soulSource, "Alerte pierre d’âme", true),
            CheckRow(ui.soulAutoOption, ui.soulAutoSource, "Équiper automatiquement la pierre d’âme", true),
            CheckRow(ui.soulButtonOption, ui.soulButtonSource, "Bouton dans la barre du haut", true),
            CheckRow(ui.metamobSyncOption, ui.metamobSyncSource, "Synchroniser les captures avec Metamob", false),
            ButtonRow(ui.metamobKeyOption, "Clé API Metamob")}},
        {"Chat", {CheckRow(ui.chatTravelOption, ui.chatTravelSource, "Bouton TP dans le chat", true),
            CheckRow(ui.chatSoulOption, ui.chatSoulSource, "Âme en sac dans le chat", true),
            CheckRow(ui.chatOcreOption, ui.chatOcreSource, "Avancement Ocre dans le chat", true),
            CheckRow(ui.chatMetamobOption, ui.chatMetamobSource, "Metamob dans le chat", true),
            ChoiceRow(ui.chatCoordsOption, ui.chatCoordsSource, "Dimension avec les coordonnées dans le chat", 1)}},
        {"Calendrier", {CheckRow(ui.calendarKraloveOption, ui.calendarKraloveSource, "Ouvertures Kralamoure dans le calendrier", true)}},
        // « Performances » (1.16.0): each part its own box, all off by default;
        // no corner choice any more (the overlay is placed and sized with the
        // mouse); its top-bar button on by default.
        {"Performances", {CheckRow(ui.extraOptions[PerfOverlayExtra], ui.extraSources[PerfOverlayExtra], "Afficher les FPS et la mémoire", false),
            CheckRow(ui.extraOptions[PerfGpuExtra], ui.extraSources[PerfGpuExtra], "Charge du GPU", false),
            CheckRow(ui.extraOptions[PerfDiskExtra], ui.extraSources[PerfDiskExtra], "Activité du disque", false),
            CheckRow(ui.extraOptions[PerfDragExtra], ui.extraSources[PerfDragExtra], "Déplacer et redimensionner l’affichage à la souris", true),
            CheckRow(ui.extraOptions[PerfButtonExtra], ui.extraSources[PerfButtonExtra], "Bouton dans la barre du haut", true)}},
        statistics,
        {"Chargement et mises à jour", {CheckRow(ui.autoLoadOption, ui.autoLoadSource, "Chargement automatique", true),
            CheckRow(ui.githubOption, ui.githubSource, "Mises à jour GitHub", true)}},
        {"À propos", {ButtonRow(ui.creditsOption, "Dofus Enhanced 1.19.0 — par Altansar")}}};
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
    Get(view.button).enumStyles["display"] = NativeDisplay;
    const auto binding = Make("TextButtonOptionElementBinding");
    Get(binding).refs["button"] = view.button;
    Get(view.element).refs["m_binding"] = binding;
    Get(view.element).refs["panel"] = Make("Panel"); // in the open options page
    return view;
}
// The key row's owned container in a view (between its label and its native
// button), and its seven controls in order.
Obj Slot(const RowView& view) {
    for (const auto child : Get(view.row).items)
        if (Get(child).klass->name == "VisualElement") return child;
    return nullptr;
}
struct KeyControls { Obj input, stars, indicator, trash, question, yes, no; };
KeyControls Controls(const RowView& view) {
    const auto slot = Slot(view);
    Check(slot != nullptr, "The view shows the key row's controls");
    const auto& items = Get(slot).items;
    Check(items.size() == 7, "The key row holds exactly its seven controls");
    for (const auto item : items) Check(Get(item).refs.at("parent") == slot, "Each control sits in the key row's container");
    return {items[0], items[1], items[2], items[3], items[4], items[5], items[6]};
}
std::string Placeholder(Obj input) { return Text(Get(input).refs["placeholder"]); }
std::string Typed(Obj input) { return Text(Get(input).refs["text"]); }
std::string Caption(Obj control) { return Text(Get(control).refs["text"]); }
void Paste(Obj input, const std::string& typed) { Get(input).refs["text"] = String(typed); }
void Click(const RowView& view) { textButtonHook(view.element, Make("Visual"), nullptr); }
// The game's click on a button (see nativeClick): true when the options UI owns it.
bool Press(Obj button) { Check(clickHandler != nullptr, "The key row's buttons use the game's click"); return clickHandler(button); }
bool Displayed(Obj element) {
    const auto& styles = Get(element).enumStyles;
    const auto found = styles.find("display");
    return found == styles.end() || found->second != 1;
}
bool SaveShown(const RowView& view) { return Get(view.button).enumStyles.at("display") == NativeDisplay; }
bool SaveHidden(const RowView& view) { return Get(view.button).enumStyles.at("display") == 1; }
// Exactly what each state shows in a view of the key row.
void CheckEditable(const RowView& view, bool save, const char* message) {
    const auto c = Controls(view);
    Check(Displayed(c.input) && !Displayed(c.stars) && !Displayed(c.indicator) && !Displayed(c.trash) && !Displayed(c.question) &&
        !Displayed(c.yes) && !Displayed(c.no) && (save ? SaveShown(view) : SaveHidden(view)), message);
}
void CheckSaved(const RowView& view, const char* message) {
    const auto c = Controls(view);
    Check(!Displayed(c.input) && Typed(c.input).empty() && Displayed(c.stars) && Typed(c.stars) == KeyStars && Displayed(c.indicator) &&
        Displayed(c.trash) && !Displayed(c.question) && !Displayed(c.yes) && !Displayed(c.no) && SaveHidden(view), message);
}
void CheckQuestion(const RowView& view, const char* message) {
    const auto c = Controls(view);
    Check(!Displayed(c.input) && Typed(c.input).empty() && !Displayed(c.stars) && !Displayed(c.indicator) && !Displayed(c.trash) &&
        Displayed(c.question) && Displayed(c.yes) && Displayed(c.no) && SaveHidden(view), message);
}
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
// Tick with the key file read again (at most once a second otherwise).
void Refresh(Fixture& f) { f.Ui().nextKeyFieldRefresh = 0; f.Ui().Tick(); }
void CheckLayout(Fixture& f) {
    const auto parent = f.Categories().back();
    Check(Name(parent) == ModCategoryTitle, "Loaded revision missing from category");
    Check(std::string(ModCategoryTitle) == "Dofus Enhanced" && std::string(ModSemanticVersion) == "1.19.0" && std::string(ModVersion) == "r123" &&
        ModRevision == 123,
        "Tab name carries no revision; version is x.y.z");
    const auto layout = Layout(f.Ui());
    Check(!Get(parent).booleans["isSubCategory"] && Rows(parent).empty() && Sections(parent).size() == SectionCount && layout.size() == SectionCount,
        "Mod must hold its sixteen native sections, from Apparence and Animations to À propos, and no row of its own");
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
            // « Couleur du curseur » (1.18.1): « Défaut », the eleven game themes and three colours, ids 0 to 14.
            const bool cursorColours = row.option == f.Ui().customCursorColorOption.get();
            Check(choices.size() >= 2 && choices.size() <= (cursorColours ? 15u : 4u), "Unexpected percentage selector remains");
            for (const auto choice : choices)
                Check(Get(choice).choice.id >= 0 && Get(choice).choice.id <= (cursorColours ? 14 : 3) &&
                    Get(Get(choice).choice.label).text.find('%') == std::string::npos,
                    "A percentage or minimum choice remains in native search models");
        }
    }
    Check(shownSources == AllSources(f.Ui()), "Every private source must be shown once, by its own row");
    // « Pierre d’âme », after the two soul rows and Capture d’âme's top-bar
    // button: the Metamob sync preference, then the key row, an owned text
    // button (no forget row any more: the saved key's trash replaces it).
    const auto& soul = Rows(Section(parent, SoulSection));
    const auto sync = soul.at(3), key = soul.at(4);
    Check(soul.size() == 5, "Pierre d’âme: Alerte, Équiper automatiquement, Bouton dans la barre du haut, Synchroniser les captures avec Metamob, Clé API Metamob");
    Check(Description(sync) == SyncDescription, "The sync row must say what it changes, what it needs and its default");
    Check(Description(key) == KeyDescription, "The key row must explain the masked field, the check, the storage and the trash");
    Check(ButtonText(key) == "Enregistrer" && Get(key).integers.at("icon") == 148, "The key row uses the native floppy button");
    Check(!Get(key).refs.at("source"), "The key row has no source: a native reset never reaches the key");
    Check(f.Ui().metamobKeyMarker.get() == Get(key).refs.at("description") && f.Ui().metamobKeyMarker.get() != f.Ui().creditsMarker.get(),
        "Each owned text-button row is known by its own description reference");
    // « À propos »: the credits row alone, named after the x.y.z version.
    const auto credits = Rows(Section(parent, AboutSection)).at(0);
    const auto description = Description(credits);
    Check(Label(credits).find("1.19.0") != std::string::npos && description.find("Altansar") != std::string::npos &&
        description.find("https://github.com/Altansar69/dofus-enhanced") != std::string::npos &&
        description.find("r123") == std::string::npos && description.find("révision") == std::string::npos,
        "Credits must name the version, the author and the project page, without the revision");
    Check(ButtonText(credits) == "Ouvrir GitHub" && f.Ui().creditsMarker.get() == Get(credits).refs.at("description"),
        "Credits button opens the project page");
    Check(sourceModels == 73 && optionModels == 73 + 3,
        "The UI must create seventy-three private sources and seventy-six rows");
    Check(uiHooks == 4, "Only native page callbacks, the text button action and its view Init must be hooked");
    for (const uintptr_t rva : {uintptr_t{0x2D885A0}, uintptr_t{0x178E8F0}, uintptr_t{0x55E5710}, uintptr_t{0x55E7BD0}, uintptr_t{0x55D37F0},
        uintptr_t{0x55D1270}, uintptr_t{0x1721860}, uintptr_t{0x170D6F0}, uintptr_t{0x1593940}, uintptr_t{0x55DFAD0}})
        Check(std::find(verifiedRvas.begin(), verifiedRvas.end(), rva) != verifiedRvas.end(), "The view Init and the key row's calls must be exact verified contracts");
    Check(clickHandler != nullptr, "The key row's buttons are handled through the game's click");
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
        "Reopening Apparence must retain the selection and existing controls");
    CheckLayout(f);
    f.StopAndRestore();
    Check(Get(f.page).integers["lastSelection"] == 0 &&
        f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}),
        "Restore from the Apparence child must select Jeu and remove the owned parent");
}

// Each section, from Apparence to À propos, opens on its own rows and stable
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
    f.state.settings.miniatureEnemies = false; f.state.settings.roleplayMiniaturePlayers = false;
    initHook(f.page, false, true, 12, nullptr);
    Check(f.Categories().size() == 3 && f.Ui().pages.size() == 1 &&
        Get(f.page).booleans["lastRebuild"], "Init must append Mod and rebuild its native summary once");
    CheckLayout(f);
    Check(!f.Ui().Attach(f.page) && f.Categories().size() == 3, "Repeated attachment must be idempotent");
    CheckModeChoices(f.Ui().modeOption.get());
    CheckModeChoices(f.Ui().roleplayModeOption.get());
    // « Dimension avec les coordonnées » on the big map, the mini-map and in the
    // chat: each id is the saved mode.
    for (const auto option : {f.Ui().mapCoordsOption.get(), f.Ui().miniMapCoordsOption.get(), f.Ui().chatCoordsOption.get()}) {
        const auto& choices = Get(Get(option).refs.at("choices")).items;
        const char* labels[] = {"Non", "Oui, sauf dans le Monde des Douze", "Oui"};
        Check(choices.size() == 3, "Each dimension row needs its three choices");
        for (size_t i = 0; i != 3; ++i)
            Check(Get(choices[i]).choice.id == static_cast<int>(i) && Get(Get(choices[i]).choice.label).text == labels[i],
                "Dimension choice IDs or labels changed");
    }
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
        Get(f.Ui().animationSources[0].get()).boolean && Get(f.Ui().animationSources[1].get()).boolean &&
        !Get(f.Ui().animationSources[2].get()).boolean && !Get(f.Ui().animationSources[5].get()).boolean &&
        !Get(f.Ui().combatMiniatureSources[4].get()).boolean && Get(f.Ui().combatMiniatureSources[0].get()).boolean &&
        !Get(f.Ui().roleplayMiniatureSources[2].get()).boolean && Get(f.Ui().roleplayMiniatureSources[3].get()).boolean &&
        Get(f.Ui().autoLoadSource.get()).boolean && !Get(f.Ui().soulSource.get()).boolean && !Get(f.Ui().soulAutoSource.get()).boolean && Get(f.Ui().glowSource.get()).integer == 2 && Get(f.Ui().transitionsSource.get()).boolean && Get(f.Ui().preloadSource.get()).boolean && !Get(f.Ui().spouseSource.get()).boolean, "Saved settings were not applied to the eleven native controls");
    Get(f.Ui().modeSource.get()).integer = 1;
    Get(f.Ui().roleplayModeSource.get()).integer = 2;
    for (const auto& source : f.Ui().animationSources) Get(source.get()).boolean = true;
    Get(f.Ui().combatMiniatureSources[0].get()).boolean = false; Get(f.Ui().combatMiniatureSources[4].get()).boolean = true;
    Get(f.Ui().roleplayMiniatureSources[1].get()).boolean = false;
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
        f.state.settings.LegacyAnimationMode() == 3 && !f.state.settings.miniatureSelf && f.state.settings.miniatureEnemies &&
        !f.state.settings.roleplayMiniatureParty && !f.state.settings.roleplayMiniaturePlayers && f.state.settings.roleplayMiniatureSelf &&
        !f.state.settings.autoLoad && !f.state.settings.githubUpdates && f.state.settings.soulReminder && f.state.settings.soulAutoEquip && f.state.settings.monsterGlowMode == 0 && !f.state.settings.fastMapTransitions && !f.state.settings.preloadNextMap && f.state.settings.spouseTeleport && !f.state.settings.metamobSync,
        "Ten native controls failed to commit independent settings together");
    f.NativePreferencesUnchanged();
    Select(f.page, 12);
    f.StopAndRestore();
    Check(f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}) &&
        Get(f.page).integers["lastSelection"] == 12, "Restore must retain the selected native category");
    for (auto source : AllSources(f.Ui())) Check(!source, "Restore retained a source reference");
    for (auto option : AllOptions(f.Ui())) Check(!option, "Restore retained an option reference");
    Check(f.Ui().pages.empty() && !f.Ui().container.get(), "Restore retained owned category/container references");
    Check(!f.Ui().metamobKeyMarker.get() && f.Ui().keyFields.empty(), "Restore retained a Metamob row reference");
}

// « Animations »: six independent checkboxes, the former Raccourcies as reset
// defaults; each row says what it does and its limits, and commits only itself.
void AnimationTogglesValuesAndDefaults() {
    Fixture f;
    f.state.settings.ApplyLegacyAnimationMode(3);
    initHook(f.page, false, true, ModId, nullptr);
    const Settings defaults;
    Check(f.Ui().animationSources.size() == std::size(AnimationToggles) && AnimationToggleLabels.size() == std::size(AnimationToggles),
        "One row per animation toggle");
    for (size_t i = 0; i < f.Ui().animationSources.size(); ++i) {
        const auto& source = Get(f.Ui().animationSources[i].get());
        Check(source.klass->name == "BoolSource" && source.boolean && source.defaultBoolean == defaults.*AnimationToggles[i],
            "Persisted toggles must keep the former Raccourcies as reset defaults");
        Check(Label(f.Ui().animationOptions[i].get()) == AnimationToggleLabels[i] &&
            Get(f.Ui().animationOptions[i].get()).klass->name == "BoolOption", "Animation rows are native checkboxes with the shared labels");
        const auto description = Description(f.Ui().animationOptions[i].get());
        Check(description.rfind("En combat", 0) == 0 &&
            description.find(defaults.*AnimationToggles[i] ? "Activé par défaut" : "Désactivé par défaut") != std::string::npos,
            "Each animation row says it acts in combat and gives its default");
        Check((i >= 2) == (description.find("Expérimental") != std::string::npos), "The fighter, movement and wait rows are marked experimental");
    }
    const auto death = Description(f.Ui().animationOptions[3].get()), waits = Description(f.Ui().animationOptions[5].get());
    Check(death.find("Déplacements instantanés") != std::string::npos && death.find("Sans attentes visuelles") != std::string::npos,
        "The death row explains why it has no effect with instant movements or waits");
    Check(waits.find("délais du serveur") != std::string::npos && waits.find("certaines attentes") != std::string::npos,
        "The waits row keeps its practical limits");
    Check(Description(f.Ui().animationOptions[0].get()).find("Certains effets restent") != std::string::npos &&
        Description(f.Ui().animationOptions[1].get()).find("monstres") != std::string::npos &&
        Description(f.Ui().animationOptions[2].get()).find("certaines animations restent") != std::string::npos,
        "Effects, casts and fighter rows keep their limits");
    auto expected = f.state.settings;
    for (size_t i = 0; i < f.Ui().animationSources.size(); ++i) {
        auto& source = Get(f.Ui().animationSources[i].get());
        source.boolean = !source.boolean;
        f.Ui().Tick();
        expected.*AnimationToggles[i] = source.boolean;
        Check(f.state.settings == expected, "An animation checkbox must change only its own toggle");
    }
    Check(changes == 6, "Animation changes must commit once each");
    f.state.settings.noVisualWaits = true; f.Ui().Sync();
    Check(Get(f.Ui().animationSources[5].get()).boolean, "External toggle changes (context menu) update the native checkbox");
    const auto section = Section(f.Categories().back(), AnimationSection);
    api.Call(section, "Reset");
    for (const auto& source : f.Ui().animationSources) Check(AtDefault(source.get()), "The Animations section reset restores every toggle default");
    f.Ui().Tick();
    for (auto field : AnimationToggles) Check(f.state.settings.*field == defaults.*field, "Reset toggles were not persisted");
    Check(f.state.settings.LegacyAnimationMode() == 1, "The reset defaults are the former Raccourcies");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(Get(f.page).integers["lastSelection"] == 0, "Restore from Mod must select Jeu");
}
// « Apparence »: a checkbox per category after each context's choice, every one
// ticked by default (Miniatures as before); each commits only its own category.
void AppearanceCategoriesValuesAndDefaults() {
    Fixture f;
    for (auto field : CombatMiniatureToggles) f.state.settings.*field = false;
    initHook(f.page, false, true, DisplayId, nullptr);
    const auto rows = Rows(Section(f.Categories().back(), AppearanceSection));
    Check(rows.size() == 13 && rows[0] == f.Ui().modeOption.get() && rows[7] == f.Ui().roleplayModeOption.get() &&
        rows[12] == f.Ui().roleplayMiniatureOptions[4].get(), "Apparence: En combat, its six categories, Hors combat, its five categories ending with PNJ");
    // The map context menu's « Affichage » shows the same categories under the same names.
    for (size_t i = 0; i < std::size(CombatCategoryTitles); ++i)
        Check(std::string(CombatCategoryTitles[i]) == CombatMiniatureLabels[i], "Context menu combat categories keep the options titles");
    for (size_t i = 0; i < std::size(RoleplayCategoryTitles); ++i)
        Check(std::string(RoleplayCategoryTitles[i]) == RoleplayMiniatureLabels[i], "Context menu roleplay categories keep the options titles");
    Check(Description(f.Ui().roleplayMiniatureOptions[4].get()) ==
        "Hors combat, avec Miniatures : les personnages non joueurs (PNJ) sont réduits. Décoché, ils gardent leur taille normale. Désactivé par défaut.",
        "The PNJ row says what it reduces and that it starts unticked");
    for (size_t i = 0; i < f.Ui().combatMiniatureSources.size(); ++i) {
        const auto& source = Get(f.Ui().combatMiniatureSources[i].get());
        Check(!source.boolean && source.defaultBoolean, "Saved combat categories shown, reset to ticked");
        Check(Description(f.Ui().combatMiniatureOptions[i].get()).rfind("En combat, avec Miniatures", 0) == 0, "Combat rows say their context");
    }
    const Settings defaults;
    for (size_t i = 0; i < f.Ui().roleplayMiniatureSources.size(); ++i) {
        const auto& source = Get(f.Ui().roleplayMiniatureSources[i].get());
        const bool ticked = defaults.*RoleplayMiniatureToggles[i];
        Check(source.boolean == ticked && source.defaultBoolean == ticked && ticked == (i != 4),
            "Roleplay categories ticked by default, except PNJ (never reduced before its checkbox)");
        Check(Description(f.Ui().roleplayMiniatureOptions[i].get()).rfind("Hors combat, avec Miniatures", 0) == 0, "Roleplay rows say their context");
    }
    for (auto option : {f.Ui().modeOption.get(), f.Ui().roleplayModeOption.get()})
        Check(Description(option).find("catégories cochées") != std::string::npos && Description(option).find("Créatures") != std::string::npos,
            "Each context's choice explains the categories and that Créatures applies to all");
    // NPCs follow their own category with Miniatures; the game's Créatures never
    // converts them (bool property 9), whatever the categories.
    const auto roleplayMode = Description(f.Ui().roleplayModeOption.get());
    Check(roleplayMode.find("les PNJ et les autres éléments le sont toujours") == std::string::npos &&
        roleplayMode.ends_with("les autres éléments le sont toujours. Créatures s’applique à tous, sauf aux PNJ, que le jeu laisse tels quels."),
        "The mode no longer says NPCs are always reduced, nor that Créatures converts them");
    auto expected = f.state.settings;
    const auto flip = [&](const Ref& source, bool Settings::* field) {
        Get(source.get()).boolean = !Get(source.get()).boolean;
        f.Ui().Tick();
        expected.*field = Get(source.get()).boolean;
        Check(f.state.settings == expected, "A category checkbox must change only its own category");
    };
    for (size_t i = 0; i < f.Ui().combatMiniatureSources.size(); ++i) flip(f.Ui().combatMiniatureSources[i], CombatMiniatureToggles[i]);
    for (size_t i = 0; i < f.Ui().roleplayMiniatureSources.size(); ++i) flip(f.Ui().roleplayMiniatureSources[i], RoleplayMiniatureToggles[i]);
    Check(changes == 11, "Category changes must commit once each");
    Check(f.state.settings.roleplayMiniatureNpcs, "Ticking PNJ commits it");
    f.state.settings.roleplayMiniatureNpcs = false; f.Ui().Sync();
    Check(!Get(f.Ui().roleplayMiniatureSources[4].get()).boolean, "An external PNJ change (the context menu) updates its native checkbox");
    f.state.settings.roleplayMiniatureMonsters = false; f.Ui().Sync();
    Check(!Get(f.Ui().roleplayMiniatureSources[3].get()).boolean, "External category changes update the native checkbox");
    f.StopAndRestore();
    for (const auto& ref : f.Ui().combatMiniatureOptions) Check(!ref.get(), "Restore releases the category rows");
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

// « Surbrillance »: six independent checkboxes after the level, Monstres alone
// ticked by default; each one commits only its own category and never the
// level, a change made in the context menu updates its checkbox, and the
// level explains the budget order.
void GlowCategories() {
    Fixture f; f.state.settings.glowNpcs = true; f.state.settings.glowPlayers = true;
    initHook(f.page, false, true, ModId, nullptr);
    CheckLayout(f);
    constexpr size_t count = std::size(GlowCategoryToggles);
    Check(f.Ui().glowCategorySources.size() == count && f.Ui().glowCategoryOptions.size() == count, "Six glow category rows");
    for (size_t i = 0; i < count; ++i) {
        const auto& source = Get(f.Ui().glowCategorySources[i].get());
        Check(source.klass->name == "BoolSource" && source.boolean == f.state.settings.*GlowCategoryToggles[i] && source.defaultBoolean == (i == 0),
            "Each category shows its saved value and resets to its default: Monstres alone ticked");
        const auto option = f.Ui().glowCategoryOptions[i].get();
        const auto text = Description(option);
        Check(Label(option) == GlowCategoryTitles[i] && text.starts_with("Hors combat : ") &&
            text.find("au niveau de surbrillance choisi ci-dessus") != std::string::npos &&
            text.ends_with(i == 0 ? " Activé par défaut." : " Désactivé par défaut."),
            "Each category row says what it outlines, with the level above, and its default");
    }
    Check(Description(f.Ui().glowCategoryOptions[5].get()).find("hors de ton groupe") != std::string::npos &&
        Description(f.Ui().glowCategoryOptions[2].get()).find("zaaps") != std::string::npos, "Other players exclude the party; interactive objects give examples");
    const auto level = Description(f.Ui().glowOption.get());
    Check(level.find("catégories cochées ci-dessous (les monstres par défaut)") != std::string::npos &&
        level.find("Sans case cochée, rien n’est entouré.") != std::string::npos &&
        level.find("Au plus 32 contours à la fois : s’il y en a plus, passent d’abord ton personnage, puis ton groupe, les monstres, les PNJ, les éléments interactifs et enfin les autres joueurs.") != std::string::npos,
        "The level describes the categories, the empty choice and the budget order");
    auto expected = f.state.settings;
    for (size_t i = 0; i < count; ++i) {
        auto& source = Get(f.Ui().glowCategorySources[i].get());
        source.boolean = !source.boolean;
        f.Ui().Tick();
        expected.*GlowCategoryToggles[i] = !(expected.*GlowCategoryToggles[i]);
        Check(f.state.settings == expected && changes == i + 1, "Each glow checkbox commits only its own category, never the level");
    }
    for (size_t i = 0; i < count; ++i) f.state.settings.*GlowCategoryToggles[i] = i % 2 == 0;
    f.Ui().Sync();
    for (size_t i = 0; i < count; ++i)
        Check(Get(f.Ui().glowCategorySources[i].get()).boolean == (i % 2 == 0), "External preferences (the context menu) update every category checkbox");
    f.StopAndRestore();
    for (size_t i = 0; i < count; ++i)
        Check(!f.Ui().glowCategorySources[i].get() && !f.Ui().glowCategoryOptions[i].get(), "Restore releases every glow category row");
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
    f.state.settings.ApplyLegacyAnimationMode(3);
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
    Check(f.state.settings == beforeReset && Get(f.Ui().animationSources[4].get()).boolean,
        "Unrelated native reset must not affect Mod models");
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    const auto searchCategory = CloneCategory(Section(displayedParent, AnimationSection));
    Get(searchCategory).integers["categoryId"] = 999;
    Get(Get(searchCategory).refs.at("options")).items = {f.Ui().animationOptions[4].get()};
    const auto rebuilds = Get(f.page).integers["initCalls"];
    api.Call(searchCategory, "Reset");
    Check(Get(searchCategory).integers["resetCalls"] == 1 &&
        !Get(f.Ui().animationSources[4].get()).boolean && Get(f.Ui().animationSources[5].get()).boolean &&
        Get(f.Ui().modeSource.get()).integer == 2 &&
        Get(f.Ui().roleplayModeSource.get()).integer == 2 &&
        Get(f.Ui().autoLoadSource.get()).boolean && !Get(f.Ui().spouseSource.get()).boolean,
        "Filtered native reset must touch only its contained option, not the other Mod settings");
    Check(f.state.settings == beforeReset, "Native reset must defer persistence until the normal pump");
    f.Ui().Tick();
    auto resetMethodOnly = beforeReset;
    resetMethodOnly.instantMovements = false;
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
    Settings settings = Config(1, 0);
    for (auto field : AnimationToggles) settings.*field = !(Settings{}.*field);
    for (auto field : CombatMiniatureToggles) settings.*field = false;
    for (auto field : RoleplayMiniatureToggles) settings.*field = !(Settings{}.*field);
    settings.roleplayMode = 1;
    settings.monsterGlowMode = 3;
    for (auto field : GlowCategoryToggles) settings.*field = !(Settings{}.*field);
    settings.customCursor = true; settings.customCursorColor = 2; settings.customCursorSize = 1;
    settings.resourceMapEnabled = settings.resourceMiniMapEnabled = settings.miniMapCenterButton = false;
    settings.archiZonesMap = false; settings.archiZonesMiniMap = true; settings.calendarKralamoure = false;
    settings.mapMenuSettings = false;
    settings.fastMapTransitions = settings.preloadNextMap = settings.spouseTeleport = false;
    settings.soulReminder = settings.soulAutoEquip = false;
    settings.autoLoad = settings.githubUpdates = false;
    settings.metamobSync = true;
    settings.mapTravelPlanner = false;
    settings.travelRoublardTemple = false;
    settings.chatTravelLinks = settings.chatSoulInventory = settings.chatOcreStatus = settings.chatMetamobStatus = false;
    for (auto field : ExtraToggles) settings.*field = !(Settings{}.*field);
    // Not rows (no row resets them, the pump never changes them): the corner an
    // earlier revision chose and the overlay's mouse place and sizes (its width
    // and height, 1.17.1), kept with the mouse placement unticked, and the
    // overlay hidden with its top-bar button.
    settings.perfOverlayCorner = 2;
    settings.perfOverlayX = 3300; settings.perfOverlayY = 4400;
    settings.perfOverlayScaleX = 175; settings.perfOverlayScaleY = 60;
    settings.perfOverlayShown = false;
    settings.coordsWorldChatMode = 2; settings.coordsWorldMapMode = 0; settings.coordsWorldMiniMapMode = 2; // away from « Oui, sauf »
    settings.statsEnabled = settings.statsButton = false;
    settings.soulButton = settings.treasureButton = settings.notebookButton = false;
    // Not a row (each gauge's own « … » menu ticks it): the vertical gauges,
    // kept by every reset, the « Jauges » one included.
    settings.gaugeVertical = 0x1 | 0x8 | 0x40;
    return settings;
}
// The preferences behind one section's rows, back to their defaults.
Settings WithSectionDefaults(Settings settings, size_t index) {
    const Settings defaults;
    if (index == AppearanceSection) {
        settings.mode = defaults.mode; settings.roleplayMode = defaults.roleplayMode;
        for (auto field : CombatMiniatureToggles) settings.*field = defaults.*field;
        for (auto field : RoleplayMiniatureToggles) settings.*field = defaults.*field;
    } else if (index == AnimationSection) {
        for (auto field : AnimationToggles) settings.*field = defaults.*field;
    } else if (index == GlowSection) {
        settings.monsterGlowMode = defaults.monsterGlowMode;
        for (auto field : GlowCategoryToggles) settings.*field = defaults.*field;
    } else if (index == CursorSection) {
        settings.customCursor = defaults.customCursor;
        settings.customCursorColor = defaults.customCursorColor; settings.customCursorSize = defaults.customCursorSize;
        settings.*ExtraToggles[CursorGameExtra] = defaults.*ExtraToggles[CursorGameExtra];
    } else if (index == GaugeSection) {
        settings.*ExtraToggles[GaugeVerticalExtra] = defaults.*ExtraToggles[GaugeVerticalExtra];
    } else if (index == MapsSection) {
        settings.resourceMapEnabled = defaults.resourceMapEnabled; settings.resourceMiniMapEnabled = defaults.resourceMiniMapEnabled;
        settings.miniMapCenterButton = defaults.miniMapCenterButton;
        settings.archiZonesMap = defaults.archiZonesMap; settings.archiZonesMiniMap = defaults.archiZonesMiniMap;
        settings.mapMenuSettings = defaults.mapMenuSettings;
        for (auto extra : {MiniMapBarExtra, MiniMapZoneNameExtra}) settings.*ExtraToggles[extra] = defaults.*ExtraToggles[extra];
        settings.coordsWorldMapMode = defaults.coordsWorldMapMode; settings.coordsWorldMiniMapMode = defaults.coordsWorldMiniMapMode;
    } else if (index == NotebookSection) {
        for (auto extra : {NotebookPingExtra, NotebookCopyExtra, NotebookTravelExtra, NotebookSendExtra, MapAddPositionExtra})
            settings.*ExtraToggles[extra] = defaults.*ExtraToggles[extra];
        settings.notebookButton = defaults.notebookButton;
    } else if (index == TreasureSection) {
        settings.treasureButton = defaults.treasureButton;
    } else if (index == TravelSection) {
        settings.fastMapTransitions = defaults.fastMapTransitions; settings.preloadNextMap = defaults.preloadNextMap; settings.spouseTeleport = defaults.spouseTeleport;
        settings.mapTravelPlanner = defaults.mapTravelPlanner; settings.travelRoublardTemple = defaults.travelRoublardTemple;
        settings.*ExtraToggles[PartyTravelExtra] = defaults.*ExtraToggles[PartyTravelExtra];
    } else if (index == SoulSection) {
        settings.soulReminder = defaults.soulReminder; settings.soulAutoEquip = defaults.soulAutoEquip;
        settings.soulButton = defaults.soulButton;
        settings.metamobSync = defaults.metamobSync;
    } else if (index == ChatSection) {
        settings.chatTravelLinks = defaults.chatTravelLinks; settings.chatSoulInventory = defaults.chatSoulInventory;
        settings.chatOcreStatus = defaults.chatOcreStatus; settings.chatMetamobStatus = defaults.chatMetamobStatus;
        settings.coordsWorldChatMode = defaults.coordsWorldChatMode;
    } else if (index == CalendarSection) {
        settings.calendarKralamoure = defaults.calendarKralamoure;
    } else if (index == PerformanceSection) {
        for (auto extra : {PerfOverlayExtra, PerfGpuExtra, PerfDiskExtra, PerfDragExtra, PerfButtonExtra}) settings.*ExtraToggles[extra] = defaults.*ExtraToggles[extra];
    } else if (index == StatsSection) {
        settings.statsEnabled = defaults.statsEnabled; settings.statsButton = defaults.statsButton;
    } else if (index == LoadingSection) {
        settings.autoLoad = defaults.autoLoad; settings.githubUpdates = defaults.githubUpdates;
    }
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

// « Curseur » (1.17.0), right after « Surbrillance »: « Curseur Dofus » (off by
// default), then the cursors' colour (« Défaut » by default since 1.18.1) and size (« Comme la
// souris de Windows » by default), each id the saved value. Each row shows the
// saved choice, says what it does and its default, commits only its own
// setting, follows an outside change and goes back to its default with the
// section's reset.
void CursorSectionRows() {
    const std::string boxDescription = "Remplace la souris de Windows dans le jeu par des curseurs dessinés par le mod dans le style de Dofus : la flèche de sélection, la main sur ce que le jeu rend cliquable (boutons, onglets, liens, cases et bords de carte…), le curseur de texte dans les champs de saisie (chat, recherches…), la main ouverte sur les barres de titre des fenêtres puis fermée quand tu les déplaces ou que tu fais glisser un objet, et les doubles flèches sur les bords des fenêtres redimensionnables. Avec « Curseurs du jeu aussi », les curseurs propres au jeu (épée, bulle, outils de récolte, repères de combat…) sont dessinés par le mod eux aussi. Décoché, la souris de Windows et les curseurs habituels du jeu reviennent aussitôt. Désactivé par défaut.";
    // 1.18.1: « Défaut (<thème>) », each game theme, then the mod's three colours.
    const std::string colourDescription = "La couleur des curseurs du mod, toujours avec un contour sombre et une ombre pour rester lisibles sur tous les décors. « Défaut » reprend les couleurs du thème choisi dans les options du jeu (Thème), dont le nom s’affiche entre parenthèses, et les suit quand tu en changes. Chaque thème du jeu (Standard, Bonta, Brakmar…) est aussi proposé seul : le curseur garde alors ses couleurs, quel que soit le thème de l’interface. Avec un thème, le curseur prend sa couleur d’accent, éclaircie si elle est trop sombre, sur un contour tiré de son fond sombre ; jaune-vert avec le thème Standard. « Or » : doré, avec une émeraude sur la flèche et sur le gant. « Émeraude » : vert, avec un ornement doré. « Argent » : gris clair. Sans effet tant que « Curseur Dofus » est décoché. Réglée sur « Défaut » par défaut.";
    // 1.17.1: the three sizes are Windows cursors at their real size.
    const std::string sizeDescription = "« Comme la souris de Windows » reprend la taille du pointeur de Windows avec la mise à l’échelle de l’écran où se trouve le jeu : 32 pixels à 100 %, 40 à 125 %, 48 à 150 %, 64 à 200 %, davantage si tu as agrandi le pointeur dans les paramètres de Windows. « Grande » l’agrandit de moitié (48 pixels à 100 %) et « Très grande » la double (64 pixels à 100 %), sans dépasser 128 pixels. À toutes les tailles, ce sont des curseurs de Windows, comme la souris : ils suivent tes mouvements sans retard, quelle que soit la fluidité du jeu. Les curseurs propres au jeu (épée, bulle, outils de récolte, repères de combat…) prennent cette taille avec « Curseurs du jeu aussi », leur taille habituelle sans. Sans effet tant que « Curseur Dofus » est décoché. « Comme la souris de Windows » par défaut.";
    Fixture f;
    f.state.settings.customCursor = true; f.state.settings.customCursorColor = 3; f.state.settings.customCursorSize = 2;
    f.state.settings.customCursorGame = false;
    Get(f.theme).integer = 4; // the game shows Brakmar
    initHook(f.page, false, true, SectionId(CursorSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    Check(Name(Section(parent, CursorSection)) == "Curseur" && Name(Section(parent, CursorSection - 1)) == "Surbrillance" &&
        Name(Section(parent, CursorSection + 1)) == "Jauges" && Rows(Section(parent, CursorSection)) ==
        std::vector<Obj>({ui.customCursorOption.get(), ui.extraOptions[CursorGameExtra].get(), ui.customCursorColorOption.get(),
            ui.customCursorSizeOption.get()}),
        "« Curseur », between « Surbrillance » and « Jauges »: the checkbox, « Curseurs du jeu aussi », the colour, the size");
    auto& box = Get(ui.customCursorSource.get());
    // 1.19.0: « Curseurs du jeu aussi », ticked by default, under « Curseur Dofus ».
    auto& gameBox = Get(ui.extraSources[CursorGameExtra].get());
    const std::string gameDescription = "Avec « Curseur Dofus », les curseurs propres au jeu sont eux aussi dessinés par le mod, dans la couleur et la taille choisies : la bulle pour parler à un PNJ, l’engrenage des actions, la maison des portes et des habitations, le cadenas, le livre, le portail de téléportation, les outils de récolte (faux, hache, hameçon, pioche, ciseaux), la goutte de plongée, la loupe, l’épée sur les monstres, les repères de combat (cœur, crâne, épée, bouclier, cible, 1, 2, 3, épingle) et le point d’interrogation. Quand une action est indisponible, sa croix ou son sablier devient un badge sur le curseur. Un curseur que le mod ne connaît pas garde le dessin du jeu. Décochée, ces curseurs reprennent leur dessin habituel ; les autres curseurs du mod restent. Sans effet tant que « Curseur Dofus » est décoché. Activé par défaut.";
    Check(Get(ui.extraOptions[CursorGameExtra].get()).klass->name == "BoolOption" && gameBox.klass->name == "BoolSource" &&
        !gameBox.boolean && gameBox.defaultBoolean && Description(ui.extraOptions[CursorGameExtra].get()) == gameDescription &&
        gameDescription.find('\'') == std::string::npos,
        "« Curseurs du jeu aussi »: a native checkbox showing the saved choice, ticked as its reset default, saying what it does");
    auto& colour = Get(ui.customCursorColorSource.get());
    auto& size = Get(ui.customCursorSizeSource.get());
    Check(Get(ui.customCursorOption.get()).klass->name == "BoolOption" && box.klass->name == "BoolSource" && box.boolean && !box.defaultBoolean,
        "« Curseur Dofus »: a native checkbox showing the saved choice, unticked as its reset default");
    Check(colour.klass->name == "IntSource" && colour.integer == 3 && colour.defaultInteger == 0 && size.klass->name == "IntSource" &&
        size.integer == 2 && size.defaultInteger == 0, "The colour and the size show the saved choices, « Défaut » and the Windows size as reset defaults");
    const auto choices = [](Obj option) {
        std::vector<std::pair<int, std::string>> list;
        for (const auto choice : Get(Get(option).refs.at("choices")).items) list.emplace_back(Get(choice).choice.id, Get(Get(choice).choice.label).text);
        return list;
    };
    Check(choices(ui.customCursorColorOption.get()) ==
        std::vector<std::pair<int, std::string>>{{0, "Défaut (Brakmar)"}, {1, "Standard"}, {3, "Bonta"}, {4, "Brakmar"}, {2, "Tribute"},
            {5, "Gold and steel"}, {7, "Belladone"}, {8, "Unicorn"}, {6, "Emerald mine"}, {9, "Sufokia"}, {10, "Pandala"}, {11, "Wabbit"},
            {12, "Or"}, {13, "Émeraude"}, {14, "Argent"}},
        "« Défaut » named after the game's theme, the eleven themes in the game's order (ids: ThemeEnum), the mod's three colours, each id the saved value");
    {
        std::set<std::string> labels;
        for (const auto& entry : choices(ui.customCursorColorOption.get())) labels.insert(entry.second);
        Check(labels.size() == 15, "Every label differs: the game's dropdown finds a click by its label");
    }
    Check(choices(ui.customCursorSizeOption.get()) ==
        std::vector<std::pair<int, std::string>>{{0, "Comme la souris de Windows"}, {1, "Grande"}, {2, "Très grande"}},
        "The three sizes, each id the saved value");
    Check(Description(ui.customCursorOption.get()) == boxDescription && Description(ui.customCursorColorOption.get()) == colourDescription &&
        Description(ui.customCursorSizeOption.get()) == sizeDescription, "Each row says what it does and its default");
    for (const auto option : {ui.customCursorOption.get(), ui.customCursorColorOption.get(), ui.customCursorSizeOption.get()})
        Check(Description(option).find('\'') == std::string::npos && Description(option).ends_with(" par défaut."), "Typographic apostrophes, the default last");
    // Each row commits only its own setting, once.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (bool value : {false, true, false}) {
        box.boolean = value;
        ui.Tick();
        expected.customCursor = value;
        Check(f.state.settings == expected && changes == ++commits, "« Curseur Dofus » commits only itself, once");
    }
    for (bool value : {true, false, true}) {
        gameBox.boolean = value;
        ui.Tick();
        expected.customCursorGame = value;
        Check(f.state.settings == expected && changes == ++commits, "« Curseurs du jeu aussi » commits only itself, once");
    }
    for (int value : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}) {
        colour.integer = value;
        ui.Tick();
        expected.customCursorColor = value;
        Check(f.state.settings == expected && changes == ++commits, "A colour commits only itself, once");
    }
    for (int value : {0, 1, 2}) {
        size.integer = value;
        ui.Tick();
        expected.customCursorSize = value;
        Check(f.state.settings == expected && changes == ++commits, "A size commits only itself, once");
    }
    // An outside change shows in each row.
    f.state.settings.customCursor = true; f.state.settings.customCursorColor = 1; f.state.settings.customCursorSize = 1;
    f.state.settings.customCursorGame = false;
    ui.Sync();
    Check(box.boolean && colour.integer == 1 && size.integer == 1 && !gameBox.boolean, "Outside changes show in the rows");
    // The section's reset: off, « Défaut », the Windows size; nothing else.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = WithSectionDefaults(f.state.settings, CursorSection);
    api.Call(Section(displayedParent, CursorSection), "Reset");
    ui.Tick();
    Check(f.state.settings == expected && !f.state.settings.customCursor && f.state.settings.customCursorColor == 0 &&
        f.state.settings.customCursorSize == 0 && f.state.settings.customCursorGame,
        "The section's reset: off, the game's icons ticked, « Défaut », the Windows size, nothing else");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(!ui.customCursorSource.get() && !ui.customCursorOption.get() && !ui.customCursorColorSource.get() && !ui.customCursorColorOption.get() &&
        !ui.customCursorSizeSource.get() && !ui.customCursorSizeOption.get(), "Restore releases the rows");
}

// « Défaut (<thème>) » (1.18.1): the colour's first label names the theme the
// game shows when the tab is added to a page, then follows it (read at most
// every 500 ms while the options exist; the dropdown rebuilt as the game
// rebuilds its resolution list); « Défaut » alone while the theme is unknown.
// The other choices, the selection (by id) and the settings never change.
// Without set_needRebuild's contract, logged once at Install, the new label
// shows at the row's next view.
void CursorThemeLabel() {
    const auto choices = [](Obj option) {
        std::vector<std::pair<int, std::string>> list;
        for (const auto choice : Get(Get(option).refs.at("choices")).items) list.emplace_back(Get(choice).choice.id, Get(Get(choice).choice.label).text);
        return list;
    };
    const auto label = [&](Obj option) { return choices(option).at(0).second; };
    {
        Fixture f;
        Get(f.theme).integer = 3;
        f.state.settings.customCursorColor = 0;
        initHook(f.page, false, true, SectionId(CursorSection), nullptr);
        auto& ui = f.Ui();
        const Obj option = ui.customCursorColorOption.get();
        Check(label(option) == "Défaut (Bonta)" && ui.cursorThemeShown == 3 && Get(option).integers["rebuilds"] == 0,
            "Named after the game's theme when the tab is added, nothing rebuilt");
        const auto rest = [&](Obj row) { const auto all = choices(row); return std::vector<std::pair<int, std::string>>(all.begin() + 1, all.end()); };
        const auto others = rest(option);
        // The player picks Brakmar in the game's « Thème »: read again within 500 ms.
        Get(f.theme).integer = 4;
        ui.Tick();
        Check(label(option) == "Défaut (Bonta)", "Read at most every 500 ms");
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(label(option) == "Défaut (Brakmar)" && Get(option).integers["rebuilds"] == 1 && ui.cursorThemeShown == 4 &&
            rest(option) == others &&
            choices(option).at(0).first == 0, "The game's theme changed: « Défaut (Brakmar) », the dropdown rebuilt, the other choices kept");
        Check(changes == 0 && Get(ui.customCursorColorSource.get()).integer == 0 && f.state.settings.customCursorColor == 0,
            "The selection and the settings unchanged");
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(Get(option).integers["rebuilds"] == 1, "The same theme: nothing rebuilt");
        // Unknown: none, out of range, or no theme option at all.
        for (const int unknown : {0, 12, -1}) {
            Get(f.theme).integer = unknown;
            ui.nextCursorThemeRead = 0;
            ui.Tick();
            Check(label(option) == "Défaut" && ui.cursorThemeShown == 0, "An unknown theme: « Défaut » alone");
        }
        Check(Get(option).integers["rebuilds"] == 2, "Rebuilt once for the unknown theme");
        Get(f.theme).integer = 11;
        Get(f.container).refs.erase("currentTheme");
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(label(option) == "Défaut", "Without the theme option: « Défaut »");
        Get(f.container).refs["currentTheme"] = f.theme;
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(label(option) == "Défaut (Wabbit)" && Get(option).integers["rebuilds"] == 3, "The theme back: « Défaut (Wabbit) »");
        // The page rebuilds its list without the tab: added again, the label is
        // read at once, whatever the wait.
        auto& categories = Get(Get(f.page).refs.at("m_options")).items;
        categories.erase(std::remove(categories.begin(), categories.end(), f.Ui().pages.back().category.get()), categories.end());
        Get(f.theme).integer = 7;
        ui.nextCursorThemeRead = GetTickCount64() + 60000;
        initHook(f.page, false, true, SectionId(CursorSection), nullptr);
        Check(label(ui.customCursorColorOption.get()) == "Défaut (Belladone)", "Added to a page again: the game's theme at once");
        // Every label differs, whatever the theme.
        for (const auto& theme : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}) {
            Get(f.theme).integer = theme;
            ui.nextCursorThemeRead = 0;
            ui.Tick();
            std::set<std::string> labels;
            for (const auto& entry : choices(option)) labels.insert(entry.second);
            Check(labels.size() == 15 && label(option).starts_with("Défaut ("), "Every label differs with each theme");
        }
        // The pages torn down (logout): the label still follows, nothing rebuilt.
        ui.Abandon();
        const auto rebuilt = Get(option).integers["rebuilds"];
        Get(f.theme).integer = 5;
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(label(option) == "Défaut (Gold and steel)" && Get(option).integers["rebuilds"] == rebuilt,
            "Without a page: the new label, no rebuild through a torn-down view");
        f.StopAndRestore();
        Check(ui.cursorThemeShown == -1 && ui.nextCursorThemeRead == 0, "Restore forgets the label's theme");
    }
    {
        const auto from = logs.size();
        failRebuildContract = true;
        Fixture f;
        failRebuildContract = false;
        Check(Logged(from, "UI_CURSOR_THEME_REBUILD_UNAVAILABLE Synthetic missing contract: set_needRebuild"), "A missing contract logged once");
        initHook(f.page, false, true, SectionId(CursorSection), nullptr);
        auto& ui = f.Ui();
        const Obj option = ui.customCursorColorOption.get();
        Check(label(option) == "Défaut (Standard)", "The fixture's Standard theme");
        Get(f.theme).integer = 9;
        ui.nextCursorThemeRead = 0;
        ui.Tick();
        Check(label(option) == "Défaut (Sufokia)" && Get(option).integers["rebuilds"] == 0 && changes == 0,
            "Without set_needRebuild: the new label, shown at the row's next view, nothing else");
        f.StopAndRestore();
    }
}

// « Jauges » (1.18.0), right after « Curseur »: one checkbox, the switch of the
// « Afficher à la verticale » row the mod adds to each gauge's menu, ticked by
// default. It says which gauges have the row, what ticking it there does, what
// unticking it here does (every gauge back horizontal, each one's choice kept)
// and its default, without the glyph the game's fonts lack; it commits only
// itself, follows an outside change, and the section's reset ticks it again
// without touching the gauges ticked in their own menus.
void GaugeSectionRows() {
    const std::string description = "Ajoute la case « Afficher à la verticale » au menu … (les trois petits points) de chaque jauge : expérience du personnage, expérience de guilde, points d’honneur, pods, points d’énergie et expérience de chaque métier. Cochée dans ce menu, la jauge passe à la verticale : elle se remplit de bas en haut, son niveau en dessous ; elle se déplace comme avant, sa longueur se change en tirant son bord haut ou bas, et le mod garde sa longueur et sa place à la verticale ; décochée, elle revient à sa place et à sa taille d’avant. Décochée ici, la case disparaît des menus et toutes les jauges reviennent à l’horizontale ; le choix de chaque jauge est gardé. Activé par défaut.";
    constexpr int Ticked = 0x2 | 0x10 | 0x2000000; // guild, pods, the breeder's experience
    Fixture f;
    f.state.settings.gaugeVerticalMenu = false;
    f.state.settings.gaugeVertical = Ticked;
    initHook(f.page, false, true, SectionId(GaugeSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    const auto option = ui.extraOptions[GaugeVerticalExtra].get();
    auto& source = Get(ui.extraSources[GaugeVerticalExtra].get());
    Check(Name(Section(parent, GaugeSection)) == "Jauges" && Name(Section(parent, GaugeSection - 1)) == "Curseur" &&
        Name(Section(parent, GaugeSection + 1)) == "Cartes" && Rows(Section(parent, GaugeSection)) == std::vector<Obj>({option}),
        "« Jauges », between « Curseur » and « Cartes », holds its one row");
    Check(Get(option).klass->name == "BoolOption" && Label(option) == "Affichage vertical dans le menu … des jauges" &&
        Get(option).refs.at("source") == ui.extraSources[GaugeVerticalExtra].get() && source.klass->name == "BoolSource" &&
        !source.boolean && source.defaultBoolean, "A native checkbox showing the saved switch, ticked as its reset default");
    Check(Description(option) == description, "The row says which gauges, what each tick does, what unticking it does and its default");
    Check(description.ends_with(" Activé par défaut.") && description.find('\'') == std::string::npos &&
        description.find("⋯") == std::string::npos && Label(option).find("⋯") == std::string::npos,
        "Its default last, typographic apostrophes, no glyph the game's fonts lack");
    // It commits only itself, once: the gauges ticked in their menus stay.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (bool value : {true, false, true}) {
        source.boolean = value;
        ui.Tick();
        expected.gaugeVerticalMenu = value;
        Check(f.state.settings == expected && changes == ++commits && f.state.settings.gaugeVertical == Ticked,
            "« Affichage vertical dans le menu … des jauges » commits only itself, once");
    }
    // An outside change shows in it.
    f.state.settings.gaugeVerticalMenu = false;
    ui.Sync();
    Check(!source.boolean, "An outside change shows in the box");
    // The section's reset: ticked again, the gauges' own choices kept.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = WithSectionDefaults(f.state.settings, GaugeSection);
    api.Call(Section(displayedParent, GaugeSection), "Reset");
    ui.Tick();
    Check(f.state.settings == expected && f.state.settings.gaugeVerticalMenu && f.state.settings.gaugeVertical == Ticked,
        "The section's reset ticks it again, nothing else, the gauges' own choices kept");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(!ui.extraSources[GaugeVerticalExtra].get() && !ui.extraOptions[GaugeVerticalExtra].get(), "Restore releases the row");
}

// « Carnet » (1.16.0, 1.17.1): the line buttons, after the pin button and
// before « Ajouter aux positions », all ticked by default: the copy button of
// every entry (a position's since 1.16.0, a note's and a message's since
// 1.17.1), the travel button of a position and the send and prepare buttons
// of a message (1.17.1). Each row says what its buttons do and what is left
// once unticked (the right-click menu still offers « Voyager », but nothing
// else copies an entry: a position's menu has no « Copier » and the form none
// any more; a message unticked for copy can still be sent or prepared, one
// unticked for sending can still be copied), commits only its own setting,
// follows an outside change and is ticked again by the section's reset.
void NotebookSectionRows() {
    Fixture f;
    f.state.settings.notebookCopyButton = f.state.settings.notebookTravelButton = f.state.settings.notebookSendButtons = false;
    initHook(f.page, false, true, SectionId(NotebookSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    Check(Rows(Section(parent, NotebookSection)) == std::vector<Obj>({ui.extraOptions[NotebookPingExtra].get(), ui.extraOptions[NotebookCopyExtra].get(),
        ui.extraOptions[NotebookTravelExtra].get(), ui.extraOptions[NotebookSendExtra].get(), ui.extraOptions[MapAddPositionExtra].get(),
        ui.notebookButtonOption.get()}),
        "Carnet: the pin, copy, travel and send buttons, « Ajouter aux positions », then the notebook's top-bar button");
    struct Row { ExtraIndex extra; bool Settings::* field; const char* title; const char* where; const char* unticked; };
    const Row rows[] = {
        {NotebookCopyExtra, &Settings::notebookCopyButton, "Bouton de copie à côté des entrées", "Dans chaque onglet du carnet, un petit bouton",
            "Décochée, le bouton disparaît et le carnet ne propose plus de copier ses entrées ; un message peut encore être envoyé ou préparé dans le chat avec « Boutons d’envoi à côté des messages ». Activé par défaut."},
        {NotebookTravelExtra, &Settings::notebookTravelButton, "Bouton de voyage à côté des positions", "Dans les positions du carnet, un petit bouton",
            "Décochée, le bouton disparaît ; le clic droit sur une position propose toujours « Voyager », qui lance le même trajet. Activé par défaut."},
        {NotebookSendExtra, &Settings::notebookSendButtons, "Boutons d’envoi à côté des messages", "Dans les messages du carnet, deux petits boutons",
            "Décochée, les deux boutons disparaissent et le carnet ne propose plus d’envoyer ni de préparer ses messages dans le chat ; « Bouton de copie à côté des entrées » permet encore de copier un message pour le coller toi-même dans le chat. Activé par défaut."}};
    for (const auto& row : rows) {
        const auto option = ui.extraOptions[row.extra].get();
        const auto& source = Get(ui.extraSources[row.extra].get());
        Check(Get(option).klass->name == "BoolOption" && Label(option) == row.title && Get(option).refs.at("source") == ui.extraSources[row.extra].get() &&
            !source.boolean && source.defaultBoolean, std::string(row.title) + ": a native checkbox showing the saved choice, ticked as its reset default");
        const auto description = Description(option);
        for (const char* term : {row.where, "à droite de chaque nom", "L’entrée sélectionnée et tes modifications en cours ne changent pas.", row.unticked})
            Check(description.find(term) != std::string::npos, std::string(row.title) + " says: " + term);
        Check(description.ends_with(" Activé par défaut.") && description.find('\'') == std::string::npos,
            std::string(row.title) + ": its default last, typographic apostrophes");
    }
    Check(Description(ui.extraOptions[NotebookCopyExtra].get()).find(
        "copie cette entrée dans le presse-papiers : les coordonnées d’une position, par exemple [-25,36], ou le texte d’une note ou d’un message.") !=
        std::string::npos, "The copy row says what is copied in each tab");
    Check(Description(ui.extraOptions[NotebookCopyExtra].get()).find("clic droit") == std::string::npos,
        "The copy row promises no right-click copy: a position's menu has no « Copier »");
    Check(Description(ui.extraOptions[NotebookTravelExtra].get()).find(
        "un petit bouton zaap à droite de chaque nom lance le trajet du mod vers cette position : zaaps, bateaux puis /travel (indisponible en combat).") !=
        std::string::npos, "The travel row says where it goes, how, and when it cannot");
    Check(Description(ui.extraOptions[NotebookSendExtra].get()).find(
        "le premier envoie ce message dans le chat, sur le canal enregistré avec lui (« actuel » : le canal en cours du chat) ; le second le place dans le "
        "brouillon du chat, sans l’envoyer. Ils utilisent le message tel qu’il est enregistré.") != std::string::npos,
        "The send row says what each button does, on which channel and with which text");
    // Each box commits only its own setting, once.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (const auto& row : rows)
        for (bool value : {true, false, true}) {
            Get(ui.extraSources[row.extra].get()).boolean = value;
            ui.Tick();
            expected.*row.field = value;
            ++commits;
            Check(f.state.settings == expected && changes == commits, std::string(row.title) + ": a click changes only its own setting, once");
        }
    // An outside change shows in each box.
    f.state.settings.notebookCopyButton = f.state.settings.notebookTravelButton = f.state.settings.notebookSendButtons = false;
    ui.Sync();
    Check(!Get(ui.extraSources[NotebookCopyExtra].get()).boolean && !Get(ui.extraSources[NotebookTravelExtra].get()).boolean &&
        !Get(ui.extraSources[NotebookSendExtra].get()).boolean, "Outside changes show in the boxes");
    // The section's reset ticks them again.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = f.state.settings;
    api.Call(Section(displayedParent, NotebookSection), "Reset");
    ui.Tick();
    expected.notebookCopyButton = expected.notebookTravelButton = expected.notebookSendButtons = true;
    Check(f.state.settings == expected, "The Carnet reset ticks the three boxes again");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
}

// « Bouton de trajet sur les membres du groupe » (1.19.0): in « Déplacements »,
// right under « Téléportation au conjoint », ticked by default; it says where the
// button is, who gets it, both routes and when it is greyed, commits only its own
// setting, follows an outside change and is ticked again by the section's reset.
void PartyTravelRow() {
    Fixture f;
    f.state.settings.partyTravelButton = false;
    initHook(f.page, false, true, SectionId(TravelSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    const auto option = ui.extraOptions[PartyTravelExtra].get();
    Check(Name(Section(parent, TravelSection)) == "Déplacements" && Rows(Section(parent, TravelSection)) == std::vector<Obj>({ui.transitionsOption.get(),
        ui.preloadOption.get(), ui.spouseOption.get(), option, ui.travelPlannerOption.get(), ui.templeOption.get()}),
        "Déplacements: the members' travel button right under the spouse's teleport");
    const auto& source = Get(ui.extraSources[PartyTravelExtra].get());
    Check(Get(option).klass->name == "BoolOption" && Label(option) == "Bouton de trajet sur les membres du groupe" &&
        Get(option).refs.at("source") == ui.extraSources[PartyTravelExtra].get() && !source.boolean && source.defaultBoolean,
        "A native checkbox showing the saved choice, ticked as its reset default");
    const auto description = Description(option);
    for (const char* term : {"un petit bouton zaap en haut de la case de chaque autre membre du groupe", "ni ton personnage ni les compagnons",
            "« Téléportation au conjoint »", "ses coordonnées et sa zone", "« Remplacer l’autopilote du jeu » cochée, par le trajet du mod",
            "décochée, par l’autopilote du jeu, comme « Voyager jusqu’à cette position »", "grisé pendant un combat", "position du membre est inconnue",
            "Rien ne part sans ton clic."})
        Check(description.find(term) != std::string::npos, std::string("The travel row says: ") + term);
    Check(description.ends_with(" Activé par défaut.") && description.find('\'') == std::string::npos, "Its default last, typographic apostrophes");
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (bool value : {true, false, true}) {
        Get(ui.extraSources[PartyTravelExtra].get()).boolean = value;
        ui.Tick();
        expected.partyTravelButton = value;
        Check(f.state.settings == expected && changes == ++commits, "A click changes only the travel button's setting, once");
    }
    f.state.settings.partyTravelButton = false;
    ui.Sync();
    Check(!Get(ui.extraSources[PartyTravelExtra].get()).boolean, "An outside change shows in the box");
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = f.state.settings;
    api.Call(Section(displayedParent, TravelSection), "Reset");
    ui.Tick();
    expected = WithSectionDefaults(expected, TravelSection);
    Check(f.state.settings == expected && f.state.settings.partyTravelButton, "The Déplacements reset ticks it again");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
}

// « Bouton dans la barre du haut » (1.16.1) of Capture d’âme (« Pierre d’âme »,
// after the two soul rows), of the treasure hunt (« Chasse au trésor », its only
// row, between « Carnet » and « Déplacements ») and of the notebook (last row of
// « Carnet »): titled like the statistics window's and the overlay's, each row
// says what its button does and what is left once unticked (the treasure panel
// and the notebook have no other shortcut), commits only its own setting,
// follows an outside change and is ticked again by its own section's reset.
void TopBarButtonRows() {
    Fixture f;
    f.state.settings.soulButton = f.state.settings.treasureButton = f.state.settings.notebookButton = false;
    initHook(f.page, false, true, SectionId(TreasureSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    Check(Name(Section(parent, TreasureSection)) == "Chasse au trésor" && Name(Section(parent, TreasureSection - 1)) == "Carnet" &&
        Name(Section(parent, TreasureSection + 1)) == "Déplacements" &&
        Rows(Section(parent, TreasureSection)) == std::vector<Obj>({ui.treasureButtonOption.get()}),
        "« Chasse au trésor », between « Carnet » and « Déplacements », holds its button's row alone");
    const auto& soul = Rows(Section(parent, SoulSection));
    Check(soul.at(0) == ui.soulOption.get() && soul.at(1) == ui.soulAutoOption.get() && soul.at(2) == ui.soulButtonOption.get() &&
        soul.at(3) == ui.metamobSyncOption.get(), "Pierre d’âme: Capture d’âme's button after the alert and the automatic equipment, before Metamob");
    Check(Rows(Section(parent, NotebookSection)).back() == ui.notebookButtonOption.get(), "Carnet: the notebook's button last");
    struct Row { const Ref& option; const Ref& source; bool Settings::* field; std::vector<const char*> terms; };
    const Row rows[] = {
        {ui.soulButtonOption, ui.soulButtonSource, &Settings::soulButton,
            {"Ajoute en haut à droite de l’écran, à gauche des autres boutons du mod, le bouton Capture d’âme",
             "un clic active ou désactive l’alerte pierre d’âme, comme la case « Alerte pierre d’âme » ci-dessus",
             "le bouton reste enfoncé tant qu’elle est active", "Décochée, le bouton disparaît sans rien changer à l’alerte",
             "qui ne se réactive alors qu’avec « Alerte pierre d’âme »", "les autres boutons du mod restent à leur place"}},
        {ui.treasureButtonOption, ui.treasureButtonSource, &Settings::treasureButton,
            {"Ajoute en haut à droite de l’écran, à côté de Capture d’âme, un bouton qui ouvre ou ferme le panneau de chasse au trésor",
             "C’est le seul moyen d’ouvrir ce panneau : décochée, le bouton disparaît et le panneau ne s’ouvre plus ; recoche-la pour retrouver le bouton.",
             "Un panneau déjà ouvert reste ouvert et se ferme toujours avec sa croix ou « Masquer le module » de son menu."}},
        {ui.notebookButtonOption, ui.notebookButtonSource, &Settings::notebookButton,
            {"Ajoute en haut à droite de l’écran, à côté de Capture d’âme, un bouton qui ouvre ou ferme le carnet.",
             "C’est le seul moyen d’ouvrir le carnet : décochée, le bouton disparaît et le carnet ne s’ouvre plus ; recoche-la pour retrouver le bouton.",
             "Tes entrées restent enregistrées",
             "Un carnet déjà ouvert reste ouvert et se ferme toujours avec sa croix ou « Masquer le module » de son menu."}}};
    for (const auto& row : rows) {
        const auto& source = Get(row.source.get());
        Check(Get(row.option.get()).klass->name == "BoolOption" && Label(row.option.get()) == "Bouton dans la barre du haut" &&
            Label(row.option.get()) == Label(ui.statsButtonOption.get()) && Get(row.option.get()).refs.at("source") == row.source.get() &&
            source.klass->name == "BoolSource" && !source.boolean && source.defaultBoolean,
            "A native checkbox titled like the other top-bar buttons, showing the saved choice, ticked as its reset default");
        const auto description = Description(row.option.get());
        for (const char* term : row.terms)
            Check(description.find(term) != std::string::npos, std::string("« Bouton dans la barre du haut » says: ") + term);
        Check(description.ends_with(" Activé par défaut.") && description.find('\'') == std::string::npos && description.find("⋯") == std::string::npos,
            "Its default last, typographic apostrophes, no glyph the game's fonts lack");
    }
    // Each box commits only its own setting, once; an outside change shows in it.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (const auto& row : rows)
        for (bool value : {true, false, true}) {
            Get(row.source.get()).boolean = value;
            ui.Tick();
            expected.*row.field = value;
            ++commits;
            Check(f.state.settings == expected && changes == commits, "A click changes only its own button's setting, once");
        }
    f.state.settings.soulButton = f.state.settings.treasureButton = f.state.settings.notebookButton = false;
    ui.Sync();
    for (const auto& row : rows) Check(!Get(row.source.get()).boolean, "Outside changes show in the boxes");
    // Each section's reset ticks its own box again and leaves the other two.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    const std::pair<size_t, bool Settings::*> sections[] = {
        {SoulSection, &Settings::soulButton}, {TreasureSection, &Settings::treasureButton}, {NotebookSection, &Settings::notebookButton}};
    for (const auto& [index, field] : sections) {
        expected = WithSectionDefaults(f.state.settings, index);
        api.Call(Section(displayedParent, index), "Reset");
        ui.Tick();
        Check(f.state.settings == expected && f.state.settings.*field, "A section's reset ticks its own top-bar button again, nothing else");
    }
    Check(f.state.settings.soulButton && f.state.settings.treasureButton && f.state.settings.notebookButton, "Every button back after the three resets");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(!ui.soulButtonSource.get() && !ui.soulButtonOption.get() && !ui.treasureButtonSource.get() && !ui.treasureButtonOption.get() &&
        !ui.notebookButtonSource.get() && !ui.notebookButtonOption.get(), "Restore releases the rows");
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
        Get(f.Ui().glowSource.get()).integer = invalid;
        f.Ui().Tick();
        Check(changes == 0 && Get(f.Ui().glowSource.get()).integer == 1, "Invalid glow mode must revert without persistence");
    }
    for (const auto source : {f.Ui().mapCoordsSource.get(), f.Ui().miniMapCoordsSource.get(), f.Ui().chatCoordsSource.get()})
        for (int invalid : {-1, 3, 99}) {
            Get(source).integer = invalid;
            f.Ui().Tick();
            Check(changes == 0 && Get(source).integer == 1, "An invalid dimension choice must revert without persistence");
        }
    // « Curseur »: fifteen colours (0 to 14), three sizes (0 to 2).
    for (const auto& [source, invalids] : {std::pair{f.Ui().customCursorColorSource.get(), std::vector<int>{-1, 15, 99}},
             std::pair{f.Ui().customCursorSizeSource.get(), std::vector<int>{-1, 3, 99}}})
        for (int invalid : invalids) {
            Get(source).integer = invalid;
            f.Ui().Tick();
            Check(changes == 0 && Get(source).integer == 0, "An invalid cursor colour or size must revert without persistence");
        }
    CheckLayout(f);
    f.StopAndRestore();
}

// « Dimension avec les coordonnées » (1.16.0): the big map and the mini-map in
// « Cartes », the chat in « Chat », the same three choices (ids the saved
// modes), « Oui, sauf dans le Monde des Douze » by default. Each row shows its
// saved choice, commits only its own, follows an outside change and goes back
// to « Oui, sauf » with its section's reset.
void DimensionChoiceRows() {
    Fixture f;
    f.state.settings.coordsWorldMapMode = 0; f.state.settings.coordsWorldMiniMapMode = 2; f.state.settings.coordsWorldChatMode = 0;
    initHook(f.page, false, true, SectionId(MapsSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    struct Row { Obj option, source; int Settings::* field; const char* title; };
    const Row rows[] = {
        {ui.mapCoordsOption.get(), ui.mapCoordsSource.get(), &Settings::coordsWorldMapMode, "Dimension avec les coordonnées sur la grande carte"},
        {ui.miniMapCoordsOption.get(), ui.miniMapCoordsSource.get(), &Settings::coordsWorldMiniMapMode, "Dimension avec les coordonnées sur la mini-carte"},
        {ui.chatCoordsOption.get(), ui.chatCoordsSource.get(), &Settings::coordsWorldChatMode, "Dimension avec les coordonnées dans le chat"}};
    // « Cartes »: the big map's then the mini-map's choice after the zone name,
    // before the archimonsters; « Chat »: the chat's choice last.
    const auto parent = f.Categories().back();
    const auto& maps = Rows(Section(parent, MapsSection));
    const auto zone = std::find(maps.begin(), maps.end(), ui.extraOptions[MiniMapZoneNameExtra].get());
    Check(maps.end() - zone >= 4 && zone[1] == rows[0].option && zone[2] == rows[1].option && zone[3] == ui.archiZonesOption.get(),
        "Cartes: the big map's and the mini-map's dimension after the zone name, before the archimonsters");
    Check(Rows(Section(parent, ChatSection)).back() == rows[2].option, "Chat: the dimension last");
    const char* labels[] = {"Non", "Oui, sauf dans le Monde des Douze", "Oui"};
    for (const auto& row : rows) {
        const auto& option = Get(row.option);
        const auto& source = Get(row.source);
        Check(option.klass->name == "MultipleChoiceOption" && Label(row.option) == row.title && option.refs.at("source") == row.source &&
            source.klass->name == "IntSource" && source.integer == f.state.settings.*row.field && source.defaultInteger == 1,
            std::string(row.title) + ": a native choice showing the saved mode, « Oui, sauf » as its reset default");
        const auto& choices = Get(option.refs.at("choices")).items;
        Check(choices.size() == 3, std::string(row.title) + ": three choices");
        for (size_t i = 0; i != 3; ++i)
            Check(Get(choices[i]).choice.id == static_cast<int>(i) && Get(Get(choices[i]).choice.label).text == labels[i],
                std::string(row.title) + ": Non, Oui sauf dans le Monde des Douze, Oui, each id its saved mode");
        const auto description = Description(row.option);
        for (const char* term : {"[x,y, dimension]", "« Oui, sauf dans le Monde des Douze » ", "(Incarnam, Srambad…)"})
            Check(description.find(term) != std::string::npos, std::string(row.title) + " says: " + term);
        Check(description.ends_with(". « Oui, sauf dans le Monde des Douze » par défaut.") && description.find('\'') == std::string::npos &&
            description.find("Activé par défaut") == std::string::npos && description.find("« Oui » par défaut") == std::string::npos,
            std::string(row.title) + ": its default last, typographic apostrophes");
    }
    for (size_t i = 0; i != 2; ++i)
        for (const char* term : {"l’infobulle de la case survolée", "« Oui » l’ajoute partout",
                "« Oui, sauf dans le Monde des Douze » seulement dans les autres dimensions",
                "« Non » jamais : l’infobulle garde les coordonnées du jeu, [x,y]."})
            Check(Description(rows[i].option).find(term) != std::string::npos, std::string(rows[i].title) + " explains: " + term);
    Check(Description(rows[0].option).rfind("Sur la grande carte", 0) == 0 && Description(rows[1].option).rfind("Sur la mini-carte", 0) == 0,
        "Each map's description names its map");
    // Each choice commits only its own setting, once.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (const auto& row : rows)
        for (int mode : {1, 2, 0}) {
            auto& source = Get(row.source);
            if (source.integer == mode) continue;
            source.integer = mode;
            ui.Tick();
            expected.*row.field = mode;
            ++commits;
            Check(f.state.settings == expected && changes == commits, std::string(row.title) + ": a choice changes only its own setting, once");
        }
    // An outside change shows in each row.
    f.state.settings.coordsWorldMapMode = 2; f.state.settings.coordsWorldMiniMapMode = 1; f.state.settings.coordsWorldChatMode = 2;
    ui.Sync();
    Check(Get(rows[0].source).integer == 2 && Get(rows[1].source).integer == 1 && Get(rows[2].source).integer == 2, "Outside changes show in the rows");
    // Each section's reset: its own rows back to « Oui, sauf », the other one's kept.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = f.state.settings;
    api.Call(Section(displayedParent, MapsSection), "Reset");
    ui.Tick();
    expected.coordsWorldMapMode = expected.coordsWorldMiniMapMode = 1;
    Check(f.state.settings == expected, "The Cartes reset: both maps back to « Oui, sauf », the chat kept");
    api.Call(Section(displayedParent, ChatSection), "Reset");
    ui.Tick();
    expected.coordsWorldChatMode = 1;
    Check(f.state.settings == expected, "The Chat reset: the chat back to « Oui, sauf »");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(!ui.mapCoordsSource.get() && !ui.mapCoordsOption.get() && !ui.miniMapCoordsSource.get() && !ui.miniMapCoordsOption.get() &&
        !ui.chatCoordsSource.get() && !ui.chatCoordsOption.get(), "Restore releases the three dimension rows");
}

// « Réglages des cartes au clic droit » (1.16.0): the last row of « Cartes »,
// ticked by default. It says what each map's right-click menu gets, commits
// only its own setting, follows an outside change and is ticked again by the
// section's reset.
void MapMenuSettingsRow() {
    Fixture f;
    f.state.settings.mapMenuSettings = false;
    initHook(f.page, false, true, SectionId(MapsSection), nullptr);
    CheckLayout(f);
    auto& ui = f.Ui();
    const auto parent = f.Categories().back();
    Check(Rows(Section(parent, MapsSection)).back() == ui.mapMenuOption.get(), "Cartes: the right-click settings last, after the archimonsters");
    const auto option = ui.mapMenuOption.get();
    const auto& source = Get(ui.mapMenuSource.get());
    Check(Get(option).klass->name == "BoolOption" && Label(option) == "Réglages des cartes au clic droit" && Get(option).refs.at("source") == ui.mapMenuSource.get() &&
        !source.boolean && source.defaultBoolean, "Réglages des cartes au clic droit: a native checkbox showing the saved choice, ticked as its reset default");
    const auto description = Description(option);
    for (const char* term : {"Au clic droit sur la grande carte ou la mini-carte, ajoute sous les entrées du jeu les réglages du mod pour cette carte",
            "sur la mini-carte, les ressources, leur barre, le bouton de recentrage, le nom de la zone au survol, les archimonstres manquants et la dimension avec les coordonnées",
            "sur la grande carte, les ressources, les archimonstres manquants et la dimension avec les coordonnées",
            "Un clic s’applique aussitôt"})
        Check(description.find(term) != std::string::npos, std::string("Réglages des cartes au clic droit says: ") + term);
    Check(description.ends_with(" Activé par défaut.") && description.find('\'') == std::string::npos,
        "Réglages des cartes au clic droit: its default last, typographic apostrophes");
    // The box commits only its own setting, once per change.
    auto expected = f.state.settings;
    unsigned commits = 0;
    for (bool value : {true, false, true}) {
        Get(ui.mapMenuSource.get()).boolean = value;
        ui.Tick();
        expected.mapMenuSettings = value;
        ++commits;
        Check(f.state.settings == expected && changes == commits, "Réglages des cartes au clic droit: a click changes only its own setting, once");
    }
    // An outside change shows in the box.
    f.state.settings.mapMenuSettings = false;
    ui.Sync();
    Check(!Get(ui.mapMenuSource.get()).boolean, "An outside change shows in the box");
    // The section's reset ticks it again.
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    expected = WithSectionDefaults(f.state.settings, MapsSection);
    api.Call(Section(displayedParent, MapsSection), "Reset");
    ui.Tick();
    Check(f.state.settings == expected && f.state.settings.mapMenuSettings, "The Cartes reset ticks it again");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
    Check(!ui.mapMenuSource.get() && !ui.mapMenuOption.get(), "Restore releases the row");
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
    Check(openedUrls == std::vector<std::string>{"https://github.com/Altansar69/dofus-enhanced"} && foreignButtonClicks == 0,
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

// Row views are pooled: the key row's controls follow the key row from view
// to view, always in the current state, and leave a view recycled for another row.
void MetamobKeyRowFollowsPooledViews() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    auto view = MakeRowView();
    textInitHook(view.element, keyModel, nullptr);
    Check(nativeTextInits == 1 && Get(view.element).refs.at("option") == keyModel, "The view's native Init runs first, unchanged");
    const auto slot = Slot(view);
    Check(slot && Get(view.row).items == std::vector<Obj>({view.label, slot, view.button}) && Get(slot).refs.at("parent") == view.row,
        "One owned container sits between the row label and its button");
    Check(Get(slot).enumStyles.at("flexDirection") == 2 && Get(slot).enumStyles.at("alignItems") == 2 &&
        Get(slot).styles.at("flexGrow") == 1.f && Get(slot).styles.at("minWidth") == 0.f,
        "The container takes the free width of the row, its controls side by side and centred");
    const auto c = Controls(view);
    Check(Get(c.input).klass->name == "TextInput" && Get(c.input).booleans.at("isPassword") && Get(c.input).integers.at("maxLength") == 256 &&
        Placeholder(c.input) == KeyHint && Get(c.input).styles.at("flexGrow") == 1.f && Get(c.input).styles.at("minHeight") == 32.f,
        "No key saved: the native masked input, bounded, with the key hint, taking the free width");
    CheckEditable(view, false, "Nothing typed: the masked field without Enregistrer");
    Check(inputsCreated == 2 && f.Ui().keyFields.size() == 1, "One masked field and one stars field per view");
    // Re-Init of the same view, also with the native search clone of the row:
    // the same controls and no new GC reference.
    const auto references = handles.size();
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(keyModel).refs.at("description");
    for (const auto model : {keyModel, clone, keyModel}) textInitHook(view.element, model, nullptr);
    Check(Slot(view) == slot && inputsCreated == 2 && f.Ui().keyFields.size() == 1 && handles.size() == references,
        "Re-Init keeps exactly one set of controls without new references");
    // A key saved meanwhile: a view bound now (a search clone) reads it, and
    // every view of the row shows it at once.
    fakeKeyStored = true;
    auto second = MakeRowView();
    textInitHook(second.element, clone, nullptr);
    CheckSaved(second, "A search clone of the key row shows the saved key");
    CheckSaved(view, "Every view of the row follows at once");
    Check(f.Ui().keyFields.size() == 2 && Slot(second) != slot, "Each view has its own controls");
    fakeKeyStored = false;
    f.Ui().Tick();
    CheckSaved(view, "The key file is read at most once a second");
    Refresh(f);
    CheckEditable(view, false, "A forgotten key brings the empty field back");
    CheckEditable(second, false, "In every view");
    // The view recycled for another row loses its controls, its field emptied
    // first and its native button shown as it was.
    Paste(c.input, FakeKey);
    f.Ui().Tick();
    CheckEditable(view, true, "A typed key shows Enregistrer");
    textInitHook(view.element, f.Ui().creditsOption.get(), nullptr);
    Check(!Slot(view) && Get(view.row).items == std::vector<Obj>({view.label, view.button}) && Typed(c.input).empty() &&
        !Get(slot).refs.at("parent") && f.Ui().keyFields.size() == 1 && SaveShown(view),
        "A view recycled for another row loses the controls, emptied first, its button as it was");
    textInitHook(view.element, keyModel, nullptr);
    Check(Slot(view) && Slot(view) != slot && inputsCreated == 6 && f.Ui().keyFields.size() == 2, "Back on the key row, the view gets fresh controls");
    CheckEditable(view, false, "Fresh controls, empty, Enregistrer hidden");
    // Recycled while Enregistrer is hidden: the native button gets its own display back.
    const auto foreign = Make("TextButtonOption");
    Get(foreign).refs["description"] = Make("DescriptionData");
    textInitHook(view.element, foreign, nullptr);
    Check(!Slot(view) && SaveShown(view) && f.Ui().keyFields.size() == 1, "A foreign row gets no controls and its button as it was");
    // A stopping module adds no controls.
    auto third = MakeRowView();
    f.state.stopRequested.store(true);
    textInitHook(third.element, keyModel, nullptr);
    Check(!Slot(third) && f.Ui().keyFields.size() == 1 && SaveShown(third), "A stopping module adds no controls");
    f.state.stopRequested.store(false);
    textInitHook(view.element, keyModel, nullptr);
    Check(f.Ui().keyFields.size() == 2 && SaveHidden(view) && SaveHidden(second), "Two views, both without Enregistrer");
    // Restore removes every control, gives each native button its own display
    // back and releases every Metamob reference.
    f.StopAndRestore();
    Check(f.Ui().keyFields.empty() && !Slot(view) && !Slot(second) && SaveShown(view) && SaveShown(second),
        "Restore removes the owned controls from the pooled views and shows their buttons as they were");
    Check(!f.Ui().metamobSyncSource.get() && !f.Ui().metamobSyncOption.get() && !f.Ui().metamobKeyOption.get() && !f.Ui().metamobKeyMarker.get(),
        "Restore releases the Metamob rows");
    textInitHook(view.element, keyModel, nullptr);
    Check(!Slot(view) && f.Ui().keyFields.empty(), "After restore the stale key row gets no controls");
    CheckNoKeyTrace();
}

// No key saved: Enregistrer appears with the first typed character, without
// copying the key; a click reads the field once, empties it and submits it.
void MetamobKeySaveFollowsTyping() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    const auto field = Controls(view).input;
    CheckEditable(view, false, "Nothing typed: no Enregistrer");
    Paste(field, "x");
    f.Ui().Tick();
    CheckEditable(view, true, "One character shows Enregistrer");
    Paste(field, "");
    f.Ui().Tick();
    CheckEditable(view, false, "An emptied field hides it again");
    Paste(field, FakeKey);
    const auto reads = lengthReads;
    for (int i = 0; i != 5; ++i) f.Ui().Tick();
    CheckEditable(view, true, "A pasted key shows Enregistrer");
    Check(lengthReads >= reads + 5 && keyCopies == 0, "Each Tick reads the field's length only: no character of the key is copied");
    Click(view);
    Check(submittedKeys == std::vector<std::string>{FakeKey} && Typed(field).empty() && keyCopies == 1 && notices.empty() &&
        foreignButtonClicks == 0 && openedUrls.empty(), "Enregistrer reads the field once, empties it and submits the key, never the native action");
    CheckEditable(view, false, "The emptied field hides Enregistrer at once");
    // The native search clone of the key row in another view uses its own
    // field; the text goes as typed (the capture sync trims and validates it).
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(f.Ui().metamobKeyOption.get()).refs.at("description");
    auto other = MakeRowView();
    textInitHook(other.element, clone, nullptr);
    const std::string spaced = std::string(" ") + FakeKey + " ";
    Paste(Controls(other).input, spaced);
    Click(other);
    Check(submittedKeys.size() == 2 && submittedKeys.back() == spaced && Typed(Controls(other).input).empty() && keyCopies == 2,
        "A search clone of the key row submits its own field");
    // A view of the key row whose controls never appeared: only the chat hint.
    const auto bare = Make("TextButtonOptionElement");
    Get(bare).refs["option"] = f.Ui().metamobKeyOption.get();
    textButtonHook(bare, Make("Visual"), nullptr);
    Check(notices == std::vector<std::string>{PasteKeyFirst} && submittedKeys.size() == 2 && foreignButtonClicks == 0,
        "Without controls the button only asks for the key");
    // A stopping module ignores the button: the field stays, no native action.
    Paste(field, FakeKey);
    f.state.stopRequested.store(true);
    Click(view);
    Check(Typed(field) == FakeKey && submittedKeys.size() == 2 && keyCopies == 2 && foreignButtonClicks == 0, "A stopping module ignores the key button");
    f.state.stopRequested.store(false);
    // Capture sync not wired: the field is still emptied, nothing is called.
    f.Ui().metamob = MetamobHooks{};
    Click(view);
    Click(view);
    Check(Typed(field).empty() && submittedKeys.size() == 2 && notices.size() == 1 && foreignButtonClicks == 0 && keyCopies == 3,
        "Without capture sync hooks a click only empties the field");
    Check(!f.state.stopRequested.load() && !f.state.stopped.load(), "Key handling never stops the mod");
    CheckNoKeyTrace();
    f.StopAndRestore();
}

// A key Metamob accepted: fixed stars, « Clé valide » and the trash, no field
// and no Enregistrer. The trash asks first: « Non » keeps the key, « Oui » forgets it
// and the row is the empty field again.
void MetamobSavedKeyTrashAsksFirst() {
    Fixture f;
    fakeKeyStored = true;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    const auto c = Controls(view);
    CheckSaved(view, "A saved key: no field to edit, no Enregistrer");
    Check(Get(c.stars).klass->name == "TextInput" && Typed(c.stars) == KeyStars && Get(c.stars).booleans.at("isPassword") &&
        !Get(c.stars).booleans.at("enabled"), "Fixed stars in a disabled masked field: nothing to edit, copy or reveal");
    Check(Get(c.indicator).klass->name == "DofusButtonCustom" && Caption(c.indicator) == ValidKey && Get(c.indicator).integers.at("icon") == 91 &&
        Get(c.indicator).integers.at("status") == 0 && Get(c.indicator).integers.at("size") == 0 && Get(c.trash).klass->name == "DofusButtonCustom" &&
        Get(c.trash).integers.at("icon") == 316 &&
        Caption(c.trash).empty() && Get(c.trash).integers.at("status") == 1, "« Clé valide » with its tick, then the game's trash icon button");
    Check(Get(c.question).klass->name == "DofusLabel" && Caption(c.question) == ConfirmQuestion && Get(c.question).enumStyles.at("whiteSpace") == 0 &&
        Caption(c.yes) == "Oui" && Get(c.yes).integers.at("status") == 1 && Caption(c.no) == "Non", "The question and its answers wait, hidden");
    // The hidden Enregistrer, clicked anyway: nothing read, said or submitted.
    Click(view);
    Check(notices.empty() && submittedKeys.empty() && keyCopies == 0 && foreignButtonClicks == 0, "A saved key: a click on the hidden Enregistrer does nothing");
    // The indicator only explains.
    Check(Press(c.indicator) && notices == std::vector<std::string>{ValidKeyNotice} && keyForgets == 0 && keyChecks.empty(),
        "« Clé valide » only explains, in the chat");
    CheckSaved(view, "and changes nothing");
    // The trash asks; « Non » keeps the key.
    Check(Press(c.trash) && keyForgets == 0 && f.Ui().confirmForget, "The trash forgets nothing by itself");
    CheckQuestion(view, "The trash asks: Voulez-vous supprimer la clé Metamob ?");
    f.Ui().Tick();
    CheckQuestion(view, "The question stays while it waits for an answer");
    Check(Press(c.no) && keyForgets == 0 && !f.Ui().confirmForget, "« Non » keeps the key");
    CheckSaved(view, "« Non » shows the saved key again");
    // Unanswered, the question goes after its delay; a late « Oui » does nothing.
    Press(c.trash);
    f.Ui().confirmDeadline = 1; // asked long ago: expired
    f.Ui().Tick();
    CheckSaved(view, "An unanswered question goes, the key stays");
    Check(Press(c.yes) && keyForgets == 0, "« Oui » after the question went forgets nothing");
    // A stopping module ignores the row's buttons.
    f.state.stopRequested.store(true);
    Check(Press(c.trash) && !f.Ui().confirmForget, "A stopping module ignores the trash");
    f.state.stopRequested.store(false);
    // The trash then « Oui »: forgotten, and the row is the empty field at once.
    Press(c.trash);
    Check(Press(c.yes) && keyForgets == 1 && !f.Ui().confirmForget, "« Oui » forgets the key through the capture sync");
    CheckEditable(view, false, "The row is the empty field again, Enregistrer hidden");
    Refresh(f); // the key file is still there: the capture sync says none until its worker deletes it
    CheckEditable(view, false, "Still the empty field while the key file is being deleted");
    fakeKeyStored = fakeForgetting = false;
    Refresh(f);
    CheckEditable(view, false, "Deleted: the empty field");
    Check(!f.Ui().confirmForget && f.Ui().keyStatus == metamob::KeyStatus::None, "Nothing pending once the file is gone");
    Paste(c.input, FakeKey);
    f.Ui().Tick();
    CheckEditable(view, true, "A new key can be typed, Enregistrer shown");
    Check(foreignButtonClicks == 0 && openedUrls.empty() && submittedKeys.empty() && keyCopies == 0, "The row's buttons never run a native action");
    // A deletion that fails (the capture sync says so in the chat): the saved key shows again once it has tried.
    fakeKeyStored = true;
    Refresh(f);
    CheckSaved(view, "A key saved again: the typed text is dropped");
    Press(c.trash);
    Press(c.yes);
    Check(keyForgets == 2, "Asked once more");
    CheckEditable(view, false, "Shown empty while the deletion runs");
    fakeForgetting = false; // the capture sync could not delete the file
    Refresh(f);
    CheckSaved(view, "A key that could not be deleted shows again");
    // Without capture sync hooks « Oui » forgets nothing and the key stays shown.
    const auto hooks = f.Ui().metamob;
    f.Ui().metamob.forgetKey = nullptr;
    Press(c.trash);
    Press(c.yes);
    CheckSaved(view, "Nothing to forget with: the saved key stays");
    f.Ui().metamob = hooks;
    // Not the key row's buttons: the game's click goes on.
    Check(!Press(view.button) && !Press(Make("DofusButtonCustom")), "Other buttons keep the game's click");
    // Restore while asking: no question left anywhere.
    Press(c.trash);
    CheckQuestion(view, "Asking before stopping");
    f.StopAndRestore();
    Check(!f.Ui().confirmForget && f.Ui().keyFields.empty() && !Slot(view) && SaveShown(view), "Restore removes the question with the controls");
    Check(!Press(c.trash) && !Press(c.yes) && keyForgets == 2, "After restore the stale buttons do nothing");
    CheckNoKeyTrace();
}

// After Enregistrer the capture sync checks the key with Metamob: accepted
// (stored), the row shows the saved key; refused, it stays the empty field.
// A key saved by another client while one is typed here replaces the field.
void MetamobSavedStateFollowsTheCheck() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    const auto c = Controls(view);
    Paste(c.input, FakeKey);
    Click(view);
    Check(submittedKeys.size() == 1, "The key goes to the capture sync");
    Refresh(f);
    CheckEditable(view, false, "Not stored yet (being checked, or refused): the empty field, Enregistrer hidden");
    fakeKeyStored = true; // accepted by Metamob: stored
    Refresh(f);
    CheckSaved(view, "Accepted: the saved key");
    Check(Typed(c.stars) == KeyStars && Typed(c.stars).size() != std::string(FakeKey).size() && !Leaks(Typed(c.stars)),
        "Fixed stars, neither the key's characters nor its length");
    // Forgotten by another client: the empty field; a key typed there, then one
    // saved by another client: the typed text goes, the saved key shows.
    fakeKeyStored = false;
    Refresh(f);
    CheckEditable(view, false, "Forgotten elsewhere: the empty field");
    Paste(c.input, "TEST-KEY-0123");
    f.Ui().Tick();
    CheckEditable(view, true, "Typing");
    fakeKeyStored = true;
    Refresh(f);
    CheckSaved(view, "A key saved by another client shows here");
    Check(Typed(c.input).empty() && keyCopies == 1, "The text typed meanwhile is dropped, never copied");
    // Refused: nothing stored, the row stays the field, the capture sync says why in the chat.
    fakeKeyStored = false;
    Refresh(f);
    Paste(c.input, FakeKey);
    Click(view);
    Refresh(f);
    CheckEditable(view, false, "Refused: still the empty field");
    Check(submittedKeys.size() == 2 && keyCopies == 2, "Submitted once more");
    f.StopAndRestore();
    CheckNoKeyTrace();
}

// Pooled views and display/search clones show the current state: set again at
// each Init (also of the same view) and kept by Tick.
void MetamobRecycledViewsShowTheCurrentState() {
    Fixture f;
    fakeKeyStored = true;
    initHook(f.page, false, true, SoulId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(keyModel).refs.at("description");
    auto view = MakeRowView(), other = MakeRowView();
    textInitHook(view.element, keyModel, nullptr);
    Press(Controls(view).trash);
    textInitHook(other.element, clone, nullptr);
    CheckQuestion(other, "A clone bound while the question is shown asks too");
    CheckQuestion(view, "and the first view still asks");
    // Recycled for the credits row, then back on the key row: still asking.
    textInitHook(view.element, f.Ui().creditsOption.get(), nullptr);
    Check(!Slot(view) && SaveShown(view), "Recycled: no controls, the button as it was");
    textInitHook(view.element, keyModel, nullptr);
    CheckQuestion(view, "Back on the key row: the question is shown");
    // Answered in one view: every view shows the saved key.
    Press(Controls(other).no);
    CheckSaved(view, "Answered in another view: the saved key");
    CheckSaved(other, "and in the view that answered");
    // The game changed a control of a bound view: its next Init shows the state again.
    Get(Controls(view).stars).enumStyles["display"] = 1;
    Get(view.button).enumStyles["display"] = NativeDisplay;
    textInitHook(view.element, keyModel, nullptr);
    CheckSaved(view, "A re-Init of the same view shows the current state again");
    // « Oui » in one view: every view, and a view bound now, show the empty field.
    Press(Controls(view).trash);
    Press(Controls(other).yes);
    auto third = MakeRowView();
    textInitHook(third.element, clone, nullptr);
    for (const auto* shown : {&view, &other, &third}) CheckEditable(*shown, false, "Deleting: the empty field in every view");
    Check(keyForgets == 1, "Forgotten once");
    // Typing in one view shows Enregistrer in that view only.
    Paste(Controls(third).input, FakeKey);
    f.Ui().Tick();
    CheckEditable(third, true, "Enregistrer in the view typed in");
    CheckEditable(view, false, "not in the others");
    f.StopAndRestore();
    for (const auto* shown : {&view, &other, &third}) Check(!Slot(*shown) && SaveShown(*shown), "Restore leaves every pooled view as it was");
    CheckNoKeyTrace();
}

// The native reset turns the sync off and never touches the key or the key row.
void MetamobNativeResetStopsSyncKeepsKey() {
    Fixture f;
    f.state.settings.metamobSync = true;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    Paste(Controls(view).input, FakeKey);
    const auto before = f.state.settings;
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    api.Call(Section(displayedParent, SoulSection), "Reset");
    Check(!Get(f.Ui().metamobSyncSource.get()).boolean && f.state.settings == before,
        "The section reset restores the off default and leaves persistence to the pump");
    f.Ui().Tick();
    auto expected = before;
    expected.metamobSync = false;
    Check(f.state.settings == expected && changes == 1, "The Pierre d’âme section reset turns the sync off; the soul rows were already at their defaults");
    Get(f.Ui().metamobSyncSource.get()).boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.metamobSync && changes == 2, "The sync can be turned on again");
    api.Call(displayedParent, "Reset");
    f.Ui().Tick();
    Check(f.state.settings == Config(2, 1), "The whole Mod reset turns the sync off with every other default");
    Check(keyForgets == 0 && submittedKeys.empty() && notices.empty() && Typed(Controls(view).input) == FakeKey && keyCopies == 0,
        "A native reset never forgets, submits or announces the key, nor touches the field");
    CheckEditable(view, true, "The key row is as it was");
    fakeKeyStored = true;
    Refresh(f);
    api.Call(displayedParent, "Reset");
    f.Ui().Tick();
    CheckSaved(view, "A native reset leaves a saved key as it is");
    Check(keyForgets == 0 && fakeKeyStored, "and never forgets it");
    f.state.settings.metamobSync = true;
    f.Ui().Sync();
    Check(Get(f.Ui().metamobSyncSource.get()).boolean, "An external change updates the native checkbox");
    f.StopAndRestore();
    CheckNoKeyTrace();
}

// Key row failures: fixed codes, rate limited, never what(), never a stop.
void MetamobKeyFieldErrorsStayLocal() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    auto view = MakeRowView();
    failKeyInsert = true;
    const auto lines = logs.size();
    textInitHook(view.element, keyModel, nullptr);
    Check(nativeTextInits == 1 && !Slot(view) && Get(view.row).items.size() == 2 && f.Ui().keyFields.empty() && SaveShown(view),
        "A failed insertion leaves the native row as it was");
    Check(logs.size() == lines + 1 && logs.back() == "METAMOB_KEY_FIELD_ERROR step=bind", "Key row failures log a fixed code");
    textInitHook(view.element, keyModel, nullptr);
    Check(logs.size() == lines + 1, "Key row failures are logged at most once per ten seconds");
    failKeyInsert = false;
    textInitHook(view.element, keyModel, nullptr);
    Check(Slot(view) && f.Ui().keyFields.size() == 1, "The next Init places the controls");
    // A view whose binding has no button (layout change): no controls.
    const auto broken = Make("TextButtonOptionElement");
    Get(broken).refs["m_binding"] = Make("TextButtonOptionElementBinding");
    f.Ui().nextKeyFieldLog = 0;
    textInitHook(broken, keyModel, nullptr);
    Check(f.Ui().keyFields.size() == 1 && logs.back() == "METAMOB_KEY_FIELD_ERROR step=bind", "A row without its button gets no controls");
    // A managed exception quoting the typed key while the field is read.
    Paste(Controls(view).input, FakeKey);
    failTextRead = true;
    f.Ui().nextKeyFieldLog = 0;
    Click(view);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=save" && submittedKeys.empty() && notices.empty() && foreignButtonClicks == 0,
        "A failed read submits nothing and logs a fixed code, never the exception text");
    // The same failure while Tick follows the field: that view loses its controls.
    f.Ui().nextKeyFieldLog = 0;
    f.Ui().Tick();
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=refresh" && !Slot(view) && f.Ui().keyFields.empty() && SaveShown(view),
        "A view failing in Tick loses its controls, its button as it was, and logs a fixed code");
    failTextRead = false;
    textInitHook(view.element, keyModel, nullptr);
    Check(Slot(view) && f.Ui().keyFields.size() == 1, "Its next Init places them again");
    // A throwing capture sync hook stays inside the options code.
    f.Ui().metamob.submitKey = [](std::string) { throw std::runtime_error(std::string("Synthetic sync failure ") + FakeKey); };
    Paste(Controls(view).input, FakeKey);
    f.Ui().nextKeyFieldLog = 0;
    Click(view);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=save" && Typed(Controls(view).input).empty() && foreignButtonClicks == 0,
        "A throwing submit hook is contained and the field is still emptied");
    // A throwing chat hook behind the indicator: contained as well.
    fakeKeyStored = true;
    Refresh(f);
    f.Ui().metamob.notice = [](const std::string&) { throw std::runtime_error(std::string("Synthetic chat failure ") + FakeKey); };
    f.Ui().nextKeyFieldLog = 0;
    Check(Press(Controls(view).indicator) && logs.back() == "METAMOB_KEY_FIELD_ERROR step=click", "A throwing chat hook is contained");
    CheckSaved(view, "and changes nothing");
    // A throwing check request: contained, the saved key still shown.
    fakeStatus = metamob::KeyStatus::Saved;
    f.Ui().metamob.checkKey = [](bool) { throw std::runtime_error(std::string("Synthetic check failure ") + FakeKey); };
    f.Ui().nextKeyFieldLog = 0;
    Refresh(f);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=check", "A throwing check request logs a fixed code");
    CheckSaved(view, "and the saved key stays shown");
    f.Ui().metamob.keyStatus = []() -> metamob::KeyStatus { throw std::runtime_error("Synthetic key file failure"); };
    f.Ui().nextKeyFieldLog = 0;
    Refresh(f);
    Check(logs.back() == "METAMOB_KEY_FIELD_ERROR step=status", "A failing key status in Tick logs a fixed code");
    CheckEditable(view, false, "and shows the empty field");
    Check(!f.state.stopRequested.load() && !f.state.stopped.load(), "Key row failures never stop the mod");
    // Restore when a container cannot be detached: its references are still released.
    failKeyRemove = true;
    f.Ui().nextKeyFieldLog = 0;
    const auto beforeRestore = logs.size();
    f.StopAndRestore();
    Check(f.Ui().keyFields.empty() && Logged(beforeRestore, "METAMOB_KEY_FIELD_ERROR step=remove") &&
        f.Categories() == std::vector<Obj>({f.game, f.nativeCategory}), "A container that cannot be detached never blocks Restore");
    CheckNoKeyTrace();
}

// The indicator after the stars tells the capture sync's status of the saved
// key, in every pooled view and clone, redrawn when that status changes; its
// click explains it in the chat and says « valide » only for a key Metamob
// accepted. A key not checked yet is checked once while the row is shown;
// « Clé non vérifiée » clicked checks it once more.
void MetamobIndicatorFollowsTheKeyStatus() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    const auto keyModel = f.Ui().metamobKeyOption.get();
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(keyModel).refs.at("description");
    auto view = MakeRowView();
    textInitHook(view.element, keyModel, nullptr);
    CheckEditable(view, false, "No key: the masked field");
    Check(Caption(Controls(view).indicator).empty() && !Get(Controls(view).indicator).integers.count("icon") && keyChecks.empty(),
        "No key: the hidden indicator is never drawn (never « Clé valide » by default), nothing checked");
    textInitHook(view.element, f.Ui().creditsOption.get(), nullptr);
    // A key from an earlier session: checked only once the row is shown, « Vérification… » meanwhile.
    fakeKeyStored = true;
    fakeStatus = metamob::KeyStatus::Saved;
    for (int i = 0; i < 3; ++i) Refresh(f);
    Check(keyChecks.empty(), "Nothing is checked while no view shows the key row");
    textInitHook(view.element, keyModel, nullptr);
    Check(!keyChecks.empty() && std::find(keyChecks.begin(), keyChecks.end(), true) == keyChecks.end(),
        "The row shown with a key not checked yet asks for the silent check");
    auto other = MakeRowView();
    textInitHook(other.element, clone, nullptr);
    struct Expected { metamob::KeyStatus status; std::string caption; int icon, look; std::string notice; };
    const Expected expected[] = {
        {metamob::KeyStatus::Saved, CheckingKey, 172, 0, CheckingNotice},
        {metamob::KeyStatus::Verified, ValidKey, 91, 0, ValidKeyNotice},
        {metamob::KeyStatus::Unverified, UnverifiedKey, 236, 0, UnverifiedNotice},
        {metamob::KeyStatus::Refused, RefusedKey, 87, 1, RefusedNotice},
        {metamob::KeyStatus::Unreadable, UnreadableKey, 94, 1, UnreadableNotice}};
    for (const auto& row : expected) {
        fakeStatus = row.status;
        Refresh(f);
        for (const auto* shown : {&view, &other}) {
            CheckSaved(*shown, "Any key there: the stars, its indicator and the trash");
            const auto indicator = Controls(*shown).indicator;
            Check(Caption(indicator) == row.caption && Get(indicator).integers.at("icon") == row.icon &&
                Get(indicator).integers.at("status") == row.look && Get(indicator).integers.at("size") == 0,
                "Each status has its caption, icon and button status, in every view of the row");
        }
        Check((row.caption.find("valide") != std::string::npos) == (row.status == metamob::KeyStatus::Verified) &&
            (row.notice.find("valide") != std::string::npos) == (row.status == metamob::KeyStatus::Verified),
            "« valide » only for a key Metamob accepted");
        notices.clear();
        const auto checks = keyChecks.size();
        Check(Press(Controls(other).indicator) && notices == std::vector<std::string>{row.notice} && keyForgets == 0,
            "The indicator's click explains that status in the chat");
        if (row.status == metamob::KeyStatus::Unverified) {
            Check(keyChecks.size() > checks && keyChecks[checks] && fakeStatus == metamob::KeyStatus::Saved,
                "« Clé non vérifiée » clicked: the key is checked once more");
            for (const auto* shown : {&view, &other})
                Check(Caption(Controls(*shown).indicator) == CheckingKey, "« Vérification… » at once in every view");
        } else {
            Check(std::find(keyChecks.begin() + static_cast<std::ptrdiff_t>(checks), keyChecks.end(), true) == keyChecks.end(),
                "Only « Clé non vérifiée » asks for a new check");
        }
        CheckSaved(view, "The click changes nothing else");
    }
    // Each view keeps the status it shows: a view bound or recycled now shows the
    // current one, a re-Init draws it again, an unchanged status is left as drawn.
    fakeStatus = metamob::KeyStatus::Refused;
    Refresh(f);
    auto third = MakeRowView();
    textInitHook(third.element, clone, nullptr);
    Check(Caption(Controls(third).indicator) == RefusedKey && Get(Controls(third).indicator).integers.at("status") == 1,
        "A view bound now shows the current status");
    Get(Controls(view).indicator).refs["text"] = String("altered");
    Refresh(f);
    Check(Caption(Controls(view).indicator) == "altered", "An unchanged status is not written again");
    textInitHook(view.element, keyModel, nullptr);
    Check(Caption(Controls(view).indicator) == RefusedKey, "A re-Init of the view draws its status again");
    // A status that changes while the trash asks shows once the question goes.
    Press(Controls(view).trash);
    fakeStatus = metamob::KeyStatus::Verified;
    Refresh(f);
    for (const auto* shown : {&view, &other, &third}) CheckQuestion(*shown, "The question stays");
    Press(Controls(third).no);
    for (const auto* shown : {&view, &other, &third})
        Check(Caption(Controls(*shown).indicator) == ValidKey && Get(Controls(*shown).indicator).integers.at("status") == 0,
            "« Non »: the new status in every view");
    Check(keyForgets == 0 && submittedKeys.empty() && keyCopies == 0 && foreignButtonClicks == 0, "The indicator never forgets, submits or copies");
    f.StopAndRestore();
    CheckNoKeyTrace();
}

// « Oui », then a new key pasted and saved at once: the capture sync deletes the
// old key file and stores the new key in one go, so the file is never seen
// missing. The row follows the capture sync's status (none until the old file
// is deleted), so the new key shows as soon as it is stored, never an empty
// field for a minute.
void MetamobNewKeyRightAfterForget() {
    Fixture f;
    fakeKeyStored = true;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    const auto c = Controls(view);
    CheckSaved(view, "A saved key");
    Press(c.trash);
    Press(c.yes);
    CheckEditable(view, false, "« Oui »: the empty field at once");
    Paste(c.input, FakeKey);
    f.Ui().Tick();
    Click(view);
    Check(keyForgets == 1 && submittedKeys == std::vector<std::string>{FakeKey} && Typed(c.input).empty(), "A new key saved right after");
    Refresh(f);
    CheckEditable(view, false, "The old key file not deleted yet: no key shown");
    fakeForgetting = false; // the old file deleted, the new key stored and verified, between two refreshes
    fakeStatus = metamob::KeyStatus::Verified;
    Refresh(f);
    CheckSaved(view, "The new key shows as soon as it is stored");
    Check(Caption(c.indicator) == ValidKey, "and Metamob accepted it");
    f.StopAndRestore();
    CheckNoKeyTrace();
}

// Logout: the game tore the options page down; the owned controls still leave.
void MetamobFieldsLeaveOnAbandon() {
    Fixture f;
    initHook(f.page, false, true, SoulId, nullptr);
    auto view = MakeRowView();
    textInitHook(view.element, f.Ui().metamobKeyOption.get(), nullptr);
    Press(Controls(view).trash);
    Check(Slot(view) && SaveHidden(view), "Abandon fixture failed to place the controls");
    f.state.stopped.store(true);
    f.Ui().Abandon();
    Check(f.Ui().pages.empty() && f.Ui().keyFields.empty() && !Slot(view) && SaveShown(view) && !f.Ui().CleanupPending() && !f.Ui().confirmForget,
        "Abandon drops the torn-down page and removes the owned controls, the button as it was");
    f.Ui().Restore();
}

// « Statistiques » (1.16.0): the counting checkbox, « Voir mes statistiques »
// (an owned text button known by its own description reference: no source, no
// native action) and the checkbox of the window's top-bar button. The lines,
// the choice of what is shown and the reset of 1.15.0 are in the window now.
void StatsSectionRows() {
    Fixture f;
    initHook(f.page, false, true, SectionId(StatsSection), nullptr);
    CheckLayout(f);
    const auto parent = f.Categories().back();
    auto& ui = f.Ui();
    Check(Rows(Section(parent, StatsSection)) == std::vector<Obj>({ui.statsOption.get(), ui.statsOpenOption.get(), ui.statsButtonOption.get()}),
        "Statistiques: the counting checkbox, « Voir mes statistiques », then the top-bar button's checkbox");
    Check(Name(Section(parent, StatsSection)) == "Statistiques" && Name(Section(parent, StatsSection - 1)) == "Performances" &&
        Name(Section(parent, StatsSection + 1)) == "Chargement et mises à jour", "« Statistiques » after « Performances », before « Chargement et mises à jour »");
    for (const auto option : AllOptions(ui))
        Check(Label(option) != "Statistiques affichées" && Label(option).rfind("— ", 0) != 0 && Label(option).rfind("Temps de jeu", 0) != 0 &&
            Label(option).rfind("Réinitialiser les statistiques", 0) != 0, "No scope choice, title line, figure or reset left in the options");
    const auto counting = Description(ui.statsOption.get());
    for (const char* term : {"Compte pour chacun de tes personnages", "temps de jeu", "kamas", "combats", "téléportations", "version 1.15.0",
            "pendant que le mod est chargé", "Tout reste sur ce PC", "rien n’est envoyé", "Décoché, rien n’est compté", "Activé par défaut."})
        Check(counting.find(term) != std::string::npos, std::string("The counting checkbox is unchanged: ") + term);
    // « Voir mes statistiques »: « Ouvrir », with the window's chart icon.
    const auto open = ui.statsOpenOption.get();
    const auto& model = Get(open);
    Check(model.klass->name == "TextButtonOption" && !model.refs.at("source") && ui.statsOpenMarker.get() == model.refs.at("description") &&
        ui.statsOpenMarker.get() != ui.creditsMarker.get() && ui.statsOpenMarker.get() != ui.metamobKeyMarker.get(),
        "« Voir mes statistiques » is an owned text button known by its own description reference");
    Check(Label(open) == "Voir mes statistiques" && ButtonText(open) == "Ouvrir" && model.integers.at("icon") == 77,
        "Its button: « Ouvrir », with FigmaIcons.chart, the window's icon");
    const auto opening = Description(open), button = Description(ui.statsButtonOption.get());
    for (const char* term : {"Ouvre la fenêtre « Statistiques »", "pour ce personnage ou pour tous tes personnages", "temps et déplacements",
            "téléportations", "sur deux ou trois colonnes", "chaque seconde tant qu’elle est ouverte", "Le bouton « Réinitialiser »",
            "en haut à droite de l’écran, à côté de Capture d’âme"})
        Check(opening.find(term) != std::string::npos, std::string("« Voir mes statistiques » says: ") + term);
    for (const char* term : {"en haut à droite de l’écran, à côté de Capture d’âme", "ouvre ou ferme la fenêtre « Statistiques »",
            "Décoché, le bouton disparaît", "« Voir mes statistiques »", "Activé par défaut."})
        Check(button.find(term) != std::string::npos, std::string("« Bouton dans la barre du haut » says: ") + term);
    for (const auto& text : {opening, button}) Check(text.find('\'') == std::string::npos, "Typographic apostrophes");
    // Ouvrir opens the window, from any view of the row, never the native action.
    auto view = MakeRowView();
    textInitHook(view.element, open, nullptr);
    Click(view);
    Check(statsOpens == 1 && foreignButtonClicks == 0 && changes == 0, "Ouvrir opens the statistics window, nothing else");
    Check(SaveShown(view) && !Slot(view), "Its view keeps the native button as it is");
    const auto clone = Make("TextButtonOption");
    Get(clone).refs["description"] = Get(open).refs.at("description");
    auto cloneView = MakeRowView();
    textInitHook(cloneView.element, clone, nullptr);
    Click(cloneView);
    Check(statsOpens == 2 && foreignButtonClicks == 0, "A native search clone of the row opens it too");
    // A stopping module ignores the button, still without the native action.
    f.state.stopRequested.store(true);
    Click(view);
    f.state.stopRequested.store(false);
    Check(statsOpens == 2 && foreignButtonClicks == 0, "A stopping module ignores it");
    f.StopAndRestore();
    Check(!ui.statsOpenOption.get() && !ui.statsOpenMarker.get() && !ui.statsButtonSource.get() && !ui.statsButtonOption.get(), "Restore releases the rows");
}

// « Bouton dans la barre du haut »: its own source (ticked by default),
// committing statsButton once per change and following outside changes.
void StatsButtonCommits() {
    Fixture f;
    f.state.settings.statsButton = false;
    initHook(f.page, false, true, SectionId(StatsSection), nullptr);
    auto& source = Get(f.Ui().statsButtonSource.get());
    Check(source.klass->name == "BoolSource" && !source.boolean && source.defaultBoolean, "The saved choice shown, ticked by default");
    source.boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.statsButton && changes == 1, "Ticked: the top-bar button comes back");
    auto expected = f.state.settings;
    source.boolean = false;
    f.Ui().Tick();
    expected.statsButton = false;
    Check(f.state.settings == expected && changes == 2 && f.state.settings.statsEnabled, "Unticking changes only statsButton");
    f.state.settings.statsButton = true;
    f.Ui().Sync();
    Check(source.boolean, "An outside change shows in the checkbox");
    f.StopAndRestore();
}

// « Compter mes statistiques »: its own source (ticked by default), committing
// statsEnabled once per change and following external changes.
void StatsCheckboxCommits() {
    Fixture f;
    f.state.settings.statsEnabled = false;
    initHook(f.page, false, true, SectionId(StatsSection), nullptr);
    auto& source = Get(f.Ui().statsSource.get());
    Check(source.klass->name == "BoolSource" && !source.boolean && source.defaultBoolean, "The saved choice shown, ticked by default");
    source.boolean = true;
    f.Ui().Tick();
    Check(f.state.settings.statsEnabled && changes == 1, "Ticking it counts again");
    auto expected = f.state.settings;
    source.boolean = false;
    f.Ui().Tick();
    expected.statsEnabled = false;
    Check(f.state.settings == expected && changes == 2, "Unticking changes only statsEnabled");
    f.state.settings.statsEnabled = true;
    f.Ui().Sync();
    Check(source.boolean, "An external change shows in the checkbox");
    f.StopAndRestore();
}

// « Performances » (1.16.0): five checkboxes and no corner choice. Each part
// is its own box and shows the overlay alone; the mouse gestures unticked
// lock the overlay at its place and size; its top-bar button hides it and
// shows it again. Neither that place, that size, that hiding nor the corner
// an earlier revision chose is a row: the pump and the section reset keep
// them, except that a part ticked here, or the button removed, shows again
// an overlay the button hid.
void PerformanceSectionRows() {
    Fixture f;
    f.state.settings.perfOverlayCorner = 3;
    f.state.settings.perfOverlayX = 6400; f.state.settings.perfOverlayY = 1200;
    f.state.settings.perfOverlayScaleX = 150; f.state.settings.perfOverlayScaleY = 90;
    initHook(f.page, false, true, SectionId(PerformanceSection), nullptr);
    CheckLayout(f);
    const auto parent = f.Categories().back();
    const auto& rows = Rows(Section(parent, PerformanceSection));
    auto& ui = f.Ui();
    Check(rows == std::vector<Obj>({ui.extraOptions[PerfOverlayExtra].get(), ui.extraOptions[PerfGpuExtra].get(),
        ui.extraOptions[PerfDiskExtra].get(), ui.extraOptions[PerfDragExtra].get(), ui.extraOptions[PerfButtonExtra].get()}),
        "Performances: the FPS, GPU, disk, mouse gestures and top-bar button boxes only, in that order");
    Check(Label(ui.extraOptions[PerfDragExtra].get()) == "Déplacer et redimensionner l’affichage à la souris" &&
        Label(ui.extraOptions[PerfButtonExtra].get()) == "Bouton dans la barre du haut", "The mouse gestures' row, the top-bar button's row");
    for (const auto option : AllOptions(ui)) Check(Label(option) != "Position de l’affichage des performances", "The corner choice is gone");
    // Each description: the part the box adds, one box enough, the mouse
    // place and size kept, the lock, the top-bar button; tutoiement and
    // typographic apostrophes.
    const auto fps = Description(ui.extraOptions[PerfOverlayExtra].get()), gpu = Description(ui.extraOptions[PerfGpuExtra].get()),
        disk = Description(ui.extraOptions[PerfDiskExtra].get()), drag = Description(ui.extraOptions[PerfDragExtra].get()),
        button = Description(ui.extraOptions[PerfButtonExtra].get());
    for (const char* term : {"Ajoute à l’affichage des performances les images par seconde (FPS)", "mémoire utilisée par le jeu",
            "L’affichage apparaît dès que cette case, « Charge du GPU » ou « Activité du disque » est cochée",
            "avec seulement les mesures cochées", "en haut à gauche par défaut", "jusqu’à ce que tu le déplaces ou le redimensionnes à la souris",
            "sa place et sa taille sont ensuite gardées", "Son bouton dans la barre du haut le masque ou le réaffiche sans décocher ces cases.",
            "Désactivé par défaut."})
        Check(fps.find(term) != std::string::npos, std::string("« Afficher les FPS et la mémoire » says: ") + term);
    for (const auto& text : {gpu, disk})
        for (const char* term : {"Ajoute à l’affichage des performances", "une fois par seconde",
                "Cette case suffit à faire apparaître l’affichage, même sans les FPS et la mémoire.", "Désactivé par défaut."})
            Check(text.find(term) != std::string::npos, std::string("The GPU and disk boxes say: ") + term);
    for (const char* term : {"Fais glisser l’affichage des performances avec le clic gauche pour le placer où tu veux",
            "Redimensionne-le comme une fenêtre, par n’importe lequel de ses quatre bords ou de ses quatre coins",
            "un bord gauche ou droit change sa largeur, un bord haut ou bas sa hauteur, un coin les deux à la fois, et le côté opposé reste en place",
            "une petite poignée de points apparaît dans son coin en bas à droite quand la souris survole l’affichage",
            "Sa largeur et sa hauteur vont chacune de la moitié au triple de leur taille normale",
            "Les mesures se rangent selon la place : plus l’affichage est large, plus elles se suivent sur une même ligne, séparées par « · »",
            "plus il est étroit ou haut, plus elles passent sur les lignes du dessous",
            "Le texte garde ses proportions et prend la plus grande taille qui tient dans le cadre, sans jamais être coupé.",
            "Au survol, le jeu affiche le curseur correspondant : une double flèche sur ses bords et ses coins, le curseur de déplacement ailleurs",
            "les curseurs du mod quand « Curseur Dofus » est coché", "L’affichage reste entièrement à l’écran", "sa place et sa taille sont gardées",
            "Décochée, l’affichage reste verrouillé à sa place et à sa taille et laisse passer les clics vers le jeu.", "Activé par défaut."})
        Check(drag.find(term) != std::string::npos, std::string("« Déplacer et redimensionner l’affichage à la souris » says: ") + term);
    Check(drag.find("son coin en haut à gauche reste en place") == std::string::npos && drag.find("sur ces deux bords") == std::string::npos &&
        disk.find("par exemple « Disque C: 4 % (1,2 Mo/s) »") != std::string::npos,
        "No longer only its right and bottom edges; the disk's example as the overlay shows it");
    for (const char* term : {"Ajoute en haut à droite, à côté de Capture d’âme, un bouton qui affiche ou masque l’affichage des performances",
            "sans décocher ses mesures", "si aucune n’est cochée, il coche « Afficher les FPS et la mémoire »",
            "Masqué avec ce bouton, l’affichage le reste après un redémarrage", "Décochée, le bouton disparaît",
            "qui réapparaît s’il était masqué", "Activé par défaut."})
        Check(button.find(term) != std::string::npos, std::string("« Bouton dans la barre du haut » says: ") + term);
    for (const auto& text : {fps, gpu, disk, drag, button})
        Check(text.find('\'') == std::string::npos && text.find("reprend son coin") == std::string::npos && text.find("Position") == std::string::npos &&
            (text.ends_with(" Activé par défaut.") || text.ends_with(" Désactivé par défaut.")),
            "Typographic apostrophes, no corner left in the descriptions, each ending with its default");
    // The GPU alone: committed as it is, without the FPS box.
    Get(ui.extraSources[PerfGpuExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 1 && f.state.settings.perfOverlayGpu && !f.state.settings.perfOverlay && !f.state.settings.perfOverlayDisk,
        "« Charge du GPU » alone");
    // The mouse gestures unticked: the place, the size and the corner kept (locked there).
    Get(ui.extraSources[PerfDragExtra].get()).boolean = false;
    ui.Tick();
    Check(changes == 2 && !f.state.settings.perfOverlayDrag && f.state.settings.perfOverlayX == 6400 && f.state.settings.perfOverlayY == 1200 &&
        f.state.settings.perfOverlayScaleX == 150 && f.state.settings.perfOverlayScaleY == 90 && f.state.settings.perfOverlayCorner == 3,
        "Unticked: the place and the size kept, locked");
    // Ticked again with the disk: still the same place, size and corner.
    Get(ui.extraSources[PerfDragExtra].get()).boolean = true;
    Get(ui.extraSources[PerfDiskExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 3 && f.state.settings.perfOverlayDrag && f.state.settings.perfOverlayDisk && f.state.settings.perfOverlayX == 6400 &&
        f.state.settings.perfOverlayY == 1200 && f.state.settings.perfOverlayScaleX == 150 && f.state.settings.perfOverlayScaleY == 90 &&
        f.state.settings.perfOverlayCorner == 3, "Ticked again: the same place and size");
    // A place and a size saved meanwhile (a drop, a resize) show nowhere in the options and stay.
    f.state.settings.perfOverlayX = 100; f.state.settings.perfOverlayY = 9900;
    f.state.settings.perfOverlayScaleX = 80; f.state.settings.perfOverlayScaleY = 240;
    ui.Sync();
    ui.Tick();
    Check(changes == 3 && f.state.settings.perfOverlayX == 100 && f.state.settings.perfOverlayY == 9900 && f.state.settings.perfOverlayScaleX == 80 &&
        f.state.settings.perfOverlayScaleY == 240, "A new place and size left as they are");
    // Hidden with its top-bar button meanwhile: left as it is by the pump, and
    // by a part unticked; a part ticked here shows it again.
    f.state.settings.perfOverlayShown = false;
    ui.Sync();
    ui.Tick();
    Check(changes == 3 && !f.state.settings.perfOverlayShown, "Hidden with its button: left as it is");
    Get(ui.extraSources[PerfGpuExtra].get()).boolean = false;
    ui.Tick();
    Check(changes == 4 && !f.state.settings.perfOverlayGpu && f.state.settings.perfOverlayDisk && !f.state.settings.perfOverlayShown,
        "A part unticked: still hidden");
    Get(ui.extraSources[PerfOverlayExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 5 && f.state.settings.perfOverlay && f.state.settings.perfOverlayShown, "« Afficher les FPS et la mémoire » ticked: shown again");
    // Its button removed while it had hidden the overlay: shown again (no
    // button left to show it); put back: nothing else changes.
    f.state.settings.perfOverlayShown = false;
    ui.Sync();
    Get(ui.extraSources[PerfButtonExtra].get()).boolean = false;
    ui.Tick();
    Check(changes == 6 && !f.state.settings.perfOverlayButton && f.state.settings.perfOverlayShown, "Its button removed: shown again");
    f.state.settings.perfOverlayShown = false;
    ui.Sync();
    Get(ui.extraSources[PerfButtonExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 7 && f.state.settings.perfOverlayButton && !f.state.settings.perfOverlayShown, "Its button put back: nothing else");
    // The section reset: the five boxes back to their defaults (no part, the
    // mouse gestures and the button on); the place, the size, the hiding and
    // the corner kept.
    Get(ui.extraSources[PerfButtonExtra].get()).boolean = false;
    f.state.settings.perfOverlayButton = false; // as if unticked before it hid the overlay
    const auto displayedParent = Get(Get(f.page).refs.at("m_optionsDisplayed")).items.back();
    api.Call(Section(displayedParent, PerformanceSection), "Reset");
    ui.Tick();
    Check(changes == 8 && !f.state.settings.perfOverlay && !f.state.settings.perfOverlayGpu && !f.state.settings.perfOverlayDisk &&
        f.state.settings.perfOverlayDrag && f.state.settings.perfOverlayButton && f.state.settings.perfOverlayX == 100 &&
        f.state.settings.perfOverlayY == 9900 && f.state.settings.perfOverlayScaleX == 80 && f.state.settings.perfOverlayScaleY == 240 &&
        !f.state.settings.perfOverlayShown &&
        f.state.settings.perfOverlayCorner == 3, "Reset: no part ticked, the button on; the place, the size, the hiding and the corner kept");
    // Any part ticked here shows it again, not only the FPS: « Charge du GPU »
    // alone, then, hidden again, « Activité du disque ».
    Get(ui.extraSources[PerfGpuExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 9 && f.state.settings.perfOverlayGpu && !f.state.settings.perfOverlay && f.state.settings.perfOverlayShown,
        "« Charge du GPU » ticked: shown again");
    f.state.settings.perfOverlayShown = false;
    ui.Sync();
    Get(ui.extraSources[PerfDiskExtra].get()).boolean = true;
    ui.Tick();
    Check(changes == 10 && f.state.settings.perfOverlayDisk && f.state.settings.perfOverlayShown, "« Activité du disque » ticked: shown again");
    f.NativePreferencesUnchanged();
    f.StopAndRestore();
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
        run(fake::AnimationTogglesValuesAndDefaults);
        run(fake::AppearanceCategoriesValuesAndDefaults);
        run(fake::GlowModeValuesAndDefaults);
        run(fake::GlowCategories);
        run(fake::CursorSectionRows);
        run(fake::CursorThemeLabel);
        run(fake::GaugeSectionRows);
        run(fake::ReducedMiniaturesDescription);
        run(fake::NativeResetAndFilteredSearchOwnership);
        run(fake::SectionResetOwnsItsRows);
        run(fake::ValidationAndLoadingGate);
        run(fake::DimensionChoiceRows);
        run(fake::MapMenuSettingsRow);
        run(fake::ResourceMapMasterPreservesSelection);
        run(fake::NotebookSectionRows);
        run(fake::PartyTravelRow);
        run(fake::TopBarButtonRows);
        run(fake::AutoLoadFailureIsRecoverable);
        run(fake::DeferredAndAccountGate);
        run(fake::CollisionAndSessionMismatch);
        run(fake::MetamobKeyRowFollowsPooledViews);
        run(fake::MetamobKeySaveFollowsTyping);
        run(fake::MetamobSavedKeyTrashAsksFirst);
        run(fake::MetamobSavedStateFollowsTheCheck);
        run(fake::MetamobIndicatorFollowsTheKeyStatus);
        run(fake::MetamobNewKeyRightAfterForget);
        run(fake::MetamobRecycledViewsShowTheCurrentState);
        run(fake::MetamobNativeResetStopsSyncKeepsKey);
        run(fake::MetamobKeyFieldErrorsStayLocal);
        run(fake::MetamobFieldsLeaveOnAbandon);
        run(fake::StatsSectionRows);
        run(fake::StatsCheckboxCommits);
        run(fake::StatsButtonCommits);
        run(fake::PerformanceSectionRows);
        fake::CheckNoKeyTrace();

        std::cout << "PASS: sixteen native sections (Apparence with six combat and five roleplay category checkboxes, PNJ off by default, Animations with six toggles, « Curseur » after « Surbrillance » (Curseur Dofus off by default, « Défaut (<thème>) » following the game's theme, its eleven themes and three colours, its three sizes, invalid choices reverted), « Jauges » after « Curseur » (the gauges' vertical row on by default, its reset keeping each gauge's choice), Carnet and Performances with the 1.13.0 rows, Performances with its five boxes and no corner choice (each part ticked alone, the mouse place and size kept and locked when unticked, the top-bar button on by default, an overlay it hid shown again by a part ticked or the button removed, kept by the section reset); layout, display clones, selection, section resets, restore to Jeu), native settings, independent map visibility, reset, session gates, retryable restoration and the Metamob rows of the soul section (masked key field, Enregistrer only once typed, saved key as fixed stars with its status indicator (Clé valide only for a key Metamob accepted; Vérification…, non vérifiée, refusée, illisible) and the trash, in-row confirmation, a new key right after a forget, pooled views, no key trace) and the Statistiques section (counting checkbox, « Voir mes statistiques » opening the statistics window from any view of its row, the checkbox of the window's top-bar button), and the « Bouton dans la barre du haut » rows of Capture d’âme, of the new « Chasse au trésor » section and of « Carnet »\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
