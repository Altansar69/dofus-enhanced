#include "ui/vertical-gauges.hpp"
#include "ui/vertical-gauge-policy.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// « Afficher à la verticale » (1.18.0). Each of the game's gauges is its own
// widget (WidgetProgressGaugeUi, one per fqd+GaugesType, kept by the gauges'
// hub gfc), with its own « … » button: the menu it opens gets a native
// checkbox row right after the gauge's own rows, before the game's « Gestion
// des jauges ». Ticked, the gauge turns vertical through the game's own
// vertical progress bar (BaseProgressBar`2.gaugeOrientation: it fills from the
// bottom, its graduations follow) and a column layout of the widget's own
// elements (inline styles, each one captured before it is written and put back
// exactly). Its window's resize flags are off meanwhile, so the game never
// applies its saved size to it; its top and bottom handles stay shown (1.18.1),
// so its length changes like the game's own resizes.
// Since 1.18.1 the vertical gauge never writes the game's layout: the game's
// saves of its place and size (UiPresetOptionsContainer.SaveUiPositionById and
// SaveUiDimensionById, after a drag or a resize) are left out for it, its own
// length and place are the mod's (Settings::gaugeVerticalBoxes), and the game's
// re-placements of it (at its horizontal saved place) are undone at once.
// Unticked, with the options' switch unticked or at Stop, the gauge is again
// what the game built, where and as large as the game had it.
// 1.18.0 kept a place written while the gauge was vertical: a drag, or a press
// on its edges meant as a resize (its handles were hidden, the press dragged
// it), left the horizontal gauge at the vertical one's top-left corner, high
// on the screen, and in the game's layout, which then put it there again.
namespace verticalGauges {
namespace {
using namespace managed;
constexpr const char* CoreImage = "Core.dll";
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Components = "Core.UILogic.Components";
constexpr const char* Menus = "Core.UILogic.Components.ContextMenu";
// The row, like the game's checkbox rows of this menu: no icon (FigmaIcons.none),
// its help after a second. A click closes the menu: the gauge under it changes shape.
constexpr const char* RowLabel = "Afficher à la verticale";
constexpr const char* RowHelp = "Affiche cette jauge à la verticale : elle se remplit de bas en haut, son niveau en dessous. "
    "Tire son bord haut ou bas pour changer sa longueur ; sa longueur et sa place à la verticale sont gardées pour la prochaine fois. "
    "Décoche pour la remettre à l’horizontale, à sa place et à sa taille d’avant.";
constexpr int Horizontal = 0, Vertical = 1; // BaseProgressBar`2+GaugeOrientationEnum
constexpr int StyleSheetChange = 0x10;      // VersionChangeType.StyleSheet: a full style update of the element
constexpr int BottomHandle = 6, TopHandle = 7; // ResizeHandler
constexpr int DragTop = 1, DragBottom = 2;     // ResizableWindow+DragPosition: no handle on the drag handle's own edge
constexpr uint64_t RowAttachMs = 5000, RestoreRetryMs = 250, KeepAfterMs = 600, RecentMs = 1000, DiagnoseAfterMs = 400;
constexpr unsigned MaxFailures = 3, MaxRestoreAttempts = 20;

// The widget's elements the vertical layout changes, by WidgetProgressGaugeBinding's
// getters: the window (widgetProgressGauge), ctr_main, gaugeContainer (the
// icon and the bar), the bar, the level (« NIV. » and lbl_value), the Omega
// block and its badge's box (lbl_omegaValue's parent), the « … » button.
enum Part : size_t { Window, Main, GaugeBox, Gauge, Level, LevelValue, Omega, OmegaBox, Button, PartCount };
struct Contract { const char* name; int argc; uintptr_t rva; };
constexpr Contract PartGetters[PartCount] = {
    {"get_widgetProgressGauge", 0, 0x154DFD0}, {"get_ctr_main", 0, 0x154D9A0}, {"get_gaugeContainer", 0, 0x154DA50},
    {"get_gauge", 0, 0x154DB00}, {"get_level", 0, 0x154DE70}, {"get_lbl_value", 0, 0x154DDC0},
    {"get_omega", 0, 0x154DF20}, {"get_lbl_omegaValue", 0, 0x154DC60}, {"get_btn_settings", 0, 0x154D8F0}};
// The inline styles written, through InlineStyleAccess's IStyle accessors.
enum Property : size_t { Width, Height, MinWidth, MinHeight, MaxHeight, Direction, PaddingLeft, PaddingTop, PaddingBottom, MarginLeft,
    MarginTop, Left, Top, PropertyCount };
struct Accessor { Contract get, set; };
constexpr Accessor Accessors[PropertyCount] = {
    {{"UnityEngine.UIElements.IStyle.get_width", 0, 0x55D2740}, {"UnityEngine.UIElements.IStyle.set_width", 1, 0x55D63F0}},
    {{"UnityEngine.UIElements.IStyle.get_height", 0, 0x55D1540}, {"UnityEngine.UIElements.IStyle.set_height", 1, 0x55D3D60}},
    {{"UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x55D1850}, {"UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x55D4410}},
    {{"UnityEngine.UIElements.IStyle.get_minHeight", 0, 0x55D1810}, {"UnityEngine.UIElements.IStyle.set_minHeight", 1, 0x55D4370}},
    {{"UnityEngine.UIElements.IStyle.get_maxHeight", 0, 0x55D1790}, {"UnityEngine.UIElements.IStyle.set_maxHeight", 1, 0x55D4230}},
    {{"UnityEngine.UIElements.IStyle.get_flexDirection", 0, 0x55D13C0}, {"UnityEngine.UIElements.IStyle.set_flexDirection", 1, 0x55D3A80}},
    {{"UnityEngine.UIElements.IStyle.get_paddingLeft", 0, 0x55D1970}, {"UnityEngine.UIElements.IStyle.set_paddingLeft", 1, 0x55D46D0}},
    {{"UnityEngine.UIElements.IStyle.get_paddingTop", 0, 0x55D19F0}, {"UnityEngine.UIElements.IStyle.set_paddingTop", 1, 0x55D4810}},
    {{"UnityEngine.UIElements.IStyle.get_paddingBottom", 0, 0x55D1930}, {"UnityEngine.UIElements.IStyle.set_paddingBottom", 1, 0x55D4630}},
    {{"UnityEngine.UIElements.IStyle.get_marginLeft", 0, 0x55D16D0}, {"UnityEngine.UIElements.IStyle.set_marginLeft", 1, 0x55D4050}},
    {{"UnityEngine.UIElements.IStyle.get_marginTop", 0, 0x55D1750}, {"UnityEngine.UIElements.IStyle.set_marginTop", 1, 0x55D4190}},
    {{"UnityEngine.UIElements.IStyle.get_left", 0, 0x55D1610}, {"UnityEngine.UIElements.IStyle.set_left", 1, 0x55D3EB0}},
    {{"UnityEngine.UIElements.IStyle.get_top", 0, 0x55D1CD0}, {"UnityEngine.UIElements.IStyle.set_top", 1, 0x55D4F70}}};
// The values compared to tell one still the mod's: StyleLength (its Length's
// value and unit, then its keyword), StyleEnum<FlexDirection> (value, keyword).
constexpr size_t LengthBytes = 12, EnumBytes = 8;
// A written value: pixels or percent (LengthUnit), or FlexDirection.
enum class Unit { Pixel, Percent, Enum };
struct Written { Part part; Property property; Unit unit; float value; };
constexpr float Column = 0;          // FlexDirection.Column
constexpr float TheLength = -1;      // the window's height: the gauge's length
constexpr float TheMaxLength = -2;   // the window's maximum height: the screen's height less 16 px
// The vertical layout, the window's width first (VerticalGaugesFrame watches
// it) and its height second (a resize by its handles changes it), with the
// widget UXML's own values in comments. The window's minimum and maximum
// height bound the game's resizes (ElementResizer takes them from the resolved
// style at the press). The others are starting values to tune in game.
constexpr Written Layout[] = {
    {Window, Width, Unit::Pixel, Thickness},           // 566 px, or the game's own size
    {Window, Height, Unit::Pixel, TheLength},          // 34 px, or the game's own size
    {Window, MinWidth, Unit::Pixel, Thickness},        // 240 px
    {Window, MinHeight, Unit::Pixel, MinLength},       // 34 px
    {Window, MaxHeight, Unit::Pixel, TheMaxLength},    // none
    {Main, Direction, Unit::Enum, Column},             // row
    {Main, PaddingLeft, Unit::Pixel, 0},               // 12 px
    {Main, PaddingTop, Unit::Pixel, 6},
    {Main, PaddingBottom, Unit::Pixel, 4},
    {Main, MinWidth, Unit::Pixel, 0},                  // 240 px
    {GaugeBox, Direction, Unit::Enum, Column},         // row: the icon above the bar
    {Gauge, MarginLeft, Unit::Pixel, 0},               // 4 px
    {Gauge, MarginTop, Unit::Pixel, 4},
    {Level, Direction, Unit::Enum, Column},            // row: « NIV. » above the level
    {Level, Width, Unit::Percent, 100},                // 68 px
    {Level, MarginLeft, Unit::Pixel, 0},               // 4 px
    {Level, MarginTop, Unit::Pixel, 4},
    {LevelValue, MarginLeft, Unit::Pixel, 0},          // 4 px
    {Omega, Direction, Unit::Enum, Column},            // row: the Omega icon above its badge
    {Omega, Width, Unit::Percent, 100},                // 68 px
    {Omega, MarginLeft, Unit::Pixel, 0},               // 4 px
    {Omega, MarginTop, Unit::Pixel, 4},
    {OmegaBox, MarginLeft, Unit::Pixel, 0},            // 8 px
    {OmegaBox, MarginTop, Unit::Pixel, 10},            // the badge is drawn 9 px above its box, over the Omega icon
    {Button, Height, Unit::Pixel, 24},                 // 100 %
    {Button, MarginLeft, Unit::Pixel, 0},              // 4 px
    {Button, MarginTop, Unit::Pixel, 2}};
static_assert(std::size(Layout) == 27 && Layout[0].part == Window && Layout[0].property == Width && Layout[1].property == Height);
constexpr float Row = 2;             // FlexDirection.Row
// The widget UXML's inline rule (widgetProgressGauge's inline sheet in the
// game's UI bundle) for the values above it defines. Put back as « none »
// (keyword Null), such a value is not the UXML's again in game: the element
// resolves it from the style sheets (InitialStyle: a column, no width, no
// margin) until a full style update re-applies the rule, and 1.19.0/1.20.0
// showed that update does not come for these gauges. Put back as the rule's
// own value instead: the same resolved style, without depending on it.
constexpr Written UxmlRule[] = {
    {Window, Width, Unit::Pixel, 566}, {Window, Height, Unit::Pixel, 34}, {Window, MinWidth, Unit::Pixel, 240},
    {Window, MinHeight, Unit::Pixel, 34}, {Main, Direction, Unit::Enum, Row}, {Main, PaddingLeft, Unit::Pixel, 12},
    {Main, MinWidth, Unit::Pixel, 240}, {GaugeBox, Direction, Unit::Enum, Row}, {Gauge, MarginLeft, Unit::Pixel, 4},
    {Level, Direction, Unit::Enum, Row}, {Level, Width, Unit::Pixel, 68}, {Level, MarginLeft, Unit::Pixel, 4},
    {LevelValue, MarginLeft, Unit::Pixel, 4}, {Omega, Direction, Unit::Enum, Row}, {Omega, Width, Unit::Pixel, 68},
    {Omega, MarginLeft, Unit::Pixel, 4}, {OmegaBox, MarginLeft, Unit::Pixel, 8}, {Button, Height, Unit::Percent, 100},
    {Button, MarginLeft, Unit::Pixel, 4}};
// The value an element resolves (VisualElement's own IResolvedStyle), read
// for the untick's diagnostic: a float, a StyleFloat (its keyword: auto or
// none), or an enum.
enum class Kind { Float, StyleFloat, Enum };
struct Resolver { Contract get; Kind kind; };
constexpr Resolver Resolvers[PropertyCount] = {
    {{"UnityEngine.UIElements.IResolvedStyle.get_width", 0, 0x55EF140}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_height", 0, 0x55EDA70}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_minWidth", 0, 0x55EDE90}, Kind::StyleFloat},
    {{"UnityEngine.UIElements.IResolvedStyle.get_minHeight", 0, 0x55EDE30}, Kind::StyleFloat},
    {{"UnityEngine.UIElements.IResolvedStyle.get_maxHeight", 0, 0x55EDD70}, Kind::StyleFloat},
    {{"UnityEngine.UIElements.IResolvedStyle.get_flexDirection", 0, 0x55ED890}, Kind::Enum},
    {{"UnityEngine.UIElements.IResolvedStyle.get_paddingLeft", 0, 0x55EDFB0}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_paddingTop", 0, 0x55EE070}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_paddingBottom", 0, 0x55EDF50}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_marginLeft", 0, 0x55EDC50}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_marginTop", 0, 0x55EDD10}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_left", 0, 0x55EDB30}, Kind::Float},
    {{"UnityEngine.UIElements.IResolvedStyle.get_top", 0, 0x55EE3B0}, Kind::Float}};
