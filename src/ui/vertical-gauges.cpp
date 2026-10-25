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
#include <vector>

// « Afficher à la verticale » (1.18.0). Each of the game's gauges is its own
// widget (WidgetProgressGaugeUi, one per fqd+GaugesType, kept by the gauges'
// hub gfc), with its own « … » button: the menu it opens gets a native
// checkbox row right after the gauge's own rows, before the game's « Gestion
// des jauges ». Ticked, the gauge turns vertical through the game's own
// vertical progress bar (BaseProgressBar`2.gaugeOrientation: it fills from the
// bottom, its graduations follow) and a column layout of the widget's own
// elements (inline styles, each one captured before it is written and put back
// exactly). Its window's resize handles are off meanwhile, so the game never
// saves a vertical size, nor applies a saved one. Unticked, with the options'
// switch unticked or at Stop, the gauge is again what the game built.
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
    "Elle garde sa longueur et sa place et se déplace comme avant ; sa taille se change seulement à l’horizontale. "
    "Décoche pour la remettre à l’horizontale.";
constexpr int Horizontal = 0, Vertical = 1; // BaseProgressBar`2+GaugeOrientationEnum
constexpr uint64_t RowAttachMs = 5000, RestoreRetryMs = 250;
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
enum Property : size_t { Width, Height, MinWidth, Direction, PaddingLeft, PaddingTop, PaddingBottom, MarginLeft, MarginTop, Left, Top,
    PropertyCount };
