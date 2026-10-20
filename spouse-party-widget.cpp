#include "spouse-party-widget.hpp"
#include "spouse-party-menu.hpp"
#include "module.hpp"
#include <array>

namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
struct State {
    Ref hub, display, slot, member, root, portrait, cached, compositor, social, party;
    Ref button, border, heart, tooltip, tooltipService;
    bool installed = false, removing = false, busy = false, reported = false;
    uint64_t refresh = 0, cooldown = 0;
    std::string tip;
    int size = 0;
} state;
Klass hubClass{}, displayClass{}, slotClass{}, bindingClass{}, toggleClass{}, iconClass{};
Method getRoot{}, getPortrait{};
Klass tooltipClass{};
Method tooltipCtor{}, addManipulator{}, removeManipulator{}, hideTooltip{};
bool Is(Obj object, Klass klass) { return object && api.object_get_class(object) == klass; }
void DiscoveryState(const std::string& stage) {
    static std::string previous;
    static uint64_t next = 0;
    const auto now = GetTickCount64();
    if (stage == previous || now < next) return;
    previous = stage; next = now + 5000;
    Log("SPOUSE_PARTY_WIDGET_STATE " + stage);
}
bool Available() {
    return state.installed && module && module->ready && module->servicesReady && !module->stopped &&
        !module->stopRequested && module->settings.spouseTeleport;
}
Obj BindingElement(Obj slot, Method getter) {
    Ref binding(api.Field(slot, "binding"));
    if (!Is(binding.get(), bindingClass)) return nullptr;
    return api.Invoke(getter, api.object_unbox(binding.get()));
}
bool ContainsSlot(Obj display, Obj slot) {
    Ref slots(api.Field(display, "m_partyMemberSlots"));
    if (!slots.get()) return false;
    int count = api.Value<int>(api.Call(slots.get(), "get_Count"));
    if (count < 0 || count > 128) return false;
    for (int i = 0; i < count; ++i) if (api.Call(slots.get(), "get_Item", {&i}) == slot) return true;
    return false;
}
bool Current() {
    return Available() && !state.removing && state.slot.get() &&
        module->cached.get() == state.cached.get() && module->compositor.get() == state.compositor.get() &&
        module->FindService("foi") == state.social.get() && module->FindService("fne") == state.party.get() &&
        module->FindService("fou") == state.tooltipService.get() &&
        module->FindService("fqv") == state.hub.get() && api.Field(state.hub.get(), "dytb") == state.display.get() &&
        !*static_cast<bool*>(api.FieldAddress(state.display.get(), "isArenaParty")) &&
        ContainsSlot(state.display.get(), state.slot.get()) &&
        api.Field(state.slot.get(), "partyMemberWrapper") == state.member.get() &&
        !*static_cast<bool*>(api.FieldAddress(state.slot.get(), "m_isWaitingMember")) &&
        BindingElement(state.slot.get(), getRoot) == state.root.get() &&
        BindingElement(state.slot.get(), getPortrait) == state.portrait.get() &&
        api.Call(state.portrait.get(), "get_parent") == state.root.get() && api.Call(state.root.get(), "get_panel");
}
Obj ReadStyle(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    return api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str());
}
void WriteStyle(Obj element, const char* property, Obj value) {
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(value)});
}
template<class T> void Style(Obj element, const char* property, T value) {
    Ref box(ReadStyle(element, property)); api.Call(box.get(), "set_value", {&value}); WriteStyle(element, property, box.get());
}
void Pixels(Obj element, const char* property, float value) {
    int unit = 0; Ref length(api.New(api.Class("UnityEngine.UIElementsModule.dll", Ui, "Length"), {&value, &unit}));
    Ref box(ReadStyle(element, property)); api.Call(box.get(), "set_value", {api.object_unbox(length.get())}); WriteStyle(element, property, box.get());
}
void Name(Obj element, const char* name) { Ref text(api.string_new(name)); api.Call(element, "set_name", {text.get()}); }
void Size(Obj element, float size) {
    for (auto property : {"width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight"}) Pixels(element, property, size);
}
void Ignore(Obj element) { int ignore = 1; api.Call(element, "set_pickingMode", {&ignore}); }
void ClearTooltip() {
    if (!state.tooltip.get()) return;
    api.Invoke(hideTooltip, state.tooltip.get());
    api.Invoke(removeManipulator, nullptr, {state.button.get(), state.tooltip.get()});
    state.tooltip.reset();
}
void Refresh(const std::string& distance, bool online) {
    const bool combat = module->InCombat();
    bool enabled = online && !combat && GetTickCount64() >= state.cooldown;
    api.Call(state.button.get(), "SetEnabled", {&enabled});
    bool off = false; api.Call(state.button.get(), "SetValueWithoutNotify", {&off});
    const auto tip = "Se téléporter au conjoint — " + distance +
        (combat ? "\nIndisponible pendant un combat." : "\nPortée et restrictions vérifiées par Dofus.") +
        "\nDistance de carte, hors détours, d'après la dernière position connue.";
    if (tip != state.tip) {
        ClearTooltip();
        Ref text(api.string_new(tip.c_str()));
        state.tooltip.reset(api.object_new(tooltipClass));
        int origin = 1, source = 7, behavior = 1; bool front = false;
        api.Invoke(tooltipCtor, state.tooltip.get(), {state.tooltipService.get(), text.get(), &origin, &source, &behavior, &front});
        api.Invoke(addManipulator, nullptr, {state.button.get(), state.tooltip.get()});
        state.tip = tip;
    }
    Ref small(api.string_new("Small"));
    const int size = api.Value<bool>(api.Call(state.root.get(), "ClassListContains", {small.get()})) ? 14 : 26;
    if (size != state.size) {
        Size(state.button.get(), float(size)); Size(state.heart.get(), size == 14 ? 12.0f : 16.0f); state.size = size;
    }
}
void Create(Obj hub, Obj display, Obj slot, Obj member, Obj root, Obj portrait) {
    state.hub.reset(hub); state.display.reset(display); state.slot.reset(slot); state.member.reset(member);
    state.root.reset(root); state.portrait.reset(portrait); state.cached.reset(module->cached.get());
    state.compositor.reset(module->compositor.get()); state.social.reset(module->FindService("foi")); state.party.reset(module->FindService("fne"));
    state.tooltipService.reset(module->FindService("fou"));
    if (!state.tooltipService.get()) throw std::runtime_error("Spouse tooltip service unavailable");
    state.border.reset(api.New(api.Class("UnityEngine.UIElementsModule.dll", Ui, "VisualElement")));
    Name(state.border.get(), "dofus_miniatures_spouse_border"); Ignore(state.border.get()); Style(state.border.get(), "position", 1);
    for (auto edge : {"left", "right", "top", "bottom"}) Pixels(state.border.get(), edge, 0);
    const std::array<float, 4> pink{0.95f, 0.47f, 0.66f, 0.8f};
    for (auto edge : {"borderLeftColor", "borderRightColor", "borderTopColor", "borderBottomColor"}) Style(state.border.get(), edge, pink);
    for (auto edge : {"borderLeftWidth", "borderRightWidth", "borderTopWidth", "borderBottomWidth"}) Style(state.border.get(), edge, 2.0f);
    api.Call(portrait, "Add", {state.border.get()});
    state.heart.reset(api.New(iconClass)); Name(state.heart.get(), "dofus_miniatures_spouse_heart");
    int heart = 166; api.Call(state.heart.get(), "set_icon", {&heart}); api.Call(state.heart.get(), "set_color", {const_cast<float*>(pink.data())});
    Ignore(state.heart.get()); Style(state.heart.get(), "position", 1); Pixels(state.heart.get(), "left", 3); Pixels(state.heart.get(), "top", 3);
    api.Call(portrait, "Add", {state.heart.get()});
    state.button.reset(api.New(toggleClass)); Name(state.button.get(), "dofus_miniatures_spouse_teleport");
    // Same native secondary component as party auto-follow/join/ready. Keep it
    // in the opposite lower corner: a fourth item would overflow the native column.
    int icon = 304, style = 2, medium = 1;
    api.Call(state.button.get(), "set_iconValue", {&icon}); api.Call(state.button.get(), "set_mainStyle", {&style});
    api.Call(state.button.get(), "set_toggleSize", {&medium});
    Style(state.button.get(), "position", 1); Pixels(state.button.get(), "left", 3); Pixels(state.button.get(), "bottom", 3);
    api.Call(portrait, "Add", {state.button.get()});
    state.reported = false; Log("SPOUSE_PARTY_WIDGET_READY native-toggle=secondary icon=teleport badge=heart");
}
bool Visible(Obj element) {
    if (!api.Call(element, "get_panel") || !api.Value<bool>(api.Call(element, "get_enabledInHierarchy"))) return false;
    Ref current(element);
    for (int depth = 0; current.get(); ++depth) {
        if (depth >= 128) return false;
        Ref resolved(api.Call(current.get(), "get_resolvedStyle"));
        if (api.Value<int>(api.Call(resolved.get(), "get_display")) != 0 ||
            api.Value<int>(api.Call(resolved.get(), "get_visibility")) != 0) return false;
        current.reset(api.Call(current.get(), "get_parent"));
    }
    return true;
}
}
bool HandleSpousePartyWidgetToggle(managed::Obj owner) {
    if (!owner || owner != state.button.get()) return false;
    if (state.busy) return true;
    state.busy = true;
    try {
        std::string distance; bool online = false;
        if (Current() && !module->InCombat() && GetTickCount64() >= state.cooldown && Visible(owner) &&
            api.Call(owner, "get_parent") == state.portrait.get() && ReadPartySpouse(state.member.get(), distance, online) && online) {
            state.cooldown = GetTickCount64() + 1500; // Suppress queued double clicks and synchronous reentry.
            bool enabled = false; api.Call(owner, "SetEnabled", {&enabled});
            TeleportPartySpouse(state.member.get());
        }
    } catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_WIDGET_CLICK_ERROR ") + e.what()); }
    state.busy = false;
    return true;
}
void RestoreSpousePartyWidget() {
    state.removing = true;
    try { ClearTooltip(); }
    catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_TOOLTIP_CLEANUP_ERROR ") + e.what()); return; }
    for (auto item : {&state.button, &state.heart, &state.border}) {
        if (!item->get()) continue;
        try { api.Call(item->get(), "RemoveFromHierarchy"); item->reset(); }
        catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_WIDGET_CLEANUP_ERROR ") + e.what()); }
    }
    if (SpousePartyWidgetCleanupPending()) return;
    state.hub.reset(); state.display.reset(); state.slot.reset(); state.member.reset(); state.root.reset(); state.portrait.reset();
    state.cached.reset(); state.compositor.reset(); state.social.reset(); state.party.reset(); state.tooltipService.reset();
    state.tip.clear(); state.size = 0; state.removing = false;
}
bool SpousePartyWidgetCleanupPending() { return state.button.get() || state.heart.get() || state.border.get() || state.tooltip.get(); }
void TickSpousePartyWidget() {
    try {
        if (!Available()) { RestoreSpousePartyWidget(); return; }
        if (GetTickCount64() < state.refresh) return;
        state.refresh = GetTickCount64() + 300;
        std::string distance; bool online = false;
        if (state.button.get() && Current() && ReadPartySpouse(state.member.get(), distance, online)) {
            Refresh(distance, online); return;
        }
        RestoreSpousePartyWidget(); if (SpousePartyWidgetCleanupPending()) return;
        Ref hub(module->FindService("fqv")); if (!Is(hub.get(), hubClass)) { DiscoveryState(hub.get() ? "hub-type" : "hub-missing"); return; }
        Ref display(api.Field(hub.get(), "dytb"));
        if (!Is(display.get(), displayClass)) { DiscoveryState(display.get() ? "display-type" : "display-missing"); return; }
        if (*static_cast<bool*>(api.FieldAddress(display.get(), "isArenaParty"))) { DiscoveryState("arena-party"); return; }
        Ref slots(api.Field(display.get(), "m_partyMemberSlots")); if (!slots.get()) { DiscoveryState("slots-missing"); return; }
        int count = api.Value<int>(api.Call(slots.get(), "get_Count")); if (count < 0 || count > 128) return;
        int filled = 0, matched = 0;
        const char* reason = "empty";
        for (int i = 0; i < count; ++i) {
            Ref slot(api.Call(slots.get(), "get_Item", {&i}));
            if (!Is(slot.get(), slotClass) || *static_cast<bool*>(api.FieldAddress(slot.get(), "m_isWaitingMember"))) continue;
            ++filled;
            Ref member(api.Field(slot.get(), "partyMemberWrapper"));
            const char* failure = nullptr;
            if (!ReadPartySpouse(member.get(), distance, online, &failure)) {
                if (!matched && (std::string(failure) != "other-member" || std::string(reason) == "empty")) reason = failure;
                continue;
            }
            ++matched;
            Ref root(BindingElement(slot.get(), getRoot)), portrait(BindingElement(slot.get(), getPortrait));
            if (!root.get() || !portrait.get()) { reason = "binding-elements"; continue; }
            if (!api.Call(root.get(), "get_panel")) { reason = "detached-root"; continue; }
            if (api.Call(portrait.get(), "get_parent") != root.get()) { reason = "portrait-hierarchy"; continue; }
            Create(hub.get(), display.get(), slot.get(), member.get(), root.get(), portrait.get());
            DiscoveryState("ready");
            Refresh(distance, online); return;
        }
        DiscoveryState("slots=" + std::to_string(count) + " filled=" + std::to_string(filled) + " matched=" + std::to_string(matched) + " reason=" + reason);
    } catch (const std::exception& e) {
        if (!state.reported) Log(std::string("SPOUSE_PARTY_WIDGET_ERROR ") + e.what());
        state.reported = true; RestoreSpousePartyWidget(); state.refresh = GetTickCount64() + 2000;
    }
}
void InstallSpousePartyWidget() {
    hubClass = api.Class("Core.dll", "", "ghu");
    displayClass = api.Class("Core.dll", "Core.UILogic.Party", "PartyDisplayUi");
    slotClass = api.Class("Core.dll", "Core.UILogic.Party", "PartyMember");
    bindingClass = api.Class("Core.dll", "Core.UILogic.Bindings", "PartyMemberItemBinding");
    getRoot = api.FindVerified(bindingClass, "get_member", 0, 0x2F8A5F0);
    getPortrait = api.FindVerified(bindingClass, "get_parentMember", 0, 0x2F8A6A0);
    toggleClass = api.Class("Core.dll", Figma, "DofusToggleButtonCustom");
    iconClass = api.Class("Core.dll", Figma, "DofusIcon");
    tooltipClass = api.Class("Core.dll", "Core.UILogic.Components.Tooltips", "TooltipManipulator");
    tooltipCtor = api.FindVerified(tooltipClass, ".ctor", 6, 0x15DFAE0);
    hideTooltip = api.FindVerified(api.Class("Core.dll", "Core.UILogic.Components.Tooltips", "BaseTooltipManipulator"), "HideTooltip", 0, 0x15CD030);
    auto extensions = api.Class("UnityEngine.UIElementsModule.dll", Ui, "VisualElementExtensions");
    addManipulator = api.FindVerified(extensions, "AddManipulator", 2, 0x56755F0);
    removeManipulator = api.FindVerified(extensions, "RemoveManipulator", 2, 0x5676A90);
    api.FindVerified(iconClass, ".ctor", 0, 0x170EBD0);
    api.FindVerified(iconClass, "set_icon", 1, 0x170F480);
    api.FindVerified(iconClass, "set_color", 1, 0x170F230);
    // The single shared Toggle hook is installed by SoulWidget, avoiding a second detour.
    state.installed = true;
}