constexpr const char* PartNames[PartCount] = {"window", "ctr_main", "gaugeContainer", "gauge", "level", "lbl_value", "omega", "omegaBox",
    "btn_settings"};
constexpr const char* PropertyNames[PropertyCount] = {"width", "height", "minWidth", "minHeight", "maxHeight", "flexDirection", "paddingLeft",
    "paddingTop", "paddingBottom", "marginLeft", "marginTop", "left", "top"};

struct Natives {
    Klass gaugeUi{}, binding{}, hub{}, window{}, resizable{}, bar{}, menu{}, item{}, view{}, callback{}, root{}, separator{};
    Klass element{}, style{}, length{}, cachedData{}, presets{}, coordinates{};
    std::array<Method, PartCount> parts{};
    std::array<Method, PropertyCount> get{}, set{};
    Method setOrientation{}, getOrientation{}, setVertical{}, setHorizontal{}, setCorner{}, enableHandle{}, disableHandle{};
    Method appendCheckbox{}, removeItemAt{}, insertItem{}, menuItems{}, itemCallback{}, itemDisabled{}, itemData{}, menuData{}, removeMenu{};
    Method parent{}, panel{}, layout{}, name{}, getStyle{}, setLength{}, presetContainer{}, loadDimension{}, loadPosition{};
    Method incrementVersion{}, classListContains{};
    std::array<Method, PropertyCount> resolve{};
    Method savePosition{}, saveDimension{};
    // Checked offsets: the gauge's type, the hub's gauges, a menu's root, a
    // window's content, its three resize flags, its lock, its top and bottom
    // handles' own switches and its drag handle's edge, a saved size's or place's x and y.
    size_t gaugeType = 0, gauges = 0, menuRoot = 0, content = 0, vertical = 0, horizontal = 0, corner = 0, locked = 0;
    size_t topOff = 0, bottomOff = 0, dragPosition = 0, savedX = 0, savedY = 0;
} n;

// One inline value the mod wrote: the element's style (InlineStyleAccess), the
// value before (the game's own, or keyword Null: none, the UXML's rule applies)
// and the one written. Put back only while it is still the mod's.
struct Owned { Ref style, before, applied; Property property = Width; Part part = Window; };
// What an element resolved for one value of the layout, as text (the untick's diagnostic).
struct Sample { Part part; Property property; std::string value; };
// A diagnostic due after an untick: the gauge's elements, what they resolved
// before it turned vertical, read again once the game has updated their style.
struct Diagnosis {
    int type = 0;
    std::array<Ref, PartCount> parts;
    std::vector<Sample> samples;
    std::string bar;
    uint64_t due = 0;
};
// A place the game keeps for a gauge (LoadUiPositionById): per-mille of the
// screen, or flush with its right or bottom edge; none saved: has false.
struct Stored {
    bool has = false;
    float x = 0, y = 0;
    bool right = false, bottom = false;
    bool operator==(const Stored&) const = default;
};
// A vertical gauge.
struct Record {
    Ref widget, hub, container, window, gauge, windowStyle;
    std::array<Ref, PartCount> parts; // the elements the vertical layout changes (their full style update when put back)
    int type = 0;
    std::wstring id;                // its UI id (its container's name), the key of the game's saves for it
    std::array<bool, 3> resizable{}; // its window's vertical, horizontal and corner resizing, as the game had them
    // Its horizontal width, its length from it (or from `chosen`) on the
    // screen's height when it was last laid out (0: unknown).
    float width = 0, length = 0, screenHeight = 0;
    float chosen = 0;                // the length chosen with the mouse (settings), 0: its horizontal width
    // Its horizontal layout's size when it turned vertical (0: unknown): a box
    // the game's hub may still keep for it.
    float laidWidth = 0, laidHeight = 0;
    // Its place: its top-left corner in fractions of the screen's width and
    // height, `at` in pixels as last written or taken; a place found at a pump
    // and not explained yet (taken as the player's once it stays).
    float fx = 0, fy = 0;
    Box at{}, seen{}, checked{};
    bool seenValid = false, checkedValid = false;
    // The game's own place for it when it turned vertical: its inline left and
    // top (boxed), the place its layout kept and the screen then.
    Ref left, top;
    Stored saved{};
    float screenWidth0 = 0, screenHeight0 = 0;
    std::vector<Owned> styles;       // the window's width first, its height second
    bool settlePending = false, undo = false, dirty = false;
    uint64_t changed = 0;            // when the player last changed its length or place (kept KeepAfterMs later)
    // Its length and place in the settings when it was laid out or last kept:
    // other ones there (a saved layout applied, 1.20.0) lay it out again.
    Settings::GaugeVerticalBox basis{};
    // What its elements resolved before anything was written, and its bar
    // (orientation, classes): compared once it is put back (the diagnostic).
    std::vector<Sample> samples;
    std::string bar;
};
// The row of the menu shown: at most one, the game's DisplayMenu closes every other menu first.
struct Row {
    Ref widget, menu, item, callback, cached, compositor;
    int type = 0;
    bool on = false, seen = false, armed = false;
    uint64_t created = 0;
};
struct Throttle { std::string last; uint64_t until = 0; };
uint64_t Now() { return GetTickCount64(); }
struct State {
    bool installed = false, failed = false, layoutChecked = false;
    std::vector<Record> records;
    std::unique_ptr<Row> row;
    // The UI ids of gauges just put back: a save of the game still on its way
    // for them is the vertical box's, left out until then.
    std::vector<std::pair<std::wstring, uint64_t>> recent;
    std::vector<Diagnosis> checks;                   // the untick diagnostics due
    int skipped = 0, unexpected = 0;                 // types left horizontal for the session; those found unexpected (logged)
    std::array<unsigned, 32> failures{};             // consecutive errors, by type bit
    unsigned restoreAttempts = 0;
    uint64_t nextRestore = 0;
    Throttle menuLog, errorLog, replacedLog, layoutLog;
    uint64_t (*clock)() = Now;                       // the tests' clock
} s;
using SectionFn = void (*)(Obj, Obj, Method);
using SavePositionFn = void (*)(Obj, Obj, Obj, bool, bool, Method);
using SaveDimensionFn = void (*)(Obj, Obj, Obj, Method);
SectionFn originalSection = nullptr;
SavePositionFn originalSavePosition = nullptr;
SaveDimensionFn originalSaveDimension = nullptr;

