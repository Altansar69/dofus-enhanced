// VerticalGaugesTests (offline): vertical-gauges.cpp on a fake IL2CPP runtime.
// The game's gauge widgets as their UXML builds them (the window in its hub
// container, ctr_main added to the window: in its content element, its parent
// the window), their hub and its dictionary, their « … » menu as
// CreateGenericGaugeSection builds it and OpenGaugeMenu shows it, a click as
// the game's SimulateClicked runs it, the inline styles (one code-set value per
// property; keyword Null: none, the UXML's rule applies), a layout pass a frame
// later, the game's own placements (from its saved places) and drags. No
// client binary, process or game file is used.
// Every native call the module makes must have been verified at install
// (FindVerified at its pinned RVA, every RVA guarded in signatures.hpp),
// except the enumerations and style value setters every module calls by name.
#include "ui/vertical-gauges.hpp"
#include "ui/vertical-gauge-policy.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace fixture {
using namespace managed;
struct Class { std::string name; };
struct MethodData { std::string type, name; int argc = 0; uintptr_t rva = 0; };
struct FieldData { size_t offset = 0; };
// A managed object laid out as IL2CPP's: its class at 0, its monitor at 8, its
// fields from 0x10 (the module reads its checked offsets there).
struct Raw {
    Class* klass = nullptr;
    void* monitor = nullptr;
    alignas(8) unsigned char fields[0x360]{};
};
static_assert(offsetof(Raw, fields) == 0x10, "IL2CPP fields start after the object header");
// The fake's own state, after the managed layout.
struct Object : Raw {
    std::map<std::string, Obj> refs;                              // named references (a widget's parts, a menu's list...)
    std::map<std::string, std::array<unsigned char, 12>> styles; // code-set inline values, by property
    std::vector<Obj> items;                                       // a list's, a dictionary's, an element's children
    alignas(8) std::array<unsigned char, 24> bytes{};             // a boxed value
    std::string name, text, property, help;
    std::wstring wide;                                            // a string's characters, as il2cpp_string_chars gives them
    std::array<bool, 8> handles{};                                // a window's resize handles shown, by ResizeHandler
    // Its parent as get_parent reads it (m_LogicalParent) and the element whose
    // children it is (m_PhysicalParent: a window's content element for the
    // window's own children).
    Obj parent = nullptr, host = nullptr, panel = nullptr, style = nullptr;
    float x = 0, y = 0, width = 0, height = 0;                    // its layout, from the last layout pass
    float uxmlWidth = 566;                                        // a window's width in its UXML rule
    int orientation = 0, icon = -1, cursor = -1;
    unsigned delay = 0;
    bool checkbox = false, isOn = false, disabled = false, close = false, hidden = false;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<std::string, FieldData> fieldInfos;
std::map<GCHandle, Obj> handles;
GCHandle handleId = 0x4A5000000000ULL;
// The game's class chains (class_get_parent, class_is_assignable_from).
const std::map<std::string, std::string> bases{
    {"ExtendedProgressBarLong", "BaseProgressBar`2"}, {"BaseProgressBar`2", "BindableElement"}, {"BindableElement", "VisualElement"},
    {"MagneticDragWindow", "ResizableWindow"}, {"ResizableWindow", "VisualElement"}, {"DofusVisualElement", "VisualElement"},
    {"DofusLabel", "VisualElement"}, {"DofusIcon", "VisualElement"}, {"DofusButtonCustom", "VisualElement"},
    {"DofusContextualMenuRoot", "VisualElement"}, {"ContextMenuItemView", "VisualElement"},
    {"ContextMenuItem", "BaseContextMenuItem"}, {"ContextMenuSeparatorItem", "BaseContextMenuItem"}, {"ContextMenuTitleItem", "BaseContextMenuItem"}};
const std::set<std::string> valueTypes{"WidgetProgressGaugeBinding", "StyleLength", "StyleEnum`1", "Length", "Rect", "Int32", "Boolean", "Enumerator",
    "ValueTuple`4"};
// What the module asked for at installation.
std::set<std::tuple<std::string, std::string, int, uintptr_t>> contracts;
std::set<std::string> fieldsChecked;
std::string failVerified, failField, barBaseName = "BaseProgressBar`2";
// The detour on CreateGenericGaugeSection and the game's code behind it.
using SectionFn = void (*)(Obj, Obj, Method);
SectionFn sectionHook = nullptr;
uintptr_t hookRva = 0;
unsigned hooks = 0;
std::vector<std::string> logs;
// Every native call, by receiver (a style's calls count on its element too).
unsigned calls = 0, nameCalls = 0, enumerations = 0, disposals = 0, lookups = 0, removeMenus = 0;
std::map<Obj, unsigned> callsOn;
// Failure injection: this method on this receiver throws (failCount times; -1: always).
std::string failMethod;
Obj failOwner = nullptr;
int failCount = 0;
bool skewReadBack = false; // the window's width reads back otherwise than written
// The settings as Module::Change keeps them (a failed save still keeps them).
unsigned changes = 0;
bool changeFails = false;
uint64_t nowMs = 100000;
// The game's interface: the panel, the gauges' layer (1920 x 1080 panel pixels
// unless a test changes the interface scale), the hub and its gauges.
float screenWidth = 1920, screenHeight = 1080;
Obj panel = nullptr, layer = nullptr, hub = nullptr, cachedData = nullptr, presets = nullptr, openMenu = nullptr;
// UiPresetOptionsContainer.LoadUiDimensionById, by UI id: the width, and the
// height (35 unless saved otherwise).
std::map<std::string, float> savedWidths, savedHeights;
// UiPresetOptionsContainer.LoadUiPositionById, by UI id: per-mille of the
// screen, or flush with its right or bottom edge.
struct SavedPosition {
    float x = 0, y = 0;
    bool right = false, bottom = false;
    bool operator==(const SavedPosition&) const = default;
};
std::map<std::string, SavedPosition> savedPositions;
// The game's layout saves (UiPresetOptionsContainer.SaveUiPositionById and
// SaveUiDimensionById), through the module's detours once installed.
using SavePositionFn = void (*)(Obj, Obj, Obj, bool, bool, Method);
using SaveDimensionFn = void (*)(Obj, Obj, Obj, Method);
SavePositionFn positionHook = nullptr;
SaveDimensionFn dimensionHook = nullptr;
unsigned positionSaves = 0, dimensionSaves = 0;
// The game's CreateGenericGaugeSection: « Masquer le module » left out
// (fow.zml()), or a menu without its separator.
bool hideRow = true, separatorRow = true;
unsigned nativeSections = 0, nativeCallbacks = 0;

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake object"); return *static_cast<Object*>(obj); }
Class* Type(const std::string& name) { auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get(); }
Obj Make(const std::string& type) {
    auto o = std::make_unique<Object>();
    o->klass = Type(type);
    auto p = o.get();
    objects.push_back(std::move(o));
    return p;
}
template<class T> Obj Box(T value, const std::string& type = "Int32") {
    static_assert(sizeof(T) <= 24);
    auto o = Make(type);
    std::memcpy(Get(o).bytes.data(), &value, sizeof(value));
    return o;
}
Obj String(const std::string& text) { auto o = Make("String"); Get(o).text = text; Get(o).wide.assign(text.begin(), text.end()); return o; }
template<class T> void Put(Obj o, size_t offset, T value) {
    Check(offset >= 0x10 && offset - 0x10 + sizeof(T) <= sizeof(Raw::fields), "Fake field outside the object");
    std::memcpy(Get(o).fields + offset - 0x10, &value, sizeof(T));
}
bool Derives(std::string type, const std::string& base) {
    for (;;) {
        if (type == base) return true;
        const auto parent = bases.find(type);
        if (parent == bases.end()) return false;
        type = parent->second;
    }
}
bool Guarded(uintptr_t rva) {
    return std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& s) { return s.rva == rva; });
}
void Fail(Obj o, const std::string& method) {
    if (o != failOwner || method != failMethod || failCount == 0) return;
    if (failCount > 0) --failCount;
    throw std::runtime_error("Injected failure: " + method);
}
// ------------------------------------------------------------------ elements
Obj Element(const std::string& type, const std::string& name) { auto o = Make(type); Get(o).name = name; return o; }
// VisualElement.Add (0x55DFAD0), as the UXML's clone adds each child: into
// the parent's content container (`into`: a window's content element, else
// the parent itself), its parent the parent itself. Without `into`, both are
// the parent, as Hierarchy.Add makes them.
void Add(Obj parent, Obj child, Obj into = nullptr) {
    if (!into) into = parent;
    Get(child).parent = parent;
    Get(child).host = into;
    Get(into).items.push_back(child);
}
void Detach(Obj child) {
    if (auto host = Get(child).host) { auto& v = Get(host).items; v.erase(std::remove(v.begin(), v.end(), child), v.end()); }
    Get(child).parent = Get(child).host = nullptr;
}
Obj PanelOf(Obj o) { for (; o; o = Get(o).host) if (Get(o).panel) return Get(o).panel; return nullptr; }
Obj StyleObject(Obj element) {
    auto& e = Get(element);
    if (!e.style) { e.style = Make("InlineStyleAccess"); Get(e.style).refs["owner"] = element; }
    return e.style;
}
bool IsEnumStyle(const std::string& property) { return property == "flexDirection"; }
std::array<unsigned char, 12> NullStyle(const std::string& property) {
    std::array<unsigned char, 12> bytes{};
    const int null = 1; // StyleKeyword.Null
    std::memcpy(bytes.data() + (IsEnumStyle(property) ? 4 : 8), &null, 4);
    return bytes;
}
std::array<unsigned char, 12> LengthStyle(float value, int unit = 0) {
    std::array<unsigned char, 12> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    std::memcpy(bytes.data() + 4, &unit, 4);
    return bytes;
}
bool Styled(Obj e, const std::string& property) { return Get(e).styles.contains(property); }
// A code-set length: its value, unit (0 pixels, 1 percent) and keyword.
std::tuple<float, int, int> LengthOf(Obj e, const std::string& property) {
    Check(Styled(e, property), "Style not written: " + Get(e).name + "." + property);
    float value = 0; int unit = -1, keyword = -1;
    const auto& bytes = Get(e).styles.at(property);
    std::memcpy(&value, bytes.data(), 4); std::memcpy(&unit, bytes.data() + 4, 4); std::memcpy(&keyword, bytes.data() + 8, 4);
    return {value, unit, keyword};
}
float Pixels(Obj e, const std::string& property) {
    const auto [value, unit, keyword] = LengthOf(e, property);
    Check(unit == 0 && keyword == 0, "Pixels expected: " + Get(e).name + "." + property);
    return value;
}
float Percent(Obj e, const std::string& property) {
    const auto [value, unit, keyword] = LengthOf(e, property);
    Check(unit == 1 && keyword == 0, "Percent expected: " + Get(e).name + "." + property);
    return value;
}
int EnumStyle(Obj e, const std::string& property) {
    Check(Styled(e, property), "Style not written: " + Get(e).name + "." + property);
    int value = 0, keyword = -1;
    std::memcpy(&value, Get(e).styles.at(property).data(), 4);
    std::memcpy(&keyword, Get(e).styles.at(property).data() + 4, 4);
    Check(keyword == 0, "Enum value expected: " + property);
    return value;
}
// The game's own inline write (its placement, a drag, any code of its own).
void GameWrite(Obj e, const std::string& property, float pixels) { Get(e).styles[property] = LengthStyle(pixels); }
float PixelsOr(Obj e, const std::string& property, float otherwise) {
    if (!Styled(e, property)) return otherwise;
    const auto [value, unit, keyword] = LengthOf(e, property);
    return unit == 0 && keyword == 0 ? value : otherwise;
}

