#include "options-ui.hpp"
#include "module.hpp"
#include "version.hpp"
#include "animation-mode-labels.hpp"
#include "glow-mode-labels.hpp"
#include "treasure-launch.hpp"
#include <algorithm>
#include <exception>

using namespace managed;

namespace {
constexpr int ModCategory = 0x4D4F44;
constexpr int GeneralCategory = ModCategory + 1;
constexpr int GameCategory = 0;
constexpr const char* Config = "Core.UILogic.Config";
constexpr const char* Elements = "Core.UILogic.Config.OptionElement";
constexpr const char* Modes[] = {"Normal", "Créatures", "Miniatures"};
Klass generalClass = nullptr;
using EnableFn = void(*)(Obj, Method);
using InitFn = void(*)(Obj, bool, bool, int, Method);
EnableFn originalEnable = nullptr;
InitFn originalInit = nullptr;
using TextButtonFn = void(*)(Obj, Obj, Method);
TextButtonFn originalTextButton = nullptr;
Method textButtonModelGetter = nullptr;

template<class T> T& FieldValue(Obj owner, const char* name) {
    return *static_cast<T*>(api.FieldAddress(owner, name));
}
void SetTextField(Obj owner, const char* field, const char* text) {
    Ref value(api.string_new(text));
    api.gc_wbarrier_set_field(owner, static_cast<void**>(api.FieldAddress(owner, field)), value.get());
}
int Count(Obj list) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > 4096) throw std::runtime_error("Invalid options list size");
    return count;
}
Obj Item(Obj list, int index) { return api.Call(list, "get_Item", {&index}); }
bool Contains(Obj list, Obj item) { return api.Value<bool>(api.Call(list, "Contains", {item})); }
bool IsGeneral(Obj page) {
    return page && generalClass && api.class_is_assignable_from(generalClass, api.object_get_class(page));
}
Obj BindingGet(Obj page, const char* getter) {
    auto binding = api.Class("Core.dll", "Core.UILogic.Bindings", "OptionTabBinding");
    return api.Invoke(api.Find(binding, getter, 0), api.FieldAddress(page, "dzyi"));
}
void Describe(Obj option, const char* title, const char* description) {
    Ref text(api.string_new(title));
    api.Call(option, "set_text", {text.get()});
    Ref data(api.New(api.Class("Core.dll", Config, "DescriptionData")));
    SetTextField(data.get(), "text", description);
    api.Call(option, "set_description", {data.get()});
}
Obj MakeCategory(const char* name, int id, bool subcategory) {
    Ref category(api.New(api.Class("Core.dll", Config, "CategoryData")));
    SetTextField(category.get(), "name", name);
    FieldValue<int>(category.get(), "categoryId") = id;
    FieldValue<bool>(category.get(), "isSubCategory") = subcategory;
    if (!api.Field(category.get(), "subcategories") || !api.Field(category.get(), "options"))
        throw std::runtime_error("Native option category lists are unavailable");
    return category.get();
}