// ------------------------------------------------------------------ helpers
std::string Hex(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", static_cast<unsigned>(value));
    return text;
}
std::string Number(float value) { return std::to_string(std::lround(value)); }
unsigned Bit(int type) { return static_cast<unsigned>(std::countr_zero(static_cast<unsigned>(type))); }
// The same line at most once a minute; another line at once.
void Report(Throttle& throttle, const std::string& line) {
    const uint64_t now = s.clock();
    if (line == throttle.last && now < throttle.until) return;
    throttle.last = line;
    throttle.until = now + 60000;
    Log(line);
}
bool Active() {
    return s.installed && !s.failed && module && module->ready && module->servicesReady && !module->stopped &&
        !module->stopRequested && !module->accountOptionsUnbound;
}
// The gauges to show vertical: none with the options' switch unticked (their
// choices are kept), none of the types left horizontal for the session.
int Mask() {
    if (!Active() || !module->settings.gaugeVerticalMenu) return 0;
    return module->settings.gaugeVertical & Settings::GaugeVerticalBits & ~s.skipped;
}
bool Is(Obj object, Klass klass) { return object && api.class_is_assignable_from(klass, api.object_get_class(object)); }
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Vertical gauge field layout: ") + name);
    return offset;
}
std::wstring Text(Obj string) {
    const wchar_t* chars = string ? api.string_chars(string) : nullptr;
    const int32_t length = string ? api.string_length(string) : 0;
    return chars && length > 0 ? std::wstring(chars, static_cast<size_t>(length)) : std::wstring();
}
Obj Parent(Obj element) { return element ? api.Invoke(n.parent, element) : nullptr; }
bool Attached(Obj element) { return element && api.Invoke(n.panel, element) != nullptr; }
struct Rect { float x, y, width, height; };
Rect LayoutOf(Obj element) { return api.Value<Rect>(api.Invoke(n.layout, element)); }
// A layout to place with: none while hidden or before the first layout pass.
bool Laid(const Rect& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) &&
        box.width > .5f && box.height > .5f && box.width <= 16384 && box.height <= 16384;
}
bool Near(const Box& a, const Box& b) { return std::abs(a.x - b.x) <= .5f && std::abs(a.y - b.y) <= .5f; }
Obj StyleOf(Obj element) {
    Obj style = element ? api.Invoke(n.getStyle, element) : nullptr;
    if (!style || api.object_get_class(style) != n.style) throw std::runtime_error("Vertical gauge inline style unavailable");
    return style;
}
// A boxed StyleLength or StyleEnum, as the getter has it.
Obj ReadStyle(Obj style, Property property) {
    Obj box = api.Invoke(n.get[property], style);
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Vertical gauge expected a boxed style");
    return box;
}
void WriteStyle(Obj style, Property property, Obj box) { api.Invoke(n.set[property], style, {api.object_unbox(box)}); }
bool Same(Obj current, Obj applied, Property property) {
    return std::memcmp(api.object_unbox(current), api.object_unbox(applied), property == Direction ? EnumBytes : LengthBytes) == 0;
}
// The pixels a StyleLength holds as a value of its own (keyword Undefined,
// unit Pixel), 0 and below included (a place against the screen's top or
// left edge); false for anything else (keyword Null: no inline value).
bool OwnPixels(Obj box, float& pixels) {
    struct { float value; int unit, keyword; } length{};
    std::memcpy(&length, api.object_unbox(box), LengthBytes);
    if (length.keyword != 0 || length.unit != 0 || !std::isfinite(length.value)) return false;
    pixels = length.value;
    return true;
}
// A size of its own: its pixels when above 0; -1 for anything else.
float PixelValue(Obj box) {
    float pixels = 0;
    return OwnPixels(box, pixels) && pixels > 0 ? pixels : -1;
}
void Assign(Obj box, Unit unit, float value) {
    if (unit == Unit::Enum) {
        // StyleEnum<FlexDirection>: its value, keyword Undefined, written as
        // the struct is laid out (no generic setter whose shared code could
        // leave the keyword as it was).
        const struct { int value, keyword; } style{static_cast<int>(value), 0};
        std::memcpy(api.object_unbox(box), &style, sizeof(style));
        return;
    }
    int lengthUnit = unit == Unit::Percent ? 1 : 0;
    Ref length(api.New(n.length, {&value, &lengthUnit}));
    api.Invoke(n.setLength, api.object_unbox(box), {api.object_unbox(length.get())});
}
Owned* OwnedOf(Record& r, Obj style, Property property) {
    for (auto& owned : r.styles) if (owned.style.get() == style && owned.property == property) return &owned;
    return nullptr;
}
// Writes one value of the vertical layout, the record owning it before the
// setter runs (a setter that throws once it has written is still undone). A
// value the mod already owns keeps its value before, unless the game wrote
// its own since: that one is then the one to put back.
void Own(Record& r, Part part, Obj style, Property property, Unit unit, float value) {
    Ref current(ReadStyle(style, property)), applied(ReadStyle(style, property));
    Assign(applied.get(), unit, value);
    Owned* owned = OwnedOf(r, style, property);
    if (!owned) {
        r.styles.push_back(Owned{Ref(style), std::move(current), std::move(applied), property, part});
        owned = &r.styles.back();
    } else {
        if (!Same(current.get(), owned->applied.get(), property)) owned->before = std::move(current);
        owned->applied = std::move(applied);
    }
    WriteStyle(style, property, owned->applied.get());
}
// A style's keyword (StyleLength: after its Length; StyleEnum: after its value); 1 is Null.
int Keyword(Obj box, Property property) {
    int keyword = 0;
    std::memcpy(&keyword, static_cast<const char*>(api.object_unbox(box)) + (property == Direction ? 4 : 8), sizeof(keyword));
    return keyword;
}
// The value the UXML's inline rule gives this element (UxmlRule), boxed as
// its style's getter has it; nullptr for one the rule leaves out.
Obj UxmlValue(const Owned& owned) {
    for (const auto& rule : UxmlRule) {
        if (rule.part != owned.part || rule.property != owned.property) continue;
        Obj box = ReadStyle(owned.style.get(), owned.property);
        Assign(box, rule.unit, rule.value);
        return box;
    }
    return nullptr;
}
// What an element resolves for a value (its IResolvedStyle), as text: its
// pixels, auto or none, or an enum's number (FlexDirection: 0 column, 2 row).
std::string ResolvedText(Obj element, Property property) {
    Ref box(element ? api.Invoke(n.resolve[property], element) : nullptr);
    if (!box.get()) return "?";
    const void* data = api.object_unbox(box.get());
    char text[32];
    if (Resolvers[property].kind == Kind::Enum) {
        int value = 0;
        std::memcpy(&value, data, sizeof(value));
        std::snprintf(text, sizeof(text), "%d", value);
        return text;
    }
    struct { float value; int keyword; } resolved{};
    std::memcpy(&resolved, data, Resolvers[property].kind == Kind::StyleFloat ? 8 : 4);
    if (resolved.keyword == 2) return "auto";
    if (resolved.keyword == 3) return "none";
    std::snprintf(text, sizeof(text), "%g", resolved.value);
    return text;
}
// The bar's own state: its orientation (its getter) and its orientation classes.
std::string BarState(Obj bar) {
    const int orientation = api.Value<int>(api.Invoke(n.getOrientation, bar));
    std::string text = std::to_string(orientation);
    for (const char* name : {"horizontal", "vertical"}) {
        Ref value(api.string_new(name));
        if (api.Value<bool>(api.Invoke(n.classListContains, bar, {value.get()}))) text += std::string("/") + name;
    }
    return text;
}
// What each element of the layout resolves now.
std::vector<Sample> Samples(const std::array<Ref, PartCount>& parts) {
    std::vector<Sample> samples;
    for (const auto& value : Layout) samples.push_back({value.part, value.property, ResolvedText(parts[value.part].get(), value.property)});
    return samples;
}
// Once the gauge is put back and the game has updated its style: what its
// elements resolve now, and any value that differs from what they resolved
// before it turned vertical (VERTICAL_GAUGE_RESTORED … diff=…), so a log
// tells exactly what a restore left different.
void Diagnose(const Diagnosis& check) {
    const auto now = Samples(check.parts);
    std::string values, diff;
    for (size_t i = 0; i < now.size() && i < check.samples.size(); ++i) {
        const auto& before = check.samples[i];
        const std::string name = std::string(PartNames[before.part]) + "." + PropertyNames[before.property];
        values += (values.empty() ? "" : ",") + name + "=" + now[i].value;
        if (now[i].value != before.value) diff += (diff.empty() ? "" : ",") + name + ":" + before.value + ">" + now[i].value;
    }
    const std::string bar = check.parts[Gauge].get() ? BarState(check.parts[Gauge].get()) : "?";
    if (bar != check.bar) diff += (diff.empty() ? "" : ",") + std::string("bar:") + check.bar + ">" + bar;
    Log("VERTICAL_GAUGE_RESTORED type=" + Hex(check.type) + " bar=" + bar + " values=" + values + " diff=" + (diff.empty() ? "none" : diff));
}
void RunDiagnoses() {
    const uint64_t now = s.clock();
    for (size_t i = 0; i < s.checks.size();) {
        if (now < s.checks[i].due) { ++i; continue; }
        Diagnosis check = std::move(s.checks[i]);
        s.checks.erase(s.checks.begin() + static_cast<std::ptrdiff_t>(i));
        try { Diagnose(check); }
        catch (const std::exception& error) { Report(s.errorLog, std::string("VERTICAL_GAUGES_ERROR diagnosis ") + error.what()); }
    }
}
// One of the window's own values of the vertical layout set again, in pixels,
// its value before unchanged: the player's resize taken (the game's resizer
// wrote it), the length bounded again, the screen's maximum followed.
void Set(Record& r, Property property, float value) {
    Owned* owned = OwnedOf(r, r.windowStyle.get(), property);
    if (!owned) throw std::runtime_error("Vertical gauge value not owned");
    Ref current(ReadStyle(r.windowStyle.get(), property)), applied(ReadStyle(r.windowStyle.get(), property));
    Assign(applied.get(), Unit::Pixel, value);
    if (!Same(current.get(), applied.get(), property)) WriteStyle(r.windowStyle.get(), property, applied.get());
    owned->applied = std::move(applied);
}
void Pixels(Obj style, Property property, float value) {
    Ref box(ReadStyle(style, property));
    Assign(box.get(), Unit::Pixel, value);
    WriteStyle(style, property, box.get());
}
// The window's inline left and top when both are pixels of their own.
bool InlinePlace(Record& r, Box& box) {
    Ref left(ReadStyle(r.windowStyle.get(), Left)), top(ReadStyle(r.windowStyle.get(), Top));
    return OwnPixels(left.get(), box.x) && OwnPixels(top.get(), box.y);
}
// The window's place: its inline left and top (the game's placement and drags
// write them, 0 at the screen's edge), else its layout once laid out.
bool Place(Record& r, Box& box) {
    Ref left(ReadStyle(r.windowStyle.get(), Left)), top(ReadStyle(r.windowStyle.get(), Top));
    const bool x = OwnPixels(left.get(), box.x), y = OwnPixels(top.get(), box.y);
    if (x && y) return true;
    const Rect laid = LayoutOf(r.window.get());
    if (!Laid(laid)) return false;
    if (!x) box.x = laid.x;
    if (!y) box.y = laid.y;
    return true;
}