// ------------------------------------------------------------------ gauges
struct Gauge {
    Obj widget = nullptr, container = nullptr, window = nullptr, content = nullptr, main = nullptr, box = nullptr, icon = nullptr, bar = nullptr;
    Obj level = nullptr, title = nullptr, value = nullptr, omega = nullptr, omegaIcon = nullptr, omegaBox = nullptr, badge = nullptr;
    Obj omegaValue = nullptr, button = nullptr, inner = nullptr;
    int type = 0;
    std::vector<Obj> Elements() const {
        return {container, window, content, main, box, icon, bar, level, title, value, omega, omegaIcon, omegaBox, badge, omegaValue, button, inner};
    }
};
std::vector<Gauge> gauges;
bool Flag(Obj window, size_t offset) { bool value = false; std::memcpy(&value, Get(window).fields + offset - 0x10, 1); return value; }
// ResizeHandler: the corners 0 to 3, Right 4, Left 5, Bottom 6, Top 7.
constexpr int BottomHandle = 6, TopHandle = 7;
// ResizableWindow.UpdateResizeHandlers (0x15BE8A0), run by each resize flag's
// setter when it changes: the left and right handles follow
// m_horizontalResizable, the top and bottom ones m_verticalResizable, the
// corners m_cornerResizable; none while the interface is locked (m_isUILocked).
void UpdateResizeHandlers(Obj window) {
    auto& shown = Get(window).handles;
    const bool locked = Flag(window, 0x2CC);
    for (int corner = 0; corner < 4; ++corner) shown[corner] = Flag(window, 0x356) && !locked;
    shown[4] = shown[5] = Flag(window, 0x355) && !locked;
    shown[BottomHandle] = shown[TopHandle] = Flag(window, 0x354) && !locked;
}
std::string Id(int type) {
    switch (type) {
    case 0x1: return "GaugeCharacter";
    case 0x2: return "GaugeGuild";
    case 0x8: return "GaugeHonorPoints";
    case 0x10: return "GaugePods";
    case 0x20: return "GaugeEnergyPoints";
    default: return "GaugeJob" + std::to_string(type);
    }
}
Obj Dictionary(Obj owner) { Obj dictionary = nullptr; std::memcpy(&dictionary, Get(owner).fields + 0xF0 - 0x10, sizeof(dictionary)); return dictionary; }
// A widget as the hub's factory builds it (goz`1+gox: the container named
// after the gauge, the cloned UXML, WidgetProgressGaugeUi), held by `owner`'s
// dictionary. The window as the UXML has it: vertically and corner resizable,
// its content element its own (ResizableWindow's constructor, Hierarchy.Add),
// the UXML's ctr_main and window_inner added to it (VisualElement.Add: in the
// content element, their parent the window).
Gauge MakeGauge(int type, Obj owner = nullptr) {
    Gauge g;
    g.type = type;
    g.container = Element("VisualElement", Id(type)); Add(layer, g.container);
    g.window = Element("MagneticDragWindow", "widgetProgressGauge"); Add(g.container, g.window);
    g.content = Element("DofusVisualElement", "content"); Add(g.window, g.content);
    g.main = Element("VisualElement", "ctr_main"); Add(g.window, g.main, g.content);
    g.box = Element("VisualElement", "gaugeContainer"); Add(g.main, g.box);
    g.icon = Element("DofusIcon", "icon"); Add(g.box, g.icon);
    g.bar = Element("ExtendedProgressBarLong", "gauge"); Add(g.box, g.bar);
    g.level = Element("VisualElement", "level"); Add(g.main, g.level);
    g.title = Element("DofusLabel", "lbl_title"); Add(g.level, g.title);
    g.value = Element("DofusLabel", "lbl_value"); Add(g.level, g.value);
    g.omega = Element("VisualElement", "omega"); Add(g.main, g.omega);
    g.omegaIcon = Element("DofusIcon", ""); Add(g.omega, g.omegaIcon);
    g.omegaBox = Element("VisualElement", ""); Add(g.omega, g.omegaBox);
    g.badge = Element("VisualElement", ""); Add(g.omegaBox, g.badge);
    g.omegaValue = Element("DofusLabel", "lbl_omegaValue"); Add(g.omegaBox, g.omegaValue);
    g.button = Element("DofusButtonCustom", "btn_settings"); Add(g.main, g.button);
    g.inner = Element("DofusVisualElement", "window_inner"); Add(g.window, g.inner, g.content);
    Put<Obj>(g.window, 0x310, g.content);
    Put<bool>(g.window, 0x354, true); Put<bool>(g.window, 0x355, false); Put<bool>(g.window, 0x356, true);
    Put<int>(g.window, 0x350, 5); // drag-position="Custom"
    UpdateResizeHandlers(g.window);
    g.widget = Make("WidgetProgressGaugeUi");
    Put<int>(g.widget, 0x184, type);
    auto& refs = Get(g.widget).refs;
    refs["dzyj"] = g.container;
    refs["widgetProgressGauge"] = g.window; refs["ctr_main"] = g.main; refs["gaugeContainer"] = g.box; refs["gauge"] = g.bar;
    refs["level"] = g.level; refs["lbl_value"] = g.value; refs["omega"] = g.omega; refs["lbl_omegaValue"] = g.omegaValue;
    refs["btn_settings"] = g.button;
    Get(Dictionary(owner ? owner : hub)).items.push_back(g.widget);
    gauges.push_back(g);
    return g;
}
Obj MakeHub() {
    auto h = Make("gfc");
    Put<Obj>(h, 0xF0, Make("Dictionary`2"));
    return h;
}
// The panel's layout pass, a frame after the writes: the container over the
// layer, each shown window at its inline place and size (else the UXML's:
// 546, 860, 566 x 34, at least 240 x 34); a hidden window has none.
void LayoutPass() {
    for (const auto& g : gauges) {
        auto& c = Get(g.container);
        c.x = c.y = 0; c.width = screenWidth; c.height = screenHeight;
        auto& w = Get(g.window);
        if (w.hidden || !PanelOf(g.window)) { w.x = w.y = w.width = w.height = 0; continue; }
        w.width = std::max(PixelsOr(g.window, "width", w.uxmlWidth), PixelsOr(g.window, "minWidth", 240));
        w.height = std::max(PixelsOr(g.window, "height", 34), 34.f);
        w.x = PixelsOr(g.window, "left", 546);
        w.y = PixelsOr(g.window, "top", 860);
    }
}
// The game placing a gauge from its saved place (goz`1.blxe), as at its first
// layout or a re-placement: blxh (per-mille of the screen, or flush with its
// right or bottom edge) at the window's size, then blxl (on the screen).
void GamePlace(const Gauge& g) {
    const auto& saved = savedPositions.at(Id(g.type));
    const float width = std::max(PixelsOr(g.window, "width", Get(g.window).uxmlWidth), PixelsOr(g.window, "minWidth", 240));
    const float height = std::max(PixelsOr(g.window, "height", 34), 34.f);
    const float x = saved.right ? screenWidth - width : saved.x / 1000.f * screenWidth;
    const float y = saved.bottom ? screenHeight - height : saved.y / 1000.f * screenHeight;
    GameWrite(g.window, "left", std::clamp(x, 0.f, screenWidth - width));
    GameWrite(g.window, "top", std::clamp(y, 0.f, screenHeight - height));
}
// The box the gauges' hub keeps for each gauge (gob`1's place and size, gom
// slots 0x15-0x18): only goz`1.blxe refreshes it, from the window's
// resolvedStyle (its last layout), before anything else: its place unless
// left and top are both 0, its size unless its width or its height is 0. A
// hidden window's layout is all 0: its box stays as it was.
struct KeptBox { float x = 0, y = 0, width = 0, height = 0; };
std::map<Obj, KeptBox> keptBoxes;
// goz`1.blxl (pq.pcs): the box's overflow past each edge of the screen,
// corrected on an axis it overflows on one side only (left before right, top
// before bottom), written inline from the box itself.
void GameKeepOnScreen(Obj window, const KeptBox& box) {
    float top = 0, left = 0, bottom = 0, right = 0;
    if (!(box.y < 0 && box.y + box.height > screenHeight)) {
        if (box.y < 0) top = -box.y;
        if (box.y + box.height > screenHeight) bottom = box.y + box.height - screenHeight;
    }
    if (!(box.x < 0 && box.x + box.width > screenWidth)) {
        if (box.x < 0) left = -box.x;
        if (box.x + box.width > screenWidth) right = box.x + box.width - screenWidth;
    }
    if (left > 0) GameWrite(window, "left", box.x + left);
    else if (right > 0) GameWrite(window, "left", box.x - right);
    if (top > 0) GameWrite(window, "top", box.y + top);
    else if (bottom > 0) GameWrite(window, "top", box.y - bottom);
}
// goz`1.blxe (0x3C13A10) for one gauge as blxd runs it: once laid out first,
// then at each re-placement (the container's geometry: the screen's size, the
// interface scale; a layout preset chosen; a fight's layout at its start and
// end; a reset of the positions), hidden gauges included (gom slot 5,
// ignoreSavedSizeAndPosition, is false for every gauge). The box the hub keeps;
// blxi (0x3C15BD0: its saved size, inline too) only while one of the window's
// resize flags is on; blxh (0x3C15240: its saved place, per-mille of the
// screen through pq.paf, or flush with its right or bottom edge at the box's
// size; inline left and top only); with neither, blxg (0x3C14EA0: blxf's
// default for the character's gauge, a 706 x 34 box centred at the bottom,
// written 707 x 35; none for the others); then blxl on that box.
void GameReplace(const Gauge& g) {
    const auto& w = Get(g.window);
    auto& kept = keptBoxes[g.window];
    if (!(w.x == 0 && w.y == 0)) { kept.x = w.x; kept.y = w.y; }
    if (w.width != 0 && w.height != 0) { kept.width = w.width; kept.height = w.height; }
    KeptBox box = kept;
    bool sized = false, placed = false;
    if (Flag(g.window, 0x354) || Flag(g.window, 0x355) || Flag(g.window, 0x356)) {
        const auto size = savedWidths.find(Id(g.type));
        if (size != savedWidths.end()) {
            box.width = size->second;
            const auto height = savedHeights.find(Id(g.type));
            box.height = height == savedHeights.end() ? 35 : height->second; // LoadUiDimensionById's y, as the fake gives it
            GameWrite(g.window, "width", box.width);
            GameWrite(g.window, "height", box.height);
            sized = true;
        }
    }
    const auto place = savedPositions.find(Id(g.type));
    if (place != savedPositions.end()) {
        box.x = place->second.right ? screenWidth - box.width : place->second.x / 1000.f * screenWidth;
        box.y = place->second.bottom ? screenHeight - box.height : place->second.y / 1000.f * screenHeight;
        GameWrite(g.window, "left", box.x);
        GameWrite(g.window, "top", box.y);
        placed = true;
    }
    if (!sized && !placed && g.type == 0x1) {
        box = {0.5f * screenWidth - 353, screenHeight - 34, 706, 34};
        GameWrite(g.window, "left", box.x);
        GameWrite(g.window, "top", box.y);
        GameWrite(g.window, "width", 707);
        GameWrite(g.window, "height", 35);
    }
    GameKeepOnScreen(g.window, box);
}
// UiPresetOptionsContainer.SaveUiPositionById (0x2092DE0) and
// SaveUiDimensionById (0x2092A80) as the game runs them: into the current
// layout, through the module's detours once it has installed them.
void NativeSavePosition(Obj, Obj id, Obj coordinates, bool right, bool bottom, Method) {
    ++positionSaves;
    float x = 0, y = 0;
    std::memcpy(&x, Get(coordinates).fields, 4); std::memcpy(&y, Get(coordinates).fields + 4, 4);
    savedPositions[Get(id).text] = {x, y, right, bottom};
}
void NativeSaveDimension(Obj, Obj id, Obj coordinates, Method) {
    ++dimensionSaves;
    float width = 0, height = 0;
    std::memcpy(&width, Get(coordinates).fields, 4); std::memcpy(&height, Get(coordinates).fields + 4, 4);
    savedWidths[Get(id).text] = width;
    savedHeights[Get(id).text] = height;
}
Obj Coordinates(float x, float y) {
    auto c = Make("UICoordinates");
    Put<float>(c, 0x10, x); Put<float>(c, 0x14, y);
    return c;
}
void GameSavePosition(const Gauge& g, float x, float y) {
    (positionHook ? positionHook : NativeSavePosition)(presets, String(Id(g.type)), Coordinates(x, y), false, false, nullptr);
}
void GameSaveDimension(const Gauge& g, float width, float height) {
    (dimensionHook ? dimensionHook : NativeSaveDimension)(presets, String(Id(g.type)), Coordinates(width, height), nullptr);
}
// pq.pae (0x2E8FE80): a window's place as the game saves it, per-mille of the
// screen from its header's world place (the gauges' header is display none:
// the window's own top-left corner; its size 0, never against an edge).
void GameSavePlace(const Gauge& g) {
    GameSavePosition(g, PixelsOr(g.window, "left", 546) / screenWidth * 1000.f, PixelsOr(g.window, "top", 860) / screenHeight * 1000.f);
}
// A drag of the window by its drag handle (ctr_main, the whole window):
// ElementDraggerWithBounds.SetNewPosition (0x1671500) writes its inline left
// and top, kept on the screen (ClampToRemainInAllowedScreenSpace), then
// ElementDragger.onDraggedAction: gob`1.WindowDragged -> pq.pae -> cnt ->
// goz`1.blwv -> SaveUiPositionById.
void GameDrag(const Gauge& g, float dx, float dy) {
    const auto& w = Get(g.window);
    GameWrite(g.window, "left", std::clamp(w.x + dx, 0.f, screenWidth - w.width));
    GameWrite(g.window, "top", std::clamp(w.y + dy, 0.f, screenHeight - w.height));
    GameSavePlace(g);
}
// A press on the window's top or bottom edge, pulled by dy: its resize handle
// when shown (ElementResizer.Top 0x167B0A0 / Bottom 0x1674F00: the height,
// and the top for the top edge, between the minimum and maximum height its
// resolved style had at the press and inside the screen; then
// NotifyEndOfResize 0x16777C0 -> onResizeDoneAction -> gob`1.blry -> cnx ->
// goz`1.blwu: SaveUiPositionById and SaveUiDimensionById). A hidden handle is
// not picked: the press lands on ctr_main, the drag handle, and drags the
// window instead.
void GamePullEdge(const Gauge& g, int handle, float dy) {
    const auto& w = Get(g.window);
    if (!Get(g.window).handles[static_cast<size_t>(handle)] || w.hidden) { GameDrag(g, 0, dy); return; }
    const float minimum = std::max(PixelsOr(g.window, "minHeight", 34), 0.f), maximum = PixelsOr(g.window, "maxHeight", 1e9f);
    if (handle == BottomHandle) {
        const float height = std::clamp(std::min(w.height + dy, screenHeight - w.y), minimum, maximum);
        GameWrite(g.window, "height", height);
    } else {
        const float height = std::clamp(std::min(w.height - dy, w.y + w.height), minimum, maximum);
        GameWrite(g.window, "top", w.y + w.height - height);
        GameWrite(g.window, "height", height);
    }
    GameSavePlace(g);
    GameSaveDimension(g, PixelsOr(g.window, "width", w.uxmlWidth), PixelsOr(g.window, "height", 34));
}
// Everything of a gauge the module may change: each element's code-set
// values, its window's resize flags and handles, its bar's orientation.
struct Snapshot {
    std::map<std::pair<Obj, std::string>, std::array<unsigned char, 12>> styles;
    std::array<bool, 3> flags{};
    std::array<bool, 8> handles{};
    int orientation = 0;
    bool operator==(const Snapshot&) const = default;
};
// What differs between two snapshots, for a failed check.
std::string Diff(const Snapshot& a, const Snapshot& b) {
    std::string text;
    for (const auto& [key, bytes] : a.styles) {
        const auto other = b.styles.find(key);
        if (other == b.styles.end() || other->second != bytes) text += " " + Get(key.first).name + "." + key.second;
    }
    for (const auto& [key, bytes] : b.styles) if (!a.styles.contains(key)) text += " +" + Get(key.first).name + "." + key.second;
    if (a.flags != b.flags) text += " flags";
    if (a.handles != b.handles) text += " handles";
    if (a.orientation != b.orientation) text += " orientation";
    return text;
}
Snapshot Take(const Gauge& g) {
    Snapshot s;
    for (auto e : g.Elements()) for (const auto& [property, bytes] : Get(e).styles) s.styles[{e, property}] = bytes;
    s.flags = {Flag(g.window, 0x354), Flag(g.window, 0x355), Flag(g.window, 0x356)};
    s.handles = Get(g.window).handles;
    s.orientation = Get(g.bar).orientation;
    return s;
}

