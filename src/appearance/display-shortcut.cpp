#include "appearance/display-shortcut.hpp"
#include "core/module.hpp"
#include "appearance/mode-policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace display_shortcut_detail {
using namespace managed;
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiNamespace = "UnityEngine.UIElements";
constexpr const char* FigmaNamespace = "Core.UILogic.Components.Figma";
constexpr const char* Labels[] = {"Normal", "Cr\xC3\xA9" "atures", "Miniatures"};
constexpr float SelectorWidth = 128.0f;
// Native Standard theme's --background_dark97, used only until its live scope resolves.
constexpr std::array<float, 4> DefaultFaceColor{27.0f/255, 29.0f/255, 50.0f/255, .97f};
static_assert(sizeof(int) == 4 && sizeof(float) == 4 && sizeof(bool) == 1);

struct Child { Ref element, visibility; };
enum class StyleKind { Length, Float, Enum };
struct OwnedStyle {
    Ref element, before, applied;
    std::string property;
    StyleKind kind;
};
struct State {
    Klass mapClass{}, hubClass{};
    Method findExisting{}, readThemeColor{};
    uint64_t nextDiscovery = 0, nextThemeRefresh = 0;
    std::array<float, 4> themeColor{};
    bool themeColorValid = false;
    int themeSourceDepth = -2;
    Ref pendingOwner, owner, root, dropdown;
    std::vector<Child> children;
    std::vector<OwnedStyle> layout;
    std::array<float, 4> popupAnchor{};
    float popupLeft = 0, popupTop = 0, popupWidth = 0, popupHeight = 0;
    bool popupGeometryValid = false;
    bool installed = false, failed = false, busy = false, openRequested = false;
    bool lastValid = false, lastCombat = false;
    int lastMode = -1;
};
State state;
using OwnerFn = void(*)(Obj, Method);
using MessageFn = void(*)(Obj, Obj, Method);
OwnerFn originalEnable = nullptr, originalClick = nullptr, originalShow = nullptr;
MessageFn originalContext = nullptr, originalRefresh = nullptr;

struct BusyGuard {
    bool previous = state.busy;
    BusyGuard() { state.busy = true; }
    ~BusyGuard() { state.busy = previous; }
};