// ------------------------------------------------------------- the widget
struct Elements { std::array<Ref, PartCount> parts; Ref container, content; };
// The widget's elements as its UXML builds them: its window in the hub's
// container (whose name is the gauge's UI id, the key of its saved place),
// ctr_main added to the window (VisualElement.Add: into the window's content
// element, ResizableWindow.contentContainer, its parent the window itself, as
// get_parent reads m_LogicalParent), the icon's and the bar's box, the level,
// the Omega block and the « … » button in ctr_main. nullptr when so, else
// what differs.
const char* Resolve(Obj widget, Elements& e) {
    Ref binding(api.Field(widget, "dzyi"));
    if (!binding.get() || api.object_get_class(binding.get()) != n.binding) return "binding";
    // A boxed copy of the widget's binding: its getters find each element by name.
    void* data = api.object_unbox(binding.get());
    for (size_t part = 0; part < PartCount; ++part) {
        e.parts[part].reset(api.Invoke(n.parts[part], data));
        if (!e.parts[part].get()) return "element";
    }
    e.parts[OmegaBox].reset(Parent(e.parts[OmegaBox].get())); // lbl_omegaValue's box: the badge's
    e.container.reset(api.Field(widget, "dzyj"));
    Obj window = e.parts[Window].get();
    if (!e.container.get() || !Is(window, n.window) || Parent(window) != e.container.get()) return "window";
    e.content.reset(Read<Obj>(window, n.content));
    if (!e.content.get() || Parent(e.content.get()) != window) return "content";
    if (Parent(e.parts[Main].get()) != window) return "main";
    const auto in = [&e](Part child, Part parent) { return Parent(e.parts[child].get()) == e.parts[parent].get(); };
    if (!in(GaugeBox, Main) || !in(Gauge, GaugeBox) || api.object_get_class(e.parts[Gauge].get()) != n.bar) return "gauge";
    if (!in(Level, Main) || !in(LevelValue, Level)) return "level";
    if (!e.parts[OmegaBox].get() || !in(Omega, Main) || !in(OmegaBox, Omega)) return "omega";
    if (!in(Button, Main)) return "button";
    return nullptr;
}
// A gauge whose widget the mod cannot read as expected stays horizontal for
// the session, logged once.
void Unexpected(int type, const char* why) {
    s.skipped |= type;
    if (s.unexpected & type) return;
    s.unexpected |= type;
    Log(std::string("VERTICAL_GAUGE_UNEXPECTED type=") + Hex(type) + " " + why);
}
// An error with one gauge: three in a row leave it horizontal for the session.
void Fault(int type, const char* step, const char* what) {
    Report(s.errorLog, std::string("VERTICAL_GAUGES_ERROR ") + step + " type=" + Hex(type) + " " + what);
    if (!ValidType(type) || ++s.failures[Bit(type)] < MaxFailures || (s.skipped & type)) return;
    s.skipped |= type;
    Log("VERTICAL_GAUGE_FAILED type=" + Hex(type) + " left horizontal for this session");
}
template<class F> void Guarded(int type, const char* step, F&& action) {
    try {
        action();
        if (ValidType(type)) s.failures[Bit(type)] = 0;
    } catch (const std::exception& error) { Fault(type, step, error.what()); }
}
// What the game saved for the gauge's UI id (its container's name) in the
// current layout, the fight's included: LoadUiDimensionById (its size) or
// LoadUiPositionById (its place).
Obj Saved(Record& r, Method load) {
    Ref presets(module->cached.get() ? api.Invoke(n.presetContainer, module->cached.get()) : nullptr);
    Ref id(presets.get() ? api.Invoke(n.name, r.container.get()) : nullptr);
    return id.get() ? api.Invoke(load, presets.get(), {id.get()}) : nullptr;
}
// The place the game keeps for the gauge (a boxed ValueTuple<id, UICoordinates,
// right edge, bottom edge>; its coordinates null when none is saved).
Stored StoredPlace(Record& r) {
    Stored stored{};
    Ref tuple(Saved(r, n.loadPosition));
    Ref point(tuple.get() ? api.Field(tuple.get(), "Item2") : nullptr);
    if (!point.get()) return stored;
    stored.has = true;
    stored.right = *static_cast<bool*>(api.FieldAddress(tuple.get(), "Item3"));
    stored.bottom = *static_cast<bool*>(api.FieldAddress(tuple.get(), "Item4"));
    stored.x = Read<float>(point.get(), n.savedX);
    stored.y = Read<float>(point.get(), n.savedY);
    return stored;
}
// The gauge's horizontal width, the length of the vertical gauge: the size
// the game wrote (its saved size, its default place), else the size it would
// apply (saved for the current layout), else the one laid out, else the UXML's
// (the last two only once the game laid it out: Apply waits for that).
float HorizontalWidth(Record& r) {
    Ref width(ReadStyle(r.windowStyle.get(), Width));
    if (const float pixels = PixelValue(width.get()); pixels > 0) return pixels;
    try {
        Ref saved(Saved(r, n.loadDimension));
        if (saved.get()) {
            const float x = Read<float>(saved.get(), n.savedX);
            if (std::isfinite(x) && x > 0) return x;
        }
    } catch (const std::exception&) {} // the next source
    const Rect laid = LayoutOf(r.window.get());
    return Laid(laid) ? laid.width : UxmlWidth;
}
// The horizontal box as the game placed it: its place, at the size it placed
// it at (the size the game wrote, its saved one, else the one laid out).
bool HorizontalBox(Record& r, Box& box) {
    const Rect laid = LayoutOf(r.window.get());
    if (!Laid(laid) || !Place(r, box)) return false;
    Ref width(ReadStyle(r.windowStyle.get(), Width)), height(ReadStyle(r.windowStyle.get(), Height));
    const float w = PixelValue(width.get()), h = PixelValue(height.get());
    box.width = w > 0 ? w : laid.width;
    box.height = h > 0 ? h : laid.height;
    return true;
}
// Without a place of its own yet, a vertical gauge goes where the game's own
// placement rule (goz`1.blxe: its saved place, flush with an edge or
// per-mille of the screen, kept on the screen) puts a box of its size, when
// the horizontal box is exactly where that rule put it: flush with the right
// edge stays flush with it, a gauge cut by the right edge comes back where it
// was saved. With no saved place, or another writer since, it starts from the
// horizontal box's top-left corner.
bool SavedVertical(Record& r, const Rect& screen, const Box& horizontal, Box& vertical) {
    if (!r.saved.has) return false;
    const auto place = [&](float width, float height) {
        return SavedPlace(r.saved.x, r.saved.y, r.saved.right, r.saved.bottom, width, height, screen.width, screen.height);
    };
    const Box game = place(horizontal.width, horizontal.height);
    if (!(std::abs(game.x - horizontal.x) <= .5f && std::abs(game.y - horizontal.y) <= .5f)) return false;
    vertical = place(Thickness, r.length);
    return true;
}
// The vertical gauge's place taken (where it was laid at first, or where the
// player left it): fractions of the screen, followed when the screen changes.
void TakePlace(Record& r, const Box& place, const Rect& screen) {
    r.fx = std::clamp(place.x / screen.width, 0.f, 1.f);
    r.fy = std::clamp(place.y / screen.height, 0.f, 1.f);
    r.at = place;
    r.seenValid = false;
}
// The vertical gauge at its own place, entirely on the screen (moved no more
// than needed); false while the screen is not laid out (later).
bool Settle(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen)) { r.settlePending = true; return false; }
    const Box box = KeepOnScreen({r.fx * screen.width, r.fy * screen.height, Thickness, r.length}, screen.width, screen.height);
    Box now{};
    const bool known = InlinePlace(r, now);
    if (!known || std::abs(now.x - box.x) > .01f) Pixels(r.windowStyle.get(), Left, box.x);
    if (!known || std::abs(now.y - box.y) > .01f) Pixels(r.windowStyle.get(), Top, box.y);
    r.at = box;
    r.settlePending = false;
    r.seenValid = r.checkedValid = false;
    return true;
}
// Its inline place is where the game's own placement puts it (blxh at its
// saved place, the horizontal one, then blxl at a box its hub keeps: the
// vertical box, or a horizontal one while hidden): the game re-placed it (a
// layout preset, a fight's layout, the screen's size), not the player.
bool GamePlaced(Record& r, const Box& now) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen)) return false;
    Stored stored{};
    try { stored = StoredPlace(r); } catch (const std::exception&) { return false; } // unknown: taken as the player's once it stays
    if (!stored.has) return false;
    const std::array<std::pair<float, float>, 3> boxes{{{Thickness, r.length}, {UxmlWidth, UxmlHeight}, {r.laidWidth, r.laidHeight}}};
    for (const auto& [width, height] : boxes) {
        if (!(width > 0 && height > 0)) continue;
        if (Near(SavedPlace(stored.x, stored.y, stored.right, stored.bottom, width, height, screen.width, screen.height), now)) return true;
    }
    return false;
}
// The player changed its length or place: kept in the settings a moment
// later (KeepAfterMs, once the drag or the resize is over).
void Changed(Record& r) {
    r.dirty = true;
    r.changed = s.clock();
}
// The top and bottom resize handles shown as the game shows them for a
// vertically resizable window (UpdateResizeHandlers): none while the interface
// is locked, nor on a disabled edge or the drag handle's own edge. Its resize
// flags stay off: the game's placements never apply its saved size to it. The
// game hides them again when the lock changes: shown again at the next pump.
void ShowHandles(Record& r) {
    Obj window = r.window.get();
    if (Read<bool>(window, n.locked)) return;
    const int drag = Read<int>(window, n.dragPosition);
    if (!Read<bool>(window, n.topOff) && drag != DragTop) { int handle = TopHandle; api.Invoke(n.enableHandle, window, {&handle}); }
    if (!Read<bool>(window, n.bottomOff) && drag != DragBottom) { int handle = BottomHandle; api.Invoke(n.enableHandle, window, {&handle}); }
}
void Build(Record& r, const Elements& e) {
    Obj window = e.parts[Window].get();
    for (size_t part = 0; part < PartCount; ++part) r.parts[part].reset(e.parts[part].get());
    // What its elements resolve as the game built them, before anything is
    // written (the untick's diagnostic); never in the way of the layout.
    try {
        r.samples = Samples(r.parts);
        r.bar = BarState(r.parts[Gauge].get());
    } catch (const std::exception&) { r.samples.clear(); }
    r.resizable = {Read<bool>(window, n.vertical), Read<bool>(window, n.horizontal), Read<bool>(window, n.corner)};
    std::array<Ref, PartCount> styles;
    for (size_t part = 0; part < PartCount; ++part) styles[part].reset(StyleOf(e.parts[part].get()));
    r.windowStyle.reset(styles[Window].get());
    {
        Ref id(api.Invoke(n.name, r.container.get()));
        r.id = Text(id.get());
    }
    const Rect screen = LayoutOf(r.container.get()); // laid out: Apply waits for it
    r.screenWidth0 = screen.width;
    r.screenHeight0 = r.screenHeight = screen.height;
    // The game's own place before anything is written, and the one its layout keeps.
    r.left.reset(ReadStyle(r.windowStyle.get(), Left));
    r.top.reset(ReadStyle(r.windowStyle.get(), Top));
    try { r.saved = StoredPlace(r); } catch (const std::exception&) { r.saved = {}; } // none known: its own place
    r.width = HorizontalWidth(r);
    const auto& kept = module->settings.gaugeVerticalBoxes[Bit(r.type)];
    r.basis = kept;
    r.chosen = static_cast<float>(kept.length);
    r.length = Length(r.chosen > 0 ? r.chosen : r.width, r.screenHeight);
    Box horizontal{};
    const bool placed = HorizontalBox(r, horizontal); // before the vertical size is written
    if (const Rect laid = LayoutOf(window); Laid(laid)) { r.laidWidth = laid.width; r.laidHeight = laid.height; }
    for (const auto& value : Layout) {
        const float pixels = value.value == TheLength ? r.length : value.value == TheMaxLength ? MaxLength(r.screenHeight) : value.value;
        Own(r, value.part, styles[value.part].get(), value.property, value.unit, pixels);
    }
    // Once: the window's width reads back byte for byte, as VerticalGaugesFrame compares it.
    if (!s.layoutChecked) {
        Ref width(ReadStyle(r.windowStyle.get(), Width));
        if (!Same(width.get(), r.styles.front().applied.get(), Width)) {
            s.failed = true;
            Log("VERTICAL_GAUGES_DISABLED style-layout");
            throw std::runtime_error("Inline width does not read back as written");
        }
        s.layoutChecked = true;
    }
    // The game's own vertical bar: Rebuild now, its fill as a height at its next geometry change.
    int vertical = Vertical;
    api.Invoke(n.setOrientation, e.parts[Gauge].get(), {&vertical});
    bool off = false;
    for (const Method setter : {n.setVertical, n.setHorizontal, n.setCorner}) api.Invoke(setter, window, {&off});
    // Its place: its own (chosen with the mouse before), else where the game's
    // rule puts it, else the horizontal box's top-left corner.
    Box place{};
    if (kept.x >= 0) place = {kept.x / 10000.f * screen.width, kept.y / 10000.f * screen.height};
    else if (!(placed && SavedVertical(r, screen, horizontal, place)) && !Place(r, place)) place = horizontal;
    TakePlace(r, place, screen);
    Settle(r);
    ShowHandles(r);
}
// The size the game's placements give the gauge while its window can be
// resized (goz`1.blxe -> blxi, gated by the window's resize flags, which the
// vertical gauge has off): its saved size in the current layout, the fight's
// included (LoadUiDimensionById); false for none, or a window the game does
// not resize.
bool SavedSize(Record& r, float& width, float& height) {
    if (!(r.resizable[0] || r.resizable[1] || r.resizable[2])) return false;
    try {
        Ref saved(Saved(r, n.loadDimension));
        if (!saved.get()) return false;
        width = Read<float>(saved.get(), n.savedX);
        height = Read<float>(saved.get(), n.savedY);
    } catch (const std::exception&) { return false; }
    return std::isfinite(width) && width > 0 && std::isfinite(height) && height > 0;
}
// Back as the game built it: its handles and resizing, the horizontal bar,
// each value still the mod's (one the game wrote since stays the game's); its
// place as the game had it (its layout and the screen unchanged since), else
// where the game's own placement puts it now (its saved place at its size);
// then kept on the screen, as the game's own placement would.
void Restore(Record& r) {
    Obj window = r.window.get();
    // Its top and bottom handles hidden, then its resize flags as the game had
    // them: a flag that changes shows its handles again (UpdateResizeHandlers);
    // one already put back (a restore tried again) shows them here, as the game would.
    for (int handle : {TopHandle, BottomHandle}) api.Invoke(n.disableHandle, window, {&handle});
    const std::array<std::pair<size_t, Method>, 3> flags{{{n.vertical, n.setVertical}, {n.horizontal, n.setHorizontal}, {n.corner, n.setCorner}}};
    for (size_t i = 0; i < flags.size(); ++i)
        if (Read<bool>(window, flags[i].first) != r.resizable[i]) { bool value = r.resizable[i]; api.Invoke(flags[i].second, window, {&value}); }
    if (Read<bool>(window, n.vertical)) ShowHandles(r);
    // The game's horizontal bar, whatever its getter (folded with an unrelated
    // getter) reads: set_gaugeOrientation swaps its class and rebuilds it, the
    // same value included.
    int horizontal = Horizontal;
    api.Invoke(n.setOrientation, r.gauge.get(), {&horizontal});
    // The size to give back is its saved size in the current layout, when it
    // has one: the one the game's placements gave it meanwhile, had its resize
    // handles been on (another layout's, a fight's, since it turned vertical).
    // Best effort: the size kept otherwise.
    try {
        float width = 0, height = 0;
        if (SavedSize(r, width, height))
            for (auto& owned : r.styles) {
                if (owned.style.get() != r.windowStyle.get() || (owned.property != Width && owned.property != Height)) continue;
                Ref box(ReadStyle(owned.style.get(), owned.property));
                Assign(box.get(), Unit::Pixel, owned.property == Width ? width : height);
                owned.before = std::move(box);
            }
    } catch (const std::exception&) {}
    // A width that did not read back as written (s.failed) leaves the comparison
    // meaningless: every value is then put back.
    for (auto owned = r.styles.rbegin(); owned != r.styles.rend(); ++owned) {
        Ref current(ReadStyle(owned->style.get(), owned->property));
        if (s.failed || Same(current.get(), owned->applied.get(), owned->property))
        {
            // None of its own before (keyword Null): the UXML rule's value, when
            // it gives one (UxmlRule), else none again (the style sheets').
            Ref rule(Keyword(owned->before.get(), owned->property) == 1 ? UxmlValue(*owned) : nullptr);
            WriteStyle(owned->style.get(), owned->property, rule.get() ? rule.get() : owned->before.get());
        }
    }
    // A value put back as « none » (keyword Null: the UXML's own, row for
    // ctr_main, 68 px for the level, 240 px the window's minimum width…) is not
    // the UXML's yet: InlineStyleAccess.RemoveInlineStyle takes the
    // property from the style sheets' cached style (StyleCache, else
    // InitialStyle: a column), never from the element's UXML inline rule
    // (m_InlineRule), which only a full style update applies again. That
    // update, as a class change asks for it: IncrementVersion(StyleSheet) on
    // each element the layout changed.
    for (const auto& part : r.parts) {
        if (!part.get()) continue;
        int styleSheet = StyleSheetChange;
        api.Invoke(n.incrementVersion, part.get(), {&styleSheet});
    }
    if (!r.left.get() || !r.top.get()) return; // failed before anything of its place was written
    const Rect screen = LayoutOf(r.container.get());
    Ref width(ReadStyle(r.windowStyle.get(), Width)), height(ReadStyle(r.windowStyle.get(), Height));
    Box box{0, 0, PixelValue(width.get()), PixelValue(height.get())};
    if (box.width <= 0) box.width = UxmlWidth;
    if (box.height <= 0) box.height = UxmlHeight;
    Stored stored = r.saved;
    try { stored = StoredPlace(r); } catch (const std::exception&) {}
    const bool same = stored == r.saved && Laid(screen) && screen.width == r.screenWidth0 && screen.height == r.screenHeight0;
    if (same || !stored.has || !Laid(screen)) {
        WriteStyle(r.windowStyle.get(), Left, r.left.get());
        WriteStyle(r.windowStyle.get(), Top, r.top.get());
    } else {
        const Box place = SavedPlace(stored.x, stored.y, stored.right, stored.bottom, box.width, box.height, screen.width, screen.height);
        Pixels(r.windowStyle.get(), Left, place.x);
        Pixels(r.windowStyle.get(), Top, place.y);
    }
    // Only a place the game wrote can be kept on the screen (its size: the
    // game's, else the UXML's); none written, the UXML's place is the game's.
    // Less than half a pixel over an edge (the game's own rounding) stays as it is.
    if (!Laid(screen)) return;
    Ref left(ReadStyle(r.windowStyle.get(), Left)), top(ReadStyle(r.windowStyle.get(), Top));
    if (!OwnPixels(left.get(), box.x) || !OwnPixels(top.get(), box.y)) return;
    const Box kept = KeepOnScreen(box, screen.width, screen.height);
    if (std::abs(kept.x - box.x) > .5f) Pixels(r.windowStyle.get(), Left, kept.x);
    if (std::abs(kept.y - box.y) > .5f) Pixels(r.windowStyle.get(), Top, kept.y);
}
// Turns one gauge vertical; a failure undoes at once what was written (or at
// the next pump, if that fails too).
void Apply(Obj widget, Obj hub) {
    const int type = Read<int>(widget, n.gaugeType);
    if (!ValidType(type) || (s.skipped & type)) return;
    Elements e;
    if (const char* why = Resolve(widget, e)) { Unexpected(type, why); return; }
    // Not laid out yet: the game places a new widget at its first layout (its
    // saved size included, the one to put back later); hidden, it waits to be
    // shown. The next pump.
    if (!Attached(e.parts[Window].get()) || !Laid(LayoutOf(e.parts[Window].get())) || !Laid(LayoutOf(e.container.get()))) return;
    if (api.Value<int>(api.Invoke(n.getOrientation, e.parts[Gauge].get())) != Horizontal) { Unexpected(type, "orientation"); return; }
    Record record;
    record.widget.reset(widget);
    record.hub.reset(hub);
    record.container.reset(e.container.get());
    record.window.reset(e.parts[Window].get());
    record.gauge.reset(e.parts[Gauge].get());
    record.type = type;
    s.records.push_back(std::move(record)); // owns the rollback before the first write
    try { Build(s.records.back(), e); }
    catch (...) {
        try { Restore(s.records.back()); s.records.pop_back(); }
        catch (const std::exception&) { s.records.back().undo = true; }
        throw;
    }
    Log("VERTICAL_GAUGE_ON type=" + Hex(type) + " length=" + Number(s.records.back().length));
}
// The length and place the player chose kept in the settings (a failed save
// keeps them for the session).
void Keep(Record& r, bool now) {
    if (!r.dirty || (!now && s.clock() - r.changed < KeepAfterMs) || !Active()) return;
    r.dirty = false;
    Settings next = module->settings;
    auto& box = next.gaugeVerticalBoxes[Bit(r.type)];
    box.length = r.chosen > 0 ? KeptLength(r.chosen) : 0;
    box.x = KeptPlace(r.fx, 1);
    box.y = KeptPlace(r.fy, 1);
    r.basis = box;
    if (next == module->settings) return;
    try { module->Change(next); }
    catch (const std::exception& error) { Log(std::string("VERTICAL_GAUGES_SAVE_ERROR ") + error.what()); }
    // length=width: none chosen (a drag only), as long as the gauge is wide at the horizontal.
    Log("VERTICAL_GAUGE_KEPT type=" + Hex(r.type) + " length=" + (box.length > 0 ? std::to_string(box.length) : std::string("width")) +
        " place=" + std::to_string(box.x) + "," + std::to_string(box.y));
}
// Back as the game built it, then forgotten; kept (and tried again) on a failure.
bool Undo(size_t index) {
    auto& r = s.records[index];
    const int type = r.type;
    Keep(r, true);
    try { Restore(r); }
    catch (const std::exception& error) { Fault(type, "restore", error.what()); return false; }
    if (!r.id.empty()) s.recent.emplace_back(r.id, s.clock() + RecentMs);
    // Its diagnostic once the game has updated its style (its next frames).
    if (!r.samples.empty() && s.checks.size() < 8) {
        Diagnosis check;
        check.type = type;
        for (size_t part = 0; part < PartCount; ++part) check.parts[part].reset(r.parts[part].get());
        check.samples = r.samples;
        check.bar = r.bar;
        check.due = s.clock() + DiagnoseAfterMs;
        s.checks.push_back(std::move(check));
    }
    s.records.erase(s.records.begin() + static_cast<std::ptrdiff_t>(index));
    Log("VERTICAL_GAUGE_OFF type=" + Hex(type));
    return true;
}
// The game wrote the window's size while vertical (the default place of the
// character's gauge: at each re-placement while it has no saved place, the
// positions reset): that size is the one to put back, its width the gauge's
// length unless the player chose one. The vertical size is written again
// before the next layout, and the gauge put back at its place at once, hidden
// or not.
void Replaced(Record& r, Obj width) {
    const float pixels = PixelValue(width);
    const Rect screen = LayoutOf(r.container.get());
    if (pixels > 0) r.width = pixels;
    if (Laid(screen)) r.screenHeight = screen.height;
    r.length = Length(r.chosen > 0 ? r.chosen : r.width, r.screenHeight);
    Own(r, Window, r.windowStyle.get(), Width, Unit::Pixel, Thickness);
    Own(r, Window, r.windowStyle.get(), Height, Unit::Pixel, r.length);
    Settle(r);
    Report(s.replacedLog, "VERTICAL_GAUGE_REPLACED type=" + Hex(r.type) + " width=" + Number(pixels));
}
// The player pulled its top or bottom edge (the game's resizer wrote its
// height, and its top for the top edge, between the minimum and maximum the
// layout gives it): that length taken, bounded again, its value before
// unchanged; its place taken too.
void Resized(Record& r, Obj height) {
    const Rect screen = LayoutOf(r.container.get());
    if (Laid(screen)) r.screenHeight = screen.height;
    float pixels = 0;
    if (!OwnPixels(height, pixels) || !(pixels > 0)) { Set(r, Height, r.length); return; } // no length of its own: the length again
    r.length = Length(pixels, r.screenHeight);
    r.chosen = r.length;
    Set(r, Height, r.length);
    Box now{};
    if (Laid(screen) && InlinePlace(r, now)) TakePlace(r, now, screen);
    Changed(r);
}
// Its inline place moved by the game's own placement: back at its place at
// once (a drag is taken at the pump, once it stays).
void KeepPlace(Record& r) {
    Box now{};
    if (!InlinePlace(r, now) || Near(now, r.at)) return;
    if (r.checkedValid && Near(now, r.checked)) return; // this place already looked at
    r.checked = now;
    r.checkedValid = true;
    if (GamePlaced(r, now)) Settle(r);
}
// Another screen height (the resolution, the interface scale): the length
// follows it, from the same width or chosen length, its maximum too, and the
// place is kept on it.
void Rescaled(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen) || screen.height == r.screenHeight) return;
    r.screenHeight = screen.height;
    r.length = Length(r.chosen > 0 ? r.chosen : r.width, r.screenHeight);
    Set(r, MaxHeight, MaxLength(r.screenHeight));
    Set(r, Height, r.length);
    r.settlePending = true;
}
// Another layout (a preset chosen, a fight's): with the resize flags off the
// game's placements no longer give the gauge its saved size there; that size
// is its horizontal width, as the game shows it horizontal, and the length
// follows it unless the player chose one.
void FollowSavedSize(Record& r) {
    float width = 0, height = 0;
    if (!SavedSize(r, width, height) || width == r.width) return;
    r.width = width;
    if (r.chosen > 0) return;
    const float length = Length(r.width, r.screenHeight);
    if (length == r.length) return;
    r.length = length;
    Set(r, Height, length);
    r.settlePending = true;
}
// At each pump: a place the game's placement wrote put back; a place of the
// player's (a drag: the game keeps the window where it was dropped) taken once
// it stays from one pump to the next.
void Track(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    Box now{};
    if (!Laid(screen) || !InlinePlace(r, now) || Near(now, r.at)) { r.seenValid = false; return; }
    if (GamePlaced(r, now)) { Settle(r); return; }
    if (r.seenValid && Near(now, r.seen)) {
        TakePlace(r, now, screen);
        Changed(r);
        return;
    }
    r.seen = now;
    r.seenValid = true;
}
// The hub's gauges (gfc.dyfh: Dictionary<fqd+GaugesType, WidgetProgressGaugeUi>),
// enumerated by name as every service lookup is (fully shared generic code).
std::vector<Ref> Widgets(Obj hub) {
    std::vector<Ref> widgets;
    Obj gauges = Read<Obj>(hub, n.gauges);
    if (!gauges) return widgets;
    Ref values(api.Call(gauges, "get_Values"));
    Ref iterator(api.Call(values.get(), "GetEnumerator"));
    try {
        for (unsigned count = 0; api.Value<bool>(api.Call(iterator.get(), "MoveNext"));) {
            if (++count > 64) throw std::runtime_error("Unexpected gauge count");
            Obj widget = api.Call(iterator.get(), "get_Current");
            if (widget && api.object_get_class(widget) == n.gaugeUi) widgets.emplace_back(widget);
        }
    } catch (...) { api.Call(iterator.get(), "Dispose"); throw; }
    api.Call(iterator.get(), "Dispose");
    return widgets;
}