// ------------------------------------------------------------------ menus
Obj NewMenu(const std::string& type = "DofusContextualMenu") {
    auto menu = Make(type), root = Make("DofusContextualMenuRoot");
    Get(menu).refs["items"] = Make("List`1");
    Put<Obj>(menu, 0x38, root);
    Get(root).refs["menuData"] = menu;
    return menu;
}
std::vector<Obj>& Rows(Obj menu) { return Get(Get(menu).refs.at("items")).items; }
Obj RootOf(Obj menu) { Obj root = nullptr; std::memcpy(&root, Get(menu).fields + 0x38 - 0x10, sizeof(root)); return root; }
Obj GameRow(Obj menu, const std::string& type, const std::string& text, bool checkbox = false) {
    auto row = Make(type);
    Get(row).text = text;
    Get(row).refs["root"] = menu;
    if (type == "ContextMenuItem") Get(row).refs["callback"] = Make("ClickCallbackHandler");
    Get(row).checkbox = checkbox;
    Get(row).close = !checkbox;
    Rows(menu).push_back(row);
    return row;
}
// The game's CreateGenericGaugeSection (0x2815820): the gauge's title, « Limiter… »
// for the character, « Couleur de jauge », « Masquer le module », the separator,
// then the hub's « Gestion des jauges » (gfc.bfrw): its title, five checkboxes
// and the jobs' sub-menu, no separator.
void NativeSection(Obj widget, Obj menu, Method) {
    ++nativeSections;
    int type = 0;
    std::memcpy(&type, Get(widget).fields + 0x184 - 0x10, sizeof(type));
    GameRow(menu, "ContextMenuTitleItem", Id(type));
    if (type == 1) GameRow(menu, "ContextMenuItem", "Limiter le pourcentage d'expérience obtenue (100%)");
    GameRow(menu, "ContextMenuItem", "Couleur de jauge");
    if (hideRow) GameRow(menu, "ContextMenuItem", "Masquer le module");
    if (separatorRow) GameRow(menu, "ContextMenuSeparatorItem", "");
    GameRow(menu, "ContextMenuTitleItem", "Gestion des jauges");
    for (const char* gauge : {"Expérience de personnage", "Expérience de guilde", "Points d'honneur", "Pods", "Points d'énergie"})
        GameRow(menu, "ContextMenuItem", gauge, true);
    GameRow(menu, "ContextMenuItem", "Expérience de métier");
}
void CloseMenu(Obj menu) {
    auto root = RootOf(menu);
    Get(root).panel = nullptr;
    for (auto view : Get(root).items) Get(view).panel = nullptr;
    if (openMenu == menu) openMenu = nullptr;
}
// DisplayMenu: every other menu closed first, then this one's views.
void Show(Obj menu) {
    if (openMenu && openMenu != menu) CloseMenu(openMenu);
    auto root = RootOf(menu);
    Get(root).panel = panel;
    Get(root).items.clear();
    for (auto row : Rows(menu)) {
        auto view = Make("ContextMenuItemView");
        Get(view).refs["itemData"] = row;
        Get(view).panel = panel;
        Get(root).items.push_back(view);
    }
    openMenu = menu;
}
// OpenGaugeMenu (0x2817500): a new menu, CreateGenericGaugeSection, DisplayMenu.
Obj OpenMenu(const Gauge& g, bool shown = true, const std::string& type = "DofusContextualMenu") {
    auto menu = NewMenu(type);
    Check(sectionHook != nullptr, "The gauge menu's builder is hooked");
    sectionHook(g.widget, menu, nullptr);
    if (shown) Show(menu);
    return menu;
}
Obj ViewOf(Obj menu, Obj row) {
    for (auto view : Get(RootOf(menu)).items) if (Get(view).refs.at("itemData") == row) return view;
    throw std::runtime_error("No view for this row");
}
// The game's SimulateClicked: a disabled row does nothing; a checkbox's view
// and item flip before the callback; then InvokeCallback (context-display's
// detour offers it to the gauge row first), then closeOnSelect closes the menu.
bool ClickRow(Obj menu, Obj row) {
    auto view = ViewOf(menu, row);
    if (Get(row).disabled) return false;
    if (Get(row).checkbox) { Get(row).isOn = !Get(row).isOn; Get(view).isOn = Get(row).isOn; }
    const bool claimed = HandleVerticalGaugeMenuCallback(Get(row).refs.at("callback"), view);
    if (!claimed) ++nativeCallbacks;
    if (Get(row).close) CloseMenu(menu);
    return claimed;
}
}

// ======================================================= the fake runtime
namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000ULL;
Klass Api::Class(const char*, const char*, const char* name) const { return fixture::Type(name); }
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    const std::string declaring = static_cast<fixture::Class*>(declaringClass)->name, nested = name;
    fixture::Check((declaring == "ContextMenuItem" && (nested == "ContextMenuItemView" || nested == "ClickCallbackHandler")) ||
        (declaring == "DofusContextualMenu" && nested == "DofusContextualMenuRoot"), "Unexpected nested class " + declaring + "+" + nested);
    return fixture::Type(name);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    using namespace fixture;
    const std::string type = static_cast<fixture::Class*>(klass)->name;
    Check(Guarded(rva), "Verified method without a native guard: " + type + "." + name);
    if (failVerified == name) throw std::runtime_error("Verified method unavailable: " + type + "." + name);
    contracts.insert({type, name, argc, rva});
    auto m = std::make_unique<MethodData>(MethodData{type, name, argc, rva});
    auto p = m.get();
    methods.push_back(std::move(m));
    return p;
}
Obj Api::Field(Obj self, const char* name) const {
    using namespace fixture;
    const std::string field = name;
    auto& o = Get(self);
    if (o.klass->name == "ValueTuple`4") {
        // A saved place's coordinates (null: none saved).
        Check(field == "Item2", "Only a saved place's coordinates are read by name");
        const auto item = o.refs.find(field);
        return item == o.refs.end() ? nullptr : item->second;
    }
    Check(o.klass->name == "WidgetProgressGaugeUi" && (field == "dzyi" || field == "dzyj"), "Only the gauge's binding and container are read by name");
    if (field == "dzyj") return o.refs.at("dzyj");
    // The binding struct, boxed (a copy): its getters find the widget's elements.
    return Box(self, "WidgetProgressGaugeBinding");
}
// A saved place's edge flags, in the boxed tuple.
void* Api::FieldAddress(Obj self, const char* name) const {
    using namespace fixture;
    const std::string field = name;
    auto& o = Get(self);
    Check(o.klass->name == "ValueTuple`4" && (field == "Item3" || field == "Item4"), "Only a saved place's edge flags are read in place");
    return o.bytes.data() + (field == "Item3" ? 0 : 1);
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fixture;
    const auto type = static_cast<fixture::Class*>(klass)->name;
    Check(type == "Length" && args.size() == 2 && contracts.contains({"Length", ".ctor", 2, 0x561D3D0}), "Only verified Length values are built");
    auto o = Make("Length");
    std::memcpy(Get(o).bytes.data(), *args.begin(), 4);
    std::memcpy(Get(o).bytes.data() + 4, *(args.begin() + 1), 4);
    return o;
}
// By name: the hub's dictionary enumeration, the menu's list, the StyleEnum
// setter (generic, as in every module); nothing else.
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    const std::string m = name;
    const std::vector<void*> args(arguments);
    auto& o = Get(self);
    ++nameCalls;
    ++callsOn[self];
    const auto& type = o.klass->name;
    if (type == "Dictionary`2" && m == "get_Values") { auto values = Make("ValueCollection"); Get(values).refs["dictionary"] = self; return values; }
    if (type == "ValueCollection" && m == "GetEnumerator") {
        ++enumerations;
        auto enumerator = Make("Enumerator");
        Get(enumerator).refs["dictionary"] = o.refs.at("dictionary");
        return enumerator;
    }
    if (type == "Enumerator") {
        const auto& values = Get(o.refs.at("dictionary")).items;
        if (m == "MoveNext") return Box(++o.cursor < static_cast<int>(values.size()), "Boolean");
        if (m == "get_Current") return values.at(static_cast<size_t>(o.cursor));
        if (m == "Dispose") { ++disposals; return nullptr; }
    }
    if (type == "List`1") {
        if (m == "get_Count") return Box(static_cast<int>(o.items.size()));
        if (m == "get_Item") return o.items.at(static_cast<size_t>(*static_cast<int*>(args.at(0))));
    }
    if (type == "StyleEnum`1" && m == "set_value") {
        // StyleEnum<T>.value: the value, keyword Undefined.
        const int keyword = 0;
        std::memcpy(o.bytes.data(), args.at(0), 4);
        std::memcpy(o.bytes.data() + 4, &keyword, 4);
        return nullptr;
    }
    throw std::runtime_error("Unverified call: " + type + "." + m);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    const auto& info = *static_cast<const MethodData*>(method);
    const std::vector<void*> args(arguments);
    const std::string& m = info.name;
    Check(info.argc == static_cast<int>(args.size()), "Managed invocation argument count: " + m);
    ++calls;
    if (info.type == "WidgetProgressGaugeBinding") {
        // A value type: self is the unboxed binding, whose fields hold the widget.
        Obj widget = nullptr;
        std::memcpy(&widget, self, sizeof(widget));
        Check(Get(widget).klass->name == "WidgetProgressGaugeUi" && m.starts_with("get_"), "Binding getter on a boxed binding");
        ++callsOn[widget];
        const auto part = Get(widget).refs.find(m.substr(4));
        return part == Get(widget).refs.end() ? nullptr : part->second;
    }
    if (info.type == "StyleLength") {
        // StyleLength.value on the unboxed struct: the Length, keyword Undefined.
        Check(m == "set_value", "StyleLength setter");
        const int keyword = 0;
        std::memcpy(self, args.at(0), 8);
        std::memcpy(static_cast<char*>(self) + 8, &keyword, 4);
        return nullptr;
    }
    Check(self != nullptr, "A native instance call on null: " + info.type + "." + m);
    auto& o = Get(self);
    Check(Derives(o.klass->name, info.type), "Method of " + info.type + " called on " + o.klass->name);
    ++callsOn[self];
    Fail(self, m);
    if (info.type == "InlineStyleAccess") {
        const std::string prefix = "UnityEngine.UIElements.IStyle.";
        Check(m.starts_with(prefix), "Inline styles through their explicit IStyle methods");
        const auto operation = m.substr(prefix.size()), property = operation.substr(4);
        const auto element = o.refs.at("owner");
        ++callsOn[element];
        auto& target = Get(element);
        if (operation.starts_with("get_")) {
            auto box = Make(IsEnumStyle(property) ? "StyleEnum`1" : "StyleLength");
            Get(box).property = property;
            const auto stored = target.styles.find(property);
            const auto bytes = stored == target.styles.end() ? NullStyle(property) : stored->second;
            std::memcpy(Get(box).bytes.data(), bytes.data(), bytes.size());
            if (skewReadBack && property == "width" && stored != target.styles.end()) Get(box).bytes[8] = 2; // read back as Auto
            return box;
        }
        Check(operation.starts_with("set_"), "Unexpected style method");
        std::array<unsigned char, 12> bytes{};
        std::memcpy(bytes.data(), args.at(0), IsEnumStyle(property) ? 8 : 12);
        int keyword = 0;
        std::memcpy(&keyword, bytes.data() + (IsEnumStyle(property) ? 4 : 8), 4);
        // Keyword Null removes the code-set value: the UXML's rule applies again.
        if (keyword == 1) target.styles.erase(property);
        else target.styles[property] = bytes;
        return nullptr;
    }
    if (m == "get_parent") return o.parent;
    if (m == "get_panel") return PanelOf(self);
    if (m == "get_name") return fixture::String(o.name);
    if (m == "get_style") return StyleObject(self);
    if (m == "get_layout") { struct Rect { float x, y, width, height; }; return Box(Rect{o.x, o.y, o.width, o.height}, "Rect"); }
    if (m == "set_gaugeOrientation") { o.orientation = *static_cast<int*>(args.at(0)); return nullptr; }
    if (m == "get_gaugeOrientation") return Box(o.orientation);
    if (m == "set_verticalResizable" || m == "set_horizontalResizable" || m == "set_cornerResizable") {
        const size_t offset = m == "set_verticalResizable" ? 0x354 : m == "set_horizontalResizable" ? 0x355 : 0x356;
        const bool value = *static_cast<bool*>(args.at(0));
        if (Flag(self, offset) != value) { Put<bool>(self, offset, value); UpdateResizeHandlers(self); } // the setter's own check
        return nullptr;
    }
    if (m == "EnableResizeHandler" || m == "DisableResizeHandler") {
        // m_resizeHandles[handler].visible
        const int handler = *static_cast<int*>(args.at(0));
        Check(handler >= 0 && handler < 8, "A ResizeHandler value");
        o.handles[static_cast<size_t>(handler)] = m == "EnableResizeHandler";
        return nullptr;
    }
    if (m == "AppendCheckboxItem") {
        // (label, null ClickCallback, isOn, disabled, closeOnSelect, help, delay, FigmaIcons, String)
        Check(args.at(1) == nullptr, "The row uses the null native callback: only its handler's identity tells it");
        auto row = Make("ContextMenuItem");
        auto& r = Get(row);
        r.checkbox = true;
        r.text = Get(static_cast<Obj>(args.at(0))).text;
        r.isOn = *static_cast<bool*>(args.at(2));
        r.disabled = *static_cast<bool*>(args.at(3));
        r.close = *static_cast<bool*>(args.at(4));
        r.help = Get(static_cast<Obj>(args.at(5))).text;
        r.delay = *static_cast<unsigned*>(args.at(6));
        r.icon = *static_cast<int*>(args.at(7));
        r.refs["callback"] = Make("ClickCallbackHandler");
        r.refs["root"] = self;
        Rows(self).push_back(row);
        return self;
    }
    if (m == "get_menuItems") return o.refs.at("items");
    if (m == "RemoveItemAt") {
        auto& list = Rows(self);
        const int index = *static_cast<int*>(args.at(0));
        Check(index >= 0 && index < static_cast<int>(list.size()), "RemoveItemAt within the menu");
        list.erase(list.begin() + index);
        return self;
    }
    if (m == "InsertItem") {
        auto& list = Rows(self);
        const int index = *static_cast<int*>(args.at(0));
        Check(index >= 0 && index <= static_cast<int>(list.size()), "InsertItem within the menu");
        list.insert(list.begin() + index, static_cast<Obj>(args.at(1)));
        Get(static_cast<Obj>(args.at(1))).refs["root"] = self; // the game gives the row its menu again
        return self;
    }
    if (m == "get_m_callback") return o.refs.at("callback");
    if (m == "get_disabled") return Box(o.disabled, "Boolean");
    if (m == "get_itemData") return o.refs.at("itemData");
    if (m == "get_menuData") return o.refs.at("menuData");
    if (m == "RemoveMenu") {
        Check(!*static_cast<bool*>(args.at(0)), "The menu closes at once");
        ++removeMenus;
        CloseMenu(o.refs.at("menuData"));
        return nullptr;
    }
    if (m == "get_uiPresetOptionsContainer") return presets;
    if (m == "LoadUiDimensionById") {
        const auto found = savedWidths.find(Get(static_cast<Obj>(args.at(0))).text);
        if (found == savedWidths.end()) return nullptr;
        const auto height = savedHeights.find(found->first);
        return Coordinates(found->second, height == savedHeights.end() ? 35.f : height->second);
    }
    if (m == "LoadUiPositionById") {
        // ValueTuple<String, UICoordinates, Boolean, Boolean>, boxed: all default when none is saved.
        auto tuple = Make("ValueTuple`4");
        const auto found = savedPositions.find(Get(static_cast<Obj>(args.at(0))).text);
        if (found == savedPositions.end()) return tuple;
        auto coordinates = Make("UICoordinates");
        Put<float>(coordinates, 0x10, found->second.x);
        Put<float>(coordinates, 0x14, found->second.y);
        Get(tuple).refs["Item1"] = static_cast<Obj>(args.at(0));
        Get(tuple).refs["Item2"] = coordinates;
        Get(tuple).bytes[0] = found->second.right;
        Get(tuple).bytes[1] = found->second.bottom;
        return tuple;
    }
    throw std::runtime_error("Unexpected fake method " + info.type + "." + m);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const {
    fixture::Check(std::string(name) == "fqd", "Only the gauges' hub is looked up");
    ++fixture::lookups;
    return fixture::hub;
}
// As the module: the settings kept, then saved (a save may fail).
void Module::Change(Settings next, bool save) {
    fixture::Check(save, "The gauges' choices use the normal persistent change path");
    ++fixture::changes;
    settings = next;
    if (fixture::changeFails) throw std::runtime_error("Injected save failure");
}
void Log(const std::string& message) { fixture::logs.push_back(message); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& m = *static_cast<const fixture::MethodData*>(method);
    fixture::Check(m.rva == rva, "A detour through its verified method");
    ++fixture::hooks;
    if (m.type == "UiPresetOptionsContainer" && m.name == "SaveUiPositionById" && m.argc == 4) {
        fixture::positionHook = reinterpret_cast<fixture::SavePositionFn>(replacement);
        *original = reinterpret_cast<void*>(&fixture::NativeSavePosition);
        return;
    }
    if (m.type == "UiPresetOptionsContainer" && m.name == "SaveUiDimensionById" && m.argc == 2) {
        fixture::dimensionHook = reinterpret_cast<fixture::SaveDimensionFn>(replacement);
        *original = reinterpret_cast<void*>(&fixture::NativeSaveDimension);
        return;
    }
    fixture::Check(m.type == "WidgetProgressGaugeUi" && m.name == "CreateGenericGaugeSection" && m.argc == 1,
        "Only the gauge menu's builder and the layout's two saves are hooked");
    fixture::hookRva = rva;
    fixture::sectionHook = reinterpret_cast<fixture::SectionFn>(replacement);
    *original = reinterpret_cast<void*>(&fixture::NativeSection);
}
#include "ui/vertical-gauges.cpp"

