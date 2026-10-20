#include "context-display.hpp"
#include "soul-group-menu.hpp"
#include "spouse-party-menu.hpp"
#include "soul-panel.hpp"
#include "map-travel.hpp"
#include "module.hpp"
#include "mode-policy.hpp"
#include "animation-mode-labels.hpp"
#include "glow-mode-labels.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace context_display_detail {
using namespace managed;
constexpr const char* MenuNamespace = "Core.UILogic.Components.ContextMenu";
constexpr const char* MakerNamespace = "Core.UILogic.Components.ContextMenu.ContextMenuFactory";
constexpr const char* Labels[] = {"Normal", "Cr\xC3\xA9" "atures", "Miniatures"};
// Core.UILogic.Components.Figma.FigmaIcons in the pinned 3.7.11.11 metadata.
constexpr int DisplayIcon = 136; // eye
constexpr int AnimationIcon = 226; // play
struct Choice { Ref item, callback; };
struct OwnedMenu {
    Ref menu, maker, cached, option, original, before, after, replacement, submenu;
    Ref animationReplacement, animationSubmenu;
    Ref glowReplacement, glowSubmenu;
    std::array<Choice, 3> choices;
    std::array<Choice, 4> animationChoices;
    std::array<Choice, 4> glowChoices;
    std::array<Choice, 2> glowCategories;
    std::array<bool, 2> glowIncludes{};
    int originalIndex = 0, mode = 0, animationMode = 0, glowMode = 1;
    bool combat = false, armed = false, seenRoot = false;
    ULONGLONG created = 0;
};
struct State {
    std::unique_ptr<OwnedMenu> owned;
    Klass menuClass = nullptr, itemClass = nullptr, viewClass = nullptr, callbackClass = nullptr;
    Method appendRadio = nullptr, appendCheckbox = nullptr;
    bool installed = false, failed = false, busy = false;
} state;
using CreateFn = Obj(*)(Obj, Method);
using CallbackFn = void(*)(Obj, Obj, Method);
CreateFn originalWorld = nullptr, originalFight = nullptr;
CallbackFn originalCallback = nullptr;
struct BusyGuard {
    bool previous = state.busy;
    BusyGuard() { state.busy = true; }
    ~BusyGuard() { state.busy = previous; }
};
bool Available() {
    return state.installed && !state.failed && module && !module->stopped &&
        !module->stopRequested.load() && module->servicesReady;
}
bool Is(Obj object, Klass klass) {
    return object && api.class_is_assignable_from(klass, api.object_get_class(object));
}
int Count(Obj list) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > 1024) throw std::runtime_error("Unexpected context-menu item count");
    return count;
}
int IndexOf(Obj menu, Obj wanted) {
    Ref items(api.Call(menu, "get_menuItems"));
    for (int i = 0, count = Count(items.get()); i < count; ++i)
        if (api.Call(items.get(), "get_Item", {&i}) == wanted) return i;
    return -1;
}
Obj ItemAt(Obj menu, int index) {
    Ref items(api.Call(menu, "get_menuItems"));
    if (index < 0 || index >= Count(items.get())) throw std::runtime_error("Context-menu item index outside list");
    return api.Call(items.get(), "get_Item", {&index});
}
Obj Root(Obj menu) {
    auto root = api.Field(menu, "dofusContextualMenuRoot");
    if (root && api.Call(root, "get_menuData") != menu)
        throw std::runtime_error("Context-menu root belongs to another model");
    return root;
}
bool SameContext(const OwnedMenu& owned) {
    if (!Available() || module->cached.get() != owned.cached.get() || module->InCombat() != owned.combat ||
        api.Field(owned.maker.get(), "m_cachedDataService") != owned.cached.get()) return false;
    const auto option = owned.combat ? module->creatureOption.get() : module->roleplayOption.get();
    if (!option || option != owned.option.get() || api.Call(owned.cached.get(), "get_creatureMode") != option)
        return false;
    Ref container(api.Call(owned.cached.get(), "get_dofusOptionsContainer"));
    return container.get() && api.Field(container.get(), owned.combat ? "creatureModeFight" : "creatureModeRoleplay") == option;
}