// --------------------------------------------------------- the game's layout
// The vertical gauge whose UI id this is: its type; -1 for one put back
// within a second (a save on its way); 0 for any other.
int VerticalId(Obj id) {
    if (!s.installed || !id || (s.records.empty() && s.recent.empty())) return 0;
    const std::wstring text = Text(id);
    if (text.empty()) return 0;
    for (const auto& r : s.records) if (r.id == text) return r.type;
    const uint64_t now = s.clock();
    for (const auto& [recent, until] : s.recent) if (now < until && recent == text) return -1;
    return 0;
}
// UiPresetOptionsContainer.SaveUiPositionById and SaveUiDimensionById, as
// goz`1.blwu (after a resize), blwv (after a drag) and blww run them: left out
// for a vertical gauge, whose place and size are the vertical box's; the
// game's layout keeps the horizontal ones. Never out of the game's call.
void Skipped(int type, const char* what) {
    Report(s.layoutLog, "VERTICAL_GAUGE_LAYOUT_KEPT type=" + Hex(type > 0 ? type : 0) + " " + what);
}
void SavePositionHook(Obj self, Obj id, Obj coordinates, bool right, bool bottom, Method method) {
    int type = 0;
    try { type = VerticalId(id); } catch (...) { type = 0; }
    if (type != 0) { try { Skipped(type, "place"); } catch (...) {} return; }
    originalSavePosition(self, id, coordinates, right, bottom, method);
}
void SaveDimensionHook(Obj self, Obj id, Obj coordinates, Method method) {
    int type = 0;
    try { type = VerticalId(id); } catch (...) { type = 0; }
    if (type != 0) { try { Skipped(type, "size"); } catch (...) {} return; }
    originalSaveDimension(self, id, coordinates, method);
}