namespace {
using namespace fixture;
size_t Logged(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
bool LoggedLine(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
uint64_t FakeNow() { return nowMs; }
void Initialize() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).klass; };
    api.class_get_name = [](Klass k) -> const char* {
        const auto& name = static_cast<Class*>(k)->name;
        return name == "BaseProgressBar`2" ? barBaseName.c_str() : name.c_str();
    };
    api.class_get_parent = [](Klass k) -> Klass {
        const auto parent = bases.find(static_cast<Class*>(k)->name);
        return parent == bases.end() ? nullptr : Type(parent->second);
    };
    api.class_is_valuetype = [](Klass k) { return valueTypes.contains(static_cast<Class*>(k)->name); };
    api.class_is_assignable_from = [](Klass base, Klass k) { return Derives(static_cast<Class*>(k)->name, static_cast<Class*>(base)->name); };
    api.object_unbox = [](Obj o) -> void* { return Get(o).bytes.data(); };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.string_length = [](Obj o) -> int32_t { return static_cast<int32_t>(Get(o).wide.size()); };
    api.string_chars = [](Obj o) -> const wchar_t* { return Get(o).wide.c_str(); };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++handleId] = o; return handleId; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Invalid GC release"); };
    // The checked offsets, in the classes the module names.
    api.class_get_field_from_name = [](Klass k, const char* name) -> void* {
        const auto key = static_cast<Class*>(k)->name + "." + name;
        fieldsChecked.insert(key);
        if (key == failField) return nullptr;
        const auto found = fieldInfos.find(key);
        return found == fieldInfos.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field) { return static_cast<FieldData*>(field)->offset; };
    fieldInfos = {{"WidgetProgressGaugeUi.m_gaugeType", {0x184}}, {"gfc.dyfh", {0xF0}}, {"ResizableWindow.<content>k__BackingField", {0x310}},
        {"ResizableWindow.m_verticalResizable", {0x354}}, {"ResizableWindow.m_horizontalResizable", {0x355}},
        {"ResizableWindow.m_cornerResizable", {0x356}}, {"ResizableWindow.m_isUILocked", {0x2CC}}, {"ResizableWindow.m_disableTopHandle", {0x2CA}},
        {"ResizableWindow.m_disableBottomHandle", {0x2CB}}, {"ResizableWindow.m_dragPosition", {0x350}},
        {"DofusContextualMenu.dofusContextualMenuRoot", {0x38}}, {"UICoordinates.x", {0x10}},
        {"UICoordinates.y", {0x14}}};
}
// One test's game and module: the gauges' layer in the panel, the hub, the
// options' services; the module installed unless a test injects a failure first.
struct Fixture {
    Module state{L"offline-vertical-gauges"};
    explicit Fixture(bool install = true) {
        logs.clear(); contracts.clear(); fieldsChecked.clear(); callsOn.clear(); gauges.clear(); savedWidths.clear(); savedPositions.clear();
        keptBoxes.clear(); savedHeights.clear();
        positionHook = nullptr; dimensionHook = nullptr; positionSaves = dimensionSaves = 0;
        failVerified.clear(); failField.clear(); failMethod.clear(); barBaseName = "BaseProgressBar`2";
        failOwner = nullptr; failCount = 0; skewReadBack = false; changeFails = false; hideRow = separatorRow = true;
        calls = nameCalls = enumerations = disposals = lookups = removeMenus = changes = hooks = nativeSections = nativeCallbacks = 0;
        sectionHook = nullptr; hookRva = 0; openMenu = nullptr; nowMs = 100000; screenWidth = 1920; screenHeight = 1080;
        panel = Make("RuntimePanel");
        layer = Element("VisualElement", "RootLow");
        Get(layer).panel = panel;
        hub = MakeHub();
        cachedData = Make("CachedDataService");
        presets = Make("UiPresetOptionsContainer");
        verticalGauges::s = verticalGauges::State{};
        verticalGauges::s.clock = FakeNow;
        verticalGauges::originalSection = nullptr;
        module = &state;
        state.ready = true;
        state.servicesReady = true;
        state.cached.reset(cachedData);
        state.compositor.reset(Make("Compositor"));
        if (install) InstallVerticalGauges();
    }
    ~Fixture() {
        RestoreVerticalGauges();
        verticalGauges::s = verticalGauges::State{};
        module = nullptr;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};
void Tick() { nowMs += 200; TickVerticalGauges(); }
void Frame() { VerticalGaugesFrame(nowMs); }
size_t Records() { return verticalGauges::s.records.size(); }
const verticalGauges::Record* RecordOf(const Gauge& g) {
    for (const auto& r : verticalGauges::s.records) if (r.widget.get() == g.widget) return &r;
    return nullptr;
}
// The row the module added to a menu (null: none), and its index.
Obj OwnedRow(Obj menu) {
    Obj found = nullptr;
    for (auto row : Rows(menu))
        if (Get(row).text == "Afficher à la verticale") { Check(!found, "One row only"); found = row; }
    return found;
}
int IndexOf(Obj menu, Obj row) {
    const auto& rows = Rows(menu);
    const auto it = std::find(rows.begin(), rows.end(), row);
    return it == rows.end() ? -1 : static_cast<int>(it - rows.begin());
}
int SeparatorIndex(Obj menu) {
    const auto& rows = Rows(menu);
    for (size_t i = 0; i < rows.size(); ++i) if (Get(rows[i]).klass->name == "ContextMenuSeparatorItem") return static_cast<int>(i);
    return -1;
}
std::vector<std::string> Texts(Obj menu) {
    std::vector<std::string> texts;
    for (auto row : Rows(menu)) texts.push_back(Get(row).klass->name == "ContextMenuSeparatorItem" ? "---" : Get(row).text);
    return texts;
}
constexpr const char* Help = "Affiche cette jauge à la verticale : elle se remplit de bas en haut, son niveau en dessous. Tire son bord haut ou bas pour changer sa longueur ; sa longueur et sa place à la verticale sont gardées pour la prochaine fois. Décoche pour la remettre à l’horizontale, à sa place et à sa taille d’avant.";
// The vertical layout, every value of the table on its element, the bar
// vertical and the window without its resize handles, `length` long.
void CheckVertical(const Gauge& g, float length, const std::string& what) {
    Check(Pixels(g.window, "width") == 48 && Pixels(g.window, "height") == length && Pixels(g.window, "minWidth") == 48,
        what + ": the window 48 px wide, " + std::to_string(length) + " px long, its minimum width 48 px");
    Check(EnumStyle(g.main, "flexDirection") == 0 && Pixels(g.main, "paddingLeft") == 0 && Pixels(g.main, "paddingTop") == 6 &&
        Pixels(g.main, "paddingBottom") == 4 && Pixels(g.main, "minWidth") == 0, what + ": ctr_main a column");
    Check(EnumStyle(g.box, "flexDirection") == 0 && Pixels(g.bar, "marginLeft") == 0 && Pixels(g.bar, "marginTop") == 4,
        what + ": the icon above the bar");
    Check(EnumStyle(g.level, "flexDirection") == 0 && Percent(g.level, "width") == 100 && Pixels(g.level, "marginLeft") == 0 &&
        Pixels(g.level, "marginTop") == 4 && Pixels(g.value, "marginLeft") == 0, what + ": « NIV. » above the level");
    Check(EnumStyle(g.omega, "flexDirection") == 0 && Percent(g.omega, "width") == 100 && Pixels(g.omega, "marginLeft") == 0 &&
        Pixels(g.omega, "marginTop") == 4 && Pixels(g.omegaBox, "marginLeft") == 0 && Pixels(g.omegaBox, "marginTop") == 10,
        what + ": the Omega icon above its badge");
    Check(Pixels(g.button, "height") == 24 && Pixels(g.button, "marginLeft") == 0 && Pixels(g.button, "marginTop") == 2,
        what + ": the « … » button 24 px high");
    Check(Pixels(g.window, "minHeight") == 160 && Pixels(g.window, "maxHeight") == screenHeight - 16,
        what + ": the game's resizes between 160 px and the screen's height less 16 px");
    const auto& shown = Get(g.window).handles;
    Check(Get(g.bar).orientation == 1 && !Flag(g.window, 0x354) && !Flag(g.window, 0x355) && !Flag(g.window, 0x356) &&
        shown == std::array<bool, 8>{false, false, false, false, false, false, true, true},
        what + ": the game's vertical bar, its resize flags off, only its top and bottom handles");
    for (auto e : {g.container, g.content, g.icon, g.title, g.badge, g.omegaValue, g.inner, g.omegaIcon})
        Check(Get(e).styles.empty(), what + ": nothing else written");
}

// ---------------------------------------------------------------- the tests
// Installation: every class, method and field as pinned, each method verified
// at its RVA (every RVA guarded), the menu's builder hooked last.
void InstallResolvesEveryContract() {
    Fixture f;
    Check(LoggedLine("VERTICAL_GAUGES_READY") && Logged("VERTICAL_GAUGES_DISABLED") == 0, "Installed and ready");
    Check(hooks == 3 && hookRva == 0x2815820 && sectionHook != nullptr && positionHook != nullptr && dimensionHook != nullptr,
        "Three detours: the layout's two saves, then CreateGenericGaugeSection");
    const std::set<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"WidgetProgressGaugeUi", "CreateGenericGaugeSection", 1, 0x2815820},
        {"WidgetProgressGaugeBinding", "get_widgetProgressGauge", 0, 0x154DFD0}, {"WidgetProgressGaugeBinding", "get_ctr_main", 0, 0x154D9A0},
        {"WidgetProgressGaugeBinding", "get_gaugeContainer", 0, 0x154DA50}, {"WidgetProgressGaugeBinding", "get_gauge", 0, 0x154DB00},
        {"WidgetProgressGaugeBinding", "get_level", 0, 0x154DE70}, {"WidgetProgressGaugeBinding", "get_lbl_value", 0, 0x154DDC0},
        {"WidgetProgressGaugeBinding", "get_omega", 0, 0x154DF20}, {"WidgetProgressGaugeBinding", "get_lbl_omegaValue", 0, 0x154DC60},
        {"WidgetProgressGaugeBinding", "get_btn_settings", 0, 0x154D8F0},
        {"BaseProgressBar`2", "set_gaugeOrientation", 1, 0x3C94C70}, {"BaseProgressBar`2", "get_gaugeOrientation", 0, 0x3925FB0},
        {"ResizableWindow", "set_verticalResizable", 1, 0x15BFCF0}, {"ResizableWindow", "set_horizontalResizable", 1, 0x15BFC40},
        {"ResizableWindow", "set_cornerResizable", 1, 0x15BF930}, {"ResizableWindow", "EnableResizeHandler", 1, 0x15BCB60},
        {"ResizableWindow", "DisableResizeHandler", 1, 0x15BC990},
        {"DofusContextualMenu", "AppendCheckboxItem", 9, 0x16D63D0}, {"DofusContextualMenu", "RemoveItemAt", 1, 0x16D8BF0},
        {"DofusContextualMenu", "InsertItem", 2, 0x16D8850}, {"DofusContextualMenu", "get_menuItems", 0, 0x68D2B0},
        {"ContextMenuItem", "get_m_callback", 0, 0x16CF040}, {"ContextMenuItem", "get_disabled", 0, 0x16CF020},
        {"ContextMenuItemView", "get_itemData", 0, 0x157EFD0}, {"DofusContextualMenuRoot", "get_menuData", 0, 0x157EFE0},
        {"DofusContextualMenuRoot", "RemoveMenu", 1, 0x16D2AB0},
        {"VisualElement", "get_parent", 0, 0x55F7180}, {"VisualElement", "get_panel", 0, 0x55F7170}, {"VisualElement", "get_layout", 0, 0x55F6930},
        {"VisualElement", "get_name", 0, 0x3158710}, {"VisualElement", "get_style", 0, 0x55F74A0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_width", 0, 0x55D2740}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_width", 1, 0x55D63F0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_height", 0, 0x55D1540}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_height", 1, 0x55D3D60},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x55D1850}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x55D4410},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_minHeight", 0, 0x55D1810},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_minHeight", 1, 0x55D4370},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_maxHeight", 0, 0x55D1790},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_maxHeight", 1, 0x55D4230},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_flexDirection", 0, 0x55D13C0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_flexDirection", 1, 0x55D3A80},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingLeft", 0, 0x55D1970},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingLeft", 1, 0x55D46D0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingTop", 0, 0x55D19F0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingTop", 1, 0x55D4810},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_paddingBottom", 0, 0x55D1930},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_paddingBottom", 1, 0x55D4630},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_marginLeft", 0, 0x55D16D0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_marginLeft", 1, 0x55D4050},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_marginTop", 0, 0x55D1750},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_marginTop", 1, 0x55D4190},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_left", 0, 0x55D1610}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_left", 1, 0x55D3EB0},
        {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_top", 0, 0x55D1CD0}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_top", 1, 0x55D4F70},
        {"Length", ".ctor", 2, 0x561D3D0}, {"StyleLength", "set_value", 1, 0x5623940},
        {"CachedDataService", "get_uiPresetOptionsContainer", 0, 0x7E0890}, {"UiPresetOptionsContainer", "LoadUiDimensionById", 1, 0x20918C0},
        {"UiPresetOptionsContainer", "LoadUiPositionById", 1, 0x2091B60}, {"UiPresetOptionsContainer", "SaveUiPositionById", 4, 0x2092DE0},
        {"UiPresetOptionsContainer", "SaveUiDimensionById", 2, 0x2092A80}};
    Check(contracts == expected, "Every native method verified at its pinned RVA, and nothing else");
    Check(fieldsChecked == std::set<std::string>{"WidgetProgressGaugeUi.m_gaugeType", "gfc.dyfh", "ResizableWindow.<content>k__BackingField",
        "ResizableWindow.m_verticalResizable", "ResizableWindow.m_horizontalResizable", "ResizableWindow.m_cornerResizable",
        "ResizableWindow.m_isUILocked", "ResizableWindow.m_disableTopHandle", "ResizableWindow.m_disableBottomHandle",
        "ResizableWindow.m_dragPosition", "DofusContextualMenu.dofusContextualMenuRoot", "UICoordinates.x", "UICoordinates.y"},
        "Every field read at a checked offset");
    // The layout's saves, with no vertical gauge: the game's own, as they were.
    const auto g = MakeGauge(0x10);
    LayoutPass();
    GameDrag(g, 10, 0);
    GameSaveDimension(g, 600, 34);
    Check(positionSaves == 1 && dimensionSaves == 1 && savedWidths.at("GaugePods") == 600 && savedPositions.contains("GaugePods"),
        "No vertical gauge: the game's saves untouched");
    // Nothing asked, nothing done: no lookup, no enumeration, no call.
    const auto before = calls + nameCalls;
    for (int i = 0; i < 5; ++i) { Tick(); Frame(); }
    Check(calls + nameCalls == before && lookups == 0 && !VerticalGaugesCleanupPending(), "No gauge ticked: no call to the game");
}
// A contract that does not match turns this feature off alone, logged once:
// no detour, no row, no call from the pump, the frame or Stop.
void MissingContractTurnsOnlyThisOff() {
    for (int failure = 0; failure < 10; ++failure) {
        Fixture f(false);
        if (failure == 8) failVerified = "SaveUiDimensionById";
        if (failure == 9) failField = "ResizableWindow.m_isUILocked";
        if (failure == 0) failVerified = "set_gaugeOrientation"; // an adapter as the inflated method's pointer
        if (failure == 1) failVerified = "LoadUiDimensionById";
        if (failure == 2) failVerified = "get_btn_settings";
        if (failure == 3) failField = "gfc.dyfh";
        if (failure == 4) fieldInfos["ResizableWindow.m_cornerResizable"].offset = 0x357;
        if (failure == 5) barBaseName = "BaseProgressBarLong";
        if (failure == 6) failVerified = "LoadUiPositionById";
        if (failure == 7) failField = "UICoordinates.y";
        InstallVerticalGauges();
        fieldInfos["ResizableWindow.m_cornerResizable"].offset = 0x356;
        Check(Logged("VERTICAL_GAUGES_DISABLED ") == 1 && !LoggedLine("VERTICAL_GAUGES_READY") && hooks == 0 && sectionHook == nullptr,
            "A missing contract disables the feature, once, without its detour");
        module->settings.gaugeVertical = 0x1;
        const auto g = MakeGauge(0x1);
        LayoutPass();
        const auto snapshot = Take(g);
        const auto before = calls + nameCalls;
        Tick(); Frame(); RestoreVerticalGauges();
        Check(calls + nameCalls == before && lookups == 0 && Take(g) == snapshot && !VerticalGaugesCleanupPending(),
            "Without its contracts the feature makes no call at all");
        Check(!HandleVerticalGaugeMenuCallback(Make("ClickCallbackHandler"), Make("ContextMenuItemView")), "No row to recognise");
    }
}
// The row: after the gauge's own rows, right before the separator opening
// « Gestion des jauges »; the game's rows kept, in their order.
void RowPlacement() {
    Fixture f;
    const auto character = MakeGauge(0x1), job = MakeGauge(0x40);
    LayoutPass();
    auto menu = OpenMenu(character);
    auto row = OwnedRow(menu);
    Check(nativeSections == 1 && row != nullptr, "The game's section built once, the row added");
    Check(Texts(menu) == std::vector<std::string>{"GaugeCharacter", "Limiter le pourcentage d'expérience obtenue (100%)", "Couleur de jauge",
        "Masquer le module", "Afficher à la verticale", "---", "Gestion des jauges", "Expérience de personnage", "Expérience de guilde",
        "Points d'honneur", "Pods", "Points d'énergie", "Expérience de métier"},
        "After « Masquer le module », before the separator of « Gestion des jauges »");
    const auto& r = Get(row);
    Check(r.help == Help && r.icon == 0 && r.close && !r.disabled && r.delay == 1000 && !r.isOn && Get(r.refs.at("root")).klass->name ==
        "DofusContextualMenu" && r.refs.at("root") == menu && Get(r.refs.at("callback")).klass->name == "ClickCallbackHandler",
        "A native checkbox: its help after a second, no icon, closing the menu, unticked for a horizontal gauge");
    Check(verticalGauges::s.row && verticalGauges::s.row->armed && verticalGauges::s.row->item.get() == row, "The row armed");
    // A ticked gauge's row is ticked; a job's menu has no « Limiter… ».
    module->settings.gaugeVertical = 0x40;
    menu = OpenMenu(job);
    row = OwnedRow(menu);
    Check(Get(row).isOn && IndexOf(menu, row) == 3 && SeparatorIndex(menu) == 4, "A job: after its own three rows, ticked");
    // Without « Masquer le module »: after « Couleur de jauge ».
    hideRow = false;
    menu = OpenMenu(character);
    row = OwnedRow(menu);
    Check(Texts(menu).at(2) == "Couleur de jauge" && IndexOf(menu, row) == 3 && SeparatorIndex(menu) == 4, "Without « Masquer le module »");
    // Without a separator: last.
    separatorRow = false;
    menu = OpenMenu(character);
    row = OwnedRow(menu);
    Check(IndexOf(menu, row) == static_cast<int>(Rows(menu).size()) - 1 && SeparatorIndex(menu) == -1, "Without a separator, the row is last");
    Check(nativeSections == 4 && nativeCallbacks == 0, "The game's section once per menu");
}
// No row unless asked and possible: the options' switch, a module not ready,
// a gauge of no known type, another kind of menu or widget, a skipped type;
// the game's rows untouched and its own section called once, even when adding
// the row fails.
void NoRowUnlessWanted() {
    Fixture f;
    const auto g = MakeGauge(0x1);
    LayoutPass();
    // The game's rows: twelve for the character (« Limiter… » among them), eleven for any other.
    const auto expectNone = [&](const std::string& what, const std::string& menuType = "DofusContextualMenu") {
        const auto sections = nativeSections;
        int type = 0;
        std::memcpy(&type, Get(g.widget).fields + 0x184 - 0x10, sizeof(type));
        auto menu = OpenMenu(g, true, menuType);
        Check(OwnedRow(menu) == nullptr && nativeSections == sections + 1 && Rows(menu).size() == (type == 1 ? 12u : 11u),
            what + ": no row, the game's menu as it is");
    };
    module->settings.gaugeVerticalMenu = false;
    expectNone("The options' switch unticked");
    module->settings.gaugeVerticalMenu = true;
    for (int state = 0; state < 4; ++state) {
        if (state == 0) module->stopped = true;
        if (state == 1) module->stopRequested = true;
        if (state == 2) module->servicesReady = false;
        if (state == 3) module->accountOptionsUnbound = true;
        expectNone("A module not ready");
        module->stopped = false; module->stopRequested = false; module->servicesReady = true; module->accountOptionsUnbound = false;
    }
    for (int type : {0, 4, 3, -1, 0x4000000}) {
        Put<int>(g.widget, 0x184, type);
        expectNone("A gauge of no known type");
    }
    Put<int>(g.widget, 0x184, 1);
    expectNone("Another kind of menu", "OtherContextualMenu");
    // The row failing: the game's section still once, the menu the game's.
    failOwner = nullptr;
    failMethod = "AppendCheckboxItem";
    auto menu = NewMenu();
    failOwner = menu;
    failCount = 1;
    const auto sections = nativeSections;
    sectionHook(g.widget, menu, nullptr);
    Check(OwnedRow(menu) == nullptr && Rows(menu).size() == 12 && nativeSections == sections + 1 &&
        Logged("VERTICAL_GAUGES_MENU_ERROR ") == 1 && !verticalGauges::s.row, "A failure adding the row leaves the game's menu, logged");
    // A skipped type (its widget unreadable): no row.
    verticalGauges::s.skipped = 0x1;
    expectNone("A gauge left horizontal for the session");
}
// A click: the bit toggled, saved once, the gauge vertical at once (before the
// pump); the menu closed by the game (closeOnSelect); then back.
void ClickTogglesAndApplies() {
    Fixture f;
    const auto g = MakeGauge(0x1);
    LayoutPass();
    const auto horizontal = Take(g);
    auto menu = OpenMenu(g);
    Check(ClickRow(menu, OwnedRow(menu)), "The row's handler claims its click");
    Check(changes == 1 && module->settings.gaugeVertical == 0x1 && LoggedLine("VERTICAL_GAUGE_SET type=0x1 on=1") &&
        LoggedLine("VERTICAL_GAUGE_ON type=0x1 length=566"), "Ticked: saved once, vertical at once");
    CheckVertical(g, 566, "Ticked");
    Check(!Get(RootOf(menu)).panel && nativeCallbacks == 0, "The game closed the menu; nothing reached the game's action");
    // Ticked again from its menu: back at once.
    menu = OpenMenu(g);
    Check(Get(OwnedRow(menu)).isOn, "The row shows the gauge vertical");
    Check(ClickRow(menu, OwnedRow(menu)), "Claimed");
    Check(changes == 2 && module->settings.gaugeVertical == 0 && LoggedLine("VERTICAL_GAUGE_SET type=0x1 on=0") &&
        LoggedLine("VERTICAL_GAUGE_OFF type=0x1") && Take(g) == horizontal && Records() == 0, "Unticked: exactly as the game built it");
    // A save that fails still applies the choice the module keeps.
    changeFails = true;
    menu = OpenMenu(g);
    ClickRow(menu, OwnedRow(menu));
    Check(module->settings.gaugeVertical == 0x1 && Logged("VERTICAL_GAUGES_SAVE_ERROR ") == 1 && Records() == 1, "A failed save is logged");
}
// Stale clicks are consumed without effect; a foreign handler is not the row's.
void StaleClicksDoNothing() {
    for (int stale = 0; stale < 9; ++stale) {
        Fixture f;
        const auto g = MakeGauge(0x1);
        LayoutPass();
        const auto horizontal = Take(g);
        auto menu = OpenMenu(g);
        auto row = OwnedRow(menu);
        auto view = ViewOf(menu, row);
        auto callback = Get(row).refs.at("callback");
        if (stale == 0) Get(view).panel = nullptr;                                  // its view detached
        if (stale == 1) view = ViewOf(menu, Rows(menu).at(2));                      // another row's view
        if (stale == 2) Get(row).disabled = true;                                   // disabled
        if (stale == 3) Rows(menu).erase(Rows(menu).begin() + IndexOf(menu, row));  // removed from the menu
        if (stale == 4) Put<int>(g.widget, 0x184, 0x2);                             // the widget another gauge now
        if (stale == 5) module->cached.reset(Make("CachedDataService"));            // another session
        if (stale == 6) module->settings.gaugeVerticalMenu = false;                 // the options' switch unticked meanwhile
        if (stale == 7) module->stopRequested = true;                               // stopping
        if (stale == 8) {                                                           // clicked twice
            Check(HandleVerticalGaugeMenuCallback(callback, view), "First click");
            Check(changes == 1 && Records() == 1, "The first click counts");
        }
        const auto saved = changes;
        Check(HandleVerticalGaugeMenuCallback(callback, view), "A stale click on the row is still the row's");
        Check(changes == saved && (stale == 8 ? Records() == 1 : Take(g) == horizontal && Records() == 0), "A stale click changes nothing");
        Check(!HandleVerticalGaugeMenuCallback(Make("ClickCallbackHandler"), view) && !HandleVerticalGaugeMenuCallback(nullptr, view),
            "A foreign handler is not the row's");
    }
}
// A gauge ticked turns vertical at the next pump. Its length is its
// horizontal width: the size the game wrote, else its saved size for the
// current layout, else the one laid out; at least 160 px, at most the
// screen's height less 16 px.
void LengthFromItsHorizontalWidth() {
    struct Case { const char* what; float written, saved, uxml, length; };
    const Case cases[] = {
        {"The size the game wrote", 707, 800, 640, 707},
        {"Its saved size", -1, 800, 640, 800},
        {"The size laid out", -1, -1, 640, 640},
        {"At most the screen's height less 16 px", 2000, -1, 566, 1064},
        {"At least 160 px", 100, -1, 566, 160}};
    for (const auto& c : cases) {
        Fixture f;
        module->settings.gaugeVertical = 0x10;
        const auto g = MakeGauge(0x10);
        if (c.written > 0) { GameWrite(g.window, "width", c.written); GameWrite(g.window, "height", 35); }
        if (c.saved > 0) savedWidths["GaugePods"] = c.saved;
        Get(g.window).uxmlWidth = c.uxml;
        LayoutPass();
        Tick();
        Check(Records() == 1 && LoggedLine("VERTICAL_GAUGE_ON type=0x10 length=" + std::to_string(static_cast<int>(c.length))),
            std::string(c.what) + ": vertical at the next pump");
        CheckVertical(g, c.length, c.what);
    }
}
// Its top-left corner stays, moved only as much as it needs to lie on the
// screen; a hidden gauge waits to be shown. Unticked, it comes back where the
// game had it, kept on the screen horizontally too.
void KeptOnTheScreen() {
    Fixture f;
    module->settings.gaugeVertical = 0x1 | 0x20;
    const auto character = MakeGauge(0x1), energy = MakeGauge(0x20);
    // The game's default place of the character's gauge: centred at the bottom, 707 x 35.
    GameWrite(character.window, "left", 606); GameWrite(character.window, "top", 1045);
    GameWrite(character.window, "width", 707); GameWrite(character.window, "height", 35);
    GameWrite(energy.window, "left", 100); GameWrite(energy.window, "top", 50);
    Get(energy.window).hidden = true;
    LayoutPass();
    const auto horizontal = Take(character);
    Tick();
    Check(Pixels(character.window, "left") == 606 && Pixels(character.window, "top") == 1080 - 707,
        "Moved up as much as needed, its left side kept");
    Check(!RecordOf(energy) && Pixels(energy.window, "left") == 100 && Pixels(energy.window, "top") == 50 && Get(energy.window).styles.size() == 2,
        "A hidden gauge waits to be shown");
    // Shown at the bottom right (its place written by the game): vertical and kept on the screen at the next pump.
    GameWrite(energy.window, "left", 1900); GameWrite(energy.window, "top", 900);
    Get(energy.window).hidden = false;
    LayoutPass();
    Tick();
    Check(Pixels(energy.window, "left") == 1920 - 48 && Pixels(energy.window, "top") == 1080 - 566 && RecordOf(energy) &&
        !RecordOf(energy)->settlePending, "Shown: vertical, kept on the screen");
    module->settings.gaugeVertical = 0;
    Tick();
    Check(Take(character) == horizontal && Records() == 0, "Unticked: back where the game had it");
    Check(Pixels(energy.window, "left") == 1920 - 566 && Pixels(energy.window, "top") == 900 && !Styled(energy.window, "width"),
        "The game's own place, kept on the screen at its UXML width");
}
// Unticked, the gauge is exactly as the game built it: every code-set value
// (keyword Null: none), its resize flags and handles, its bar. A value the
// game wrote meanwhile stays the game's; dragged while vertical, it comes back
// where the game had it all the same, the game's layout untouched.
void RoundTripIsExact() {
    Fixture f;
    const auto g = MakeGauge(0x8);
    GameWrite(g.window, "left", 300); GameWrite(g.window, "top", 200); GameWrite(g.window, "width", 600); GameWrite(g.window, "height", 40);
    GameWrite(g.main, "paddingTop", 2); // the game's own value on an element the layout changes
    LayoutPass();
    const auto horizontal = Take(g);
    module->settings.gaugeVertical = 0x8;
    Tick();
    CheckVertical(g, 600, "Honour");
    module->settings.gaugeVertical = 0;
    Tick();
    Check(Take(g) == horizontal && LoggedLine("VERTICAL_GAUGE_OFF type=0x8"), "Every value, flag and the bar exactly as before");
    module->settings.gaugeVertical = 0x8;
    Tick();
    LayoutPass();
    GameWrite(g.bar, "marginTop", 7);                                    // the game's own write on a value the mod owns
    GameDrag(g, 1200, -100);                                             // dragged while vertical
    module->settings.gaugeVertical = 0;
    Tick();
    Check(Pixels(g.bar, "marginTop") == 7, "A value the game wrote since stays the game's");
    Check(Pixels(g.window, "left") == 300 && Pixels(g.window, "top") == 200 && positionSaves == 0 && savedPositions.empty(),
        "Dragged while vertical: back where the game had it, the drag never saved in the game's layout");
    Check(Pixels(g.window, "width") == 600 && Pixels(g.window, "height") == 40 && Pixels(g.main, "paddingTop") == 2 && !Styled(g.main, "minWidth") &&
        Get(g.bar).orientation == 0 && Flag(g.window, 0x354) && !Flag(g.window, 0x355) && Flag(g.window, 0x356), "The rest as before");
}
// The game re-places a vertical gauge (the never-moved character's gauge gets
// its default place at each re-placement: a layout preset, a fight's layout,
// the screen's size; a reset of the positions): the next frame writes the
// vertical size again before the layout, the game's new size becomes the one
// to put back and its width the length, and the gauge is kept on the screen
// at once, from the place the game just wrote (at the bottom), hidden or not:
// no layout ever shows it below the screen. A frame costs one call per
// vertical gauge, none without.
void GameReplacementIsUndone() {
    Fixture f;
    const auto g = MakeGauge(0x1), pods = MakeGauge(0x10);
    LayoutPass();
    GameReplace(g); GameReplace(pods); // their first placement
    LayoutPass();
    Check(Get(g.window).x == 607 && Get(g.window).y == 1046 && Get(g.window).width == 707, "The character's default place, centred at the bottom");
    const auto idle = calls + nameCalls;
    for (int i = 0; i < 10; ++i) Frame();
    Check(calls + nameCalls == idle, "No vertical gauge: no call in a frame");
    module->settings.gaugeVertical = 0x1 | 0x10;
    Tick();
    LayoutPass();
    Check(Records() == 2 && RecordOf(pods) && Get(g.window).y == 1080 - 707, "Both vertical, the character's gauge on the screen");
    const auto quiet = calls + nameCalls;
    for (int i = 0; i < 10; ++i) Frame();
    Check(calls + nameCalls == quiet + 80, "Each frame: four calls per vertical gauge, its width, height, left and top read back");
    const auto onScreen = [&g] {
        const auto& w = Get(g.window);
        return w.x >= 0 && w.y >= 0 && w.x + w.width <= 1920 && w.y + w.height <= 1080;
    };
    GameReplace(g); GameReplace(pods); // a re-placement: the resize flags off, no saved place, blxg's default
    Check(Pixels(g.window, "top") == 1046 && Pixels(g.window, "width") == 707, "The game's default written");
    Frame();
    Check(Pixels(g.window, "width") == 48 && Pixels(g.window, "height") == 707 && LoggedLine("VERTICAL_GAUGE_REPLACED type=0x1 width=707") &&
        Pixels(g.window, "top") == 1080 - 707 && Pixels(g.window, "left") == 607 && !RecordOf(g)->settlePending,
        "The vertical size again before the next layout, as long as the game's new width, kept on the screen at once");
    LayoutPass();
    Check(onScreen(), "Laid out on the screen: never below it");
    // Hidden meanwhile (display none): placed by the game all the same, kept on the screen at once.
    Get(g.window).hidden = true;
    LayoutPass();
    GameReplace(g);
    Frame();
    Check(Pixels(g.window, "top") == 1080 - 707 && !RecordOf(g)->settlePending, "Hidden: kept on the screen from the place the game wrote");
    Get(g.window).hidden = false;
    LayoutPass();
    Check(onScreen() && Get(g.window).y == 1080 - 707, "Shown again: on the screen at once");
    Tick();
    module->settings.gaugeVertical = 0x10;
    Tick();
    // Its 707 x 35 box kept on the screen: 1 px up (the game's own check uses blxf's 706 x 34 box).
    Check(Pixels(g.window, "width") == 707 && Pixels(g.window, "height") == 35 && Pixels(g.window, "left") == 607 &&
        Pixels(g.window, "top") == 1080 - 35 && !Styled(g.window, "minWidth") && Records() == 1, "Unticked: the game's latest size and place");
}
// A layout round trip (a fight with its own layout, a layout preset): with
// the resize flags off, the game's placements no longer give the gauge its
// saved size there; the gauge follows that size all the same, as long as the
// game shows it horizontal there, and gives it back when unticked, as the
// game's next placement would; a layout without one keeps the size the game
// wrote (the character's default).
void FollowsTheLayoutsSavedSize() {
    const auto main = [] {
        savedWidths = {{"GaugeCharacter", 300.f}};
        savedPositions = {{"GaugeCharacter", {100, 300, false, false}}};
    };
    {
        Fixture f;
        main();
        const auto g = MakeGauge(0x1);
        LayoutPass(); GameReplace(g); LayoutPass();
        // The game's own: 300 px wide in the main layout, its default in an empty fight layout, 300 px again.
        savedWidths.clear(); savedPositions.clear();
        GameReplace(g); LayoutPass();
        Check(Get(g.window).width == 707, "The game alone: its default in the fight's layout");
        main();
        GameReplace(g); LayoutPass();
        Check(Get(g.window).width == 300 && Get(g.window).x == 192 && Get(g.window).y == 324, "The game alone: 300 px again after the fight");
    }
    {
        Fixture f;
        main();
        const auto g = MakeGauge(0x1);
        LayoutPass(); GameReplace(g); LayoutPass();
        module->settings.gaugeVertical = 0x1;
        Tick(); LayoutPass();
        CheckVertical(g, 300, "Main layout");
        savedWidths.clear(); savedPositions.clear(); // the fight's layout: nothing saved for this gauge
        GameReplace(g); Frame(); LayoutPass(); Tick(); LayoutPass();
        Check(Pixels(g.window, "height") == 707, "The fight's layout: as long as the game's default there");
        main();                                      // the fight over: the main layout again
        GameReplace(g); Frame(); LayoutPass(); Tick(); LayoutPass();
        Check(Pixels(g.window, "height") == 300 && Get(g.window).y + Get(g.window).height <= 1080, "Main layout again: 300 px long again");
        module->settings.gaugeVertical = 0;
        Tick(); LayoutPass();
        Check(Get(g.window).width == 300 && Get(g.window).height == 35 && Records() == 0, "Unticked after the fight: its saved 300 px");
    }
    {
        Fixture f;
        main();
        const auto g = MakeGauge(0x1);
        LayoutPass(); GameReplace(g); LayoutPass();
        module->settings.gaugeVertical = 0x1;
        Tick(); LayoutPass();
        savedWidths["GaugeCharacter"] = 500;        // the fight's layout: a size of its own
        GameReplace(g); Frame(); LayoutPass(); Tick(); LayoutPass();
        Check(Pixels(g.window, "height") == 500, "The fight's layout: as long as its saved size there");
        module->settings.gaugeVertical = 0;          // unticked during the fight
        Tick(); LayoutPass();
        Check(Get(g.window).width == 500 && Get(g.window).height == 35, "Unticked during the fight: the fight's saved size");
    }
}
// Hidden (« Masquer le module »: display none) during a re-placement, a
// vertical gauge is placed by the game at the horizontal box its hub still
// keeps (refreshed only from a shown layout): near the right edge, that box
// is kept on the screen farther left. Before it is shown again, it goes where
// the game's next re-placement would put the vertical box. A re-placement
// that found it shown since keeps the vertical box: nothing to do.
void HiddenDuringAReplacement() {
    for (bool savedSize : {false, true}) {
        Fixture f;
        savedPositions["GaugePods"] = {975, 250, false, false}; // left vertical at 1872 x 270
        if (savedSize) savedWidths["GaugePods"] = 700;          // the hub's box: the UXML's, laid out before blxi
        module->settings.gaugeVertical = 0x10;
        const auto g = MakeGauge(0x10);
        LayoutPass(); GameReplace(g); LayoutPass();
        Check(Pixels(g.window, "left") == 1920 - (savedSize ? 700 : 566), "Placed horizontal by the game, on the screen");
        Tick(); LayoutPass();
        Check(Get(g.window).x == 1872 && Get(g.window).y == 270, "Vertical where it was left");
        Get(g.window).hidden = true;
        LayoutPass();
        GameReplace(g);                                         // a fight's layout, a preset: re-placed while hidden
        Check(Pixels(g.window, "left") == 1920 - 566, "The game placed it at the horizontal box its hub kept");
        Frame(); Tick();
        Check(Pixels(g.window, "left") == 1872 && Pixels(g.window, "top") == 270, "Back where the game's next re-placement puts it");
        Get(g.window).hidden = false;
        LayoutPass();
        Check(Get(g.window).x == 1872 && Get(g.window).y == 270 && Get(g.window).width == 48, "Shown again at its place");
        GameReplace(g); Frame(); LayoutPass();                  // the game's next re-placement, shown: the same place
        Check(Get(g.window).x == 1872 && Get(g.window).y == 270, "The game's next re-placement keeps it there");
        // Re-placed shown, then hidden and re-placed: the hub keeps the vertical box; nothing written.
        Get(g.window).hidden = true;
        LayoutPass();
        GameReplace(g);
        const auto vertical = Take(g);
        Frame(); Tick();
        Check(Take(g) == vertical && Pixels(g.window, "left") == 1872, "The vertical box kept by the hub: its place untouched");
        module->settings.gaugeVertical = 0;
        Tick();
        Check(Records() == 0 && Pixels(g.window, "left") == 1920 - (savedSize ? 700 : 566), "Unticked: the horizontal box on the screen");
    }
}
// The hub's gauges: a new one ticked turns vertical at the next pump; one the
// game discarded (gone from its hub, or a new hub) is forgotten without a
// call; one detached but still held keeps its layout, never applied twice;
// with the account unbound a frame does nothing.
void FollowsTheHubsGauges() {
    Fixture f;
    module->settings.gaugeVertical = 0x1 | 0x40;
    const auto character = MakeGauge(0x1);
    LayoutPass();
    Tick();
    Check(Records() == 1 && RecordOf(character), "The shown gauge vertical");
    const auto job = MakeGauge(0x40); // shown later (the jobs panel's button)
    Tick();
    Check(!RecordOf(job) && Get(job.window).styles.empty() && Get(job.bar).orientation == 0,
        "Not laid out yet: left to the game's first placement");
    LayoutPass();
    Tick();
    Check(Records() == 2 && RecordOf(job) && Get(job.bar).orientation == 1, "A new gauge vertical at the next pump once laid out");
    Detach(job.container);
    const auto detached = Take(job);
    Tick(); Tick();
    Check(RecordOf(job) && Take(job) == detached, "A detached gauge still held keeps its record, never applied twice");
    auto& values = Get(Dictionary(hub)).items;
    values.erase(std::remove(values.begin(), values.end(), job.widget), values.end());
    std::map<Obj, unsigned> seen;
    for (auto e : job.Elements()) seen[e] = callsOn[e];
    const auto widgetCalls = callsOn[job.widget];
    Tick();
    for (auto e : job.Elements()) Check(callsOn[e] == seen[e], "No call on a discarded gauge");
    Check(callsOn[job.widget] == widgetCalls && !RecordOf(job) && LoggedLine("VERTICAL_GAUGE_GONE type=0x40"), "Forgotten untouched");
    // A new hub (the game's interface built again): the old gauge forgotten
    // untouched, the new one vertical.
    hub = MakeHub();
    const auto again = MakeGauge(0x1);
    LayoutPass();
    Tick();
    Check(Records() == 1 && RecordOf(again) && !RecordOf(character) && Get(character.bar).orientation == 1 && Get(again.bar).orientation == 1,
        "The new hub's gauge vertical, the old one untouched");
    module->accountOptionsUnbound = true;
    const auto before = calls + nameCalls;
    Frame();
    Check(calls + nameCalls == before, "No call in a frame while the account is unbound");
    module->accountOptionsUnbound = false;
    Check(enumerations > 0 && disposals == enumerations && lookups == enumerations, "One enumeration of the hub's gauges a pump, each disposed");
}
// Hidden and shown again (« Masquer le module », « Gestion des jauges », the
// jobs panel's button, the XP button): only its display changes, the gauge
// stays vertical without a write.
void StaysVerticalAcrossShowAndHide() {
    Fixture f;
    module->settings.gaugeVertical = 0x10;
    const auto g = MakeGauge(0x10);
    LayoutPass();
    Tick();
    LayoutPass();
    const auto vertical = Take(g);
    for (bool hidden : {true, false, true, false}) {
        Get(g.window).hidden = hidden;
        LayoutPass();
        Tick(); Frame();
        Check(Take(g) == vertical && Records() == 1, "Hidden or shown, the same vertical gauge");
    }
}
// The options' switch unticked: every gauge back as the game built it at the
// next pump, each one's choice kept, the row gone from the menus (an open one
// closed: no stale box); ticked again: the same gauges vertical.
void OptionsSwitch() {
    Fixture f;
    module->settings.gaugeVertical = 0x1 | 0x10;
    const auto character = MakeGauge(0x1), pods = MakeGauge(0x10);
    LayoutPass();
    const auto horizontalCharacter = Take(character), horizontalPods = Take(pods);
    Tick();
    Check(Records() == 2, "Both vertical");
    auto menu = OpenMenu(pods);
    Check(OwnedRow(menu) && Get(OwnedRow(menu)).isOn, "The row ticked");
    module->settings.gaugeVerticalMenu = false;
    Tick();
    Check(Take(character) == horizontalCharacter && Take(pods) == horizontalPods && Records() == 0 &&
        module->settings.gaugeVertical == (0x1 | 0x10), "Unticked in the options: every gauge horizontal, the choices kept");
    Check(!Get(RootOf(menu)).panel && removeMenus == 1 && !verticalGauges::s.row, "The open menu closed");
    Check(!OwnedRow(OpenMenu(pods)), "No row while unticked");
    module->settings.gaugeVerticalMenu = true;
    Tick();
    Check(Records() == 2 && changes == 0, "Ticked again: the same gauges vertical");
    CheckVertical(pods, 566, "Again");
}
// Stop: every gauge back and the row's menu closed, before the menu detour
// stops recognising it; a failure keeps it pending, tried again (at most four
// times a second) until it works, or abandoned after twenty attempts.
void StopRestoresEverything() {
    {
        Fixture f;
        module->settings.gaugeVertical = 0x1 | 0x2;
        const auto character = MakeGauge(0x1), guild = MakeGauge(0x2);
        LayoutPass();
        const auto c = Take(character), g = Take(guild);
        Tick();
        auto menu = OpenMenu(guild);
        const auto row = OwnedRow(menu);
        module->stopped = true;
        RestoreVerticalGauges();
        Check(Take(character) == c && Take(guild) == g && !VerticalGaugesCleanupPending() && !Get(RootOf(menu)).panel,
            "Stop: every gauge back, the menu closed");
        Check(!HandleVerticalGaugeMenuCallback(Get(row).refs.at("callback"), ViewOf(menu, row)), "The row forgotten");
        Tick(); Frame();
        Check(Records() == 0, "Nothing again once stopped");
    }
    {
        Fixture f;
        module->settings.gaugeVertical = 0x1;
        const auto g = MakeGauge(0x1);
        LayoutPass();
        const auto horizontal = Take(g);
        Tick();
        failOwner = g.bar; failMethod = "set_gaugeOrientation"; failCount = 2;
        module->stopped = true;
        RestoreVerticalGauges();
        Check(VerticalGaugesCleanupPending() && failCount == 1, "A failure keeps it pending");
        RestoreVerticalGauges();
        Check(failCount == 1, "Not again within 250 ms");
        nowMs += 250;
        RestoreVerticalGauges();
        Check(failCount == 0 && VerticalGaugesCleanupPending(), "Tried again");
        nowMs += 250;
        RestoreVerticalGauges();
        Check(!VerticalGaugesCleanupPending() && Take(g) == horizontal, "Then back as the game built it");
    }
    {
        Fixture f;
        module->settings.gaugeVertical = 0x1;
        const auto g = MakeGauge(0x1);
        LayoutPass();
        Tick();
        failOwner = g.bar; failMethod = "set_gaugeOrientation"; failCount = -1;
        module->stopped = true;
        for (int attempt = 0; attempt < 19; ++attempt) { RestoreVerticalGauges(); nowMs += 250; }
        Check(VerticalGaugesCleanupPending() && Logged("VERTICAL_GAUGES_RESTORE_ABANDONED") == 0, "Nineteen attempts");
        RestoreVerticalGauges();
        Check(!VerticalGaugesCleanupPending() && LoggedLine("VERTICAL_GAUGES_RESTORE_ABANDONED gauges=1"), "Abandoned after twenty");
        failCount = 0;
    }
}
// The row's menu: forgotten once the game closed it, or when never shown
// within five seconds.
void RowFollowsItsMenu() {
    Fixture f;
    const auto g = MakeGauge(0x20);
    LayoutPass();
    auto menu = OpenMenu(g);
    Tick();
    Check(verticalGauges::s.row && verticalGauges::s.row->seen, "Shown: kept");
    CloseMenu(menu);
    Tick();
    Check(!verticalGauges::s.row && !VerticalGaugesCleanupPending(), "Closed by the game: forgotten");
    OpenMenu(g, false);
    Tick();
    Check(verticalGauges::s.row != nullptr, "Not shown yet: kept a while");
    nowMs += 5000;
    Tick();
    Check(!verticalGauges::s.row, "Never shown within five seconds: forgotten");
}
// A gauge the module cannot read as expected stays horizontal for the
// session, logged once, its row gone; three errors in a row do the same; a
// width that does not read back as written turns the whole feature off,
// everything written put back.
void FailuresStayLocal() {
    // ctr_main elsewhere than added to its window (in the hub's container, or
    // added to the window's content element itself, its parent that element),
    // or a window whose content element is not its own.
    for (int how = 0; how < 3; ++how) {
        Fixture f;
        module->settings.gaugeVertical = 0x1 | 0x10;
        const auto character = MakeGauge(0x1), pods = MakeGauge(0x10);
        if (how < 2) {
            Detach(character.main);
            Add(how == 0 ? character.container : character.content, character.main);
        } else Put<Obj>(character.window, 0x310, character.box);
        LayoutPass();
        const auto horizontal = Take(character);
        Tick(); Tick();
        Check(Logged(std::string("VERTICAL_GAUGE_UNEXPECTED type=0x1 ") + (how < 2 ? "main" : "content")) == 1 &&
            Logged("VERTICAL_GAUGE_UNEXPECTED") == 1 && Take(character) == horizontal && RecordOf(pods) && Records() == 1,
            "An unexpected widget: horizontal, logged once; the others vertical");
        Check(!OwnedRow(OpenMenu(character)) && OwnedRow(OpenMenu(pods)), "No row for it");
    }
    {
        Fixture f;
        module->settings.gaugeVertical = 0x8;
        const auto g = MakeGauge(0x8);
        LayoutPass();
        const auto horizontal = Take(g);
        failOwner = StyleObject(g.button); failMethod = "UnityEngine.UIElements.IStyle.set_marginTop"; failCount = -1;
        for (int i = 0; i < 3; ++i) {
            Tick();
            Check(Take(g) == horizontal && Records() == 0, "Each failed attempt undone at once");
        }
        Check(Logged("VERTICAL_GAUGES_ERROR apply type=0x8") == 1 && LoggedLine("VERTICAL_GAUGE_FAILED type=0x8 left horizontal for this session"),
            "Three in a row: left horizontal, logged once");
        failCount = 0;
        Tick();
        Check(Records() == 0 && !OwnedRow(OpenMenu(g)), "Not tried again this session, no row");
    }
    {
        Fixture f;
        module->settings.gaugeVertical = 0x2;
        const auto g = MakeGauge(0x2);
        LayoutPass();
        const auto horizontal = Take(g);
        skewReadBack = true;
        Tick();
        Check(LoggedLine("VERTICAL_GAUGES_DISABLED style-layout") && Take(g) == horizontal && Records() == 0,
            "A width that does not read back as written: everything put back, the feature off");
        skewReadBack = false;
        Tick(); Frame();
        Check(Records() == 0 && !OwnedRow(OpenMenu(g)), "Off for the session: no row, nothing applied");
    }
}
// Another screen height (the resolution, the interface scale): the length
// follows from the same horizontal width, the gauge kept on the screen.
void FollowsTheScreensHeight() {
    Fixture f;
    module->settings.gaugeVertical = 0x1;
    const auto g = MakeGauge(0x1);
    GameWrite(g.window, "width", 900);
    LayoutPass();
    Tick();
    Check(Pixels(g.window, "height") == 900 && Pixels(g.window, "top") == 180, "900 px long on 1080, on the screen");
    screenHeight = 720;
    LayoutPass();
    Tick();
    Check(Pixels(g.window, "height") == 704 && Pixels(g.window, "top") == 16, "704 px long on 720, on the screen");
    screenHeight = 1080;
    LayoutPass();
    Tick();
    Check(Pixels(g.window, "height") == 900 && Pixels(g.window, "top") == 180, "900 px long again, at its place again");
    module->settings.gaugeVertical = 0;
    Tick();
    Check(Pixels(g.window, "width") == 900 && !Styled(g.window, "height") && !Styled(g.window, "top"), "Unticked: the game's own size");
}
// Dragged into a corner while vertical, then unticked (its menu, the options'
// switch or Stop): back at the game's own place, here the UXML's (no inline
// place), not at the corner. A place against the screen's top or left edge
// (0 px) the game wrote is a place too: a vertical gauge kept there, the
// game's first placement not laid out yet included.
void EdgePlacesAreKept() {
    for (int way = 0; way < 3; ++way) {
        Fixture f;
        module->settings.gaugeVertical = 0x10;
        const auto g = MakeGauge(0x10);
        LayoutPass();
        Tick();
        LayoutPass();
        GameDrag(g, 2000, -2000); // dragged into the top right corner
        LayoutPass();
        Check(Pixels(g.window, "left") == 1920 - 48 && Pixels(g.window, "top") == 0, "In the top right corner");
        if (way == 0) module->settings.gaugeVertical = 0;
        if (way == 1) module->settings.gaugeVerticalMenu = false;
        if (way < 2) Tick();
        if (way == 2) { module->stopped = true; RestoreVerticalGauges(); }
        Check(Records() == 0 && !Styled(g.window, "left") && !Styled(g.window, "top") && !Styled(g.window, "width") && savedPositions.empty(),
            "Horizontal again at the game's own place, the corner never in its layout");
    }
    Fixture f;
    module->settings.gaugeVertical = 0x20;
    const auto g = MakeGauge(0x20);
    LayoutPass();                                                   // laid out at the UXML's place, 546 x 860
    GameWrite(g.window, "left", 0); GameWrite(g.window, "top", 0);  // then placed by the game, not laid out yet
    Tick();
    Check(RecordOf(g) && Pixels(g.window, "left") == 0 && Pixels(g.window, "top") == 0, "Vertical in the top left corner, where the game placed it");
}
// The game keeps each gauge's place in its layout (per-mille of the screen;
// never the vertical box's since 1.18.1) and places a new widget
// there at its horizontal size (its saved one), kept on the screen: a gauge
// left vertical against the right edge comes back farther left. Turned
// vertical, it goes back where the game keeps it, as the game's next
// re-placement puts it; unticked, it is the horizontal box the game placed.
// Flush with the right or the bottom edge alike; a place the game did not
// give it stays its own.
void BackWhereTheGameKeepsIt() {
    Fixture f;
    module->settings.gaugeVertical = 0x10 | 0x20 | 0x2 | 0x8;
    savedPositions["GaugePods"] = {975, 250, false, false};        // left vertical at 1872 x 270
    savedPositions["GaugeEnergyPoints"] = {0, 0, true, false};      // flush with the right edge, at the top
    savedPositions["GaugeGuild"] = {500, 0, false, true};           // flush with the bottom edge, in the middle
    savedPositions["GaugeHonorPoints"] = {500, 500, false, false};  // 960 x 540, moved since by another writer
    const auto pods = MakeGauge(0x10), energy = MakeGauge(0x20), guild = MakeGauge(0x2), honour = MakeGauge(0x8);
    LayoutPass();                                                   // their first layout, then the game's placement
    GameWrite(pods.window, "width", 700); GameWrite(pods.window, "height", 35); // its saved size first (blxi)
    for (const auto* g : {&pods, &energy, &guild, &honour}) GamePlace(*g);
    GameWrite(honour.window, "left", 300);
    Check(Pixels(pods.window, "left") == 1920 - 700 && Pixels(pods.window, "top") == 270 && Pixels(energy.window, "left") == 1920 - 566 &&
        Pixels(energy.window, "top") == 0 && Pixels(guild.window, "left") == 960 && Pixels(guild.window, "top") == 1080 - 34,
        "The game placed the new widgets horizontal, on the screen");
    const auto horizontal = Take(pods);
    Tick();
    Check(Records() == 4 && Pixels(pods.window, "left") == 1872 && Pixels(pods.window, "top") == 270 && Pixels(pods.window, "height") == 700,
        "Vertical where it was left, as long as its saved width");
    Check(Pixels(energy.window, "left") == 1920 - 48 && Pixels(energy.window, "top") == 0, "Flush with the right edge");
    Check(Pixels(guild.window, "left") == 960 && Pixels(guild.window, "top") == 1080 - 566, "Flush with the bottom edge");
    Check(Pixels(honour.window, "left") == 300 && Pixels(honour.window, "top") == 1080 - 566, "Another writer's place: its own, on the screen");
    LayoutPass();
    const auto vertical = Take(pods), energyVertical = Take(energy), guildVertical = Take(guild);
    for (const auto* g : {&pods, &energy, &guild}) GamePlace(*g);  // the game's next re-placement (a fight, another layout)
    Check(Take(pods) == vertical && Take(energy) == energyVertical && Take(guild) == guildVertical, "The game's re-placement keeps them there");
    module->settings.gaugeVertical = 0x20 | 0x2 | 0x8;
    Tick();
    Check(Take(pods) == horizontal && Records() == 3, "Unticked: the horizontal box the game placed");
}
// The owner's 1.18.0 report: the character's gauge, 734 x 35 at its saved
// place (741.8, 540.3 per-mille: against the right edge, mid-height), turned
// vertical, then a press on its top edge pulled up to make it longer, then
// unticked. It must come back exactly as the game had it, the game's layout
// untouched, and stay there at the game's next re-placement (a fight, a
// layout preset).
void BackWhereTheGameHadIt() {
    Fixture f;
    savedWidths["GaugeCharacter"] = 734;
    savedPositions["GaugeCharacter"] = {741.8f, 540.3f, false, false};
    const auto g = MakeGauge(0x1);
    LayoutPass(); GameReplace(g); LayoutPass();
    Check(std::abs(Get(g.window).x - (1920 - 734)) < .01f && std::abs(Get(g.window).y - 583.524f) < .01f && Get(g.window).width == 734,
        "The game placed it against the right edge, at mid-height");
    const auto horizontal = Take(g);
    const auto layout = savedPositions;
    const auto widths = savedWidths, heights = savedHeights;
    module->settings.gaugeVertical = 0x1;
    Tick(); LayoutPass();
    Check(Records() == 1 && Get(g.window).height == 734 && Get(g.window).y == 1080 - 734, "Vertical, 734 px long, on the screen");
    GamePullEdge(g, TopHandle, -150);  // its top edge pulled up 150 px
    LayoutPass(); Frame(); Tick(); LayoutPass();
    module->settings.gaugeVertical = 0;
    Tick(); LayoutPass();
    Check(Records() == 0 && Take(g) == horizontal, "Unticked: exactly where and how the game had it (not at the top right):" + Diff(horizontal, Take(g)) + ", found at " +
        std::to_string(PixelsOr(g.window, "left", -1)) + " x " + std::to_string(PixelsOr(g.window, "top", -1)));
    Check(savedPositions == layout && savedWidths == widths && savedHeights == heights, "The game's layout untouched");
    GameReplace(g); LayoutPass();
    Check(Take(g) == horizontal, "The game's next re-placement: the same place and size");
    Check(module->settings.gaugeVerticalBoxes[0].length == 884 && LoggedLine("VERTICAL_GAUGE_KEPT type=0x1 length=884 place=7418,1815"),
        "The length its top edge gave it kept in the settings, with its place");
}
// While vertical, its top and bottom edges change its length as the game's
// own resizes do (the game's resizer on its handles, between 160 px and the
// screen's height less 16 px), its width staying 48 px; the game's saves of
// that size and place are left out, its layout keeping the horizontal ones.
// The length kept in the settings, given again when ticked again, bounded by
// a smaller screen; unticked, the game's own size and place. The interface
// locked: no handle; unlocked: shown again at the next pump.
void ResizedWhileVertical() {
    Fixture f;
    savedWidths["GaugePods"] = 700;
    savedPositions["GaugePods"] = {500, 500, false, false};
    const auto g = MakeGauge(0x10);
    LayoutPass(); GameReplace(g); LayoutPass();
    const auto horizontal = Take(g);
    const auto layout = savedPositions;
    const auto widths = savedWidths;
    module->settings.gaugeVertical = 0x10;
    Tick(); LayoutPass();
    CheckVertical(g, 700, "Pods");
    Check(Get(g.window).x == 960 && Get(g.window).y == 380, "At its horizontal top-left corner, on the screen");
    const auto height = [&g] { return Pixels(g.window, "height"); };
    const auto top = [&g] { return Pixels(g.window, "top"); };
    GamePullEdge(g, BottomHandle, -300);
    LayoutPass(); Frame(); LayoutPass();
    Check(height() == 400 && Pixels(g.window, "width") == 48 && top() == 380 && Get(g.window).height == 400,
        "Its bottom edge pulled up: 400 px long, 48 px wide, its top kept");
    GamePullEdge(g, BottomHandle, -1000);
    LayoutPass(); Frame(); LayoutPass();
    Check(height() == 160, "At least 160 px");
    GamePullEdge(g, TopHandle, -2000);
    LayoutPass(); Frame(); LayoutPass();
    Check(height() == 540 && top() == 0, "Its top edge pulled up to the screen's top");
    GamePullEdge(g, BottomHandle, 2000);
    LayoutPass(); Frame(); LayoutPass();
    Check(height() == 1064 && top() == 0, "At most the screen's height less 16 px");
    Check(positionSaves == 0 && dimensionSaves == 0 && savedPositions == layout && savedWidths == widths && savedHeights.empty() &&
        LoggedLine("VERTICAL_GAUGE_LAYOUT_KEPT type=0x10 place") && LoggedLine("VERTICAL_GAUGE_LAYOUT_KEPT type=0x10 size"),
        "The game's saves of the vertical size and place left out, logged");
    Tick();
    Check(module->settings.gaugeVerticalBoxes[4] == Settings::GaugeVerticalBox{}, "Not kept during the resize");
    for (int i = 0; i < 3; ++i) Tick();
    Check(module->settings.gaugeVerticalBoxes[4] == Settings::GaugeVerticalBox{1064, 5000, 0} &&
        LoggedLine("VERTICAL_GAUGE_KEPT type=0x10 length=1064 place=5000,0"), "Kept in the settings once the resize is over");
    // The interface locked: the game hides every handle; unlocked, it hides them too (the flags off): shown again at the next pump.
    Put<bool>(g.window, 0x2CC, true); UpdateResizeHandlers(g.window);
    Tick();
    Check(Get(g.window).handles == std::array<bool, 8>{}, "Locked: no handle");
    Put<bool>(g.window, 0x2CC, false); UpdateResizeHandlers(g.window);
    Tick();
    Check(Get(g.window).handles[BottomHandle] && Get(g.window).handles[TopHandle], "Unlocked: its top and bottom handles again");
    module->settings.gaugeVertical = 0;
    Tick(); LayoutPass();
    Check(Take(g) == horizontal && savedPositions == layout && savedWidths == widths && dimensionSaves == 0,
        "Unticked: the game's own size and place, its layout untouched");
    GameReplace(g); LayoutPass();
    Check(Take(g) == horizontal, "The game's next placement agrees");
    module->settings.gaugeVertical = 0x10;
    Tick(); LayoutPass();
    Check(height() == 1064 && top() == 0 && Pixels(g.window, "left") == 960 && LoggedLine("VERTICAL_GAUGE_ON type=0x10 length=1064"),
        "Ticked again: its own length and place");
    screenHeight = 720;
    LayoutPass(); Tick(); LayoutPass();
    Check(height() == 704 && Pixels(g.window, "maxHeight") == 704 && module->settings.gaugeVerticalBoxes[4].length == 1064,
        "A smaller screen: bounded by it, its choice kept");
}
// Dragged while vertical: its place taken once it stays (the game's saves of
// it left out), kept in the settings, kept across the game's re-placements
// (they put it at its horizontal saved place: back at its own at once, hidden
// or not) and when ticked again. A save of the game on its way just after it
// is put back is still left out; the next ones are the game's.
void DraggedWhileVertical() {
    Fixture f;
    savedPositions["GaugeCharacter"] = {100, 300, false, false};
    savedWidths["GaugeCharacter"] = 300;
    const auto g = MakeGauge(0x1);
    LayoutPass(); GameReplace(g); LayoutPass();
    const auto horizontal = Take(g);
    module->settings.gaugeVertical = 0x1;
    Tick(); LayoutPass();
    Check(Get(g.window).x == 192 && Get(g.window).y == 324, "Vertical at its horizontal top-left corner");
    const auto at = [&g](float x, float y) { return std::abs(Pixels(g.window, "left") - x) < .5f && std::abs(Pixels(g.window, "top") - y) < .5f; };
    GameDrag(g, 1000, -200);
    LayoutPass(); Frame();
    Check(at(1192, 124) && positionSaves == 0 && savedPositions.at("GaugeCharacter") == SavedPosition{100, 300, false, false},
        "Dropped there, the game's layout untouched");
    Tick(); Tick();
    Check(RecordOf(g)->at.x == 1192 && RecordOf(g)->at.y == 124, "Its place taken once it stays");
    for (int i = 0; i < 3; ++i) Tick();
    Check(module->settings.gaugeVerticalBoxes[0] == Settings::GaugeVerticalBox{0, verticalGauges::KeptPlace(1192, 1920),
        verticalGauges::KeptPlace(124, 1080)}, "Kept in the settings, without a length of its own");
    GameReplace(g);                                   // a fight's layout: the game puts it at its saved place
    Check(at(192, 324), "The game re-placed it at its horizontal place");
    Frame();
    Check(at(1192, 124), "Back at its own place at once");
    Get(g.window).hidden = true;
    LayoutPass(); GameReplace(g); Frame();
    Check(at(1192, 124), "Hidden: back at its own place too");
    Get(g.window).hidden = false;
    LayoutPass();
    module->settings.gaugeVertical = 0;
    Tick(); LayoutPass();
    Check(Take(g) == horizontal, "Unticked: where the game had it");
    GameDrag(g, 0, 0);                                // a save of the game on its way
    Check(positionSaves == 0, "A save on its way just after left out");
    nowMs += 1000;
    Tick();
    GameDrag(g, 10, 0);
    Check(positionSaves == 1 && std::abs(savedPositions.at("GaugeCharacter").x - 202.f / 1920.f * 1000.f) < .01f, "Then the game's own saves again");
    LayoutPass();
    module->settings.gaugeVertical = 0x1;
    Tick(); LayoutPass();
    Check(at(1192, 124), "Ticked again: at its own place");
}
// The rules: a gauge's own type, the settings' mask, the length, the place on the screen.
void Policy() {
    using verticalGauges::KeepOnScreen;
    using verticalGauges::Length;
    using verticalGauges::SavedPlace;
    using verticalGauges::ValidType;
    for (int type : {0x1, 0x2, 0x8, 0x10, 0x20, 0x40, 0x80, 0x1000000, 0x2000000}) Check(ValidType(type), "Each gauge's own type");
    for (int type : {0, 4, 3, 0x41, -1, 0x4000000, 0x3FFFFFB}) Check(!ValidType(type), "No gauge, or several");
    Check(Settings::ValidGaugeVertical(0) && Settings::ValidGaugeVertical(0x3FFFFFB) && !Settings::ValidGaugeVertical(4) &&
        !Settings::ValidGaugeVertical(-1) && !Settings::ValidGaugeVertical(0x4000000), "The settings' mask");
    Check(Length(566, 1080) == 566 && Length(707, 1080) == 707 && Length(2000, 1080) == 1064 && Length(100, 1080) == 160 &&
        Length(0, 1080) == 566 && Length(-5, 1080) == 566 && Length(900, 0) == 900 && Length(900, -1) == 900 && Length(50, 0) == 160 &&
        Length(800, 100) == 160, "The length: the width, between 160 px and the screen's height less 16 px");
    using verticalGauges::KeptLength;
    using verticalGauges::KeptPlace;
    using verticalGauges::MaxLength;
    Check(MaxLength(1080) == 1064 && MaxLength(720) == 704 && MaxLength(100) == 160 && MaxLength(0) == 16384 && MaxLength(-1) == 16384,
        "The longest: the screen's height less 16 px, at least 160 px");
    Check(KeptLength(884.4f) == 884 && KeptLength(50) == 160 && KeptLength(1e9f) == 16384 && KeptLength(NAN) == 0 &&
        Settings::ValidGaugeVerticalLength(KeptLength(1064)), "A chosen length as the settings keep it");
    Check(KeptPlace(960, 1920) == 5000 && KeptPlace(0, 1080) == 0 && KeptPlace(-5, 1920) == 0 && KeptPlace(3000, 1920) == 10000 &&
        KeptPlace(1, 0) == 0 && KeptPlace(NAN, 1920) == 0, "A place as the settings keep it: ten-thousandths of the screen");
    const auto same = [](verticalGauges::Box a, verticalGauges::Box b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; };
    Check(same(KeepOnScreen({100, 100, 48, 500}, 1920, 1080), {100, 100, 48, 500}), "On the screen: unchanged");
    Check(same(KeepOnScreen({1900, 900, 48, 500}, 1920, 1080), {1872, 580, 48, 500}), "Bottom right: moved in, no further");
    Check(same(KeepOnScreen({-20, -5, 48, 500}, 1920, 1080), {0, 0, 48, 500}), "Top left: moved in");
    Check(same(KeepOnScreen({300, 300, 3000, 2000}, 1920, 1080), {0, 0, 3000, 2000}), "Larger than the screen: its top-left corner on it");
    Check(same(KeepOnScreen({1900, 900, 48, 500}, 0, 1080), {1900, 900, 48, 500}), "An unknown screen: unchanged");
    Check(same(SavedPlace(500, 250, false, false, 48, 566, 1920, 1080), {960, 270, 48, 566}), "A saved place: per-mille of the screen");
    Check(same(SavedPlace(975, 250, false, false, 566, 34, 1920, 1080), {1354, 270, 566, 34}) &&
        same(SavedPlace(975, 250, false, false, 48, 566, 1920, 1080), {1872, 270, 48, 566}), "Kept on the screen at each size");
    Check(same(SavedPlace(0, 0, true, true, 48, 566, 1920, 1080), {1872, 514, 48, 566}) &&
        same(SavedPlace(500, 500, true, false, 566, 34, 1920, 1080), {1354, 540, 566, 34}), "Flush with the right or bottom edge");
}
}