// The native root owns keyboard navigation and menu teardown. RemoveMenu(false)
// schedules its usual close; invalidated, null-action handlers remain inert until then.
void RestoreOwned(bool closeRoot) {
    auto owned = std::move(state.owned);
    if (!owned) return;
    owned->armed = false;
    BusyGuard busy;
    std::string firstError;
    const auto attempt = [&](auto action) {
        try { action(); }
        catch (const std::exception& e) { if (firstError.empty()) firstError = e.what(); }
        catch (...) { if (firstError.empty()) firstError = "Unknown menu restoration error"; }
    };
    int restoreIndex = owned->originalIndex;
    attempt([&] {
        const int current = IndexOf(owned->menu.get(), owned->replacement.get());
        if (current >= 0) restoreIndex = current;
    });
    // Each removal is independent so a failed row removal does not skip the other.
    for (auto replacement : {owned->replacement.get(), owned->animationReplacement.get(), owned->glowReplacement.get()}) attempt([&] {
        for (int index; (index = IndexOf(owned->menu.get(), replacement)) >= 0;)
            api.Call(owned->menu.get(), "RemoveItemAt", {&index});
    });
    attempt([&] {
        if (IndexOf(owned->menu.get(), owned->original.get()) >= 0) return;
        const int after = owned->after.get() ? IndexOf(owned->menu.get(), owned->after.get()) : -1;
        const int before = owned->before.get() ? IndexOf(owned->menu.get(), owned->before.get()) : -1;
        if (after >= 0) restoreIndex = after;
        else if (before >= 0) restoreIndex = before + 1;
        Ref items(api.Call(owned->menu.get(), "get_menuItems"));
        restoreIndex = std::clamp(restoreIndex, 0, Count(items.get()));
        api.Call(owned->menu.get(), "InsertItem", {&restoreIndex, owned->original.get()});
    });
    attempt([&] {
        bool enabled = api.Value<bool>(api.Call(owned->option.get(), "get_property"));
        api.Call(owned->original.get(), "set_isOn", {&enabled});
    });
    if (closeRoot) {
        // Native submenus are siblings under the service's visual root. Closing
        // the main root alone does not unregister or detach the other popups.
        for (auto menu : {owned->submenu.get(), owned->animationSubmenu.get(), owned->glowSubmenu.get(), owned->menu.get()}) attempt([&] {
            Ref root(Root(menu));
            if (root.get() && api.Call(root.get(), "get_panel")) {
                bool delayed = false;
                api.Call(root.get(), "RemoveMenu", {&delayed});
            }
        });
    }
    if (!firstError.empty()) Log("CONTEXT_DISPLAY_RESTORE_ERROR " + firstError);
}
void Fail(const char* reason) {
    state.failed = true;
    Log(std::string("CONTEXT_DISPLAY_DISABLED ") + reason);
    RestoreOwned(true);
}
void SyncGlowCheckboxViews(OwnedMenu& owned) {
    Ref root(Root(owned.glowSubmenu.get()));
    if (!root.get() || !api.Call(root.get(), "get_panel")) return;
    Ref views(api.Call(root.get(), "GetChildItemsInMenu"));
    for (int index = 0, count = Count(views.get()); index < count; ++index) {
        Ref view(api.Call(views.get(), "get_Item", {&index}));
        if (!Is(view.get(), state.viewClass) || !api.Call(view.get(), "get_panel")) continue;
        Ref item(api.Call(view.get(), "get_itemData"));
        for (size_t category = 0; category < owned.glowCategories.size(); ++category) {
            if (item.get() != owned.glowCategories[category].item.get()) continue;
            if (api.Call(item.get(), "get_root") != owned.glowSubmenu.get()) continue;
            // The model setter only writes ContextMenuItem.isOn. Native views
            // and their embedded CheckboxCustom have separate state, including
            // a native click handler that can run after our callback. Reconcile
            // both owned checkboxes after dispatch as well as during selection.
            api.Call(view.get(), "set_isOn", {&owned.glowIncludes[category]});
        }
    }
}
void Sync(OwnedMenu& owned) {
    const int mode = ActiveDisplayMode(module->settings, owned.combat);
    if (mode < 0 || mode > 2) throw std::runtime_error("Invalid context display selection");
    for (int i = 0; i < 3; ++i) {
        bool selected = mode == i;
        api.Call(owned.choices[i].item.get(), "set_isOn", {&selected});
    }
    owned.mode = mode;
    const int animationMode = module->settings.animationMode;
    if (!Settings::ValidAnimationMode(animationMode)) throw std::runtime_error("Invalid context animation selection");
    for (int i = 0; i < 4; ++i) {
        bool selected = animationMode == i;
        api.Call(owned.animationChoices[i].item.get(), "set_isOn", {&selected});
    }
    owned.animationMode = animationMode;
    const int glowMode = module->settings.monsterGlowMode;
    if (!Settings::ValidMonsterGlowMode(glowMode)) throw std::runtime_error("Invalid context glow selection");
    for (int i = 0; i < 4; ++i) {
        bool selected = glowMode == i;
        api.Call(owned.glowChoices[i].item.get(), "set_isOn", {&selected});
    }
    owned.glowMode = glowMode;
    owned.glowIncludes = {module->settings.glowNpcs, module->settings.glowInteractives};
    for (size_t i = 0; i < owned.glowCategories.size(); ++i)
        api.Call(owned.glowCategories[i].item.get(), "set_isOn", {&owned.glowIncludes[i]});
    SyncGlowCheckboxViews(owned);
}
void BuildSubmenu(Ref& submenu, Ref& replacement, Choice* choices, int count,
    const char* const* labels, const char* caption, int selectedMode, bool disabled, int icon, bool close = true) {
    if (selectedMode < 0 || selectedMode >= count) throw std::runtime_error("Invalid initial context selection");
    submenu.reset(api.New(state.menuClass));
    // Derive the boxed struct from the verified method's actual parameter metadata.
    // AddressableEntry is not re-declared in native code and no fixed struct ABI is assumed.
    const auto assetClass = api.class_from_type(api.method_get_param(state.appendRadio, 4));
    if (!assetClass || !api.class_is_valuetype(assetClass))
        throw std::runtime_error("Radio menu asset parameter is not a value type");
    Ref asset(api.object_new(assetClass));
    if (!asset.get()) throw std::runtime_error("Cannot allocate default menu asset");
    Ref empty(api.string_new(""));
    int group = 0; // Each submenu owns its own independent radio group.
    bool allowOff = false, refreshParent = false;
    unsigned helpDelay = 1000;
    for (int mode = 0; mode < count; ++mode) {
        Ref label(api.string_new(labels[mode]));
        bool selected = mode == selectedMode;
        api.Invoke(state.appendRadio, submenu.get(), {label.get(), nullptr, &selected, &group,
            api.object_unbox(asset.get()), &allowOff, &disabled, &close, empty.get(), empty.get(), &helpDelay, &refreshParent});
        choices[mode].item.reset(ItemAt(submenu.get(), mode));
        choices[mode].callback.reset(api.Call(choices[mode].item.get(), "get_m_callback"));
        if (!Is(choices[mode].callback.get(), state.callbackClass))
            throw std::runtime_error("Unexpected radio item callback handler");
    }
    Ref staging(api.New(state.menuClass));
    Ref title(api.string_new(caption));
    int style = 1; // ListElementStyleEnum.action; native menu renders iconLeft.
    api.Call(staging.get(), "AppendSubMenu", {title.get(), submenu.get(), nullptr, &disabled, &style, &icon});
    replacement.reset(ItemAt(staging.get(), 0));
    int index = 0;
    api.Call(staging.get(), "RemoveItemAt", {&index});
}
void BuildGlowCategories(OwnedMenu& owned) {
    constexpr const char* labels[] = {"Inclure les PNJ", "Inclure les éléments interactifs"};
    constexpr const char* helps[] = {
        "Ajoute les PNJ visibles hors combat au niveau de surbrillance choisi.",
        "Ajoute les objets visibles avec une action disponible hors combat : ressources, portes, zaaps…"};
    owned.glowIncludes = {module->settings.glowNpcs, module->settings.glowInteractives};
    api.Call(owned.glowSubmenu.get(), "AppendSeparator");
    Ref empty(api.string_new(""));
    bool disabled = false, close = false;
    unsigned delay = 1000; int icon = 0;
    for (size_t i = 0; i < owned.glowCategories.size(); ++i) {
        Ref label(api.string_new(labels[i])), help(api.string_new(helps[i]));
        Ref items(api.Call(owned.glowSubmenu.get(), "get_menuItems"));
        const int index = Count(items.get());
        api.Invoke(state.appendCheckbox, owned.glowSubmenu.get(), {label.get(), nullptr, &owned.glowIncludes[i],
            &disabled, &close, help.get(), &delay, &icon, empty.get()});
        auto& choice = owned.glowCategories[i];
        choice.item.reset(ItemAt(owned.glowSubmenu.get(), index));
        choice.callback.reset(api.Call(choice.item.get(), "get_m_callback"));
        if (!Is(choice.callback.get(), state.callbackClass))
            throw std::runtime_error("Unexpected glow checkbox callback handler");
    }
}
void Augment(Obj maker, Obj menu) {
    if (!Available() || state.busy || !menu || !maker ||
        api.Field(maker, "m_cachedDataService") != module->cached.get()) return;
    if (state.owned && state.owned->menu.get() == menu) return;
    BusyGuard busy;
    auto owned = std::make_unique<OwnedMenu>();
    owned->menu.reset(menu);
    owned->maker.reset(maker);
    owned->cached.reset(module->cached.get());
    owned->combat = module->InCombat();
    owned->option.reset(owned->combat ? module->creatureOption.get() : module->roleplayOption.get());
    if (!SameContext(*owned)) return;
    Ref items(api.Call(menu, "get_menuItems"));
    int found = -1;
    const int count = Count(items.get());
    for (int index = 0; index < count; ++index) {
        Ref item(api.Call(items.get(), "get_Item", {&index}));
        if (!Is(item.get(), state.itemClass)) continue;
        Ref callback(api.Call(item.get(), "get_m_callback"));
        if (!callback.get() || std::string(api.class_get_name(api.object_get_class(callback.get()))) != "ClickCallbackHandler`1") continue;
        if (api.Call(callback.get(), "get_arg1") != owned->option.get()) continue;
        if (found >= 0) throw std::runtime_error("Ambiguous native creature menu item");
        found = index;
        owned->original.reset(item.get());
    }
    if (found < 0) return; // No native creature entry, therefore nothing to replace.
    RestoreOwned(true); // At most one native world menu is retained, even if never displayed.
    owned->originalIndex = found;
    if (found > 0) owned->before.reset(ItemAt(menu, found - 1));
    if (found + 1 < count) owned->after.reset(ItemAt(menu, found + 1));
    owned->mode = ActiveDisplayMode(module->settings, owned->combat);
    owned->animationMode = module->settings.animationMode;
    owned->glowMode = module->settings.monsterGlowMode;
    const bool disabled = api.Value<bool>(api.Call(owned->original.get(), "get_disabled"));
    // Allocate all owned rows before the first native list mutation.
    BuildSubmenu(owned->submenu, owned->replacement, owned->choices.data(), 3, Labels, "Affichage", owned->mode, disabled, DisplayIcon);
    BuildSubmenu(owned->animationSubmenu, owned->animationReplacement, owned->animationChoices.data(), 4,
        AnimationModeLabels.data(), "Animations", owned->animationMode, false, AnimationIcon);
    BuildSubmenu(owned->glowSubmenu, owned->glowReplacement, owned->glowChoices.data(), 4,
        GlowModeLabels.data(), "Surbrillance", owned->glowMode, false, DisplayIcon, false);
    BuildGlowCategories(*owned);
    owned->created = GetTickCount64();
    state.owned = std::move(owned); // Publish rollback ownership before the first list mutation.
    api.Call(menu, "RemoveItemAt", {&found});
    api.Call(menu, "InsertItem", {&found, state.owned->replacement.get()});
    int animationIndex = found + 1;
    api.Call(menu, "InsertItem", {&animationIndex, state.owned->animationReplacement.get()});
    int glowIndex = found + 2;
    api.Call(menu, "InsertItem", {&glowIndex, state.owned->glowReplacement.get()});
    state.owned->armed = true;
    Log(std::string("CONTEXT_DISPLAY_READY context=") + (state.owned->combat ? "combat" : "roleplay"));
}
Obj Create(Obj maker, Method method, CreateFn original) {
    Obj menu = original(maker, method);
    try { Augment(maker, menu); }
    catch (const std::exception& e) { Fail(e.what()); }
    catch (...) { Fail("Unknown context menu creation error"); }
    return menu;
}
Obj WorldHook(Obj maker, Method method) { return Create(maker, method, originalWorld); }
Obj FightHook(Obj maker, Method method) { return Create(maker, method, originalFight); }
void CallbackHook(Obj callback, Obj view, Method method) {
    if (HandleMapTravelCallback(callback, view)) return;
    if (soul::HandlePanelMenuCallback(callback, view)) return;
    if (HandleSoulGroupMenuCallback(callback, view)) return;
    if (HandleSpousePartyMenuCallback(callback, view)) return;
    auto* owned = state.owned.get();
    int selected = -1, glowCategory = -1;
    bool animation = false, glow = false;
    if (owned) for (int index = 0; index < 3; ++index)
        if (owned->choices[index].callback.get() == callback) { selected = index; break; }
    if (owned && selected < 0) for (int index = 0; index < 4; ++index)
        if (owned->animationChoices[index].callback.get() == callback) { selected = index; animation = true; break; }
    if (owned && selected < 0) for (int index = 0; index < 4; ++index)
        if (owned->glowChoices[index].callback.get() == callback) { selected = index; glow = true; break; }
    if (owned && selected < 0) for (int index = 0; index < 2; ++index)
        if (owned->glowCategories[index].callback.get() == callback) { selected = index; glowCategory = index; glow = true; break; }
    if (selected < 0) { originalCallback(callback, view, method); return; }
    // Known handlers have null managed actions. Never forward a mismatched/stale
    // invocation to the native creature toggle, and never identify rows by their text.
    try {
        const auto& choice = glowCategory >= 0 ? owned->glowCategories[glowCategory] : glow ? owned->glowChoices[selected] : animation ? owned->animationChoices[selected] : owned->choices[selected];
        const auto submenu = glow ? owned->glowSubmenu.get() : animation ? owned->animationSubmenu.get() : owned->submenu.get();
        const auto replacement = glow ? owned->glowReplacement.get() : animation ? owned->animationReplacement.get() : owned->replacement.get();
        if (!owned->armed || state.busy || !SameContext(*owned) || !Is(view, state.viewClass) ||
            api.Call(view, "get_itemData") != choice.item.get() ||
            api.Call(choice.item.get(), "get_root") != submenu ||
            !api.Call(view, "get_panel") || IndexOf(owned->menu.get(), replacement) < 0 ||
            api.Value<bool>(api.Call(choice.item.get(), "get_disabled"))) return;
        BusyGuard busy;
        auto next = module->settings;
        if (glowCategory == 0) next.glowNpcs = !next.glowNpcs;
        else if (glowCategory == 1) next.glowInteractives = !next.glowInteractives;
        else if (glow) next.monsterGlowMode = selected;
        else if (animation) next.animationMode = selected;
        else (owned->combat ? next.mode : next.roleplayMode) = selected;
        module->Change(next);
        if (state.owned.get() == owned && SameContext(*owned)) Sync(*owned);
    } catch (const std::exception& e) { Fail(e.what()); }
    catch (...) { Fail("Unknown context menu selection error"); }
}
}