// ---------------------------------------------------------------- the row
int Count(Obj list) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > 256) throw std::runtime_error("Unexpected gauge menu size");
    return count;
}
Obj ItemAt(Obj list, int index) { return api.Call(list, "get_Item", {&index}); }
bool Present(Obj menu, Obj item) {
    Ref rows(api.Invoke(n.menuItems, menu));
    for (int i = 0, count = Count(rows.get()); i < count; ++i) if (ItemAt(rows.get(), i) == item) return true;
    return false;
}
Obj RootOf(Obj menu) {
    Obj root = menu ? Read<Obj>(menu, n.menuRoot) : nullptr;
    return root && api.Invoke(n.menuData, root) == menu ? root : nullptr;
}
// The row's menu closed if still shown, the row forgotten.
void CloseRow() {
    auto row = std::move(s.row);
    if (!row) return;
    row->armed = false;
    Obj root = RootOf(row->menu.get());
    if (root && Attached(root)) {
        bool delayed = false;
        api.Invoke(n.removeMenu, root, {&delayed});
    }
}
// After the game's own section: the row before its first separator (the one
// opening « Gestion des jauges »), so right after « Masquer le module » (or
// « Couleur de jauge » when the game leaves it out); last without one.
void AddRow(Obj widget, Obj menu) {
    if (!Active() || !module->settings.gaugeVerticalMenu || !widget || !menu) return;
    if (api.object_get_class(widget) != n.gaugeUi || api.object_get_class(menu) != n.menu) return;
    const int type = Read<int>(widget, n.gaugeType);
    if (!ValidType(type) || (s.skipped & type)) return;
    s.row.reset(); // the last menu's row: the game closed that menu before building this one
    bool on = (module->settings.gaugeVertical & type) != 0, disabled = false, close = true;
    unsigned delay = 1000;
    int icon = 0;
    Ref label(api.string_new(RowLabel)), help(api.string_new(RowHelp)), empty(api.string_new(""));
    api.Invoke(n.appendCheckbox, menu, {label.get(), nullptr, &on, &disabled, &close, help.get(), &delay, &icon, empty.get()});
    Ref rows(api.Invoke(n.menuItems, menu));
    int last = Count(rows.get()) - 1;
    if (last < 0) throw std::runtime_error("Gauge menu row missing");
    Ref item(ItemAt(rows.get(), last));
    Ref callback(item.get() ? api.Invoke(n.itemCallback, item.get()) : nullptr);
    if (!callback.get() || api.object_get_class(callback.get()) != n.callback) throw std::runtime_error("Unexpected gauge menu callback handler");
    int at = -1;
    for (int i = 0; i < last && at < 0; ++i) {
        Ref row(ItemAt(rows.get(), i));
        if (row.get() && api.object_get_class(row.get()) == n.separator) at = i;
    }
    auto row = std::make_unique<Row>();
    row->widget.reset(widget);
    row->menu.reset(menu);
    row->item.reset(item.get());
    row->callback.reset(callback.get());
    row->cached.reset(module->cached.get());
    row->compositor.reset(module->compositor.get());
    row->type = type;
    row->on = on;
    row->created = s.clock();
    s.row = std::move(row);
    // Appended last, the row moves up to its place (InsertItem gives it its menu again).
    if (at >= 0) {
        api.Invoke(n.removeItemAt, menu, {&last});
        api.Invoke(n.insertItem, menu, {&at, item.get()});
    }
    s.row->armed = true;
}
void SectionHook(Obj self, Obj menu, Method method) {
    originalSection(self, menu, method);
    try { AddRow(self, menu); }
    catch (const std::exception& error) { Report(s.menuLog, std::string("VERTICAL_GAUGES_MENU_ERROR ") + error.what()); }
    catch (...) { Report(s.menuLog, "VERTICAL_GAUGES_MENU_ERROR unknown"); }
}
// The clicked gauge follows at once (the pump would ~200 ms later).
void Follow(Obj widget) {
    const int type = Read<int>(widget, n.gaugeType);
    const bool wanted = (Mask() & type) != 0;
    for (size_t i = 0; i < s.records.size(); ++i)
        if (s.records[i].widget.get() == widget) {
            if (!wanted) Undo(i);
            return;
        }
    if (!wanted) return;
    Ref hub(module->FindService("fqd"));
    if (Is(hub.get(), n.hub)) Guarded(type, "apply", [&] { Apply(widget, hub.get()); });
}
// A click on the row: the game already ticked or unticked it, and closes the
// menu right after (closeOnSelect). Anything stale is consumed without effect.
void Click(Obj view) {
    auto& row = *s.row;
    if (!row.armed || !Active() || !module->settings.gaugeVerticalMenu || row.cached.get() != module->cached.get() ||
        row.compositor.get() != module->compositor.get() || !view || !Attached(view) || api.Invoke(n.itemData, view) != row.item.get() ||
        api.Value<bool>(api.Invoke(n.itemDisabled, row.item.get())) || !Present(row.menu.get(), row.item.get()))
        return;
    Obj widget = row.widget.get();
    if (!widget || Read<int>(widget, n.gaugeType) != row.type) return;
    row.armed = false;
    const int type = row.type;
    Ref keep(widget);
    Settings next = module->settings;
    next.gaugeVertical ^= type;
    try { module->Change(next); }
    catch (const std::exception& error) { Log(std::string("VERTICAL_GAUGES_SAVE_ERROR ") + error.what()); }
    Log("VERTICAL_GAUGE_SET type=" + Hex(type) + " on=" + ((module->settings.gaugeVertical & type) ? "1" : "0"));
    Follow(keep.get());
}
// The row's menu: forgotten once closed (or never shown within five seconds),
// closed when the options change under it (no stale box).
void TickRow() {
    if (!s.row) return;
    auto& row = *s.row;
    Obj root = RootOf(row.menu.get());
    const bool attached = root && Attached(root);
    if (attached) row.seen = true;
    if (!attached) {
        if (row.seen || s.clock() - row.created > RowAttachMs) s.row.reset();
        return;
    }
    if (!module->settings.gaugeVerticalMenu || ((module->settings.gaugeVertical & row.type) != 0) != row.on) CloseRow();
}
// Every gauge put back; one that fails stays (CleanupPending) and is tried again.
void RestoreAll() {
    try { CloseRow(); }
    catch (const std::exception& error) { s.row.reset(); Report(s.errorLog, std::string("VERTICAL_GAUGES_ERROR menu ") + error.what()); }
    for (size_t i = 0; i < s.records.size();)
        if (!Undo(i)) ++i;
}
void Tick() {
    TickRow();
    RunDiagnoses();
    const uint64_t now = s.clock();
    std::erase_if(s.recent, [now](const auto& recent) { return now >= recent.second; });
    const int mask = Mask();
    if (!mask && s.records.empty()) return; // nothing asked, nothing to put back: no call to the game
    Ref hub(module->FindService("fqd"));
    if (!Is(hub.get(), n.hub)) return;      // the hub not there: everything kept as it is
    const auto widgets = Widgets(hub.get());
    const auto held = [&widgets](Obj widget) {
        return std::any_of(widgets.begin(), widgets.end(), [widget](const Ref& w) { return w.get() == widget; });
    };
    for (size_t i = 0; i < s.records.size();) {
        auto& r = s.records[i];
        // Discarded by the game (a new hub, a new widget): forgotten without a call.
        if (r.hub.get() != hub.get() || !held(r.widget.get())) {
            Log("VERTICAL_GAUGE_GONE type=" + Hex(r.type));
            s.records.erase(s.records.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        // Another length or place in the settings than its own, the player's
        // own change not pending (a saved layout applied, 1.20.0): put back,
        // then laid out again below with them.
        if (!r.undo && !r.dirty && !(module->settings.gaugeVerticalBoxes[Bit(r.type)] == r.basis)) {
            Log("VERTICAL_GAUGE_RELAYOUT type=" + Hex(r.type));
            r.undo = true;
        }
        if (r.undo || !(mask & r.type)) {
            if (!Undo(i)) ++i;
            continue;
        }
        Guarded(r.type, "place", [&r] {
            FollowSavedSize(r);
            Rescaled(r);
            Track(r);
            if (r.settlePending) Settle(r);
            ShowHandles(r);
            Keep(r, false);
        });
        ++i;
    }
    for (const auto& widget : widgets) {
        const int type = Read<int>(widget.get(), n.gaugeType);
        if (!ValidType(type) || !(mask & type)) continue;
        if (std::any_of(s.records.begin(), s.records.end(), [&widget](const Record& r) { return r.widget.get() == widget.get(); })) continue;
        Guarded(type, "apply", [&] { Apply(widget.get(), hub.get()); });
    }
}
// Each frame: the game's own size writes (its placements run in the panel's
// update, after this compositor frame) undone before the next layout, the
// player's resizes taken, the game's re-placements put back.
void Frame() {
    for (auto& r : s.records) {
        try {
            if (r.undo || r.styles.size() < 2) continue;
            Ref width(ReadStyle(r.windowStyle.get(), Width));
            if (!Same(width.get(), r.styles[0].applied.get(), Width)) { Replaced(r, width.get()); continue; }
            Ref height(ReadStyle(r.windowStyle.get(), Height));
            if (!Same(height.get(), r.styles[1].applied.get(), Height)) Resized(r, height.get());
            KeepPlace(r);
        } catch (const std::exception& error) { Report(s.errorLog, std::string("VERTICAL_GAUGES_ERROR frame type=") + Hex(r.type) + " " + error.what()); }
        catch (...) { Report(s.errorLog, "VERTICAL_GAUGES_ERROR frame type=" + Hex(r.type) + " unknown"); } // never out of the compositor's Update
    }
}

void Install() {
    n.gaugeUi = api.Class(CoreImage, "Core.UILogic.XpGauge", "WidgetProgressGaugeUi");
    n.gaugeType = RequireField(n.gaugeUi, "m_gaugeType", 0x184);
    n.binding = api.Class(CoreImage, "Core.UILogic.Bindings", "WidgetProgressGaugeBinding");
    for (size_t part = 0; part < PartCount; ++part)
        n.parts[part] = api.FindVerified(n.binding, PartGetters[part].name, PartGetters[part].argc, PartGetters[part].rva);
    n.hub = api.Class(CoreImage, "", "gfc");
    n.gauges = RequireField(n.hub, "dyfh", 0xF0);
    // The window: its content element holds ctr_main; its resize flags, its
    // lock and its top and bottom handles.
    n.window = api.Class(CoreImage, Components, "MagneticDragWindow");
    n.resizable = api.Class(CoreImage, Components, "ResizableWindow");
    n.content = RequireField(n.resizable, "<content>k__BackingField", 0x310);
    n.vertical = RequireField(n.resizable, "m_verticalResizable", 0x354);
    n.horizontal = RequireField(n.resizable, "m_horizontalResizable", 0x355);
    n.corner = RequireField(n.resizable, "m_cornerResizable", 0x356);
    n.locked = RequireField(n.resizable, "m_isUILocked", 0x2CC);
    n.topOff = RequireField(n.resizable, "m_disableTopHandle", 0x2CA);
    n.bottomOff = RequireField(n.resizable, "m_disableBottomHandle", 0x2CB);
    n.dragPosition = RequireField(n.resizable, "m_dragPosition", 0x350);
    n.setVertical = api.FindVerified(n.resizable, "set_verticalResizable", 1, 0x15BFCF0);
    n.setHorizontal = api.FindVerified(n.resizable, "set_horizontalResizable", 1, 0x15BFC40);
    n.setCorner = api.FindVerified(n.resizable, "set_cornerResizable", 1, 0x15BF930);
    n.enableHandle = api.FindVerified(n.resizable, "EnableResizeHandler", 1, 0x15BCB60);
    n.disableHandle = api.FindVerified(n.resizable, "DisableResizeHandler", 1, 0x15BC990);
    // The bar (ExtendedProgressBarLong): its orientation is BaseProgressBar<long, …>'s,
    // fully shared code with a plain signature (the enum by value).
    n.bar = api.Class(CoreImage, Components, "ExtendedProgressBarLong");
    const auto barBase = api.class_get_parent(n.bar);
    if (!barBase || std::string(api.class_get_name(barBase)) != "BaseProgressBar`2") throw std::runtime_error("Gauge bar base changed");
    n.setOrientation = api.FindVerified(barBase, "set_gaugeOrientation", 1, 0x3C94C70);
    n.getOrientation = api.FindVerified(barBase, "get_gaugeOrientation", 0, 0x3925FB0);
    // The menu and its rows.
    n.menu = api.Class(CoreImage, Menus, "DofusContextualMenu");
    n.menuRoot = RequireField(n.menu, "dofusContextualMenuRoot", 0x38);
    n.appendCheckbox = api.FindVerified(n.menu, "AppendCheckboxItem", 9, 0x16D63D0);
    n.removeItemAt = api.FindVerified(n.menu, "RemoveItemAt", 1, 0x16D8BF0);
    n.insertItem = api.FindVerified(n.menu, "InsertItem", 2, 0x16D8850);
    n.menuItems = api.FindVerified(n.menu, "get_menuItems", 0, 0x68D2B0);
    n.item = api.Class(CoreImage, Menus, "ContextMenuItem");
    n.itemCallback = api.FindVerified(n.item, "get_m_callback", 0, 0x16CF040);
    n.itemDisabled = api.FindVerified(n.item, "get_disabled", 0, 0x16CF020);
    n.view = api.NestedClass(n.item, "ContextMenuItemView");
    n.itemData = api.FindVerified(n.view, "get_itemData", 0, 0x157EFD0);
    n.callback = api.NestedClass(n.item, "ClickCallbackHandler");
    n.root = api.NestedClass(n.menu, "DofusContextualMenuRoot");
    n.menuData = api.FindVerified(n.root, "get_menuData", 0, 0x157EFE0);
    n.removeMenu = api.FindVerified(n.root, "RemoveMenu", 1, 0x16D2AB0);
    n.separator = api.Class(CoreImage, Menus, "ContextMenuSeparatorItem");
    // Elements and their inline styles.
    n.element = api.Class(UiImage, UiSpace, "VisualElement");
    n.parent = api.FindVerified(n.element, "get_parent", 0, 0x55F7180);
    n.panel = api.FindVerified(n.element, "get_panel", 0, 0x55F7170);
    n.layout = api.FindVerified(n.element, "get_layout", 0, 0x55F6930);
    n.name = api.FindVerified(n.element, "get_name", 0, 0x3158710);
    n.incrementVersion = api.FindVerified(n.element, "IncrementVersion", 1, 0x55E5580);
    n.classListContains = api.FindVerified(n.element, "ClassListContains", 1, 0x55E1840);
    for (size_t property = 0; property < PropertyCount; ++property)
        n.resolve[property] = api.FindVerified(n.element, Resolvers[property].get.name, Resolvers[property].get.argc, Resolvers[property].get.rva);
    n.getStyle = api.FindVerified(n.element, "get_style", 0, 0x55F74A0);
    n.style = api.Class(UiImage, UiSpace, "InlineStyleAccess");
    for (size_t property = 0; property < PropertyCount; ++property) {
        const auto& accessor = Accessors[property];
        n.get[property] = api.FindVerified(n.style, accessor.get.name, accessor.get.argc, accessor.get.rva);
        n.set[property] = api.FindVerified(n.style, accessor.set.name, accessor.set.argc, accessor.set.rva);
    }
    n.length = api.Class(UiImage, UiSpace, "Length");
    api.FindVerified(n.length, ".ctor", 2, 0x561D3D0);
    n.setLength = api.FindVerified(api.Class(UiImage, UiSpace, "StyleLength"), "set_value", 1, 0x5623940);
    // The size and the place the game would apply: those saved for the gauge's
    // UI id; and the game's saves of them, left out for a vertical gauge.
    n.cachedData = api.Class(CoreImage, "Core.Services.Player.OptionsService", "CachedDataService");
    n.presetContainer = api.FindVerified(n.cachedData, "get_uiPresetOptionsContainer", 0, 0x7E0890);
    n.presets = api.Class(CoreImage, "Core.Engine.Options.OptionsContainers", "UiPresetOptionsContainer");
    n.loadDimension = api.FindVerified(n.presets, "LoadUiDimensionById", 1, 0x20918C0);
    n.loadPosition = api.FindVerified(n.presets, "LoadUiPositionById", 1, 0x2091B60);
    n.savePosition = api.FindVerified(n.presets, "SaveUiPositionById", 4, 0x2092DE0);
    n.saveDimension = api.FindVerified(n.presets, "SaveUiDimensionById", 2, 0x2092A80);
    n.coordinates = api.Class(CoreImage, "Core.UI", "UICoordinates");
    n.savedX = RequireField(n.coordinates, "x", 0x10);
    n.savedY = RequireField(n.coordinates, "y", 0x14);
    // Last: the detours. The layout's two saves (each runs the game's own unless
    // the UI id is a vertical gauge's), then the menu's builder (its only
    // caller, OpenGaugeMenu, shows the menu right after).
    AddHook(n.savePosition, 0x2092DE0, reinterpret_cast<void*>(&SavePositionHook), reinterpret_cast<void**>(&originalSavePosition));
    AddHook(n.saveDimension, 0x2092A80, reinterpret_cast<void*>(&SaveDimensionHook), reinterpret_cast<void**>(&originalSaveDimension));
    AddHook(api.FindVerified(n.gaugeUi, "CreateGenericGaugeSection", 1, 0x2815820), 0x2815820,
        reinterpret_cast<void*>(&SectionHook), reinterpret_cast<void**>(&originalSection));
}
}
}