bool Available() {
    return state.installed && !state.failed && module && !module->stopped && !module->stopRequested.load();
}
void RequireValue(Obj box) {
    if (!box || !api.class_is_valuetype(api.object_get_class(box)))
        throw std::runtime_error("Shortcut expected a boxed UI value type");
}
std::string StyleName(const char* prefix, const char* property) {
    return std::string("UnityEngine.UIElements.IStyle.") + prefix + property;
}
Obj ReadStyle(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    auto value = api.Call(style.get(), StyleName("get_", property).c_str());
    RequireValue(value);
    return value;
}
void WriteStyle(Obj element, const char* property, Obj value) {
    RequireValue(value);
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), StyleName("set_", property).c_str(), {api.object_unbox(value)});
}
void EnumStyle(Obj element, const char* property, int value) {
    Ref box(ReadStyle(element, property));
    api.Call(box.get(), "set_value", {&value});
    WriteStyle(element, property, box.get());
}
void FloatStyle(Obj element, const char* property, float value) {
    Ref box(ReadStyle(element, property));
    api.Call(box.get(), "set_value", {&value});
    WriteStyle(element, property, box.get());
}
void LengthStyle(Obj element, const char* property, float pixels, int unit) {
    Ref box(ReadStyle(element, property));
    Ref length(api.New(api.Class(UiImage, UiNamespace, "Length"), {&pixels, &unit}));
    RequireValue(length.get());
    api.Call(box.get(), "set_value", {api.object_unbox(length.get())});
    WriteStyle(element, property, box.get());
}
void PixelStyle(Obj element, const char* property, float value) { LengthStyle(element, property, value, 0); }
void PercentStyle(Obj element, const char* property, float value) { LengthStyle(element, property, value, 1); }
void RefreshFaceTheme(uint64_t nowMs) {
    if (nowMs < state.nextThemeRefresh) return;
    state.nextThemeRefresh = nowMs + 500;
    Ref face(api.Field(state.dropdown.get(), "m_dropdown"));
    // customStyle exposes declarations on this element only, unlike USS var()
    // resolution. The game's theme declares this property on :root. Search
    // outward for the nearest declaration instead of silently retaining the
    // bright closed-state fill when the face has no local declaration.
    // CustomStyleProperty<Color> is a value type containing one managed string.
    Ref name(api.string_new("--background_dark97"));
    Obj property = name.get();
    std::array<float, 4> color = DefaultFaceColor;
    int sourceDepth = -1;
    Ref scope(face.get());
    for (int depth = 0; scope.get() && depth < 128; ++depth) {
        Ref custom(api.Call(scope.get(), "get_customStyle"));
        if (custom.get()) {
            if (!state.readThemeColor)
                state.readThemeColor = api.FindVerified(api.object_get_class(custom.get()), "TryGetValue", 2, 0x55FCCB0);
            std::array<float, 4> candidate{};
            if (api.Value<bool>(api.Invoke(state.readThemeColor, custom.get(), {&property, &candidate})) &&
                std::all_of(candidate.begin(), candidate.end(), [](float f) { return std::isfinite(f) && f >= 0 && f <= 1; })) {
                color = candidate;
                sourceDepth = depth;
                break;
            }
        }
        scope.reset(api.Call(scope.get(), "get_parent"));
    }
    Ref box(ReadStyle(face.get(), "backgroundColor"));
    // Compare the live inline value, not only our cache: native refreshes may
    // reset it while the theme color itself is unchanged.
    if (api.Value<int>(api.Call(box.get(), "get_keyword")) != 0 ||
        api.Value<std::array<float, 4>>(api.Call(box.get(), "get_value")) != color) {
        api.Call(box.get(), "set_value", {&color});
        WriteStyle(face.get(), "backgroundColor", box.get());
    }
    if (!state.themeColorValid || color != state.themeColor || sourceDepth != state.themeSourceDepth)
        Log("DISPLAY_SHORTCUT_THEME depth=" + std::to_string(sourceDepth) +
            " rgba=" + std::to_string(color[0]) + "," + std::to_string(color[1]) + "," +
            std::to_string(color[2]) + "," + std::to_string(color[3]));
    state.themeColor = color;
    state.themeColorValid = true;
    state.themeSourceDepth = sourceDepth;
}
bool SameStyle(Obj left, Obj right, StyleKind kind) {
    const int keyword = api.Value<int>(api.Call(left, "get_keyword"));
    if (keyword != api.Value<int>(api.Call(right, "get_keyword"))) return false;
    if (keyword != 0) return true; // Auto/Null/None do not use the stored numeric value.
    Ref a(api.Call(left, "get_value")), b(api.Call(right, "get_value"));
    if (kind == StyleKind::Enum) return api.Value<int>(a.get()) == api.Value<int>(b.get());
    if (kind == StyleKind::Float) return api.Value<float>(a.get()) == api.Value<float>(b.get());
    return api.Value<int>(api.Call(a.get(), "get_unit")) == api.Value<int>(api.Call(b.get(), "get_unit")) &&
        api.Value<float>(api.Call(a.get(), "get_value")) == api.Value<float>(api.Call(b.get(), "get_value"));
}
void OwnLayoutStyle(Obj element, const char* property, StyleKind kind, float value, int unit = 0) {
    OwnedStyle saved;
    saved.element.reset(element);
    saved.before.reset(ReadStyle(element, property));
    saved.applied.reset(ReadStyle(element, property));
    saved.property = property;
    saved.kind = kind;
    if (kind == StyleKind::Length) {
        Ref length(api.New(api.Class(UiImage, UiNamespace, "Length"), {&value, &unit}));
        RequireValue(length.get());
        api.Call(saved.applied.get(), "set_value", {api.object_unbox(length.get())});
    } else if (kind == StyleKind::Float) api.Call(saved.applied.get(), "set_value", {&value});
    else {
        int enumeration = static_cast<int>(value);
        api.Call(saved.applied.get(), "set_value", {&enumeration});
    }
    state.layout.push_back(std::move(saved)); // Own the rollback before a setter can throw after mutation.
    const auto& entry = state.layout.back();
    WriteStyle(element, property, entry.applied.get());
}
float Resolved(Obj element, const char* property) {
    Ref style(api.Call(element, "get_resolvedStyle"));
    return api.Value<float>(api.Call(style.get(), (std::string("get_") + property).c_str()));
}
float InlinePixels(Obj element, const char* property) {
    Ref style(ReadStyle(element, property));
    Ref length(api.Call(style.get(), "get_value"));
    if (api.Value<int>(api.Call(length.get(), "get_unit")) != 0)
        throw std::runtime_error("Native popup position is not expressed in panel pixels");
    return api.Value<float>(api.Call(length.get(), "get_value"));
}
std::array<float, 4> Bounds(Obj element) {
    Ref bounds(api.Call(element, "get_worldBound"));
    return {api.Value<float>(api.Call(bounds.get(), "get_x")), api.Value<float>(api.Call(bounds.get(), "get_y")),
        api.Value<float>(api.Call(bounds.get(), "get_width")), api.Value<float>(api.Call(bounds.get(), "get_height"))};
}
bool FiniteBounds(const std::array<float, 4>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}
Obj Hierarchy(Obj element) {
    auto result = api.Call(element, "get_hierarchy");
    RequireValue(result);
    return result;
}
Obj BindingElement(Obj owner, const char* getter) {
    auto klass = api.Class("Core.dll", "Core.UILogic.Bindings", "MapInfoBinding");
    return api.Invoke(api.Find(klass, getter, 0), api.FieldAddress(owner, "dzyi"));
}
Obj CreatureButton(Obj owner) { return BindingElement(owner, "get_tg_creature"); }
void ReserveReadableCell(Obj owner, Obj root) {
    Ref group(BindingElement(owner, "get_ctr_buttonsToHide"));
    Ref toolbar(BindingElement(owner, "get_ctr_buttons"));
    Ref screen(BindingElement(owner, "get_MapInfoScreenContainer"));
    Ref wrapper(toolbar.get() ? api.Call(toolbar.get(), "get_parent") : nullptr);
    if (!group.get() || !toolbar.get() || !screen.get() || !wrapper.get() ||
        api.Call(root, "get_parent") != group.get() || api.Call(group.get(), "get_parent") != toolbar.get() ||
        api.Call(wrapper.get(), "get_parent") != screen.get())
        throw std::runtime_error("Native MapInfo toolbar hierarchy changed");

    // Pinned MapInfo USS has two auto-sized flex rows inside an align-self:start
    // wrapper, within a 500px screen container. Constrain the WHOLE flow before
    // widening our real cell. Wrapping keeps neighbours in layout, not underneath
    // an absolute label. Width remains intrinsic when the controls already fit.
    for (auto element : {wrapper.get(), toolbar.get(), group.get()}) {
        OwnLayoutStyle(element, "maxWidth", StyleKind::Length, 100, 1);
        OwnLayoutStyle(element, "minWidth", StyleKind::Length, 0);
    }
    OwnLayoutStyle(wrapper.get(), "flexShrink", StyleKind::Float, 0);
    for (auto element : {toolbar.get(), group.get()}) {
        OwnLayoutStyle(element, "flexWrap", StyleKind::Enum, 1); // Wrap.Wrap.
        OwnLayoutStyle(element, "flexShrink", StyleKind::Float, 1);
    }
    for (const auto* property : {"width", "minWidth", "maxWidth"})
        OwnLayoutStyle(root, property, StyleKind::Length, SelectorWidth);
    OwnLayoutStyle(root, "flexShrink", StyleKind::Float, 0);
}
bool MatchesSession(Obj owner) {
    return owner && module && module->servicesReady && module->cached.get() &&
        api.Call(owner, "blrn") == module->cached.get();
}
void RestoreBoolean(Obj root) {
    bool enabled = ActiveDisplayMode(module->settings, module->InCombat()) != 0;
    api.Call(root, "SetValueWithoutNotify", {&enabled});
}
void CloseDropdown() {
    state.popupGeometryValid = false;
    if (state.dropdown.get()) {
        bool closed = false;
        api.Call(state.dropdown.get(), "set_activated", {&closed});
    }
}
bool CanInteract(Obj root) {
    if (!api.Value<bool>(api.Call(root, "get_enabledInHierarchy"))) return false;
    Ref ancestor(root);
    for (unsigned depth = 0; ancestor.get(); ++depth) {
        if (depth >= 128) throw std::runtime_error("Unexpected creature button ancestry");
        Ref resolved(api.Call(ancestor.get(), "get_resolvedStyle"));
        if (api.Value<int>(api.Call(resolved.get(), "get_display")) != 0)
            return false;
        ancestor.reset(api.Call(ancestor.get(), "get_parent"));
    }
    return true;
}