bool ModelsLoaded(const OptionsUi& ui) {
    auto loaded = [](const Ref& source) {
        return source.get() && api.Value<bool>(api.Call(source.get(), "IsLoaded"));
    };
    return loaded(ui.modeSource) && loaded(ui.roleplayModeSource) &&
        loaded(ui.animationSource) && loaded(ui.autoLoadSource) && loaded(ui.soulSource) && loaded(ui.soulAutoSource) && loaded(ui.glowSource) && loaded(ui.transitionsSource) && loaded(ui.preloadSource) && loaded(ui.spouseSource) && loaded(ui.resourcesSource) && loaded(ui.githubSource);
}
template<class T> void SetSource(Obj source, T value, bool notify) {
    if (api.Value<T>(api.Call(source, "get_property")) != value)
        api.Call(source, notify ? "set_property" : "SetValueWithoutNotify", {&value});
}
void ApplySettings(OptionsUi& ui, const Settings& settings, bool notify) {
    SetSource(ui.modeSource.get(), settings.mode, notify);
    SetSource(ui.roleplayModeSource.get(), settings.roleplayMode, notify);
    SetSource(ui.animationSource.get(), settings.animationMode, notify);
    SetSource(ui.autoLoadSource.get(), settings.autoLoad, notify);
    SetSource(ui.githubSource.get(), settings.githubUpdates, notify);
    SetSource(ui.soulSource.get(), settings.soulReminder, notify);
    SetSource(ui.soulAutoSource.get(), settings.soulAutoEquip, notify);
    SetSource(ui.glowSource.get(), settings.monsterGlowMode, notify);
    SetSource(ui.transitionsSource.get(), settings.fastMapTransitions, notify);
    SetSource(ui.preloadSource.get(), settings.preloadNextMap, notify);
    SetSource(ui.spouseSource.get(), settings.spouseTeleport, notify);
    SetSource(ui.resourcesSource.get(), settings.resourceMapEnabled, notify);
}
using Choices = std::vector<std::pair<int, std::string>>;
Obj MakeChoiceOption(Obj source, Obj container, const char* title, const char* description, const Choices& entries) {
    int size = -1, icon = 0; // FigmaIcons.none
    bool wheel = false;
    Ref option(api.New(api.Class("Core.dll", Elements, "MultipleChoiceOption"),
        {source, container, &size, &wheel, &icon}));
    Describe(option.get(), title, description);
    auto choicesSetter = api.Find(api.object_get_class(option.get()), "set_choices", 1);
    Ref choices(api.New(api.class_from_type(api.method_get_param(choicesSetter, 0))));
    auto addChoice = api.Find(api.object_get_class(choices.get()), "Add", 1);
    auto tupleClass = api.class_from_type(api.method_get_param(addChoice, 0));
    if (!tupleClass || !api.class_is_valuetype(tupleClass)) throw std::runtime_error("Native choice tuple type unavailable");
    for (const auto& [value, label] : entries) {
        int id = value;
        Ref text(api.string_new(label.c_str()));
        Ref tuple(api.New(tupleClass, {&id, text.get()}));
        api.Invoke(addChoice, choices.get(), {api.object_unbox(tuple.get())});
    }
    api.Invoke(choicesSetter, option.get(), {choices.get()});
    return option.get();
}
Obj MakeBoolOption(Obj source, Obj container, Obj tooltip, const char* title, const char* description) {
    Ref option(api.New(api.Class("Core.dll", Elements, "BoolOption"), {tooltip, source, container}));
    Describe(option.get(), title, description);
    return option.get();
}