int main() {
    try {
        Initialize();
        const auto run = [](void (*test)()) {
            test();
            Check(handles.empty(), "Vertical gauges test leaked GC references");
        };
        run(InstallResolvesEveryContract);
        run(MissingContractTurnsOnlyThisOff);
        run(RowPlacement);
        run(NoRowUnlessWanted);
        run(ClickTogglesAndApplies);
        run(StaleClicksDoNothing);
        run(LengthFromItsHorizontalWidth);
        run(KeptOnTheScreen);
        run(RoundTripIsExact);
        run(GameReplacementIsUndone);
        run(FollowsTheLayoutsSavedSize);
        run(HiddenDuringAReplacement);
        run(FollowsTheHubsGauges);
        run(StaysVerticalAcrossShowAndHide);
        run(OptionsSwitch);
        run(StopRestoresEverything);
        run(RowFollowsItsMenu);
        run(FailuresStayLocal);
        run(FollowsTheScreensHeight);
        run(EdgePlacesAreKept);
        run(BackWhereTheGameHadIt);
        run(ResizedWhileVertical);
        run(DraggedWhileVertical);
        run(BackWhereTheGameKeepsIt);
        run(Policy);
        std::cout << "PASS: « Afficher à la verticale »: every contract verified at its pinned RVA, a missing one turning only this feature off "
            "(logged once, no detour, no call); the row in each gauge's « … » menu right before « Gestion des jauges » (after « Masquer le module », "
            "or « Couleur de jauge » without it; last without a separator), ticked for a vertical gauge, absent when unwanted or impossible; a click "
            "saving the choice once and turning the gauge vertical or back at once, stale or foreign clicks without effect; the vertical layout "
            "(the game's vertical bar, 27 inline values, its resize flags off, its top and bottom handles shown) as long as the gauge's "
            "horizontal width (the game's, its saved size, its layout; 160 px to the screen's height less 16), kept on the screen; restored "
            "exactly, the game's own values since kept; the game's size writes undone a frame later and the gauge kept at its place at once, "
            "hidden or not (four calls per vertical gauge and frame, none without); each layout's saved size followed and given back; a gauge "
            "re-placed while hidden back at its place; new gauges once the game laid them out, discarded, detached and hidden ones; the "
            "options' switch, Stop with its retries and their limit; unexpected widgets (ctr_main not added to its window) and repeated failures "
            "left horizontal; another screen height followed; dragged into a corner, or pulled by an edge (the 1.18.0 report: the top right "
            "corner), then unticked: back exactly where and as large as the game had it, its layout untouched; resized by its top and bottom "
            "edges (160 px to the screen's height less 16, 48 px wide), dragged, kept across the game's re-placements, both kept in the settings "
            "and given again, never saved in the game's layout (its saves of them left out, a save on its way too); a new gauge where the game "
            "keeps its horizontal place\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
