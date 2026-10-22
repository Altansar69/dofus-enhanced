#include "options-ui.hpp"
#include "module.hpp"
#include "version.hpp"
#include "animation-mode-labels.hpp"
#include "glow-mode-labels.hpp"
#include <algorithm>
#include <exception>
#include <string>

using namespace managed;

namespace {
constexpr int ModCategory = 0x4D4F44;
// Native sections of the mod's category (see Attach), in display order: ids
// ModCategory + 1 to ModCategory + ModSections.
constexpr int ModSections = 7;
constexpr int GameCategory = 0;
constexpr const char* Config = "Core.UILogic.Config";
constexpr const char* Elements = "Core.UILogic.Config.OptionElement";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* UiModule = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Modes[] = {"Normal", "Créatures", "Miniatures"};
Klass generalClass = nullptr;
using EnableFn = void(*)(Obj, Method);
using InitFn = void(*)(Obj, bool, bool, int, Method);
EnableFn originalEnable = nullptr;
InitFn originalInit = nullptr;
// TextButtonOptionElement.<Init>b__3_0(VisualElement), the click, and Init(TextButtonOption).
using TextButtonFn = void(*)(Obj, Obj, Method);
TextButtonFn originalTextButton = nullptr, originalTextButtonInit = nullptr;
Method textButtonModelGetter = nullptr, openUrl = nullptr, setPassword = nullptr;
constexpr const char* ProjectUrl = "https://github.com/Altansar69/dofus-miniatures";
// Metamob rows. The key never goes to Settings, a log, a notice or an exception.
constexpr int IconSave = 148, IconTrash = 316; // FigmaIcons.floppy, FigmaIcons.trash
constexpr int KeyMaxLength = 256;
constexpr ULONGLONG ForgetConfirmMs = 5000, KeyFieldRefreshMs = 1000, KeyFieldLogMs = 10000;
constexpr size_t MaxKeyFields = 8; // pooled views showing the key row at once (normally one)
constexpr const char* KeyHint = "Colle ici ta clé API Metamob";
constexpr const char* StoredKeyHint = "Clé enregistrée (masquée) : colle une nouvelle clé pour la remplacer";
constexpr const char* PasteKeyFirst = "Metamob : colle d’abord ta clé dans le champ.";
constexpr const char* ConfirmForget = "Metamob : reclique sur Supprimer pour oublier ta clé.";

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
        loaded(ui.animationSource) && loaded(ui.autoLoadSource) && loaded(ui.soulSource) && loaded(ui.soulAutoSource) && loaded(ui.glowSource) && loaded(ui.glowNpcsSource) && loaded(ui.glowInteractivesSource) && loaded(ui.transitionsSource) && loaded(ui.preloadSource) && loaded(ui.spouseSource) && loaded(ui.resourcesSource) && loaded(ui.resourcesMiniSource) && loaded(ui.miniCenterSource) && loaded(ui.githubSource) && loaded(ui.metamobSyncSource);
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
    SetSource(ui.glowNpcsSource.get(), settings.glowNpcs, notify);
    SetSource(ui.glowInteractivesSource.get(), settings.glowInteractives, notify);
    SetSource(ui.transitionsSource.get(), settings.fastMapTransitions, notify);
    SetSource(ui.preloadSource.get(), settings.preloadNextMap, notify);
    SetSource(ui.spouseSource.get(), settings.spouseTeleport, notify);
    SetSource(ui.resourcesSource.get(), settings.resourceMapEnabled, notify);
    SetSource(ui.resourcesMiniSource.get(), settings.resourceMiniMapEnabled, notify);
    SetSource(ui.miniCenterSource.get(), settings.miniMapCenterButton, notify);
    SetSource(ui.metamobSyncSource.get(), settings.metamobSync, notify);
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
// Owned text-button rows have no delegate (null callback): TextButtonHook knows
// them by their unique description reference, kept as marker.
Obj MakeTextButtonOption(Obj tooltip, const char* title, const char* description, const char* button, int icon, Ref& marker) {
    Ref option(api.New(api.Class("Core.dll", Config, "TextButtonOption"), {nullptr, tooltip}));
    Describe(option.get(), title, description);
    SetTextField(option.get(), "buttonText", button);
    if (icon) FieldValue<int>(option.get(), "icon") = icon; // ButtonOption.icon, shown by TextButtonOptionElement.Init
    marker.reset(api.Call(option.get(), "get_description"));
    if (!marker.get()) throw std::runtime_error("Owned option identity unavailable");
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
    prepared.glowNpcsSource.reset(api.New(booleanType, {&defaults.glowNpcs, &readOnly}));
    prepared.glowInteractivesSource.reset(api.New(booleanType, {&defaults.glowInteractives, &readOnly}));
    prepared.transitionsSource.reset(api.New(booleanType, {&defaults.fastMapTransitions, &readOnly}));
    prepared.preloadSource.reset(api.New(booleanType, {&defaults.preloadNextMap, &readOnly}));
    prepared.spouseSource.reset(api.New(booleanType, {&defaults.spouseTeleport, &readOnly}));
    prepared.resourcesSource.reset(api.New(booleanType, {&defaults.resourceMapEnabled, &readOnly}));
    prepared.resourcesMiniSource.reset(api.New(booleanType, {&defaults.resourceMiniMapEnabled, &readOnly}));
    prepared.miniCenterSource.reset(api.New(booleanType, {&defaults.miniMapCenterButton, &readOnly}));
    prepared.metamobSyncSource.reset(api.New(booleanType, {&defaults.metamobSync, &readOnly}));
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
    prepared.glowOption.reset(MakeChoiceOption(prepared.glowSource.get(), current.get(), "Surbrillance",
        "Affiche le contour des monstres derrière les modules d’affichage, derrière ces modules et les murs, ou tout le temps. La grande carte, l’inventaire, les sorts et les autres fenêtres restent devant le contour dans tous les modes. Les clics restent ceux du jeu. Par défaut : derrière l’interface.", glowModes));
    prepared.glowNpcsOption.reset(MakeBoolOption(prepared.glowNpcsSource.get(), current.get(), tooltip.get(), "Inclure les PNJ",
        "Ajoute les PNJ visibles hors combat au niveau de surbrillance choisi ci-dessus. Indépendant des éléments interactifs. Désactivé par défaut."));
    prepared.glowInteractivesOption.reset(MakeBoolOption(prepared.glowInteractivesSource.get(), current.get(), tooltip.get(), "Inclure les éléments interactifs",
        "Ajoute les objets visibles avec une action disponible hors combat : ressources récoltables, portes, zaaps… Utilise le niveau de surbrillance choisi ci-dessus. Désactivé par défaut."));
    prepared.transitionsOption.reset(MakeBoolOption(prepared.transitionsSource.get(), current.get(), tooltip.get(), "Transitions rapides entre cartes",
        "Raccourcit les fondus de sortie et d’arrivée hors combat. Le chargement des ressources et l’attente du serveur restent nécessaires : cette option ne garantit pas des changements de carte sans chargement. Activé par défaut."));
    prepared.preloadOption.reset(MakeBoolOption(prepared.preloadSource.get(), current.get(), tooltip.get(), "Précharger la prochaine carte (expérimental)",
        "Pendant le déplacement vers une sortie connue, prépare les données et une partie des décors de la prochaine carte dans le cache du jeu. Une seule carte conservée brièvement, ressources limitées. Peut réduire le chargement local, avec un surcoût de mémoire ; le serveur et la construction de la scène restent nécessaires. Activé par défaut."));
    prepared.spouseOption.reset(MakeBoolOption(prepared.spouseSource.get(), current.get(), tooltip.get(), "Téléportation au conjoint",
        "Ajoute un bouton de téléportation, un cœur et la distance sur la vignette de votre conjoint dans le groupe, ainsi qu’une entrée au clic droit. Le bouton est indisponible si votre conjoint est hors ligne ou pendant un combat. La téléportation respecte les conditions du jeu et se lance uniquement au clic. Activé par défaut."));
    prepared.resourcesOption.reset(MakeBoolOption(prepared.resourcesSource.get(), current.get(), tooltip.get(), "Ressources sur la grande carte",
        "Dans les filtres de la carte (M), ouvrez un métier puis cochez les ressources souhaitées. La carte et la mini-carte affichent leurs points de récolte recensés par case. Cela ne renseigne pas leur disponibilité en temps réel. Désactiver conserve votre sélection."));
    prepared.resourcesMiniOption.reset(MakeBoolOption(prepared.resourcesMiniSource.get(), current.get(), tooltip.get(), "Ressources sur la mini-carte",
        "Affiche les repères et la sélection des ressources sur la mini-carte. Indépendant de la grande carte ; les ressources sélectionnées restent partagées."));
    prepared.miniCenterOption.reset(MakeBoolOption(prepared.miniCenterSource.get(), current.get(), tooltip.get(), "Bouton de recentrage sur la mini-carte",
        "Affiche en bas à gauche de la mini-carte le bouton qui la ramène sur ton personnage, avec le même aspect que le bouton joueur de la grande carte. Décocher le masque."));
    // Section « À propos », alone: credits, an owned text button (see TextButtonHook).
    const std::string creditsTitle = std::string(ModDisplayName) + " " + ModSemanticVersion + " — par Altansar";
    const std::string creditsText = std::string("Mod créé par Altansar. Code source, versions et signalements : ") + ProjectUrl +
        " (révision " + ModVersion + ", Dofus 3.7.12.12). Le bouton ouvre la page du projet dans le navigateur.";
    prepared.creditsOption.reset(MakeTextButtonOption(tooltip.get(), creditsTitle.c_str(), creditsText.c_str(), "Ouvrir GitHub", 0, prepared.creditsMarker));
    // « Pierre d’âme », after the soul rows: the Metamob sync preference, then two owned text buttons. The
    // key itself stays in the capture sync, reached through ui.metamob only.
    prepared.metamobSyncOption.reset(MakeBoolOption(prepared.metamobSyncSource.get(), current.get(), tooltip.get(), "Synchroniser les captures avec Metamob",
        "Après chaque capture d’un archimonstre ou d’un boss (pierre d’âme remplie en combat), ajoute 1 à ce monstre dans ta quête Ocre sur Metamob, sans rien modifier d’autre. Nécessite ta clé API personnelle (metamob.fr › Paramètres › Clé API). Désactivé par défaut."));
    prepared.metamobKeyOption.reset(MakeTextButtonOption(tooltip.get(), "Clé API Metamob",
        "Colle ta clé dans le champ masqué puis clique sur Enregistrer. Elle est vérifiée auprès de Metamob, chiffrée pour ton compte Windows et gardée uniquement sur ce PC ; elle n’est jamais affichée, journalisée ni incluse dans les mises à jour.",
        "Enregistrer", IconSave, prepared.metamobKeyMarker));
    prepared.metamobForgetOption.reset(MakeTextButtonOption(tooltip.get(), "Oublier la clé Metamob",
        "Efface la clé enregistrée sur ce PC : clique sur Supprimer, puis une seconde fois dans les 5 secondes. Les captures ne sont plus synchronisées tant qu’aucune nouvelle clé n’est enregistrée. La clé reste valable sur Metamob : régénère-la sur le site pour la révoquer.",
        "Supprimer", IconTrash, prepared.metamobForgetMarker));
    ui.creditsOption = std::move(prepared.creditsOption);
    ui.creditsMarker = std::move(prepared.creditsMarker);
    ui.metamobSyncSource = std::move(prepared.metamobSyncSource);
    ui.metamobSyncOption = std::move(prepared.metamobSyncOption);
    ui.metamobKeyOption = std::move(prepared.metamobKeyOption);
    ui.metamobKeyMarker = std::move(prepared.metamobKeyMarker);
    ui.metamobForgetOption = std::move(prepared.metamobForgetOption);
    ui.metamobForgetMarker = std::move(prepared.metamobForgetMarker);
    ui.resourcesMiniSource = std::move(prepared.resourcesMiniSource);
    ui.resourcesMiniOption = std::move(prepared.resourcesMiniOption);
    ui.miniCenterSource = std::move(prepared.miniCenterSource);
    ui.miniCenterOption = std::move(prepared.miniCenterOption);
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
    ui.glowNpcsSource = std::move(prepared.glowNpcsSource);
    ui.glowNpcsOption = std::move(prepared.glowNpcsOption);
    ui.glowInteractivesSource = std::move(prepared.glowInteractivesSource);
    ui.glowInteractivesOption = std::move(prepared.glowInteractivesOption);
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
// Metamob key field. Fixed codes only: a managed exception text can carry the
// typed key, so what() is never logged here and a failure never stops the mod.
void KeyFieldError(OptionsUi& ui, const char* step) noexcept {
    try {
        const auto now = GetTickCount64();
        if (now < ui.nextKeyFieldLog) return;
        ui.nextKeyFieldLog = now + KeyFieldLogMs;
        Log(std::string("METAMOB_KEY_FIELD_ERROR step=") + step);
    } catch (...) {}
}
// Plaintext copies of the key held here are overwritten before release (a
// resize within the capacity never reallocates and zeroes the stale bytes).
void Wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    SecureZeroMemory(text.data(), text.size());
    text.clear();
}
bool KeyStored(OptionsUi& ui) noexcept {
    try { return ui.metamob.keyStored && ui.metamob.keyStored(); }
    catch (...) { KeyFieldError(ui, "stored"); return false; }
}
void KeyHintText(Obj input, bool stored) {
    Ref hint(api.string_new(stored ? StoredKeyHint : KeyHint));
    api.Call(input, "set_placeholderText", {hint.get()});
}
std::vector<OptionsUi::KeyField>::iterator FindKeyField(OptionsUi& ui, Obj element) {
    return std::find_if(ui.keyFields.begin(), ui.keyFields.end(),
        [element](const OptionsUi::KeyField& field) { return field.element.get() == element; });
}
// Emptied, then detached; its references go even when the view is already gone.
void DropKeyField(OptionsUi& ui, std::vector<OptionsUi::KeyField>::iterator field) noexcept {
    try {
        Ref empty(api.string_new(""));
        api.Call(field->input.get(), "set_text", {empty.get()});
        api.Call(field->input.get(), "RemoveFromHierarchy");
    } catch (...) { KeyFieldError(ui, "remove"); }
    ui.keyFields.erase(field);
}
void DropKeyFields(OptionsUi& ui) noexcept {
    while (!ui.keyFields.empty()) DropKeyField(ui, ui.keyFields.begin());
}
// Inline style, as the notebook's inputs: the field takes the free width
// between the row label and the row button.
void WriteStyle(Obj element, const char* property, void* value) {
    Ref style(api.Call(element, "get_style"));
    Ref box(api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str()));
    if (!box.get() || !api.class_is_valuetype(api.object_get_class(box.get()))) throw std::runtime_error("Key field style unavailable");
    api.Call(box.get(), "set_value", {value});
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box.get())});
}
void Pixels(Obj element, const char* property, float value) {
    int pixel = 0; // LengthUnit.Pixel
    Ref length(api.New(api.Class(UiModule, UiSpace, "Length"), {&value, &pixel}));
    WriteStyle(element, property, api.object_unbox(length.get()));
}
// The game's own TextInput in password mode, complete before it is placed: a
// failure never leaves a half-built field in the row.
Obj NewKeyInput(bool stored) {
    Ref input(api.New(api.Class("Core.dll", Figma, "TextInput")));
    bool masked = true;
    api.Invoke(setPassword, input.get(), {&masked});
    int maxLength = KeyMaxLength;
    api.Call(input.get(), "set_maxLength", {&maxLength});
    KeyHintText(input.get(), stored);
    float grow = 1.f;
    WriteStyle(input.get(), "flexGrow", &grow);
    Pixels(input.get(), "minWidth", 0.f); Pixels(input.get(), "minHeight", 32.f);
    Pixels(input.get(), "marginLeft", 12.f); Pixels(input.get(), "marginRight", 8.f);
    return input.get();
}
Obj RowButton(Obj element) {
    auto rowBinding = api.Class("Core.dll", "Core.UI.Bindings", "TextButtonOptionElementBinding");
    return api.Invoke(api.Find(rowBinding, "get_btn_button", 0), api.FieldAddress(element, "m_binding"));
}
// After TextButtonOptionElement.Init: exactly one owned field, just before the
// button, in a view showing the key row (or a search clone of it); none in any
// other view, since the game pools these views for every text-button row.
void BindKeyField(OptionsUi& ui, Obj element, Obj option) noexcept {
    try {
        auto field = FindKeyField(ui, element);
        const bool key = Active() && option && ui.metamobKeyMarker.get() &&
            api.Call(option, "get_description") == ui.metamobKeyMarker.get();
        if (!key) { if (field != ui.keyFields.end()) DropKeyField(ui, field); return; }
        Ref button(RowButton(element));
        Ref row(button.get() ? api.Call(button.get(), "get_parent") : nullptr);
        if (!row.get()) throw std::runtime_error("Key row layout unavailable");
        const bool stored = KeyStored(ui);
        if (field != ui.keyFields.end()) {
            if (api.Call(field->input.get(), "get_parent") == row.get()) {
                if (field->stored != stored) { KeyHintText(field->input.get(), stored); field->stored = stored; }
                return;
            }
            DropKeyField(ui, field); // taken out of the row by the game: placed again below
        }
        if (ui.keyFields.size() >= MaxKeyFields) DropKeyField(ui, ui.keyFields.begin());
        ui.keyFields.reserve(ui.keyFields.size() + 1); // the push_back below cannot fail once inserted
        OptionsUi::KeyField owned;
        owned.input.reset(NewKeyInput(stored));
        owned.element.reset(element);
        owned.stored = stored;
        int index = api.Value<int>(api.Call(row.get(), "IndexOf", {button.get()}));
        if (index < 0) throw std::runtime_error("Key row button detached");
        api.Call(row.get(), "Insert", {&index, owned.input.get()});
        ui.keyFields.push_back(std::move(owned));
    } catch (...) { KeyFieldError(ui, "bind"); }
}
// A key saved or forgotten while its row is shown: the hint follows within a second.
void RefreshKeyFields(OptionsUi& ui) noexcept {
    try {
        const auto now = GetTickCount64();
        if (ui.keyFields.empty() || now < ui.nextKeyFieldRefresh) return;
        ui.nextKeyFieldRefresh = now + KeyFieldRefreshMs;
        const bool stored = KeyStored(ui);
        for (auto& field : ui.keyFields)
            if (field.stored != stored) { KeyHintText(field.input.get(), stored); field.stored = stored; }
    } catch (...) { KeyFieldError(ui, "refresh"); }
}
// « Enregistrer »: the field is read once and emptied at once; the capture sync
// then verifies, stores and wipes the text.
void SaveKeyClick(OptionsUi& ui, Obj element) noexcept {
    std::string key;
    try {
        const auto field = FindKeyField(ui, element);
        if (field != ui.keyFields.end()) {
            key = api.String(api.Call(field->input.get(), "get_text"));
            Ref empty(api.string_new(""));
            api.Call(field->input.get(), "set_text", {empty.get()});
        }
        if (key.empty()) { if (ui.metamob.notice) ui.metamob.notice(PasteKeyFirst); }
        else if (ui.metamob.submitKey) ui.metamob.submitKey(std::move(key));
    } catch (...) { KeyFieldError(ui, "save"); }
    Wipe(key);
}
// « Supprimer »: a first click asks for confirmation, a second one within five
// seconds forgets the stored key.
void ForgetKeyClick(OptionsUi& ui) noexcept {
    try {
        const auto now = GetTickCount64();
        if (ui.forgetDeadline && now < ui.forgetDeadline) {
            ui.forgetDeadline = 0;
            if (ui.metamob.forgetKey) ui.metamob.forgetKey();
        } else {
            ui.forgetDeadline = now + ForgetConfirmMs;
            if (ui.metamob.notice) ui.metamob.notice(ConfirmForget);
        }
    } catch (...) { KeyFieldError(ui, "forget"); }
}
void TextButtonHook(Obj self, Obj element, Method method) {
    const auto ui = module ? module->ui.get() : nullptr;
    Obj description = nullptr;
    try {
        Ref option(api.Invoke(textButtonModelGetter, self));
        // Native search/display uses MemberwiseClone: the unique description
        // reference identifies our rows even when the model is cloned. The
        // credits and Metamob markers are set and released together.
        if (ui && ui->creditsMarker.get() && option.get()) description = api.Call(option.get(), "get_description");
        if (description && description == ui->creditsMarker.get()) {
            if (Active()) { Ref url(api.string_new(ProjectUrl)); api.Invoke(openUrl, nullptr, {url.get()}); }
            return;
        }
    } catch (const std::exception& error) {
        // Never fall through to a possibly owned action with a null delegate.
        Log(std::string("CREDITS_LINK_ERROR ") + error.what());
        return;
    }
    // Metamob rows: their own handling, which never throws, never the native action.
    if (description && description == ui->metamobKeyMarker.get()) { if (Active()) SaveKeyClick(*ui, self); return; }
    if (description && description == ui->metamobForgetMarker.get()) { if (Active()) ForgetKeyClick(*ui); return; }
    originalTextButton(self, element, method);
}
// The game pools row views and re-Inits them with any model: after the native
// Init, the view gets or loses the masked key field.
void TextButtonInitHook(Obj self, Obj option, Method method) {
    originalTextButtonInit(self, option, method);
    if (module && module->ui) BindKeyField(*module->ui, self, option);
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
    openUrl = api.FindVerified(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"), "OpenURL", 1, 0x53ACA50);
    AddHook(api.Find(textButton, "<Init>b__3_0", 1), 0x2D88800,
            reinterpret_cast<void*>(TextButtonHook), reinterpret_cast<void**>(&originalTextButton));
    // Metamob key row: the game's masked TextInput, kept in the row view.
    AddHook(api.FindVerified(textButton, "Init", 1, 0x2D885A0), 0x2D885A0,
            reinterpret_cast<void*>(TextButtonInitHook), reinterpret_cast<void**>(&originalTextButtonInit));
    auto inputClass = api.Class("Core.dll", Figma, "TextInput");
    api.FindVerified(inputClass, ".ctor", 0, 0x178DC90);
    api.FindVerified(inputClass, "get_text", 0, 0x178DDA0);
    api.FindVerified(inputClass, "set_text", 1, 0x178EC50);
    api.FindVerified(inputClass, "set_placeholderText", 1, 0x178EAA0);
    api.FindVerified(inputClass, "set_maxLength", 1, 0x178E9B0);
    setPassword = api.FindVerified(inputClass, "set_isPassword", 1, 0x178E8F0);
    auto elementClass = api.Class(UiModule, UiSpace, "VisualElement");
    api.FindVerified(elementClass, "get_parent", 0, 0x55F7180);
    api.FindVerified(elementClass, "IndexOf", 1, 0x55E55B0);
    api.FindVerified(elementClass, "Insert", 2, 0x55E5710);
    api.FindVerified(elementClass, "RemoveFromHierarchy", 0, 0x55E7BD0);
    api.FindVerified(elementClass, "get_style", 0, 0x55F74A0);
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
    // Parent/section shape of the native menu: the page shows each section as
    // its own block, in this order, with the ids that follow ModCategory.
    struct Section { const char* title; std::vector<Obj> rows; };
    const Section layout[] = {
        {"Apparence et animations", {modeOption.get(), roleplayModeOption.get(), animationOption.get()}},
        {"Surbrillance", {glowOption.get(), glowNpcsOption.get(), glowInteractivesOption.get()}},
        {"Cartes", {resourcesOption.get(), resourcesMiniOption.get(), miniCenterOption.get()}},
        {"Déplacements", {transitionsOption.get(), preloadOption.get(), spouseOption.get()}},
        {"Pierre d’âme", {soulOption.get(), soulAutoOption.get(),
            metamobSyncOption.get(), metamobKeyOption.get(), metamobForgetOption.get()}},
        {"Chargement et mises à jour", {autoLoadOption.get(), githubOption.get()}},
        {"À propos", {creditsOption.get()}}};
    static_assert(std::size(layout) == ModSections, "Restore maps every section id back to the game category");
    auto sections = api.Field(category.get(), "subcategories");
    int id = ModCategory;
    for (const auto& [title, rows] : layout) {
        Ref section(MakeCategory(title, ++id, true));
        auto options = api.Field(section.get(), "options");
        for (const auto row : rows) api.Call(options, "Add", {row});
        api.Call(sections, "Add", {section.get()});
    }
    Page owned;
    owned.owner.reset(page);
    owned.category = std::move(category);
    pages.push_back(std::move(owned));
    try { api.Call(categories.get(), "Add", {pages.back().category.get()}); }
    catch (...) { pages.pop_back(); throw; }
    Log(std::string("UI_MOD_REGISTERED category=") + ModCategoryTitle + " controls=native loaded=" + ModVersion + " version=" + ModSemanticVersion);
    return true;
}
void OptionsUi::Tick() {
    if (!module || !module->servicesReady || module->stopped) return;
    RefreshKeyFields(*this);
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
    next.glowNpcs = api.Value<bool>(api.Call(glowNpcsSource.get(), "get_property"));
    next.glowInteractives = api.Value<bool>(api.Call(glowInteractivesSource.get(), "get_property"));
    next.fastMapTransitions = api.Value<bool>(api.Call(transitionsSource.get(), "get_property"));
    next.preloadNextMap = api.Value<bool>(api.Call(preloadSource.get(), "get_property"));
    next.spouseTeleport = api.Value<bool>(api.Call(spouseSource.get(), "get_property"));
    next.resourceMapEnabled = api.Value<bool>(api.Call(resourcesSource.get(), "get_property"));
    next.resourceMiniMapEnabled = api.Value<bool>(api.Call(resourcesMiniSource.get(), "get_property"));
    next.miniMapCenterButton = api.Value<bool>(api.Call(miniCenterSource.get(), "get_property"));
    next.metamobSync = api.Value<bool>(api.Call(metamobSyncSource.get(), "get_property"));
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
    // Owned key fields first: no pooled view may keep one once the hooks stop.
    DropKeyFields(*this);
    forgetDeadline = 0;
    pendingPage.reset();
    stableList.reset();
    std::exception_ptr failure;
    for (auto it = pages.begin(); it != pages.end();) {
        try {
            auto categories = api.Field(it->owner.get(), "m_options");
            int selected = SelectedCategory(it->owner.get());
            // The mod's parent or any of its sections: back to « Jeu ».
            if (selected >= ModCategory && selected <= ModCategory + ModSections)
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
    glowNpcsSource.reset(); glowNpcsOption.reset();
    glowInteractivesSource.reset(); glowInteractivesOption.reset();
    preloadSource.reset();
    preloadOption.reset();
    resourcesSource.reset();
    resourcesOption.reset();
    resourcesMiniSource.reset(); resourcesMiniOption.reset();
    miniCenterSource.reset(); miniCenterOption.reset();
    creditsOption.reset(); creditsMarker.reset();
    metamobSyncSource.reset(); metamobSyncOption.reset();
    metamobKeyOption.reset(); metamobKeyMarker.reset(); metamobForgetOption.reset(); metamobForgetMarker.reset();
    spouseSource.reset();
    spouseOption.reset();
    transitionsSource.reset();
    transitionsOption.reset();
    container.reset();
}
void OptionsUi::Abandon() {
    DropKeyFields(*this);
    pages.clear(); pendingPage.reset(); stableList.reset();
}
bool OptionsUi::KeepResidentHook(void*) const { return false; }