bool EnsureModels(OptionsUi& ui, Obj page) {
    Ref current(api.Call(module->cached.get(), "get_dofusOptionsContainer"));
    if (!current.get() || api.Field(current.get(), "creatureModeFight") != module->creatureOption.get())
        return false;
    if (ui.container.get()) {
        if (ui.container.get() != current.get()) return false;
        return ModelsLoaded(ui);
    }
    Ref existingInt(api.Field(current.get(), "creaturesMode"));
    if (!existingInt.get()) throw std::runtime_error("Integer option type unavailable");
    Settings defaults;
    bool readOnly = false;
    const auto integerType = api.object_get_class(existingInt.get());
    const auto booleanType = api.object_get_class(module->creatureOption.get());
    OptionsUi prepared;
    prepared.modeSource.reset(api.New(integerType, {&defaults.mode, &readOnly}));
    prepared.animationSource.reset(api.New(integerType, {&defaults.animationMode, &readOnly}));
    prepared.roleplayModeSource.reset(api.New(integerType, {&defaults.roleplayMode, &readOnly}));
    prepared.autoLoadSource.reset(api.New(booleanType, {&defaults.autoLoad, &readOnly}));
    prepared.githubSource.reset(api.New(booleanType, {&defaults.githubUpdates, &readOnly}));
    prepared.soulSource.reset(api.New(booleanType, {&defaults.soulReminder, &readOnly}));
    prepared.soulAutoSource.reset(api.New(booleanType, {&defaults.soulAutoEquip, &readOnly}));
    prepared.glowSource.reset(api.New(integerType, {&defaults.monsterGlowMode, &readOnly}));
    prepared.transitionsSource.reset(api.New(booleanType, {&defaults.fastMapTransitions, &readOnly}));
    prepared.preloadSource.reset(api.New(booleanType, {&defaults.preloadNextMap, &readOnly}));
    prepared.spouseSource.reset(api.New(booleanType, {&defaults.spouseTeleport, &readOnly}));
    prepared.resourcesSource.reset(api.New(booleanType, {&defaults.resourceMapEnabled, &readOnly}));
    if (!ModelsLoaded(prepared)) return false;
    // Native reset uses constructor defaults, independently of persisted values.
    ApplySettings(prepared, module->settings, false);
    Ref tooltip(api.Call(page, "blrl"));
    if (!tooltip.get()) throw std::runtime_error("Native options tooltip service unavailable");
    Choices modes;
    Choices animationModes;
    Choices glowModes;
    for (int i = 0; i < static_cast<int>(GlowModeLabels.size()); ++i)
        glowModes.emplace_back(i, GlowModeLabels[i]);
    for (int i = 0; i < static_cast<int>(AnimationModeLabels.size()); ++i)
        animationModes.emplace_back(i, AnimationModeLabels[i]);
    for (int i = 0; i != 3; ++i) modes.emplace_back(i, Modes[i]);
    prepared.modeOption.reset(MakeChoiceOption(prepared.modeSource.get(), current.get(), "En combat",
        "En combat uniquement : apparence normale, créatures ou miniatures. Les raccourcis utilisent ce même réglage. Miniatures réduit à 60 % de la taille d’origine, avec un minimum automatique de 50 % de la hauteur de référence d’un joueur pour les monstres et invocations, lorsque les mesures sont disponibles.", modes));
    prepared.animationOption.reset(MakeChoiceOption(prepared.animationSource.get(), current.get(), "Animations",
        "Raccourcies utilise les séquences et effets réduits du jeu. Sans animations des combattants évite les animations d'attaque, de sort et de mort prises en charge, et filtre leurs effets, en conservant les déplacements. Instantané ajoute les sauts de déplacements et d'attentes visuelles pris en charge. Les délais serveur et certaines attentes restent inchangés. Ces modes sont expérimentaux : cela ne supprime pas toutes les animations.", animationModes));
    prepared.roleplayModeOption.reset(MakeChoiceOption(prepared.roleplayModeSource.get(), current.get(), "Hors combat",
        "Affichage des personnages et monstres hors combat, indépendant du réglage en combat. Les raccourcis utilisent ce même réglage. Miniatures réduit à 60 % de la taille d’origine, avec un minimum automatique de 50 % de la hauteur de référence d’un joueur pour les monstres et invocations, lorsque les mesures sont disponibles.", modes));
    prepared.autoLoadOption.reset(MakeBoolOption(prepared.autoLoadSource.get(), current.get(), tooltip.get(), "Chargement automatique",
        "Charge la dernière version compatible lors des prochains lancements de Dofus, sans saisie du PID. Si les mises à jour GitHub sont activées, vérifie les nouvelles versions avant le chargement. Les clients déjà ouverts conservent leur version chargée."));
    prepared.githubOption.reset(MakeBoolOption(prepared.githubSource.get(), current.get(), tooltip.get(), "Mises à jour GitHub",
        "À chaque nouveau lancement, recherche une version publiée sur Altansar69/dofus-miniatures, vérifie son intégrité et sa compatibilité avec Dofus, puis la télécharge. Sans réseau, conserve la version locale compatible. Nécessite le chargement automatique. Un correctif du mod peut rester nécessaire après une mise à jour du jeu."));
    prepared.soulOption.reset(MakeBoolOption(prepared.soulSource.get(), current.get(), tooltip.get(), "Alerte pierre d’âme",
        "Pour un combat contenant un boss ou un archimonstre avec une âme capturable définie. Masqué pour les groupes ordinaires et les cartes interdisant la capture. Pendant le placement, affiche la tranche minimale requise, le stock et la pierre équipée. Équiper choisit uniquement cette tranche. En combat : Capture d’âme en rouge si l’état Chercheur d’âmes manque, en vert s’il est actif. L’effet ne garantit pas à lui seul la capture."));
    prepared.soulAutoOption.reset(MakeBoolOption(prepared.soulAutoSource.get(), current.get(), tooltip.get(), "Équiper automatiquement la pierre d’âme",
        "Avec l’alerte pierre d’âme activée, tente d’équiper la pierre de la tranche requise pendant le placement d’un combat capturable. Une seule tentative par combat et tranche, uniquement si une pierre utilisable est disponible. En cas de refus, le bouton Équiper reste disponible. Activé par défaut."));
    ui.modeSource = std::move(prepared.modeSource);
    prepared.glowOption.reset(MakeChoiceOption(prepared.glowSource.get(), current.get(), "Surbrillance des monstres",
        "Affiche le contour des monstres derrière les modules d’affichage, derrière ces modules et les murs, ou tout le temps. La grande carte, l’inventaire, les sorts et les autres fenêtres restent devant le contour dans tous les modes. Les clics restent ceux du jeu. Par défaut : derrière l’interface.", glowModes));
    prepared.transitionsOption.reset(MakeBoolOption(prepared.transitionsSource.get(), current.get(), tooltip.get(), "Transitions rapides entre cartes",
        "Raccourcit les fondus de sortie et d’arrivée hors combat. Le chargement des ressources et l’attente du serveur restent nécessaires : cette option ne garantit pas des changements de carte sans chargement. Activé par défaut."));
    prepared.preloadOption.reset(MakeBoolOption(prepared.preloadSource.get(), current.get(), tooltip.get(), "Précharger la prochaine carte (expérimental)",
        "Pendant le déplacement vers une sortie connue, prépare les données et une partie des décors de la prochaine carte dans le cache du jeu. Une seule carte conservée brièvement, ressources limitées. Peut réduire le chargement local, avec un surcoût de mémoire ; le serveur et la construction de la scène restent nécessaires. Activé par défaut."));
    prepared.spouseOption.reset(MakeBoolOption(prepared.spouseSource.get(), current.get(), tooltip.get(), "Téléportation au conjoint",
        "Ajoute un bouton de téléportation, un cœur et la distance sur la vignette de votre conjoint dans le groupe, ainsi qu’une entrée au clic droit. Le bouton est indisponible si votre conjoint est hors ligne ou pendant un combat. La téléportation respecte les conditions du jeu et se lance uniquement au clic. Activé par défaut."));
    prepared.resourcesOption.reset(MakeBoolOption(prepared.resourcesSource.get(), current.get(), tooltip.get(), "Ressources sur les cartes",
        "Dans les filtres de la carte (M), ouvrez un métier puis cochez les ressources souhaitées. La carte et la mini-carte affichent leurs points de récolte recensés par case. Cela ne renseigne pas leur disponibilité en temps réel. Désactiver conserve votre sélection."));
    prepared.treasureOption.reset(api.New(api.Class("Core.dll", Config, "TextButtonOption"),
        {nullptr, tooltip.get()}));
    Describe(prepared.treasureOption.get(), "Chasse au trésor",
        "Ouvre l’outil officiel DofusDB dans une fenêtre déplaçable et redimensionnable. Saisissez les coordonnées, la direction et l’indice pour trouver le suivant. La fenêtre peut rester au-dessus du jeu.");
    SetTextField(prepared.treasureOption.get(), "buttonText", "Ouvrir DofusDB");
    prepared.treasureMarker.reset(api.Call(prepared.treasureOption.get(), "get_description"));
    if (!prepared.treasureMarker.get()) throw std::runtime_error("Treasure option identity unavailable");
    ui.treasureOption = std::move(prepared.treasureOption);
    ui.treasureMarker = std::move(prepared.treasureMarker);
    ui.resourcesSource = std::move(prepared.resourcesSource);
    ui.resourcesOption = std::move(prepared.resourcesOption);
    ui.spouseSource = std::move(prepared.spouseSource);
    ui.spouseOption = std::move(prepared.spouseOption);
    ui.preloadSource = std::move(prepared.preloadSource);
    ui.preloadOption = std::move(prepared.preloadOption);
    ui.transitionsSource = std::move(prepared.transitionsSource);
    ui.transitionsOption = std::move(prepared.transitionsOption);
    ui.glowSource = std::move(prepared.glowSource);
    ui.glowOption = std::move(prepared.glowOption);
    ui.animationSource = std::move(prepared.animationSource);
    ui.modeOption = std::move(prepared.modeOption);
    ui.animationOption = std::move(prepared.animationOption);
    ui.roleplayModeSource = std::move(prepared.roleplayModeSource);
    ui.roleplayModeOption = std::move(prepared.roleplayModeOption);
    ui.autoLoadSource = std::move(prepared.autoLoadSource);
    ui.githubSource = std::move(prepared.githubSource);
    ui.githubOption = std::move(prepared.githubOption);
    ui.autoLoadOption = std::move(prepared.autoLoadOption);
    ui.soulSource = std::move(prepared.soulSource);
    ui.soulOption = std::move(prepared.soulOption);
    ui.soulAutoSource = std::move(prepared.soulAutoSource);
    ui.soulAutoOption = std::move(prepared.soulAutoOption);
    ui.container = std::move(current);
    return true;
}

