#include "souls/soul-item-view.hpp"
#include "core/module.hpp"

namespace soul {
namespace {
using namespace managed;
constexpr const char* SlotSpace = "Core.UILogic.Components.Slots";
constexpr const char* BuilderSpace = "Core.UILogic.Components.Tooltips.Builder";
constexpr const char* PositionSpace = "Core.UILogic.Components.Tooltips.Builder.Internal";
constexpr const char* TooltipName = "dofus_miniatures_soul_equipped_item";
struct ItemState {
    Ref slot, item, tooltip, tooltipService, tooltipName, compositor, dependencies, criteria;
    bool spell = false, cursorOwned = false;
    void (*click)(managed::Obj) = nullptr;
    bool installed = false, entered = false, dirty = false, reported = false, bound = false;
} itemState, spellState;
using EventFn = void(*)(Obj, Obj, Method);
using RefreshFn = void(*)(Obj, Method);
EventFn originalEnter = nullptr, originalLeave = nullptr, originalDetach = nullptr, originalClick = nullptr;
RefreshFn originalRefresh = nullptr;
Method setCursor = nullptr, removeCursor = nullptr;
void Report(ItemState& state, const char* operation, const std::exception& error) {
    if (!state.reported) Log(std::string("SOUL_ITEM_VIEW_ERROR ") + operation + ": " + error.what());
    state.reported = true;
}
bool Session() {
    return module && !module->stopped && module->servicesReady && module->compositor.get();
}
void CloseTooltip(ItemState& state) {
    // Check the actual builder before removing by name, including when the
    // session changed. Never close a tooltip another UI replaced ours with.
    if (state.tooltip.get() && state.tooltipService.get() && state.tooltipName.get()) {
        Ref current(api.Call(state.tooltipService.get(), "bfqc", {state.tooltipName.get()}));
        if (current.get() == state.tooltip.get()) {
            bool force = false;
            api.Call(state.tooltipService.get(), "bfps", {state.tooltipName.get(), &force});
        }
    }
    state.tooltip.reset(); state.tooltipService.reset(); state.tooltipName.reset();
    state.dependencies.reset(); state.criteria.reset();
}
void SafeClose(ItemState& state) {
    try { CloseTooltip(state); }
    catch (const std::exception& error) { Report(state, "hide", error); }
}
bool Visible(ItemState& state) {
    if (!state.slot.get() || !state.item.get() || !state.bound || !Session()) return false;
    if (!api.Call(state.slot.get(), "get_panel")) return false;
    Ref ancestor(state.slot.get());
    for (unsigned depth = 0; ancestor.get(); ++depth) {
        if (depth >= 128) return false;
        Ref style(api.Call(ancestor.get(), "get_resolvedStyle"));
        if (api.Value<int>(api.Call(style.get(), "get_display")) != 0) return false;
        ancestor.reset(api.Call(ancestor.get(), "get_parent"));
    }
    return true;
}
// Use Dofus's UI cursor layer keyed by this exact slot, never a global OS cursor.
void UpdateCursor(ItemState& state) {
    if (!state.spell) return;
    int layer = 3; // CursorLayerEnum.UI.
    if (state.entered && Visible(state)) {
        int64_t hand = 2097152; // CursorType.Link is a 64-bit enum.
        int normal = 0; // CursorStateEnum.None is a 32-bit enum.
        state.cursorOwned = true; // Retain cleanup ownership even after a partial native failure.
        api.Invoke(setCursor, nullptr, {&layer, &hand, &normal, state.slot.get()});
    } else if (state.cursorOwned) {
        api.Invoke(removeCursor, nullptr, {&layer, state.slot.get()});
        state.cursorOwned = false;
    }
}
void SafeCursor(ItemState& state) {
    try { UpdateCursor(state); }
    catch (const std::exception& error) { Report(state, "cursor", error); }
}
void ShowTooltip(ItemState& state) {
    if (!state.entered || !Visible(state)) return;
    Ref tooltipService(module->FindService("fou"));
    Ref dependencies(module->FindService(state.spell ? "frc" : "fqm"));
    Ref criteria(module->FindService("fkq"));
    if (!tooltipService.get() || !dependencies.get() || !criteria.get())
        throw std::runtime_error("Item tooltip services unavailable");
    if (state.tooltip.get()) {
        if (state.tooltipService.get() == tooltipService.get() &&
            state.compositor.get() == module->compositor.get() &&
            state.dependencies.get() == dependencies.get() && state.criteria.get() == criteria.get() &&
            api.Call(tooltipService.get(), "bfqc", {state.tooltipName.get()}) == state.tooltip.get()) return;
        CloseTooltip(state);
    }
    auto serviceClass = api.Class("Core.dll", "", "gbr");
    if (!api.class_is_assignable_from(serviceClass, api.object_get_class(tooltipService.get())))
        throw std::runtime_error("Unsupported tooltip service");
    const auto tooltipName = state.spell ? "dofus_miniatures_capture_spell" : TooltipName;
    Ref name(api.string_new(tooltipName));
    if (api.Call(tooltipService.get(), "bfqc", {name.get()}))
        throw std::runtime_error("Item tooltip name is already owned");
    auto builderClass = api.Class("Core.dll", BuilderSpace, state.spell ? "SpellTooltipBuilder" : "ItemTooltipBuilder");
    auto positionClass = api.Class("Core.dll", PositionSpace, "TooltipPositioning");
    // Metadata order selects the (VisualElement) positioning constructor and
    // the factory overload taking TooltipPositioning; verify both exact RVAs.
    api.Native(api.Find(positionClass, ".ctor", 1), 0x1666720);
    Ref position(api.New(positionClass, {state.slot.get()}));
    if(state.spell) {
        int source=1,tooltip=7; // TopCenter of slot, BottomCenter of tooltip.
        api.Call(position.get(),"set_sourcePlacementOrigin",{&source});
        api.Call(position.get(),"set_tooltipPlacementOrigin",{&tooltip});
    }
    Ref builder;
    if (state.spell) {
        auto infoClass = api.Class("Core.dll", BuilderSpace, "SpellTooltipInfo");
        auto infoCtor = api.FindVerified(infoClass, ".ctor", 2, 0x1647FD0);
        Ref info(api.object_new(infoClass));
        api.Invoke(infoCtor, info.get(), {state.item.get(), nullptr});
        auto ctor = api.FindVerified(builderClass, ".ctor", 4, 0x1647AC0);
        builder.reset(api.object_new(builderClass));
        api.Invoke(ctor, builder.get(), {info.get(), dependencies.get(), criteria.get(), position.get()});
    } else {
        auto factory = api.Find(builderClass, "CreateItemTooltip", 9);
        api.Native(factory, 0x1610130);
        bool no = false;
        builder.reset(api.Invoke(factory, nullptr, {dependencies.get(), criteria.get(), state.item.get(),
            position.get(), &no, &no, &no, nullptr, &no}));
    }
    if (!builder.get()) throw std::runtime_error("Native item tooltip unavailable");
    api.Call(builder.get(), "set_nameId", {name.get()});
    state.tooltip.reset(builder.get()); state.tooltipService.reset(tooltipService.get());
    state.tooltipName.reset(name.get()); state.compositor.reset(module->compositor.get());
    state.dependencies.reset(dependencies.get()); state.criteria.reset(criteria.get());
    Ref registered(api.Call(tooltipService.get(), "bfpo", {builder.get()}));
    if (!registered.get() || api.String(registered.get()) != tooltipName)
        throw std::runtime_error("Native item tooltip registration failed");
    state.dirty = false;
    state.reported = false;
}
ItemState* Owned(Obj owner) {
    if (!owner) return nullptr;
    if (owner == itemState.slot.get()) return &itemState;
    if (owner == spellState.slot.get()) return &spellState;
    return nullptr;
}
void EnterHook(Obj owner, Obj event, Method method) {
    originalEnter(owner, event, method);
    auto* view = Owned(owner); if (!view) return;
    view->entered = true;
    SafeCursor(*view);
    try { ShowTooltip(*view); }
    catch (const std::exception& error) { Report(*view, "show", error); SafeClose(*view); }
}
void LeaveHook(Obj owner, Obj event, Method method) {
    if (auto* view = Owned(owner)) { view->entered = false; SafeCursor(*view); SafeClose(*view); }
    originalLeave(owner, event, method);
}
void DetachHook(Obj owner, Obj event, Method method) {
    if (auto* view = Owned(owner)) { view->entered = false; SafeCursor(*view); SafeClose(*view); }
    originalDetach(owner, event, method);
}
void ClickHook(Obj owner, Obj event, Method method) {
    if (auto* view = Owned(owner)) {
        try {
            if (view->spell && view->click && Visible(*view) &&
                api.Value<bool>(api.Call(owner, "get_enabledInHierarchy"))) view->click(view->item.get());
        } catch (const std::exception& error) { Report(*view, "click", error); }
        return; // The item preview has no click action; spells use only our validated route.
    }
    originalClick(owner, event, method);
}
void RefreshHook(Obj owner, Method method) {
    originalRefresh(owner, method);
    if (auto* view = Owned(owner)) view->dirty = true;
}
}
void InstallItemView() {
    if (itemState.installed) return;
    auto position=api.Class("Core.dll",PositionSpace,"TooltipPositioning");
    api.Native(api.Find(position,"set_sourcePlacementOrigin",1),0x7D2B60);
    api.Native(api.Find(position,"set_tooltipPlacementOrigin",1),0x758DE0);
    auto cursor = api.Class("Core.dll", "Core.UILogic", "DofusCursor");
    setCursor = api.FindVerified(cursor, "SetCursor", 4, 0x280A070);
    removeCursor = api.FindVerified(cursor, "RemoveCursor", 2, 0x2809EA0);
    auto slot = api.Class("Core.dll", SlotSpace, "Slot");
    api.Native(api.Find(slot, ".ctor", 0), 0x165E8E0);
    api.Native(api.Find(slot, "set_data", 1), 0x165EC60);
    api.Native(api.Find(slot, "set_slotSize", 1), 0x165F4F0);
    api.Native(api.Find(slot, "set_slotType", 1), 0x165F5F0);
    api.Native(api.Find(slot, "DisableDrag", 0), 0x165ADC0);
    auto service = api.Class("Core.dll", "", "gbr");
    api.Native(api.Find(service, "bfpo", 1), 0x1EEE490);
    api.Native(api.Find(service, "bfps", 2), 0x1EEF050);
    api.Native(api.Find(service, "bfqc", 1), 0x1EEFF60);
    auto builder = api.Class("Core.dll", BuilderSpace, "ItemTooltipBuilder");
    api.Native(api.Find(builder, "UpdateContent", 2), 0x1617AA0);
    AddHook(api.Find(slot, "OnMouseEnter", 1), 0x165C170,
        reinterpret_cast<void*>(&EnterHook), reinterpret_cast<void**>(&originalEnter));
    AddHook(api.Find(slot, "OnMouseLeave", 1), 0x165C200,
        reinterpret_cast<void*>(&LeaveHook), reinterpret_cast<void**>(&originalLeave));
    AddHook(api.Find(slot, "OnDetachedFromPanel", 1), 0x165BDF0,
        reinterpret_cast<void*>(&DetachHook), reinterpret_cast<void**>(&originalDetach));
    AddHook(api.Find(slot, "OnClick", 1), 0x165B840,
        reinterpret_cast<void*>(&ClickHook), reinterpret_cast<void**>(&originalClick));
    AddHook(api.Find(slot, "Refresh", 0), 0x165CF10,
        reinterpret_cast<void*>(&RefreshHook), reinterpret_cast<void**>(&originalRefresh));
    itemState.installed = true;
}
static void ReleaseView(ItemState& state);
static Obj CreateView(ItemState& view, Obj parent) {
    ReleaseView(view);
    if (!itemState.installed) throw std::runtime_error("Soul item view is not installed");
    view.slot.reset(api.New(api.Class("Core.dll", SlotSpace, "Slot")));
    api.Call(view.slot.get(), "DisableDrag");
    api.Call(parent, "Add", {view.slot.get()});
    return view.slot.get();
}
Obj CreateItemView(Obj parent) {
    auto slot = CreateView(itemState, parent);
    // Size the native tile/image as well as the outer element in the panel.
    int medium = 2; // Native 80px Slot; its embedded inventory Tile fills the content area.
    api.Call(slot, "set_slotSize", {&medium});
    return slot;
}
Obj CreateSpellView(Obj parent) {
    spellState.spell = true;
    auto slot = CreateView(spellState, parent);
    int medium = 2, spell = 4;
    api.Call(slot, "set_slotSize", {&medium});
    api.Call(slot, "set_slotType", {&spell});
    return slot;
}
void SetSpellClick(void (*click)(managed::Obj)) { spellState.click = click; }
static bool UpdateView(ItemState& state, Obj item) {
    if (!state.slot.get()) return false;
    try {
        if (state.compositor.get() && (!Session() || state.compositor.get() != module->compositor.get()))
            CloseTooltip(state);
        if (!state.bound || item != state.item.get()) {
            CloseTooltip(state);
            // Native setters can fail after partially changing their binding.
            // Force a retry even if the next inventory snapshot reverts to the
            // previously committed wrapper.
            state.bound = false;
            api.Call(state.slot.get(), "set_data", {item});
            state.item.reset(item);
            state.bound = true;
            state.dirty = false;
        }
        if (!Visible(state)) { CloseTooltip(state); return true; }
        if (state.tooltip.get() && state.dirty) {
            if (state.spell) CloseTooltip(state);
            else {
                bool reset = true;
                api.Call(state.tooltip.get(), "UpdateContent", {item, &reset});
            }
            state.dirty = false;
        }
        if (state.entered) ShowTooltip(state);
        return true;
    } catch (const std::exception& error) { Report(state, "update", error); SafeClose(state); return state.bound && state.item.get() == item; }
}
static void ReleaseView(ItemState& state) {
    state.entered = false;
    // A failed removal must retain the slot for the panel's cleanup retry.
    UpdateCursor(state);
    SafeClose(state);
    if (state.slot.get()) {
        try { api.Call(state.slot.get(), "set_data", {nullptr}); }
        catch (const std::exception& error) { Report(state, "unbind", error); }
        try { api.Call(state.slot.get(), "RemoveFromHierarchy"); }
        catch (const std::exception& error) { Report(state, "detach", error); }
    }
    state.slot.reset(); state.item.reset(); state.compositor.reset();
    state.dirty = state.bound = false;
}
bool UpdateItemView(Obj item) { return UpdateView(itemState, item); }
bool UpdateSpellView(Obj spell, bool enabled) {
    const bool bound = UpdateView(spellState, spell);
    enabled = enabled && bound && spell;
    if (spellState.slot.get()) api.Call(spellState.slot.get(), "SetEnabled", {&enabled});
    // Native SetEnabled(false) removes its cursor. Keep the hover affordance on
    // the displayed spell; actual casting still checks native availability.
    SafeCursor(spellState);
    return bound;
}
void ReleaseItemView() { ReleaseView(itemState); }
void ReleaseSpellView() { ReleaseView(spellState); }

}