// Best-effort restoration proceeds through every saved child/property, even when
// a detached/disposed panel rejects an individual operation. References always clear.
void RestoreOwned() {
    BusyGuard busy;
    std::string firstError;
    auto attempt = [&](auto action) {
        try { action(); }
        catch (const std::exception& e) { if (firstError.empty()) firstError = e.what(); }
        catch (...) { if (firstError.empty()) firstError = "Unknown UI restoration error"; }
    };
    attempt([] { CloseDropdown(); });
    attempt([] { if (state.dropdown.get()) api.Call(state.dropdown.get(), "RemoveFromHierarchy"); });
    for (auto& child : state.children)
        attempt([&] { WriteStyle(child.element.get(), "visibility", child.visibility.get()); });
    state.children.clear();
    for (auto it = state.layout.rbegin(); it != state.layout.rend(); ++it)
        attempt([&] {
            Ref current(ReadStyle(it->element.get(), it->property.c_str()));
            // Preserve any native/theme update made after our last write.
            if (SameStyle(current.get(), it->applied.get(), it->kind))
                WriteStyle(it->element.get(), it->property.c_str(), it->before.get());
        });
    state.layout.clear();
    state.dropdown.reset();
    state.root.reset();
    state.owner.reset();
    state.pendingOwner.reset();
    state.nextDiscovery = 0;
    state.nextThemeRefresh = 0;
    state.themeColorValid = false;
    state.themeSourceDepth = -2;
    state.openRequested = false;
    state.lastValid = false;
    state.lastMode = -1;
    if (!firstError.empty()) Log("DISPLAY_SHORTCUT_RESTORE_ERROR " + firstError);
}
void Fail(const char* reason) {
    if (state.failed) return;
    state.failed = true;
    Log(std::string("DISPLAY_SHORTCUT_DISABLED ") + reason);
    RestoreOwned();
}
void Capture(Obj owner) {
    if (Available()) state.pendingOwner.reset(owner);
}
void DiscoverExisting(uint64_t nowMs) {
    if (auto pending = state.pendingOwner.get(); pending && MatchesSession(pending)) {
        Ref root(CreatureButton(pending));
        if (root.get() && api.Call(root.get(), "get_panel") && api.Call(root.get(), "get_parent")) return;
    }
    if (state.owner.get() && MatchesSession(state.owner.get()) && state.root.get() && state.dropdown.get() &&
        api.Call(state.root.get(), "get_panel") && api.Call(state.dropdown.get(), "get_panel")) return;
    if (nowMs < state.nextDiscovery) return;
    state.nextDiscovery = nowMs + 250;
    Ref hub(module->FindService("fqw"));
    if (!hub.get() || api.object_get_class(hub.get()) != state.hubClass) return;
    int mapInfo = 4; // fqw.PersistentWindows.MapInfo in the pinned metadata.
    Obj found = nullptr;
    // ghz.bfsu only looks up an existing window; it does not open/reload one.
    if (!api.Value<bool>(api.Invoke(state.findExisting, hub.get(), {&mapInfo, &found})) ||
        !found || api.object_get_class(found) != state.mapClass) return;
    Ref owner(found);
    if (!MatchesSession(owner.get())) return;
    state.pendingOwner.reset(owner.get());
    Log("DISPLAY_SHORTCUT_DISCOVERED existing-mapinfo=1");
}

