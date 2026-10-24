#include "souls/soul-panel.hpp"
#include "souls/soul-item-view.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <array>

namespace soul::detail {
using namespace managed;
bool populateHooked = false; // the hook on DofusContextualMenuManipulator.OnContextualMenuEvent is installed
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* FigmaSpace = "Core.UILogic.Components.Figma";
constexpr const char* DragSpace = "Core.UILogic.Components.Manipulators";
constexpr const char* WindowSpace = "Core.UILogic.Components";
struct State {
    Ref root, window, header, dragger, requirement, stock, equippedRow, itemSlot, equipped, status, action, statusRow, spellSlot;
    Ref dependencies, cached, input;
    Ref compositor, contextService, menuButton, menuManipulator, menu, menuItem, menuCallback;
    Method appendTitle = nullptr, appendSeparator = nullptr, appendMenu = nullptr, addManipulator = nullptr, removeManipulator = nullptr;
    bool menuArmed = false;
    std::array<Ref,4> inputEvents;
    std::vector<Ref> manipulators;
    Method unsubscribe = nullptr;
    bool installed = false, hiding = false, equipEnabled = false, cleanupPending = false;
    std::atomic_bool ownsDragFlag{false};
    bool nativeDragging = false, gestureObserved = false;
    int x = 24, y = 180, inputX = 24, inputY = 180;
    int savedX = 24, savedY = 180;
    uint64_t layoutGeneration = 0, dragGeneration = 0;
    uint64_t nextSpellSize = 0;
    void (*equip)() = nullptr;
    void (*positionChanged)(int, int, uint64_t) = nullptr;
    void (*hideModule)() = nullptr;
} state;
using ActionFn = void(*)(Obj, Method);
using DragFn = void(*)(Obj, Obj, Method);
using DragFlagFn = void(*)(bool, Method);
ActionFn originalAction = nullptr;
ActionFn originalNotify = nullptr;
DragFn originalDown = nullptr, originalUp = nullptr;
DragFn originalPopulate = nullptr;
DragFlagFn setDragging = nullptr;
thread_local bool enteringOwnedDrag = false;
std::vector<bool (*)(Obj)> auxiliaryActions;
std::vector<std::pair<bool (*)(Obj, Obj), bool (*)(Obj, Obj)>> auxiliaryMenus;

bool MenuAvailable() {
    return state.installed && !state.hiding && !state.cleanupPending && state.hideModule &&
        module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested &&
        module->settings.soulReminder && module->cached.get() == state.cached.get() &&
        module->compositor.get() == state.compositor.get() &&
        module->FindService("fpt") == state.dependencies.get() && module->FindService("fpz") == state.contextService.get() &&
        state.window.get() && state.menuButton.get() && api.Call(state.window.get(), "get_panel") &&
        api.Call(state.menuButton.get(), "get_panel") == api.Call(state.window.get(), "get_panel") &&
        api.Field(state.header.get(), "rightButton") == state.menuButton.get() &&
        api.Value<bool>(api.Call(state.menuButton.get(), "get_enabledInHierarchy"));
}
Obj MenuRoot() {
    if (!state.menu.get()) return nullptr;
    Obj root = api.Field(state.menu.get(), "dofusContextualMenuRoot");
    return root && api.Call(root, "get_menuData") == state.menu.get() ? root : nullptr;
}
void ClosePanelMenu() {
    state.menuArmed = false;
    Ref root(MenuRoot());
    if (root.get() && api.Call(root.get(), "get_panel")) {
        bool delayed = false;
        api.Call(root.get(), "RemoveMenu", {&delayed});
    }
    state.menuCallback.reset(); state.menuItem.reset(); state.menu.reset();
}
void PopulateMenuHook(Obj owner, Obj event, Method method) {
    for (const auto& handlers : auxiliaryMenus) { try { if (handlers.first(owner, event)) return; } catch (...) { Log("AUXILIARY_MENU_POPULATE_ERROR"); return; } }
    if (!owner || owner != state.menuManipulator.get()) { originalPopulate(owner, event, method); return; }
    try {
        if (!MenuAvailable() || !event) return;
        Ref menu(api.Call(event, "get_menu"));
        if (!menu.get() || menu.get() == state.menu.get()) return;
        ClosePanelMenu();
        Ref rows(api.Call(menu.get(), "get_menuItems"));
        int index = api.Value<int>(api.Call(rows.get(), "get_Count"));
        if (index < 0 || index > 128) throw std::runtime_error("Soul menu item bounds");
        state.menu.reset(menu.get());
        Ref title(api.string_new("Pierre d'âme"));
        api.Invoke(state.appendTitle, menu.get(), {title.get()});
        api.Invoke(state.appendSeparator, menu.get());
        index = api.Value<int>(api.Call(rows.get(), "get_Count"));
        Ref text(api.string_new("Masquer le module")), empty(api.string_new(""));
        // Capture d’âme's top-bar button follows soulButton (1.16.1): hidden,
        // only the options switch the alert back on.
        Ref help(api.string_new(module->settings.soulButton ?
            "Réactiver Capture d’âme depuis la barre des modules ou les options du mod." :
            "Réactiver Capture d’âme depuis les options du mod : « Alerte pierre d’âme »."));
        Ref asset(api.object_new(api.class_from_type(api.method_get_param(state.appendMenu, 7))));
        Ref color(api.object_new(api.class_from_type(api.method_get_param(state.appendMenu, 10))));
        bool disabled = false, close = true;
        unsigned delay = 500; int style = 1, icon = 137; // FigmaIcons.eyeSlashed.
        // Null fallback action makes a detached or stale row harmless.
        api.Invoke(state.appendMenu, menu.get(), {text.get(), nullptr, &disabled, &close, help.get(), &delay,
            empty.get(), api.object_unbox(asset.get()), &style, &icon, api.object_unbox(color.get()), empty.get(), empty.get()});
        state.menuItem.reset(api.Call(rows.get(), "get_Item", {&index}));
        state.menuCallback.reset(api.Call(state.menuItem.get(), "get_m_callback"));
        state.menuArmed = true;
        Log("SOUL_PANEL_MENU_READY action=hide");
    } catch (const std::exception& e) {
        state.menuArmed = false;
        Log(std::string("SOUL_PANEL_MENU_ERROR ") + e.what());
        try { ClosePanelMenu(); } catch (...) {}
    }
}

void RequireValue(Obj value) {
    if (!value || !api.class_is_valuetype(api.object_get_class(value)))
        throw std::runtime_error("Soul panel expected a boxed style value");
}
std::string StyleName(const char* operation, const char* property) {
    return std::string("UnityEngine.UIElements.IStyle.") + operation + property;
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
void Pixels(Obj element, const char* property, float pixels) {
    Ref box(ReadStyle(element, property));
    int unit = 0; // LengthUnit.Pixel; coordinates are native panel units.
    Ref length(api.New(api.Class(UiImage, UiSpace, "Length"), {&pixels, &unit}));
    api.Call(box.get(), "set_value", {api.object_unbox(length.get())});
    WriteStyle(element, property, box.get());
}
void FloatStyle(Obj element, const char* property, float value) {
    Ref box(ReadStyle(element, property));
    api.Call(box.get(), "set_value", {&value});
    WriteStyle(element, property, box.get());
}
void TextColor(Obj element, StatusTone tone) {
    const bool warning = tone == StatusTone::Warning, good = tone == StatusTone::Good;
    float r = warning ? 1.0f : good ? 0.40f : 0.90f;
    float g = warning ? 0.36f : 0.90f;
    float b = warning ? 0.32f : good ? 0.48f : 0.90f;
    float a = 1.0f;
    Ref color(api.New(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Color"), {&r,&g,&b,&a}));
    Ref box(ReadStyle(element, "color"));
    api.Call(box.get(), "set_value", {api.object_unbox(color.get())});
    WriteStyle(element, "color", box.get());
}
void Text(Obj element, const std::string& text) {
    Ref value(api.string_new(text.c_str()));
    api.Call(element, "set_text", {value.get()});
}
float Resolved(Obj element, const char* property) {
    Ref resolved(api.Call(element, "get_resolvedStyle"));
    return api.Value<float>(api.Call(resolved.get(), (std::string("get_") + property).c_str()));
}
bool Captured() {
    if (!state.header.get()) return false;
    const auto klass = api.Class(UiImage, UiSpace, "MouseCaptureController");
    return api.Value<bool>(api.Invoke(api.Find(klass, "HasMouseCapture", 1), nullptr, {state.header.get()}));
}
void MatchSpellSize() {
    if (!state.spellSlot.get() || GetTickCount64()<state.nextSpellSize) return;
    state.nextSpellSize=GetTickCount64()+600;
    const auto slotClass=api.Class("Core.dll","Core.UILogic.Components.Slots","Slot");
    const auto gridClass=api.Class("Core.dll","Core.UILogic.Components.Slots","SlotGrid");
    std::vector<Ref> pending;pending.emplace_back(state.root.get());
    for(size_t visited=0;!pending.empty()&&visited<4096;++visited) {
        Ref node(std::move(pending.back()));pending.pop_back();
        if(!node.get() || node.get()==state.window.get() || Resolved(node.get(),"display")!=0)continue;
        if(api.object_get_class(node.get())==slotClass && api.Value<int>(api.Call(node.get(),"get_slotType"))==4) {
            Ref parent(api.Call(node.get(),"get_parent"));bool actionGrid=false;
            for(int depth=0;parent.get()&&depth<12;++depth) {
                if(api.object_get_class(parent.get())==gridClass){actionGrid=true;break;}
                parent.reset(api.Call(parent.get(),"get_parent"));
            }
            struct Rect{float x,y,w,h;};
            const auto bounds=api.Value<Rect>(api.Call(node.get(),"get_worldBound"));
            if(actionGrid&&std::isfinite(bounds.w)&&std::isfinite(bounds.h)&&bounds.w>=24&&bounds.w<=160&&bounds.h>=24&&bounds.h<=160) {
                int size=api.Value<int>(api.Call(node.get(),"get_slotSize"));
                if(size<0||size>4)continue;
                api.Call(state.spellSlot.get(),"set_slotSize",{&size});
                for(const auto* p:{"width","minWidth","maxWidth"})Pixels(state.spellSlot.get(),p,bounds.w);
                for(const auto* p:{"height","minHeight","maxHeight"})Pixels(state.spellSlot.get(),p,bounds.h);
                return;
            }
        }
        Ref hierarchy(api.Call(node.get(),"get_hierarchy"));
        const int n=api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
        if(n<0||n>512||pending.size()+n>4096)return;
        for(int i=n-1;i>=0;--i)pending.emplace_back(api.Call(hierarchy.get(),"get_Item",{&i}));
    }
}
bool Active() {
    return state.dragger.get() && *static_cast<bool*>(api.FieldAddress(state.dragger.get(), "m_active"));
}
void ReleaseOwnedCapture() {
    // UnregisterCallbacksFromTarget merely unregisters callbacks. Mirror the
    // native MouseUp cleanup BEFORE removing the window. Setter observation
    // proves who last wrote the global flag even after mouse capture is lost.
    // Any foreign setter write relinquishes ownership immediately.
    if (state.ownsDragFlag.exchange(false) && setDragging) {
        state.nativeDragging = false;
        setDragging(false, nullptr);
    }
    if (state.dragger.get()) {
        if (Captured()) {
            *static_cast<bool*>(api.FieldAddress(state.dragger.get(), "m_active")) = false;
            const auto klass = api.Class(UiImage, UiSpace, "MouseCaptureController");
            api.Invoke(api.Find(klass, "ReleaseMouse", 1), nullptr, {state.header.get()});
        } else {
            *static_cast<bool*>(api.FieldAddress(state.dragger.get(), "m_active")) = false;
        }
        api.Call(state.dragger.get(), "RemoveSnapPreview");
    }
}
void RetainManipulators(const char* field) {
    Ref list(api.Field(state.window.get(), field));
    const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
    if (count < 0 || count > 32) throw std::runtime_error("Unexpected native widget manipulator count");
    for (int i = 0; i < count; ++i) {
        Ref manipulator(api.Call(list.get(), "get_Item", {&i}));
        if (!manipulator.get()) throw std::runtime_error("Missing native widget manipulator");
        state.manipulators.push_back(std::move(manipulator));
    }
}
void DisconnectInputs() {
    // MagneticDragWindow.Init subscribes both draggers and disabled resizers.
    // Detach only removes the window from the magnetic registry. Unsubscribe
    // the exact delegates owned by this widget, preserving every foreign one.
    for (const auto& event : state.inputEvents) {
        if (!event.get()) continue;
        Ref list(api.Field(event.get(), "m_input"));
        if (!list.get()) continue;
        const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
        if (count < 0 || count > 16384) throw std::runtime_error("Unexpected native input subscriber count");
        for (int i = count; i-- > 0;) {
            Ref callback(api.Call(list.get(), "get_Item", {&i}));
            if (!callback.get()) continue;
            const auto target = api.Call(callback.get(), "get_Target");
            if (std::any_of(state.manipulators.begin(),state.manipulators.end(),
                [&](const Ref& owned) { return owned.get() == target; }))
                api.Invoke(state.unsubscribe,event.get(),{callback.get()});
        }
    }
}
void DragFlagHook(bool value, Method method) {
    setDragging(value, method);
    state.ownsDragFlag.store(value && enteringOwnedDrag);
    state.nativeDragging = value;
    if (value && state.window.get() && !state.hiding) {
        state.dragGeneration = state.layoutGeneration;
        state.gestureObserved = true;
    }
}
void DownHook(Obj owner, Obj event, Method method) {
    const bool previous = enteringOwnedDrag;
    enteringOwnedDrag = owner && owner == state.dragger.get() && !state.hiding;
    if (enteringOwnedDrag && !Active()) state.dragGeneration = state.layoutGeneration;
    struct RestoreScope { bool previous; ~RestoreScope() { enteringOwnedDrag = previous; } } restore{previous};
    originalDown(owner, event, method);
}
int SafeCoordinate(float value, int fallback) {
    if (!std::isfinite(value)) return fallback;
    return static_cast<int>(std::lround(std::clamp(value, 0.0f, 100000.0f)));
}
float ImmediatePosition(Obj element, const char* property) {
    Ref style(ReadStyle(element,property));
    if (api.Value<int>(api.Call(style.get(),"get_keyword")) == 0) {
        struct LengthValue { float value; int unit; };
        const auto length = api.Value<LengthValue>(api.Call(style.get(),"get_value"));
        if (length.unit == 0 && std::isfinite(length.value)) return length.value;
    }
    return Resolved(element,property);
}
void ObservePosition() {
    state.x = SafeCoordinate(ImmediatePosition(state.window.get(),"left"),state.x);
    state.y = SafeCoordinate(ImmediatePosition(state.window.get(),"top"),state.y);
}
void SavePosition(bool userGesture) {
    if (state.x == state.savedX && state.y == state.savedY) return;
    state.savedX = state.x;
    state.savedY = state.y;
    if (userGesture && state.positionChanged) state.positionChanged(state.x, state.y, state.dragGeneration);
}
void ClampPosition(bool readPosition) {
    if (!state.window.get() || !state.root.get()) return;
    if (readPosition) ObservePosition();
    const float rootWidth = Resolved(state.root.get(), "width");
    const float rootHeight = Resolved(state.root.get(), "height");
    float width = Resolved(state.window.get(), "width");
    float height = Resolved(state.window.get(), "height");
    if (!std::isfinite(width) || width <= 0) width = 244;
    if (!std::isfinite(height) || height <= 0) height = 144;
    if (std::isfinite(rootWidth) && rootWidth > 0)
        state.x = std::clamp(state.x, 0, SafeCoordinate(std::max(0.0f, rootWidth - width), 0));
    if (std::isfinite(rootHeight) && rootHeight > 0)
        state.y = std::clamp(state.y, 0, SafeCoordinate(std::max(0.0f, rootHeight - height), 0));
    Pixels(state.window.get(), "left", static_cast<float>(state.x));
    Pixels(state.window.get(), "top", static_cast<float>(state.y));
}
void NotifyHook(Obj owner, Method method) {
    const auto window = state.window.get();
    bool involved = false;
    try {
        if (window && !state.hiding && state.gestureObserved &&
            *static_cast<bool*>(api.FieldAddress(owner,"m_hasBeenDragged"))) {
            involved = owner == state.dragger.get();
            if (!involved && *static_cast<bool*>(api.FieldAddress(owner,"m_isSnapGroup"))) {
                Ref windows(api.Field(owner,"m_snappedWindows"));
                const int count = windows.get() ? api.Value<int>(api.Call(windows.get(),"get_Count")) : 0;
                if (count < 0 || count > 512) throw std::runtime_error("Unexpected snapped window count");
                for (int i = 0; i < count && !involved; ++i)
                    involved = api.Call(windows.get(),"get_Item",{&i}) == window;
            }
        }
    } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_NOTIFY_ERROR ") + e.what()); }
    originalNotify(owner,method);
    if (!involved || window != state.window.get() || state.hiding) return;
    try {
        if (state.dragGeneration != state.layoutGeneration) {
            state.x = state.savedX = state.inputX;
            state.y = state.savedY = state.inputY;
            ClampPosition(false);
        } else {
            // Called after native SnapPosition for own and grouped moves.
            // Inline coordinates are current even before the next UI layout.
            ClampPosition(true);
            SavePosition(true);
        }
        state.gestureObserved = false;
    } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_NOTIFY_ERROR ") + e.what()); }
}
Obj Label(Obj content) {
    Ref label(api.New(api.Class(UiImage, UiSpace, "Label")));
    EnumStyle(label.get(), "whiteSpace", 0); // WhiteSpace.Normal.
    Pixels(label.get(), "fontSize", 14);
    Pixels(label.get(), "marginBottom", 2);
    // Bare Unity labels otherwise inherit the root's default (often black),
    // which is unreadable against the native dark widget background.
    TextColor(label.get(), StatusTone::Neutral);
    api.Call(content, "Add", {label.get()});
    return label.get();
}
void Create(Obj root, int x, int y) {
    state.root.reset(root);
    state.x = state.inputX = state.savedX = std::clamp(x, 0, 100000);
    state.y = state.inputY = state.savedY = std::clamp(y, 0, 100000);
    if (!module) throw std::runtime_error("Soul panel session unavailable");
    state.dependencies.reset(module->FindService("fpt"));
    if (!state.dependencies.get() || api.object_get_class(state.dependencies.get()) != api.Class("Core.dll","","goc"))
        throw std::runtime_error("Native widget dependencies unavailable");
    state.cached.reset(api.Call(state.dependencies.get(), "bfri"));
    state.input.reset(api.Call(state.dependencies.get(), "bfre"));
    if (state.cached.get() != module->cached.get() || !state.input.get() ||
        api.object_get_class(state.input.get()) != api.Class("Core.dll","","eqo"))
        throw std::runtime_error("Native widget services do not match current session");
    bool no = false, yes = true;
    Ref title(api.string_new("Pierre d'\xC3\xA2me")), empty(api.string_new(""));
    state.window.reset(api.New(api.Class("Core.dll", WindowSpace, "MagneticDragWindow")));
    // The native drag completion path emits a layout-save message for a
    // nonempty name. Identity is the rooted object, never a native name.
    api.Call(state.window.get(), "set_name", {empty.get()});
    api.Call(state.window.get(), "set_headerTitle", {title.get()});
    int headerIcon = 128; // FigmaIcons.equipItem, shared with the Capture d'âme toolbar toggle.
    api.Call(state.window.get(), "set_headerIcon", {&headerIcon});
    api.Call(state.window.get(), "set_headerShowCloseButton", {&no});
    api.Call(state.window.get(), "set_verticalResizable", {&no});
    api.Call(state.window.get(), "set_horizontalResizable", {&no});
    api.Call(state.window.get(), "set_cornerResizable", {&no});
    api.Call(state.window.get(), "set_hasBackground", {&yes});
    int large = 1; // Native 24px HeaderWidget, with visible title and hover grip.
    api.Call(state.window.get(), "set_headerSize", {&large});
    EnumStyle(state.window.get(), "position", 1);
    FloatStyle(state.window.get(), "flexGrow", 0);
    Pixels(state.window.get(), "width", 244);
    state.header.reset(api.Call(state.window.get(), "get_headerWidget"));
    state.compositor.reset(module->compositor.get());
    state.contextService.reset(module->FindService("fpz"));
    if (!state.contextService.get()) throw std::runtime_error("Soul panel context menu service unavailable");
    int more = 202; // Native three-dot icon, FigmaIcons.moreActions.
    api.Call(state.window.get(), "set_headerIconRight", {&more});
    api.Call(state.header.get(), "set_showRightButton", {&yes});
    state.menuButton.reset(api.Field(state.header.get(), "rightButton"));
    if (!state.menuButton.get()) throw std::runtime_error("Soul panel menu button unavailable");
    int leftMouse = 0;
    state.menuManipulator.reset(api.New(api.Class("Core.dll", "Core.UILogic.Components.ContextMenu", "DofusContextualMenuManipulator"),
        {state.contextService.get(), nullptr, &leftMouse, &no}));
    api.Invoke(state.addManipulator, nullptr, {state.menuButton.get(), state.menuManipulator.get()});
    Ref draggers(api.Field(state.window.get(), "m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(), "get_Item", {state.header.get()}));
    if (!state.header.get() || !state.dragger.get()) throw std::runtime_error("Soul panel native dragger unavailable");
    RetainManipulators("m_elementDraggers");
    RetainManipulators("m_resizerList");
    const char* eventGetters[] = {"ydf","ydh","ydj","ydl"};
    for (size_t i = 0; i < state.inputEvents.size(); ++i) {
        state.inputEvents[i].reset(api.Call(state.input.get(),eventGetters[i]));
        if (!state.inputEvents[i].get()) throw std::runtime_error("Native widget input event unavailable");
    }
    // Before attachment, so native OnAttach registers the magnetic window.
    // Captured event/manipulator refs also permit cleanup after partial Init.
    api.Call(state.window.get(), "Init", {state.dependencies.get()});
    Ref content(api.Call(state.window.get(), "get_contentContainer"));
    FloatStyle(content.get(), "flexGrow", 0);
    for (auto property : {"paddingTop", "paddingBottom", "paddingLeft", "paddingRight"}) Pixels(content.get(), property, 6);
    state.requirement.reset(Label(content.get()));
    state.stock.reset(Label(content.get()));
    state.equippedRow.reset(api.New(api.Class(UiImage, UiSpace, "VisualElement")));
    EnumStyle(state.equippedRow.get(), "flexDirection", 2); // FlexDirection.Row.
    Pixels(state.equippedRow.get(), "marginBottom", 2);
    api.Call(content.get(), "Add", {state.equippedRow.get()});
    state.itemSlot.reset(CreateItemView(state.equippedRow.get()));
    for (auto property : {"width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight"})
        Pixels(state.itemSlot.get(), property, 80);
    FloatStyle(state.itemSlot.get(), "flexShrink", 0);
    Pixels(state.itemSlot.get(), "marginRight", 8);
    state.equipped.reset(Label(state.equippedRow.get()));
    // Give the readable icon a fixed area; wrap the item name in the remaining
    // width instead of letting a fixed-width label squeeze the icon or overflow.
    Pixels(state.equipped.get(), "minWidth", 0);
    FloatStyle(state.equipped.get(), "flexGrow", 1);
    FloatStyle(state.equipped.get(), "flexShrink", 1);
    state.statusRow.reset(api.New(api.Class(UiImage, UiSpace, "VisualElement")));
    EnumStyle(state.statusRow.get(), "flexDirection", 2);
    EnumStyle(state.statusRow.get(), "alignItems", 2); // Align.Center.
    api.Call(content.get(), "Add", {state.statusRow.get()});
    state.spellSlot.reset(CreateSpellView(state.statusRow.get()));
    for (auto property : {"width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight"})
        Pixels(state.spellSlot.get(), property, 80);
    FloatStyle(state.spellSlot.get(), "flexShrink", 0);
    Pixels(state.spellSlot.get(), "marginRight", 8);
    state.status.reset(Label(state.statusRow.get()));
    Pixels(state.status.get(), "minWidth", 0);
    FloatStyle(state.status.get(), "flexGrow", 1);
    FloatStyle(state.status.get(), "flexShrink", 1);
    state.action.reset(api.New(api.Class("Core.dll", FigmaSpace, "ListElementBasic")));
    int actionStyle = 1;
    api.Call(state.action.get(), "set_listElementStyle", {&actionStyle});
    Text(state.action.get(), "\xC3\x89quiper");
    api.Call(content.get(), "Add", {state.action.get()});
    api.Call(root, "Add", {state.window.get()});
    ClampPosition(false);
}
bool CanClick() {
    if (!state.window.get() || state.hiding || !state.equipEnabled || !state.equip) return false;
    if (!api.Call(state.window.get(), "get_panel")) return false;
    if (!api.Value<bool>(api.Call(state.action.get(), "get_enabledInHierarchy"))) return false;
    Ref ancestor(state.action.get());
    for (unsigned depth = 0; ancestor.get(); ++depth) {
        if (depth >= 128) throw std::runtime_error("Soul panel unexpected UI ancestry");
        Ref resolved(api.Call(ancestor.get(), "get_resolvedStyle"));
        if (api.Value<int>(api.Call(resolved.get(), "get_display")) != 0) return false;
        ancestor.reset(api.Call(ancestor.get(), "get_parent"));
    }
    return true;
}
void ActionHook(Obj owner, Method method) {
    // Registrations are made during installation, before hooks are enabled.
    for (const auto handler : auxiliaryActions) {
        try { if (handler(owner)) return; }
        catch (...) { Log("AUXILIARY_PANEL_ACTION_ERROR"); return; }
    }
    if (owner != state.action.get()) { originalAction(owner, method); return; }
    try {
        if (!CanClick()) return;
        state.equipEnabled = false;
        bool disabled = false;
        api.Call(owner, "SetEnabled", {&disabled});
        state.equip(); // Controller revalidates session, placement and inventory.
    } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_ACTION_ERROR ") + e.what()); }
    catch (...) { Log("SOUL_PANEL_ACTION_ERROR unknown"); }
}
void UpHook(Obj owner, Obj event, Method method) {
    const bool owned = owner && owner == state.dragger.get();
    const bool wasActive = owned && Active();
    originalUp(owner, event, method);
    if (!owned || state.hiding || owner != state.dragger.get()) return;
    try {
        // Native MouseUp can clear m_active on an unrelated mouse button while
        // retaining capture. Finish that owned capture before persisting.
        if (!Active()) {
            ReleaseOwnedCapture();
            ClampPosition(true);
            SavePosition(wasActive);
        }
    } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_DRAG_ERROR ") + e.what()); }
    catch (...) { Log("SOUL_PANEL_DRAG_ERROR unknown"); }
}
}

namespace soul {
void SetAuxiliaryPanelAction(bool (*handler)(managed::Obj)) {
    if (handler && std::find(detail::auxiliaryActions.begin(), detail::auxiliaryActions.end(), handler) == detail::auxiliaryActions.end())
        detail::auxiliaryActions.push_back(handler);
}
void SetAuxiliaryPanelMenus(bool (*populate)(managed::Obj, managed::Obj), bool (*callback)(managed::Obj, managed::Obj)) {
    const auto pair = std::make_pair(populate, callback);
    if (populate && callback && std::find(detail::auxiliaryMenus.begin(), detail::auxiliaryMenus.end(), pair) == detail::auxiliaryMenus.end())
        detail::auxiliaryMenus.push_back(pair);
}
bool PanelMenusReady() { return detail::populateHooked; }
managed::Obj GetUiRoot() {
    using namespace managed;
    const auto klass = managed::api.Class("Core.dll", "", "pq");
    const auto method = managed::api.Find(klass, "pbs", 0);
    managed::api.Native(method, 0x2E93CC0);
    Ref document(api.Invoke(method, nullptr));
    Ref service(module ? module->FindService("fqp") : nullptr);
    if (!document.get() || !service.get() || api.object_get_class(service.get())!=api.Class("Core.dll","","gmo")) return nullptr;
    Ref layers(api.Field(service.get(),"dzrv"));
    if (!layers.get() || api.object_get_class(layers.get())!=api.Class("Core.dll","","gmy") ||
        api.Call(layers.get(),"get_parent")!=document.get()) return nullptr;
    Ref hierarchy(api.Call(layers.get(),"get_hierarchy"));
    const int count=api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
    if (count<0 || count>64) return nullptr;
    Obj found=nullptr;
    for (int i=0;i<count;++i) {
        Ref child(api.Call(hierarchy.get(),"get_Item",{&i}));
        if (!child.get() || api.String(api.Call(child.get(),"get_name"))!="RootLow") continue;
        if (found || api.Call(child.get(),"get_parent")!=layers.get()) return nullptr;
        found=child.get();
    }
    // Native HUD layer stays below windows and native tooltip layers.
    return found;
}
void Panel::Install(void (*equip)(), void (*positionChanged)(int, int, uint64_t), void (*hideModule)()) {
    using namespace detail;
    if (!state.installed) {
        InstallItemView();
        auto slot=api.Class("Core.dll","Core.UILogic.Components.Slots","Slot");
        api.Native(api.Find(slot,"get_slotType",0),0x1597E90);
        api.Native(api.Find(slot,"get_slotSize",0),0x165EC30);
        auto window = api.Class("Core.dll", WindowSpace, "MagneticDragWindow");
        api.Native(api.Find(window, ".ctor", 0), 0x15B2DE0);
        api.Native(api.Find(window, "Init", 1), 0x15B2010);
        api.Native(api.Find(window, "set_hasBackground", 1), 0x15B3440);
        auto base = api.Class("Core.dll", WindowSpace, "ResizableWindow");
        const std::pair<const char*,uintptr_t> setters[] = {
            {"set_headerTitle",0x15BFC00},{"set_headerIcon",0x15BFB10},{"set_headerSize",0x15BFB80},
            {"set_headerIconRight",0x15BFAD0},
            {"set_headerShowCloseButton",0x15BFB50},{"set_verticalResizable",0x15BFCF0},
            {"set_horizontalResizable",0x15BFC40},{"set_cornerResizable",0x15BF930},
            {"set_dragPosition",0x15BF9D0},{"set_isUILocked",0x15BFC60}};
        for (auto [name,rva] : setters) api.Native(api.Find(base,name,1),rva);
        api.FindVerified(api.Class("Core.dll", FigmaSpace, "HeaderWidget"), "set_showRightButton", 1, 0x172DB70);
        constexpr const char* menus = "Core.UILogic.Components.ContextMenu";
        auto manipulator = api.Class("Core.dll", menus, "DofusContextualMenuManipulator");
        api.FindVerified(manipulator, ".ctor", 4, 0x16D0BB0);
        AddHook(api.FindVerified(manipulator, "OnContextualMenuEvent", 1, 0x16D04D0), 0x16D04D0,
            reinterpret_cast<void*>(&PopulateMenuHook), reinterpret_cast<void**>(&originalPopulate));
        populateHooked = true;
        auto menuClass = api.Class("Core.dll", menus, "DofusContextualMenu");
        state.appendTitle = api.FindVerified(menuClass, "AppendTitle", 1, 0x16D7F70);
        state.appendSeparator = api.FindVerified(menuClass, "AppendSeparator", 0, 0x16D76E0);
        state.appendMenu = api.FindVerified(api.Class("Core.dll", menus, "DofusContextualMenu"), "AppendDefaultItem", 13, 0x16D6C60);
        api.FindVerified(api.Class("Core.dll", menus, "DofusContextualMenuPopulateEvent"), "get_menu", 0, 0x11E7EC0);
        auto extensions = api.Class(UiImage, UiSpace, "VisualElementExtensions");
        state.addManipulator = api.FindVerified(extensions, "AddManipulator", 2, 0x56755F0);
        state.removeManipulator = api.FindVerified(extensions, "RemoveManipulator", 2, 0x5676A90);
        api.Native(api.Find(base,"get_headerWidget",0),0x1585330);
        api.Native(api.Find(base,"get_contentContainer",0),0x1585350);
        auto dependencies = api.Class("Core.dll","","goc");
        api.Native(api.Find(dependencies,"bfre",0),0x68D2C0);
        api.Native(api.Find(dependencies,"bfri",0),0x682030);
        auto input = api.Class("Core.dll","","eqo");
        for (auto [name,rva] : {std::pair{"ydf",uintptr_t{0x69A450}}, {"ydh",uintptr_t{0x741A60}},
                {"ydj",uintptr_t{0x11E7EC0}}, {"ydl",uintptr_t{0x11E7E80}}})
            api.Native(api.Find(input,name,0),rva);
        api.Native(api.Find(api.Class("mscorlib.dll","System","Delegate"),"get_Target",0),0x68D2D0);
        state.unsubscribe = api.FindVerified(api.Class("Core.dll","Core.Engine.Inputs.Events","InputCustomEvent"),
            "UnSubscribe",1,0x20FF620);
        auto action = api.Class("Core.dll", FigmaSpace, "ListElementBasic");
        api.Native(api.Find(action, ".ctor", 0), 0x1734630);
        api.Native(api.Find(action, "set_listElementStyle", 1), 0x1734700);
        auto mouse = api.Class(UiImage, UiSpace, "MouseCaptureController");
        api.Native(api.Find(mouse, "HasMouseCapture", 1), 0x57A9FD0);
        api.Native(api.Find(mouse, "ReleaseMouse", 1), 0x57AA140);
        api.NativeAt(0x166DF30);
        AddNativeHook(0x166DF30, reinterpret_cast<void*>(&DragFlagHook), reinterpret_cast<void**>(&setDragging));
        auto list = api.Class("Core.dll", FigmaSpace, "ListElementBase");
        AddHook(api.Find(list, "<AssignClickAction>b__27_0", 0), 0x1733850,
            reinterpret_cast<void*>(&ActionHook), reinterpret_cast<void**>(&originalAction));
        auto dragger = api.Class("Core.dll", DragSpace, "ElementDraggerWithBounds");
        api.Native(api.Find(dragger,"RemoveSnapPreview",0),0x16714A0);
        AddHook(api.Find(dragger,"NotifyDragCompleted",0),0x16702F0,
            reinterpret_cast<void*>(&NotifyHook),reinterpret_cast<void**>(&originalNotify));
        AddHook(api.Find(dragger, "OnMouseDown", 1), 0x1670470,
            reinterpret_cast<void*>(&DownHook), reinterpret_cast<void**>(&originalDown));
        AddHook(api.Find(dragger, "OnMouseUp", 1), 0x16709C0,
            reinterpret_cast<void*>(&UpHook), reinterpret_cast<void**>(&originalUp));
        state.installed = true;
    }
    state.equip = equip;
    state.positionChanged = positionChanged;
    state.hideModule = hideModule;
}
void Panel::Render(managed::Obj root, const PanelView& view, int x, int y, uint64_t layoutGeneration) {
    using namespace detail;
    if (!state.installed) throw std::runtime_error("Soul panel is not installed");
    if (!root || !api.Call(root, "get_panel")) { Hide(); return; }
    const bool replaced = state.dependencies.get() && (!module ||
        module->FindService("fpt") != state.dependencies.get() || module->cached.get() != state.cached.get() ||
        module->compositor.get() != state.compositor.get() || module->FindService("fpz") != state.contextService.get());
    if (state.cleanupPending || root != state.root.get() || replaced || (state.window.get() && !api.Call(state.window.get(), "get_panel"))) {
        Hide();
        if (state.window.get()) throw std::runtime_error("Soul panel cleanup pending");
    }
    try {
        if (!state.window.get()) {
            Create(root, x, y);
            state.layoutGeneration = layoutGeneration;
        }
        Ref options(api.Call(state.cached.get(),"get_dofusOptionsContainer"));
        Ref lockOption(api.Field(options.get(),"lockUI"));
        bool locked = api.Value<bool>(api.Call(lockOption.get(),"get_property"));
        if (locked && Active()) ReleaseOwnedCapture();
        api.Call(state.window.get(),"set_isUILocked",{&locked});
        const bool applied = layoutGeneration != state.layoutGeneration || x != state.inputX || y != state.inputY;
        if (applied) {
            // A native layout can be applied during a drag, including another
            // layout with identical coordinates. Never persist the old gesture
            // into the newly selected layout, or clear a foreign drag owner.
            ReleaseOwnedCapture();
            state.layoutGeneration = layoutGeneration;
            state.inputX = x; state.inputY = y;
            state.x = state.savedX = std::clamp(x, 0, 100000);
            state.y = state.savedY = std::clamp(y, 0, 100000);
        }
        if (Active() && !Captured()) {
            ReleaseOwnedCapture();
            ClampPosition(true);
            SavePosition(true);
        }
        if (state.nativeDragging) {
            // A neighbouring native widget may be dragging this one in a group.
            // Never rewrite the saved location over that live native gesture.
            ObservePosition();
        } else if (!Active()) {
            ClampPosition(false);
            SavePosition(false);
            state.gestureObserved = false;
        }
        Text(state.requirement.get(), view.requirement);
        Text(state.stock.get(), view.stock);
        Text(state.equipped.get(), view.equipped);
        Text(state.status.get(), view.status);
        TextColor(state.equipped.get(), !view.equipmentChecked ? StatusTone::Neutral :
            view.equippedCorrect ? StatusTone::Good : StatusTone::Warning);
        TextColor(state.status.get(), view.statusTone);
        const auto spell = view.showEquipment ? nullptr : view.captureSpell;
        const bool spellBound = UpdateSpellView(spell, !view.showEquipment && view.castEnabled);
        if(spell&&spellBound)MatchSpellSize();
        EnumStyle(state.spellSlot.get(), "display", spell && spellBound ? 0 : 1);
        EnumStyle(state.statusRow.get(), "display", view.status.empty() && !spell ? 1 : 0);
        Pixels(state.window.get(), "width", view.showEquipment ? 244.0f : 220.0f);
        EnumStyle(state.status.get(), "display", view.status.empty() ? 1 : 0);
        EnumStyle(state.requirement.get(), "display", !view.showEquipment || view.requirement.empty() ? 1 : 0);
        EnumStyle(state.stock.get(), "display", !view.showEquipment || view.stock.empty() ? 1 : 0);
        EnumStyle(state.equippedRow.get(), "display", !view.showEquipment || (view.equipped.empty() && !view.equippedItem) ? 1 : 0);
        const auto item = view.showEquipment ? view.equippedItem : nullptr;
        // Clearing the native binding also closes its owned hover tooltip when
        // placement ends, even if a stale item remains in a caller's view.
        const bool itemBound = UpdateItemView(item);
        EnumStyle(state.itemSlot.get(), "display", itemBound && item ? 0 : 1);
        EnumStyle(state.action.get(), "display", view.showEquipment && view.showEquip ? 0 : 1);
        state.equipEnabled = view.showEquipment && view.showEquip && view.equipEnabled;
        bool enabled = state.equipEnabled;
        api.Call(state.action.get(), "SetEnabled", {&enabled});
    } catch (...) { Hide(); throw; }
}
void Panel::Hide() {
    using namespace detail;
    if (state.hiding) return;
    state.hiding = true;
    state.cleanupPending = true;
    state.equipEnabled = false;
    state.menuArmed = false;
    bool cleanupComplete = true;
    auto attempt = [&](auto operation) {
        try { operation(); }
        catch (const std::exception& e) { cleanupComplete = false; Log(std::string("SOUL_PANEL_CLEANUP_ERROR ") + e.what()); }
        catch (...) { cleanupComplete = false; Log("SOUL_PANEL_CLEANUP_ERROR unknown"); }
    };
    attempt([] { ReleaseOwnedCapture(); });
    attempt([] { ClosePanelMenu(); });
    attempt([] {
        if (state.menuManipulator.get()) {
            api.Invoke(state.removeManipulator, nullptr, {state.menuButton.get(), state.menuManipulator.get()});
            state.menuManipulator.reset();
        }
    });
    attempt([] { ReleaseItemView(); });
    attempt([] { ReleaseSpellView(); });
    attempt([] { if (state.window.get()) EnumStyle(state.window.get(), "display", 1); });
    attempt([] {
        if (state.window.get()) {
            bool locked = true;
            api.Call(state.window.get(), "set_isUILocked", {&locked});
            int none = 0;
            api.Call(state.window.get(), "set_dragPosition", {&none});
        }
    });
    attempt([] { DisconnectInputs(); });
    attempt([] { if (state.window.get()) api.Call(state.window.get(), "RemoveFromHierarchy"); });
    if (!cleanupComplete) { state.hiding = false; return; }
    state.spellSlot.reset(); state.statusRow.reset();
    state.action.reset(); state.status.reset(); state.equipped.reset();
    state.itemSlot.reset(); state.equippedRow.reset();
    state.stock.reset(); state.requirement.reset(); state.dragger.reset();
    state.header.reset(); state.window.reset(); state.root.reset();
    state.menuButton.reset(); state.contextService.reset(); state.compositor.reset();
    for (auto& event : state.inputEvents) event.reset();
    state.manipulators.clear(); state.input.reset(); state.cached.reset(); state.dependencies.reset();
    state.layoutGeneration = state.dragGeneration = 0;
    state.nextSpellSize = 0;
    state.nativeDragging = state.gestureObserved = false;
    state.cleanupPending = false;
    state.hiding = false;
}
bool PanelCleanupPending() { return detail::state.cleanupPending; }
managed::Obj PanelWindow() { using namespace detail; return state.hiding || state.cleanupPending ? nullptr : state.window.get(); }
bool HandlePanelMenuCallback(managed::Obj callback, managed::Obj view) {
    using namespace detail;
    for (const auto& handlers : auxiliaryMenus) { try { if (handlers.second(callback, view)) return true; } catch (...) { Log("AUXILIARY_MENU_CALLBACK_ERROR"); return true; } }
    if (!callback || callback != state.menuCallback.get()) return false;
    try {
        if (!state.menuArmed || !MenuAvailable() || !view || !api.Call(view, "get_panel") ||
            api.Call(view, "get_itemData") != state.menuItem.get() || api.Value<bool>(api.Call(state.menuItem.get(), "get_disabled"))) return true;
        Ref root(MenuRoot());
        if (!root.get() || !api.Call(root.get(), "get_panel")) return true;
        Ref rows(api.Call(state.menu.get(), "get_menuItems"));
        int count = api.Value<int>(api.Call(rows.get(), "get_Count"));
        if (count < 0 || count > 128) return true;
        bool present = false;
        for (int i = 0; i < count; ++i) if (api.Call(rows.get(), "get_Item", {&i}) == state.menuItem.get()) { present = true; break; }
        if (!present) return true;
        state.menuArmed = false; // Disarm before native close/settings callbacks.
        const auto hide = state.hideModule;
        ClosePanelMenu();
        hide();
    } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_MENU_CLICK_ERROR ") + e.what()); }
    return true;
}
}