int SelectedCategory(Obj page) {
    const auto uid = api.String(api.Field(page, "m_currentId"));
    auto categories = api.Field(page, "m_options");
    for (int i = 0, count = Count(categories); i < count; ++i) {
        Ref category(Item(categories, i));
        if (!uid.empty() && api.String(api.Field(category.get(), "uid")) == uid)
            return FieldValue<int>(category.get(), "categoryId");
        auto children = api.Field(category.get(), "subcategories");
        for (int j = 0, childCount = Count(children); j < childCount; ++j) {
            Ref child(Item(children, j));
            if (!uid.empty() && api.String(api.Field(child.get(), "uid")) == uid)
                return FieldValue<int>(child.get(), "categoryId");
        }
    }
    return GameCategory;
}
void Rebuild(Obj page, int selected) {
    bool rebuild = true;
    bool connected = FieldValue<bool>(page, "isPlayerConnected");
    api.Call(page, "Init", {&rebuild, &connected, &selected});
}
bool SelectionReady(Obj page, const std::string& uid) {
    Ref displayed(api.Field(page, "m_displayedOption"));
    if (!displayed.get() || uid.empty()) return false;
    if (api.String(api.Field(displayed.get(), "uid")) == uid) return true;
    auto children = api.Field(displayed.get(), "subcategories");
    for (int i = 0, count = Count(children); i < count; ++i)
        if (api.String(api.Field(Item(children, i), "uid")) == uid) return true;
    return false;
}
void UiError(const std::exception& error) {
    Log(std::string("UI_ERROR ") + error.what());
    if (module) module->stopRequested.store(true);
}
bool Active() {
    return module && module->ready.load() && !module->stopped && !module->stopRequested.load();
}
void TextButtonHook(Obj self, Obj element, Method method) {
    try {
        Ref option(api.Invoke(textButtonModelGetter, self));
        // Native search/display uses MemberwiseClone: the unique description
        // reference identifies our action even when the model itself changes.
        if (module && module->ui && module->ui->treasureMarker.get() && option.get() &&
            api.Call(option.get(), "get_description") == module->ui->treasureMarker.get()) {
            if (Active()) OpenTreasureOverlay(module->folder);
            return;
        }
    } catch (const std::exception& error) {
        // Never fall through to a possibly owned action with a null delegate.
        Log(std::string("TREASURE_OVERLAY_ERROR ") + error.what());
        return;
    }
    originalTextButton(self, element, method);
}
void EnableHook(Obj self, Method method) {
    originalEnable(self, method);
    if (!Active()) return;
    try { module->ui->CapturePage(self); }
    catch (const std::exception& error) { UiError(error); }
}
void InitHook(Obj self, bool justCreated, bool connected, int category, Method method) {
    if (Active() && module->servicesReady && IsGeneral(self)) {
        try { if (module->ui->Attach(self)) justCreated = true; }
        catch (const std::exception& error) { UiError(error); }
    }
    originalInit(self, justCreated, connected, category, method);
}
}