void InstallContextDisplayMenu() {
    using namespace context_display_detail;
    state.menuClass = api.Class("Core.dll", MenuNamespace, "DofusContextualMenu");
    state.itemClass = api.Class("Core.dll", MenuNamespace, "ContextMenuItem");
    state.viewClass = api.NestedClass(state.itemClass, "ContextMenuItemView");
    state.callbackClass = api.NestedClass(state.itemClass, "ClickCallbackHandler");
    const auto root = api.NestedClass(state.menuClass, "DofusContextualMenuRoot");
    const auto cached = api.Class("Core.dll", "Core.Services.Player.OptionsService", "CachedDataService");
    state.appendRadio = api.Find(state.menuClass, "AppendRadioButtonItem", 12);
    api.Native(state.appendRadio, 0x16D72F0);
    state.appendCheckbox = api.Find(state.menuClass, "AppendCheckboxItem", 9);
    api.Native(state.appendCheckbox, 0x16D63D0);
    api.Native(api.Find(state.menuClass, "AppendSeparator", 0), 0x16D76E0);
    api.Native(api.Find(state.menuClass, ".ctor", 0), 0x16D8CC0);
    api.Native(api.Find(state.menuClass, "AppendSubMenu", 6), 0x16D7CF0);
    api.Native(api.Find(state.menuClass, "InsertItem", 2), 0x16D8850);
    api.Native(api.Find(state.menuClass, "RemoveItemAt", 1), 0x16D8BF0);
    api.Native(api.Find(state.menuClass, "get_menuItems", 0), 0x68D2B0);
    api.Native(api.Find(state.itemClass, "get_m_callback", 0), 0x16CF040);
    api.Native(api.Find(state.itemClass, "get_disabled", 0), 0x16CF020);
    api.Native(api.Find(state.itemClass, "set_isOn", 1), 0x11ECD70);
    api.Native(api.Find(state.viewClass, "get_itemData", 0), 0x157EFD0);
    api.Native(api.Find(state.viewClass, "set_isOn", 1), 0x16CE970);
    api.Native(api.Find(root, "get_menuData", 0), 0x157EFE0);
    api.Native(api.Find(root, "GetChildItemsInMenu", 0), 0x16D18C0);
    api.Native(api.Find(root, "RemoveMenu", 1), 0x16D2AB0);
    api.Native(api.Find(cached, "get_creatureMode", 0), 0x18B7E20);
    AddHook(api.Find(api.Class("Core.dll", MakerNamespace, "WorldMenuMaker"), "CreateMenu", 0), 0x1700070,
        reinterpret_cast<void*>(WorldHook), reinterpret_cast<void**>(&originalWorld));
    AddHook(api.Find(api.Class("Core.dll", MakerNamespace, "FightWorldMenuMaker"), "CreateMenu", 0), 0x16DC360,
        reinterpret_cast<void*>(FightHook), reinterpret_cast<void**>(&originalFight));
    AddHook(api.Find(state.callbackClass, "InvokeCallback", 1), 0x16CA600,
        reinterpret_cast<void*>(CallbackHook), reinterpret_cast<void**>(&originalCallback));
    state.failed = false;
    state.installed = true;
}
void TickContextDisplayMenu() {
    using namespace context_display_detail;
    if (!state.owned || state.busy) return;
    try {
        if (!SameContext(*state.owned)) { RestoreOwned(true); return; }
        Ref root(Root(state.owned->menu.get()));
        const bool attached = root.get() && api.Call(root.get(), "get_panel");
        if (attached) state.owned->seenRoot = true;
        if ((!attached && state.owned->seenRoot) || (!attached && GetTickCount64() - state.owned->created > 5000)) {
            RestoreOwned(true); // An independent submenu root can still be attached.
            return;
        }
        // Native views snapshot state during creation. Closing when preferences
        // change elsewhere avoids presenting stale radio marks, checkbox marks or a stale choice.
        if (ActiveDisplayMode(module->settings, state.owned->combat) != state.owned->mode ||
            module->settings.animationMode != state.owned->animationMode ||
            module->settings.monsterGlowMode != state.owned->glowMode ||
            module->settings.glowNpcs != state.owned->glowIncludes[0] ||
            module->settings.glowInteractives != state.owned->glowIncludes[1])
            RestoreOwned(true);
        else SyncGlowCheckboxViews(*state.owned);
    } catch (const std::exception& e) { Fail(e.what()); }
    catch (...) { Fail("Unknown context menu lifecycle error"); }
}
void RestoreContextDisplayMenu() {
    using namespace context_display_detail;
    state.installed = false;
    RestoreOwned(true);
}