void InstallVerticalGauges() {
    using namespace verticalGauges;
    if (s.installed) return;
    // Optional: a contract that does not match turns this feature off, nothing else.
    try { Install(); }
    catch (const std::exception& error) { Log(std::string("VERTICAL_GAUGES_DISABLED ") + error.what()); return; }
    s.installed = true;
    s.failed = false;
    Log("VERTICAL_GAUGES_READY");
}
void TickVerticalGauges() {
    using namespace verticalGauges;
    if (!s.installed) return;
    try {
        if (!Active()) { RestoreAll(); return; }
        Tick();
        if (s.failed) RestoreAll();
    } catch (const std::exception& error) { Report(s.errorLog, std::string("VERTICAL_GAUGES_ERROR ") + error.what()); }
    catch (...) { Report(s.errorLog, "VERTICAL_GAUGES_ERROR unknown"); }
}
void VerticalGaugesFrame(uint64_t) {
    using namespace verticalGauges;
    if (s.records.empty() || !Active()) return;
    Frame();
}
void RestoreVerticalGauges() {
    using namespace verticalGauges;
    const uint64_t now = s.clock();
    if (now < s.nextRestore) return;
    RestoreAll();
    if (s.records.empty()) { s.restoreAttempts = 0; s.nextRestore = 0; return; }
    s.nextRestore = now + RestoreRetryMs;
    if (++s.restoreAttempts < MaxRestoreAttempts) return;
    Log("VERTICAL_GAUGES_RESTORE_ABANDONED gauges=" + std::to_string(s.records.size()));
    s.records.clear();
    s.restoreAttempts = 0;
    s.nextRestore = 0;
}
bool VerticalGaugesCleanupPending() {
    using namespace verticalGauges;
    return !s.records.empty() || s.row != nullptr;
}
bool HandleVerticalGaugeMenuCallback(managed::Obj callback, managed::Obj view) {
    using namespace verticalGauges;
    if (!callback || !s.row || callback != s.row->callback.get()) return false;
    // Never out of the game's InvokeCallback.
    try { Click(view); }
    catch (const std::exception& error) { Report(s.menuLog, std::string("VERTICAL_GAUGES_MENU_ERROR ") + error.what()); }
    catch (...) { Report(s.menuLog, "VERTICAL_GAUGES_MENU_ERROR unknown"); }
    return true;
}