void OptionsUi::Install() {
    generalClass = api.Class("Core.dll", Config, "OptionGeneralUi");
    auto tab = api.Class("Core.dll", Config, "OptionTabUi");
    AddHook(api.Find(generalClass, "OnEnable", 0), 0x2D80470,
            reinterpret_cast<void*>(EnableHook), reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(tab, "Init", 3), 0x2D84EC0,
            reinterpret_cast<void*>(InitHook), reinterpret_cast<void**>(&originalInit));
    auto textButton = api.Class("Core.dll", Config, "TextButtonOptionElement");
    textButtonModelGetter = api.FindVerified(textButton, "get_m_textButtonOption", 0, 0x2D88930);
    AddHook(api.Find(textButton, "<Init>b__3_0", 1), 0x2D88800,
            reinterpret_cast<void*>(TextButtonHook), reinterpret_cast<void**>(&originalTextButton));
}
void OptionsUi::CapturePage(Obj page) {
    pendingPage.reset(page);
    stableList.reset();
    stableSelection.clear();
    stableTicks = 0;
}
bool OptionsUi::Attach(Obj page) {
    if (!module || !module->servicesReady || module->stopped || !IsGeneral(page)) return false;
    Ref categories(api.Field(page, "m_options"));
    for (const auto& owned : pages) {
        if (owned.owner.get() == page && Contains(categories.get(), owned.category.get())) return false;
    }
    if (!EnsureModels(*this, page)) return false;
    for (int i = 0, count = Count(categories.get()); i < count; ++i) {
        auto existing = Item(categories.get(), i);
        if (FieldValue<int>(existing, "categoryId") == ModCategory)
            throw std::runtime_error("Mod category identifier already belongs to another entry");
    }
    Ref category(MakeCategory(ModCategoryTitle, ModCategory, false));
    // Keep the parent/section shape used by the native menu and earlier releases.
    Ref general(MakeCategory("Général", GeneralCategory, true));
    auto options = api.Field(general.get(), "options");
    api.Call(options, "Add", {modeOption.get()});
    api.Call(options, "Add", {roleplayModeOption.get()});
    api.Call(options, "Add", {animationOption.get()});
    api.Call(options, "Add", {autoLoadOption.get()});
    api.Call(options, "Add", {githubOption.get()});
    api.Call(options, "Add", {soulOption.get()});
    api.Call(options, "Add", {soulAutoOption.get()});
    api.Call(options, "Add", {glowOption.get()});
    api.Call(options, "Add", {transitionsOption.get()});
    api.Call(options, "Add", {preloadOption.get()});
    api.Call(options, "Add", {spouseOption.get()});
    api.Call(options, "Add", {resourcesOption.get()});
    api.Call(options, "Add", {treasureOption.get()});
    api.Call(api.Field(category.get(), "subcategories"), "Add", {general.get()});
    Page owned;
    owned.owner.reset(page);
    owned.category = std::move(category);
    pages.push_back(std::move(owned));
    try { api.Call(categories.get(), "Add", {pages.back().category.get()}); }
    catch (...) { pages.pop_back(); throw; }
    Log(std::string("UI_MOD_REGISTERED category=Mod controls=native loaded=") + ModVersion);
    return true;
}
void OptionsUi::Tick() {
    if (!module || !module->servicesReady || module->stopped) return;
    const auto page = pendingPage.get();
    if (page) {
        const bool attached = std::any_of(pages.begin(), pages.end(), [page](const Page& item) {
            return item.owner.get() == page && Contains(api.Field(page, "m_options"), item.category.get());
        });
        if (!attached) {
            Ref root(api.Call(page, "blqu"));
            if (root.get() && api.Call(root.get(), "get_panel")) {
                Ref list(BindingGet(page, "get_gd_category"));
                Ref displayed(api.Field(page, "m_optionsDisplayed"));
                auto selection = api.String(api.Field(page, "m_currentId"));
                // Summary/selection builders yield before changing the UI; wait
                // for a settled list on two distinct main-thread pumps.
                if (displayed.get() && api.Call(list.get(), "get_itemsSource") == displayed.get() &&
                    SelectionReady(page, selection)) {
                    if (stableList.get() == displayed.get() && stableSelection == selection) ++stableTicks;
                    else { stableList.reset(displayed.get()); stableSelection = selection; stableTicks = 1; }
                    if (stableTicks >= 2) {
                        int selected = SelectedCategory(page);
                        if (Attach(page)) Rebuild(page, selected);
                    }
                } else { stableList.reset(); stableTicks = 0; }
            }
        }
    }
    if (!ModelsLoaded(*this)) return;
    auto next = module->settings;
    next.mode = api.Value<int>(api.Call(modeSource.get(), "get_property"));
    next.animationMode = api.Value<int>(api.Call(animationSource.get(), "get_property"));
    next.roleplayMode = api.Value<int>(api.Call(roleplayModeSource.get(), "get_property"));
    next.autoLoad = api.Value<bool>(api.Call(autoLoadSource.get(), "get_property"));
    next.githubUpdates = api.Value<bool>(api.Call(githubSource.get(), "get_property"));
    next.soulReminder = api.Value<bool>(api.Call(soulSource.get(), "get_property"));
    next.soulAutoEquip = api.Value<bool>(api.Call(soulAutoSource.get(), "get_property"));
    next.monsterGlowMode = api.Value<int>(api.Call(glowSource.get(), "get_property"));
    next.fastMapTransitions = api.Value<bool>(api.Call(transitionsSource.get(), "get_property"));
    next.preloadNextMap = api.Value<bool>(api.Call(preloadSource.get(), "get_property"));
    next.spouseTeleport = api.Value<bool>(api.Call(spouseSource.get(), "get_property"));
    next.resourceMapEnabled = api.Value<bool>(api.Call(resourcesSource.get(), "get_property"));
    if (next.mode < 0 || next.mode > 2 || next.roleplayMode < 0 || next.roleplayMode > 2 ||
        !Settings::ValidAnimationMode(next.animationMode) || !Settings::ValidMonsterGlowMode(next.monsterGlowMode)) {
        Sync(); return;
    }
    if (!(next == module->settings)) {
        const bool autoLoadChanged = next.autoLoad != module->settings.autoLoad;
        try { module->Change(next); }
        catch (const std::exception& error) {
            if (!autoLoadChanged) throw;
            // A rejected startup registration is recoverable. Keep the module
            // running and restore the native checkbox to the committed state.
            Log(std::string("UI_CHANGE_REJECTED autoLoad: ") + error.what());
            Sync();
        }
    }
}
void OptionsUi::Sync() {
    if (!module || !ModelsLoaded(*this)) return;
    // OptionData owns the native updateValue callback for the current row.
    ApplySettings(*this, module->settings, true);
}
void OptionsUi::Restore() {
    pendingPage.reset();
    stableList.reset();
    std::exception_ptr failure;
    for (auto it = pages.begin(); it != pages.end();) {
        try {
            auto categories = api.Field(it->owner.get(), "m_options");
            int selected = SelectedCategory(it->owner.get());
            if (selected == ModCategory || selected == GeneralCategory)
                selected = GameCategory;
            api.Call(categories, "Remove", {it->category.get()});
            // Retry rebuilding even when removal succeeded on an earlier pump.
            Rebuild(it->owner.get(), selected);
            it = pages.erase(it);
        } catch (...) {
            if (!failure) failure = std::current_exception();
            ++it;
        }
    }
    if (failure) std::rethrow_exception(failure);
    treasureOption.reset();
    // Init schedules asynchronous UI rebuilding. Keep this small tombstone and
    // its click guard until process exit, even after category removal returns.
    modeOption.reset();
    animationOption.reset();
    modeSource.reset();
    animationSource.reset();
    roleplayModeSource.reset();
    roleplayModeOption.reset();
    autoLoadSource.reset();
    githubSource.reset();
    githubOption.reset();
    autoLoadOption.reset();
    soulSource.reset();
    soulOption.reset();
    soulAutoSource.reset();
    soulAutoOption.reset();
    glowSource.reset();
    glowOption.reset();
    preloadSource.reset();
    preloadOption.reset();
    resourcesSource.reset();
    resourcesOption.reset();
    spouseSource.reset();
    spouseOption.reset();
    transitionsSource.reset();
    transitionsOption.reset();
    container.reset();
}
bool OptionsUi::KeepResidentHook(void* target) const {
    return treasureMarker.handle && target == reinterpret_cast<void*>(gameBase + 0x2D88800);
}