struct Accessor { Contract get, set; };
constexpr Accessor Accessors[PropertyCount] = {
    {{"UnityEngine.UIElements.IStyle.get_width", 0, 0x55D2740}, {"UnityEngine.UIElements.IStyle.set_width", 1, 0x55D63F0}},
    {{"UnityEngine.UIElements.IStyle.get_height", 0, 0x55D1540}, {"UnityEngine.UIElements.IStyle.set_height", 1, 0x55D3D60}},
    {{"UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x55D1850}, {"UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x55D4410}},
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
// The vertical layout, the window's width first (VerticalGaugesFrame watches
// it), with the widget UXML's own values in comments. The window's values
// follow from the length; the others are starting values to tune in game.
constexpr Written Layout[] = {
    {Window, Width, Unit::Pixel, Thickness},           // 566 px, or the game's own size
    {Window, Height, Unit::Pixel, TheLength},          // 34 px, or the game's own size
    {Window, MinWidth, Unit::Pixel, Thickness},        // 240 px
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
static_assert(std::size(Layout) == 25 && Layout[0].part == Window && Layout[0].property == Width);

struct Natives {
    Klass gaugeUi{}, binding{}, hub{}, window{}, resizable{}, bar{}, menu{}, item{}, view{}, callback{}, root{}, separator{};
    Klass element{}, style{}, length{}, cachedData{}, presets{}, coordinates{};
    std::array<Method, PartCount> parts{};
    std::array<Method, PropertyCount> get{}, set{};
    Method setOrientation{}, getOrientation{}, setVertical{}, setHorizontal{}, setCorner{};
    Method appendCheckbox{}, removeItemAt{}, insertItem{}, menuItems{}, itemCallback{}, itemDisabled{}, itemData{}, menuData{}, removeMenu{};
    Method parent{}, panel{}, layout{}, name{}, getStyle{}, setLength{}, presetContainer{}, loadDimension{}, loadPosition{};
    // Checked offsets: the gauge's type, the hub's gauges, a menu's root, a
    // window's content and its three resize flags, a saved size's or place's x and y.
    size_t gaugeType = 0, gauges = 0, menuRoot = 0, content = 0, vertical = 0, horizontal = 0, corner = 0, savedX = 0, savedY = 0;
} n;

// One inline value the mod wrote: the element's style (InlineStyleAccess), the
// value before (the game's own, or keyword Null: none, the UXML's rule applies)
// and the one written. Put back only while it is still the mod's.
struct Owned { Ref style, before, applied; Property property = Width; };
// A vertical gauge.
struct Record {
    Ref widget, hub, container, window, gauge, windowStyle;
    int type = 0;
    std::array<bool, 3> resizable{}; // its window's vertical, horizontal and corner resizing, as the game had them
    // Its horizontal width, its length from it on the screen's height when it
    // was last laid out (0: unknown).
    float width = 0, length = 0, screenHeight = 0;
    // Its horizontal layout's size when it turned vertical (0: unknown): a box
    // the game's hub may still keep for it (PlacedWhileHidden).
    float laidWidth = 0, laidHeight = 0;
    std::vector<Owned> styles;       // the window's width first
    bool clampPending = false, undo = false;
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
    int skipped = 0, unexpected = 0;                 // types left horizontal for the session; those found unexpected (logged)
    std::array<unsigned, 32> failures{};             // consecutive errors, by type bit
    unsigned restoreAttempts = 0;
    uint64_t nextRestore = 0;
    Throttle menuLog, errorLog, replacedLog;
    uint64_t (*clock)() = Now;                       // the tests' clock
} s;
using SectionFn = void (*)(Obj, Obj, Method);
SectionFn originalSection = nullptr;

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
Obj Parent(Obj element) { return element ? api.Invoke(n.parent, element) : nullptr; }
bool Attached(Obj element) { return element && api.Invoke(n.panel, element) != nullptr; }
struct Rect { float x, y, width, height; };
Rect LayoutOf(Obj element) { return api.Value<Rect>(api.Invoke(n.layout, element)); }
// A layout to place with: none while hidden or before the first layout pass.
bool Laid(const Rect& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) &&
        box.width > .5f && box.height > .5f && box.width <= 16384 && box.height <= 16384;
}
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
        int number = static_cast<int>(value);
        api.Call(box, "set_value", {&number}); // StyleEnum<T>.set_value: generic, by name as every style helper
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
void Own(Record& r, Obj style, Property property, Unit unit, float value) {
    Ref current(ReadStyle(style, property)), applied(ReadStyle(style, property));
    Assign(applied.get(), unit, value);
    Owned* owned = OwnedOf(r, style, property);
    if (!owned) {
        r.styles.push_back(Owned{Ref(style), std::move(current), std::move(applied), property});
        owned = &r.styles.back();
    } else {
        if (!Same(current.get(), owned->applied.get(), property)) owned->before = std::move(current);
        owned->applied = std::move(applied);
    }
    WriteStyle(style, property, owned->applied.get());
}
void Pixels(Obj style, Property property, float value) {
    Ref box(ReadStyle(style, property));
    Assign(box.get(), Unit::Pixel, value);
    WriteStyle(style, property, box.get());
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
// The place the game's own placements give the gauge (goz`1.blxe): its saved
// place (its drags save the vertical box's own), kept on the screen at its
// size. The game placed a new widget there at its horizontal size, so a gauge
// left vertical near the right edge came back farther left, then went back at
// the game's next re-placement. Used only when the horizontal box is exactly
// where that rule put it: with no saved place, or another writer since, the
// gauge keeps its own place.
bool SavedVertical(Record& r, const Rect& screen, const Box& horizontal, Box& vertical) {
    try {
        Ref saved(Saved(r, n.loadPosition)); // a boxed ValueTuple<id, UICoordinates, right edge, bottom edge>
        Ref point(saved.get() ? api.Field(saved.get(), "Item2") : nullptr);
        if (!point.get()) return false;     // none saved
        const bool right = *static_cast<bool*>(api.FieldAddress(saved.get(), "Item3"));
        const bool bottom = *static_cast<bool*>(api.FieldAddress(saved.get(), "Item4"));
        const float x = Read<float>(point.get(), n.savedX), y = Read<float>(point.get(), n.savedY);
        const auto place = [&](float width, float height) { return SavedPlace(x, y, right, bottom, width, height, screen.width, screen.height); };
        const Box game = place(horizontal.width, horizontal.height);
        if (!(std::abs(game.x - horizontal.x) <= .5f && std::abs(game.y - horizontal.y) <= .5f)) return false;
        vertical = place(Thickness, r.length);
        return true;
    } catch (const std::exception&) { return false; } // its own place
}
// The vertical box entirely on the screen, moved no more than needed (the
// game keeps its windows so: the place stays across its re-placements), from
// its inline place (the game's placements write it, hidden or not), else its
// layout. false while neither is known (hidden) or the screen is not laid
// out: later.
bool Clamp(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen)) return false;
    Box box{0, 0, Thickness, r.length};
    if (!Place(r, box)) return false;
    const Box kept = KeepOnScreen(box, screen.width, screen.height);
    if (kept.x != box.x) Own(r, r.windowStyle.get(), Left, Unit::Pixel, kept.x);
    if (kept.y != box.y) Own(r, r.windowStyle.get(), Top, Unit::Pixel, kept.y);
    return true;
}
void Build(Record& r, const Elements& e) {
    Obj window = e.parts[Window].get();
    r.resizable = {Read<bool>(window, n.vertical), Read<bool>(window, n.horizontal), Read<bool>(window, n.corner)};
    std::array<Ref, PartCount> styles;
    for (size_t part = 0; part < PartCount; ++part) styles[part].reset(StyleOf(e.parts[part].get()));
    r.windowStyle.reset(styles[Window].get());
    const Rect screen = LayoutOf(r.container.get());
    r.width = HorizontalWidth(r);
    r.screenHeight = Laid(screen) ? screen.height : 0;
    r.length = Length(r.width, r.screenHeight);
    Box horizontal{};
    const bool placed = HorizontalBox(r, horizontal); // before the vertical size is written
    if (const Rect laid = LayoutOf(window); Laid(laid)) { r.laidWidth = laid.width; r.laidHeight = laid.height; }
    for (const auto& value : Layout)
        Own(r, styles[value.part].get(), value.property, value.unit, value.value == TheLength ? r.length : value.value);
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
    // Where the game keeps it, when the game placed it from there; then on the screen.
    Box place{};
    if (placed && Laid(screen) && SavedVertical(r, screen, horizontal, place)) {
        if (place.x != horizontal.x) Own(r, r.windowStyle.get(), Left, Unit::Pixel, place.x);
        if (place.y != horizontal.y) Own(r, r.windowStyle.get(), Top, Unit::Pixel, place.y);
    }
    r.clampPending = !Clamp(r);
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
// Back as the game built it: its window's resizing, the horizontal bar, each
// value still the mod's (one the game wrote since stays the game's); then the
// horizontal box kept on the screen, as the game's own placement would.
void Restore(Record& r) {
    Obj window = r.window.get();
    const std::array<std::pair<size_t, Method>, 3> flags{{{n.vertical, n.setVertical}, {n.horizontal, n.setHorizontal}, {n.corner, n.setCorner}}};
    for (size_t i = 0; i < flags.size(); ++i)
        if (Read<bool>(window, flags[i].first) != r.resizable[i]) { bool value = r.resizable[i]; api.Invoke(flags[i].second, window, {&value}); }
    if (api.Value<int>(api.Invoke(n.getOrientation, r.gauge.get())) != Horizontal) {
        int horizontal = Horizontal;
        api.Invoke(n.setOrientation, r.gauge.get(), {&horizontal});
    }
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
            WriteStyle(owned->style.get(), owned->property, owned->before.get());
    }
    // Only a place the game wrote can be kept on the screen (its size: the
    // game's, else the UXML's); none written, the UXML's place is the game's.
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen)) return;
    Ref left(ReadStyle(r.windowStyle.get(), Left)), top(ReadStyle(r.windowStyle.get(), Top));
    Ref width(ReadStyle(r.windowStyle.get(), Width)), height(ReadStyle(r.windowStyle.get(), Height));
    Box box{0, 0, PixelValue(width.get()), PixelValue(height.get())};
    if (!OwnPixels(left.get(), box.x) || !OwnPixels(top.get(), box.y)) return;
    if (box.width <= 0) box.width = UxmlWidth;
    if (box.height <= 0) box.height = UxmlHeight;
    const Box kept = KeepOnScreen(box, screen.width, screen.height);
    if (kept.x != box.x) Pixels(r.windowStyle.get(), Left, kept.x);
    if (kept.y != box.y) Pixels(r.windowStyle.get(), Top, kept.y);
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
    if (!Attached(e.parts[Window].get()) || !Laid(LayoutOf(e.parts[Window].get()))) return;
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
// Back as the game built it, then forgotten; kept (and tried again) on a failure.
bool Undo(size_t index) {
    const int type = s.records[index].type;
    try { Restore(s.records[index]); }
    catch (const std::exception& error) { Fault(type, "restore", error.what()); return false; }
    s.records.erase(s.records.begin() + static_cast<std::ptrdiff_t>(index));
    Log("VERTICAL_GAUGE_OFF type=" + Hex(type));
    return true;
}
// The game wrote the window's size while vertical (the default place of the
// character's gauge: at each re-placement while it has no saved place, the
// positions reset): that size is the one to put back, its width the gauge's
// length. The vertical size is written again before the next layout, and the
// gauge kept on the screen at once from the place the game just wrote (its
// default place is at the bottom), hidden or not; at the pump otherwise.
void Replaced(Record& r, Obj width) {
    const float pixels = PixelValue(width);
    const Rect screen = LayoutOf(r.container.get());
    if (pixels > 0) r.width = pixels;
    if (Laid(screen)) r.screenHeight = screen.height;
    r.length = Length(r.width, r.screenHeight);
    Own(r, r.windowStyle.get(), Width, Unit::Pixel, Thickness);
    Own(r, r.windowStyle.get(), Height, Unit::Pixel, r.length);
    r.clampPending = true;
    r.clampPending = !Clamp(r);
    Report(s.replacedLog, "VERTICAL_GAUGE_REPLACED type=" + Hex(r.type) + " width=" + Number(pixels));
}
// Another screen height (the resolution, the interface scale): the length
// follows it, from the same horizontal width, and the place is kept on it.
void Rescaled(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen) || screen.height == r.screenHeight) return;
    r.screenHeight = screen.height;
    const float length = Length(r.width, r.screenHeight);
    if (length != r.length) {
        r.length = length;
        Own(r, r.windowStyle.get(), Height, Unit::Pixel, length);
    }
    r.clampPending = true;
}
// Another layout (a preset chosen, a fight's): with the resize flags off the
// game's placements no longer give the gauge its saved size there; that size
// is its horizontal width, as the game shows it horizontal, and the length
// follows it.
void FollowSavedSize(Record& r) {
    float width = 0, height = 0;
    if (!SavedSize(r, width, height) || width == r.width) return;
    r.width = width;
    const float length = Length(r.width, r.screenHeight);
    if (length == r.length) return;
    r.length = length;
    Own(r, r.windowStyle.get(), Height, Unit::Pixel, length);
    r.clampPending = true;
}
// Hidden (display none: no layout), the gauge is still placed by the game's
// re-placements, at the box the game's hub keeps for it (goz`1.blxe refreshes
// that box only from a shown layout, before applying any saved size): a
// horizontal one, the UXML's or the one laid out before it turned vertical,
// until a re-placement finds it shown. A place the game's rule gives such a
// box (its saved place, kept on the screen) goes where the same rule puts the
// vertical box, as its next re-placement once shown would. true when written.
bool PlacedWhileHidden(Record& r) {
    const Rect screen = LayoutOf(r.container.get());
    if (!Laid(screen) || Laid(LayoutOf(r.window.get()))) return false;
    const std::array<Box, 2> kept{{{0, 0, UxmlWidth, UxmlHeight}, {0, 0, r.laidWidth, r.laidHeight}}};
    for (size_t i = 0; i < kept.size(); ++i) {
        Box horizontal = kept[i], vertical{};
        if (!(horizontal.width > 0 && horizontal.height > 0) || (i > 0 && horizontal.width == kept[0].width && horizontal.height == kept[0].height))
            continue;
        if (!Place(r, horizontal) || !SavedVertical(r, screen, horizontal, vertical)) continue;
        if (vertical.x == horizontal.x && vertical.y == horizontal.y) return false;
        if (vertical.x != horizontal.x) Own(r, r.windowStyle.get(), Left, Unit::Pixel, vertical.x);
        if (vertical.y != horizontal.y) Own(r, r.windowStyle.get(), Top, Unit::Pixel, vertical.y);
        return true;
    }
    return false;
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
        if (r.undo || !(mask & r.type)) {
            if (!Undo(i)) ++i;
            continue;
        }
        Guarded(r.type, "place", [&r] {
            FollowSavedSize(r);
            Rescaled(r);
            if (PlacedWhileHidden(r)) r.clampPending = true;
            if (r.clampPending) r.clampPending = !Clamp(r);
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
// update, after this compositor frame) are undone before the next layout.
void Frame() {
    for (auto& r : s.records) {
        try {
            if (r.undo || r.styles.empty()) continue;
            Ref width(ReadStyle(r.windowStyle.get(), Width));
            if (!Same(width.get(), r.styles.front().applied.get(), Width)) Replaced(r, width.get());
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
    // The window: its content element holds ctr_main; its resize flags.
    n.window = api.Class(CoreImage, Components, "MagneticDragWindow");
    n.resizable = api.Class(CoreImage, Components, "ResizableWindow");
    n.content = RequireField(n.resizable, "<content>k__BackingField", 0x310);
    n.vertical = RequireField(n.resizable, "m_verticalResizable", 0x354);
    n.horizontal = RequireField(n.resizable, "m_horizontalResizable", 0x355);
    n.corner = RequireField(n.resizable, "m_cornerResizable", 0x356);
    n.setVertical = api.FindVerified(n.resizable, "set_verticalResizable", 1, 0x15BFCF0);
    n.setHorizontal = api.FindVerified(n.resizable, "set_horizontalResizable", 1, 0x15BFC40);
    n.setCorner = api.FindVerified(n.resizable, "set_cornerResizable", 1, 0x15BF930);
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
    // The size and the place the game would apply: those saved for the gauge's UI id.
    n.cachedData = api.Class(CoreImage, "Core.Services.Player.OptionsService", "CachedDataService");
    n.presetContainer = api.FindVerified(n.cachedData, "get_uiPresetOptionsContainer", 0, 0x7E0890);
    n.presets = api.Class(CoreImage, "Core.Engine.Options.OptionsContainers", "UiPresetOptionsContainer");
    n.loadDimension = api.FindVerified(n.presets, "LoadUiDimensionById", 1, 0x20918C0);
    n.loadPosition = api.FindVerified(n.presets, "LoadUiPositionById", 1, 0x2091B60);
    n.coordinates = api.Class(CoreImage, "Core.UI", "UICoordinates");
    n.savedX = RequireField(n.coordinates, "x", 0x10);
    n.savedY = RequireField(n.coordinates, "y", 0x14);
    // Last: the menu's builder (its only caller, OpenGaugeMenu, shows the menu right after).
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