void SaveAndHideChildren() {
    Ref hierarchy(Hierarchy(state.root.get()));
    int count = api.Value<int>(api.Call(hierarchy.get(), "get_childCount"));
    if (count < 0 || count > 128) throw std::runtime_error("Unexpected creature button child count");
    for (int index = 0; index < count; ++index) {
        Ref element(api.Call(hierarchy.get(), "get_Item", {&index}));
        if (!element.get() || element.get() == state.dropdown.get()) continue;
        if (std::any_of(state.children.begin(), state.children.end(), [&](const Child& child) {
            return child.element.get() == element.get();
        })) {
            // Native content refreshes can change a cached child's presentation.
            EnumStyle(element.get(), "visibility", 1);
            continue;
        }
        Child child;
        child.element.reset(element.get());
        child.visibility.reset(ReadStyle(element.get(), "visibility"));
        state.children.push_back(std::move(child)); // Save before the first mutation.
        EnumStyle(element.get(), "visibility", 1); // Visibility.Hidden retains layout.
    }
}
void UpdateCaption(int mode) {
    if (mode < 0 || mode > 2) return;
    Ref label(api.string_new(Labels[mode]));
    api.Call(state.dropdown.get(), "set_choiceText", {label.get()});
    const std::string description = std::string("Affichage : ") + Labels[mode];
    Ref tooltip(api.string_new(description.c_str()));
    api.Call(state.dropdown.get(), "set_tooltip", {tooltip.get()});
}
void FitPopup(bool opened) {
    if (!*static_cast<bool*>(api.FieldAddress(state.dropdown.get(), "m_activated"))) return;
    Ref popup(api.Field(state.dropdown.get(), "m_listElementContent"));
    Ref panel(api.Field(state.dropdown.get(), "m_panelRootVisualContainer"));
    if (!popup.get() || !panel.get()) { CloseDropdown(); return; }
    const auto anchor = Bounds(state.root.get());
    const float width = Resolved(panel.get(), "width"), height = Resolved(panel.get(), "height");
    if (!FiniteBounds(anchor) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
        CloseDropdown(); return;
    }
    if (opened) {
        // Reuse native world-to-panel positioning, preserving UI scale. The
        // popup is external to our clipped anchor, so its width is independent.
        state.popupLeft = InlinePixels(popup.get(), "left");
        state.popupTop = InlinePixels(popup.get(), "top");
        state.popupAnchor = anchor;
        state.popupWidth = width; state.popupHeight = height;
        state.popupGeometryValid = std::isfinite(state.popupLeft) && std::isfinite(state.popupTop);
    } else if (state.popupGeometryValid) {
        bool changed = std::fabs(width - state.popupWidth) > 0.5f || std::fabs(height - state.popupHeight) > 0.5f;
        for (size_t i = 0; i < anchor.size(); ++i) changed = changed || std::fabs(anchor[i] - state.popupAnchor[i]) > 0.5f;
        // Native Show computes anchor coordinates once. Close on resize/move;
        // the next real opening obtains fresh native coordinates.
        if (changed) { CloseDropdown(); return; }
    }
    if (!state.popupGeometryValid) { CloseDropdown(); return; }
    const float popupWidth = std::min(160.0f, width);
    float popupHeight = Resolved(popup.get(), "height");
    if (!std::isfinite(popupHeight) || popupHeight <= 0) popupHeight = 120.0f;
    popupHeight = std::min(popupHeight, height);
    PixelStyle(popup.get(), "minWidth", 0);
    PixelStyle(popup.get(), "width", popupWidth);
    PixelStyle(popup.get(), "maxWidth", popupWidth);
    PixelStyle(popup.get(), "minHeight", 0);
    PixelStyle(popup.get(), "maxHeight", height);
    PixelStyle(popup.get(), "left", std::clamp(state.popupLeft, 0.0f, std::max(0.0f, width - popupWidth)));
    PixelStyle(popup.get(), "top", std::clamp(state.popupTop, 0.0f, std::max(0.0f, height - popupHeight)));
}
void ShowHook(Obj dropdown, Method method) {
    originalShow(dropdown, method);
    if (!Available() || !dropdown || dropdown != state.dropdown.get()) return;
    try { FitPopup(true); }
    catch (const std::exception& error) { Fail(error.what()); }
}
void SyncSelection(bool combat, int mode) {
    if (mode < 0 || mode > 2) throw std::runtime_error("Invalid active shortcut mode");
    api.Call(state.dropdown.get(), "SelectItemWithoutNotify", {&mode});
    RestoreBoolean(state.root.get());
    UpdateCaption(mode);
    state.lastCombat = combat;
    state.lastMode = mode;
    state.lastValid = true;
}
void Attach(Obj owner, Obj root) {
    state.owner.reset(owner);
    state.root.reset(root);
    state.dropdown.reset(api.New(api.Class("Core.dll", FigmaNamespace, "Dropdown")));
    Ref name(api.string_new("display-mode-shortcut"));
    Ref empty(api.string_new(""));
    api.Call(state.dropdown.get(), "set_name", {name.get()});
    api.Call(state.dropdown.get(), "set_captionText", {empty.get()});
    int textType = 0;
    api.Call(state.dropdown.get(), "set_dropdownType", {&textType});
    api.Call(state.dropdown.get(), "set_dropdownSize", {&textType}); // Native small variant. Constructor defaults to medium.
    *static_cast<bool*>(api.FieldAddress(state.dropdown.get(), "hasMouseWheel")) = false;
    *static_cast<bool*>(api.FieldAddress(state.dropdown.get(), "closeOnSelection")) = true;
    for (int mode = 0; mode < 3; ++mode) {
        Ref label(api.string_new(Labels[mode]));
        int choice = 0, none = 0;
        api.Call(state.dropdown.get(), "AddListElementBasic", {label.get(), nullptr, &choice, &none, &none, &mode});
    }
    EnumStyle(state.dropdown.get(), "position", 1); // Position.Absolute.
    for (const auto* property : {"left", "top", "right", "bottom"}) PixelStyle(state.dropdown.get(), property, 0);
    // The native cell participates in the row layout. Its child remains bounded
    // to that reserved space, including picking and the native popup anchor.
    for (const auto* property : {"width", "maxWidth", "height", "maxHeight"}) PercentStyle(state.dropdown.get(), property, 100);
    for (const auto* property : {"minWidth", "minHeight", "marginLeft", "marginRight", "marginTop", "marginBottom", "paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) PixelStyle(state.dropdown.get(), property, 0);
    EnumStyle(state.dropdown.get(), "overflow", 1); // Overflow.Hidden clips rendering and picking to the reserved cell.
    for (const auto* field : {"m_dropdown", "m_content"}) {
        Ref content(api.Field(state.dropdown.get(), field));
        if (!content.get()) throw std::runtime_error("Native dropdown content missing");
        PixelStyle(content.get(), "minWidth", 0);
        PixelStyle(content.get(), "minHeight", 0);
        PercentStyle(content.get(), "maxWidth", 100);
        PercentStyle(content.get(), "maxHeight", 100);
        for (const auto* property : {"paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) PixelStyle(content.get(), property, 0);
    }
    Ref caption(api.Field(state.dropdown.get(), "captionIconContainer"));
    if (caption.get()) EnumStyle(caption.get(), "display", 1);
    Ref captionLabel(api.Field(state.dropdown.get(), "m_captionLabel"));
    if (captionLabel.get()) {
        Ref container(api.Call(captionLabel.get(), "get_parent"));
        if (container.get()) EnumStyle(container.get(), "display", 1);
    }
    Ref choice(api.Field(state.dropdown.get(), "m_choiceLabel"));
    Ref chevron(api.Field(state.dropdown.get(), "m_chevronIcon"));
    if (!choice.get() || !chevron.get()) throw std::runtime_error("Native dropdown label/caret missing");
    PixelStyle(choice.get(), "fontSize", 14);
    PixelStyle(choice.get(), "minWidth", 0);
    Ref choiceContainer(api.Call(choice.get(), "get_parent"));
    Ref caretContainer(api.Call(chevron.get(), "get_parent"));
    for (auto element : {choice.get(), choiceContainer.get(), chevron.get(), caretContainer.get()}) {
        if (!element) continue;
        for (const auto* property : {"marginLeft", "marginRight", "paddingLeft", "paddingRight"}) PixelStyle(element, property, 0);
    }
    for (auto element : {chevron.get(), caretContainer.get()}) {
        if (!element) continue;
        for (const auto* property : {"width", "minWidth", "maxWidth"}) PixelStyle(element, property, 16);
        FloatStyle(element, "flexShrink", 0);
    }
    PixelStyle(chevron.get(), "height", 16);
    PixelStyle(choice.get(), "marginRight", 6);
    Ref face(api.Field(state.dropdown.get(), "m_dropdown"));
    // Native ctor puts the caret beside m_content, not inside it. Inset their
    // shared parent so the arrow itself has breathing room at the right edge.
    PixelStyle(face.get(), "paddingLeft", 8);
    PixelStyle(face.get(), "paddingRight", 10);
    // The closed native Dropdown uses a highlighted secondary-button fill and
    // inner shadow, unlike the dark open state. Use the open theme fill in the
    // tick and remove that shadow only on our owned face; keep label/caret and
    // popup styling native, without lowering their opacity.
    bool shadow = false;
    api.Call(face.get(), "set_hasShadow", {&shadow});
    ReserveReadableCell(owner, root);
    SaveAndHideChildren();
    Ref hierarchy(Hierarchy(root));
    api.Call(hierarchy.get(), "Add", {state.dropdown.get()});
    SyncSelection(module->InCombat(), ActiveDisplayMode(module->settings, module->InCombat()));
    Log("DISPLAY_SHORTCUT_READY native-dropdown=1");
}

void EnableHook(Obj owner, Method method) {
    originalEnable(owner, method);
    try { Capture(owner); } catch (const std::exception& e) { Fail(e.what()); }
}
void ContextHook(Obj owner, Obj message, Method method) {
    originalContext(owner, message, method);
    try { Capture(owner); } catch (const std::exception& e) { Fail(e.what()); }
}
void RefreshHook(Obj owner, Obj message, Method method) {
    originalRefresh(owner, message, method);
    try { Capture(owner); } catch (const std::exception& e) { Fail(e.what()); }
}
void ClickHook(Obj owner, Method method) {
    if (Available()) {
        try {
            if (MatchesSession(owner)) {
                // Covers manual loading after OnEnable already ran. Defer tree changes
                // until the main-thread tick; the old click must not dispatch a toggle.
                Ref root(CreatureButton(owner));
                if (root.get()) {
                    Capture(owner);
                    state.openRequested = true;
                    RestoreBoolean(root.get());
                    return;
                }
            }
        } catch (const std::exception& e) { Fail(e.what()); }
    }
    originalClick(owner, method);
}
}

void InstallDisplayShortcut() {
    using namespace display_shortcut_detail;
    auto map = api.Class("Core.dll", "Core.UILogic.GameUICore", "MapInfoUI");
    state.mapClass = map;
    state.hubClass = api.Class("Core.dll", "", "ghz");
    state.findExisting = api.Find(state.hubClass, "bfsu", 2);
    api.Native(state.findExisting, 0x1FBBC70);
    auto dropdown = api.Class("Core.dll", FigmaNamespace, "Dropdown");
    auto binding = api.Class("Core.dll", "Core.UILogic.Bindings", "MapInfoBinding");
    auto toggle = api.Class("Core.dll", FigmaNamespace, "DofusToggleButtonCustom");
    api.Native(api.Find(binding, "get_tg_creature", 0), 0x2F6C770);
    api.Native(api.Find(binding, "get_ctr_buttonsToHide", 0), 0x2F6B720);
    api.Native(api.Find(binding, "get_ctr_buttons", 0), 0x2F6B7D0);
    api.Native(api.Find(binding, "get_MapInfoScreenContainer", 0), 0x2F6AA20);
    api.Native(api.Find(map, "blrn", 0), 0x3BFE0D0);
    api.Native(api.Find(toggle, "SetValueWithoutNotify", 1), 0x1714F40);
    api.Native(api.Find(dropdown, ".ctor", 0), 0x17282C0);
    api.Native(api.Find(dropdown, "AddListElementBasic", 6), 0x1724840);
    api.Native(api.Find(dropdown, "SelectItemWithoutNotify", 1), 0x17266E0);
    api.Native(api.Find(dropdown, "set_activated", 1), 0x1728300);
    api.Native(api.Find(dropdown, "set_captionText", 1), 0x1728470);
    api.Native(api.Find(dropdown, "set_choiceText", 1), 0x1728550);
    api.Native(api.Find(dropdown, "set_dropdownSize", 1), 0x1728620);
    api.Native(api.Find(api.Class(UiImage, UiNamespace, "VisualElement"), "set_tooltip", 1), 0x55F9AB0);
    api.Native(api.Find(dropdown, "set_dropdownType", 1), 0x1728730);
    api.Native(api.Find(api.Class(UiImage, UiNamespace, "Length"), ".ctor", 2), 0x561D3D0);
    api.Native(api.Find(api.Class(UiImage, UiNamespace, "ResolvedStyleAccess"), "get_display", 0), 0x55D9890);
    api.Native(api.Find(api.Class(UiImage, UiNamespace, "VisualElement"), "get_customStyle", 0), 0x55F5C30);
    api.Native(api.Find(api.Class("Core.dll", FigmaNamespace, "DofusVisualElement"), "set_hasShadow", 1), 0x1722D30);
    AddHook(api.Find(dropdown, "Show", 0), 0x1726AE0, reinterpret_cast<void*>(ShowHook), reinterpret_cast<void**>(&originalShow));
    AddHook(api.Find(map, "OnEnable", 0), 0x2C63890, reinterpret_cast<void*>(EnableHook), reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(map, "OnContextChanged", 1), 0x2C63540, reinterpret_cast<void*>(ContextHook), reinterpret_cast<void**>(&originalContext));
    AddHook(api.Find(map, "OnRefreshCreatureMode", 1), 0x2C65410, reinterpret_cast<void*>(RefreshHook), reinterpret_cast<void**>(&originalRefresh));
    AddHook(api.Find(map, "OnClickedBtnCreature", 0), 0x2C63270, reinterpret_cast<void*>(ClickHook), reinterpret_cast<void**>(&originalClick));
    state.failed = false;
    state.nextDiscovery = 0;
    state.installed = true;
}

void TickDisplayShortcut(uint64_t nowMs) {
    using namespace display_shortcut_detail;
    if (!Available() || !module->servicesReady || state.busy) return;
    BusyGuard busy;
    try {
        DiscoverExisting(nowMs ? nowMs : GetTickCount64());
        if (auto pending = state.pendingOwner.get()) {
            Ref owner(pending);
            state.pendingOwner.reset();
            if (MatchesSession(owner.get())) {
                Ref root(CreatureButton(owner.get()));
                if (root.get() && api.Call(root.get(), "get_panel") && api.Call(root.get(), "get_parent")) {
                    if (owner.get() != state.owner.get() || root.get() != state.root.get() ||
                        !state.dropdown.get() || !api.Call(state.dropdown.get(), "get_panel")) {
                        const bool open = state.openRequested;
                        RestoreOwned();
                        state.openRequested = open;
                        Attach(owner.get(), root.get());
                    }
                } else state.pendingOwner.reset(owner.get()); // Retry after attachment/layout.
            }
        }
        if (!state.dropdown.get()) return;
        if (!MatchesSession(state.owner.get())) { RestoreOwned(); return; }
        if (CreatureButton(state.owner.get()) != state.root.get()) {
            Ref owner(state.owner.get());
            RestoreOwned();
            state.pendingOwner.reset(owner.get());
            return;
        }
        if (!api.Call(state.root.get(), "get_panel") || !api.Call(state.dropdown.get(), "get_panel")) {
            CloseDropdown();
            state.lastValid = false;
            state.openRequested = false;
            return;
        }
        // The popup lives outside this subtree. Parent minimization or disabling
        // must close it before any unpolled selection can affect preferences.
        if (!CanInteract(state.root.get())) {
            CloseDropdown();
            state.lastValid = false;
            state.openRequested = false;
            return;
        }
        SaveAndHideChildren();
        RefreshFaceTheme(nowMs ? nowMs : GetTickCount64());
        bool combat = module->InCombat();
        int mode = ActiveDisplayMode(module->settings, combat);
        if (!state.lastValid || combat != state.lastCombat || mode != state.lastMode) {
            // A new context/native preference takes precedence over an unpolled choice.
            CloseDropdown();
            state.openRequested = false;
            SyncSelection(combat, mode);
        } else {
            int selected = *static_cast<int*>(api.FieldAddress(state.dropdown.get(), "selectedIndex"));
            if (selected < 0 || selected > 2) SyncSelection(combat, mode);
            else if (selected != state.lastMode) {
                auto next = module->settings;
                (combat ? next.mode : next.roleplayMode) = selected;
                module->Change(next);
                combat = module->InCombat();
                SyncSelection(combat, ActiveDisplayMode(module->settings, combat));
            }
        }
        UpdateCaption(state.lastMode);
        FitPopup(false);
        if (state.openRequested) {
            state.openRequested = false;
            bool open = true;
            api.Call(state.dropdown.get(), "set_activated", {&open});
        }
    } catch (const std::exception& e) { Fail(e.what()); }
}

void RestoreDisplayShortcut() {
    using namespace display_shortcut_detail;
    state.installed = false;
    RestoreOwned();
}
