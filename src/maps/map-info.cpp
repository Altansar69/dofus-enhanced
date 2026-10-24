#include "maps/map-info.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mapInfo {
namespace {
using namespace managed;
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* DataImage = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
constexpr const char* ViewSpace = "Core.Services.Roleplay.CartographyService";
// The two maps are the two cartography views (fhw): the mini-map is the
// banner's (CartographyBannerView), the big map the window's (CartographyMapView).
enum class View { None, Mini, Big };
// The tooltip template (UXML #cartographyTooltip) is 154 px wide. Its row holds
// #ctr_coords (padding 10 + 10) and, on a zaap, #ctr_zaapPrice (padding 10 + 10,
// the price and an 18 px kamas icon). While this module writes the coordinates,
// #ctr_coords keeps its width (flex-shrink 0: its lines never wrap) and the root
// gets the row's measured width, so the tooltip is placed with its real size.
// #ctr_coords draws the black background around #lbl_coords, a DofusLabel,
// single-line by default: it then takes the one-line height of its text style
// (--line-height, DofusLabel.SetTextHeight), so the zone's line would overflow
// a box kept on one line. While the label shows several lines it is not
// single-line, as the game's own multi-line labels of that style (bestiary and
// buff properties): DofusLabel drops that height and the box grows with every
// line. One line keeps the label's own height (the game's look).
constexpr float TemplateWidth = 154, BoxPadding = 20, KamasIcon = 18, Margin = 2, MaxWidth = 480;
constexpr float CharWidth = 8; // per code point, for a text that cannot be measured yet
constexpr size_t RecordBudget = 8, AdjustedBudget = 4;
constexpr int AncestorBudget = 8;
// No-break spaces keep the dimension's name in one piece; the space before it
// stays an ordinary one, the only place where a line could still break.
constexpr const char* NoBreakSpace = "\xC2\xA0";
struct Vec2 { float x, y; };
using BerzFn = void(*)(Obj, Vec2, Obj, Method);
using UpdateFn = void(*)(Obj, Obj, Method);
BerzFn originalBerz = nullptr;
UpdateFn originalUpdate = nullptr;
struct Natives {
    Klass bannerView{}, mapView{}, label{}, length{};
    Method coordsLabel{}, priceLabel{}, setText{}, getText{}, getSingleLine{}, setSingleLine{}, measure{}, subAreaName{}, worldName{};
    Method parent{}, name{}, panel{}, style{}, getMinWidth{}, setMinWidth{}, setLength{}, getShrink{}, setShrink{}, setFloat{};
} n;
// A tooltip the game is building: its infos, its map and that map's dimension
// (its name and WorldMapData id), remembered when the map asks for the tooltip
// (fhw.berz).
struct Record { Ref infos; View view = View::None; std::string world; int worldId = 0; };
// Template elements this module changed, with their own values: the
// coordinates box (#ctr_coords, flex-shrink), its root (#cartographyTooltip,
// min-width) and its label (#lbl_coords, single-line), given back when the
// game's text returns or at cleanup.
struct Adjusted { Ref coords, shrink, root, width, label; bool singleLine = true; };
struct State {
    bool installed = false;
    std::vector<Record> records; // most recent last
    std::vector<Adjusted> adjusted;
    Ref label;                   // the coordinates label this module rewrote last
    std::string shown, native;   // what it showed then, and the game's own text
    bool loggedMini = false, loggedBig = false;
    uint64_t nextLog = 0;
} s;

bool Active() {
    return s.installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
// The zone name, or a dimension choice other than « Non » on either map (even
// « Oui, sauf » over the Monde des Douze: another world can be shown next).
bool Wanted() {
    const auto& o = module->settings;
    return o.miniMapZoneName || o.coordsWorldMapMode != 0 || o.coordsWorldMiniMapMode != 0;
}
void Report(const char* area, const std::exception& error) {
    const auto now = GetTickCount64();
    if (now < s.nextLog) return;
    s.nextLog = now + 10000;
    Log(std::string("MAP_INFO_ERROR ") + area + ": " + error.what());
}
void RequireField(Klass type, const char* name, size_t offset) {
    for (auto t = type; t; t = api.class_get_parent(t)) {
        auto field = api.class_get_field_from_name(t, name);
        if (!field) continue;
        if (api.field_get_offset(field) == offset) return;
        break;
    }
    throw std::runtime_error(std::string("Map info field mismatch: ") + name);
}
template<class T> T At(Obj owner, const char* name) { return *static_cast<T*>(api.FieldAddress(owner, name)); }

View Classify(Obj view) {
    const auto type = view ? api.object_get_class(view) : nullptr;
    return type && type == n.bannerView ? View::Mini : type && type == n.mapView ? View::Big : View::None;
}
void Remember(Obj view, Obj infos) {
    const auto kind = Classify(view);
    if (kind == View::None || !infos) return;
    std::string world;
    int worldId = 0;
    Ref data(api.Field(view, "<dugi>k__BackingField"));
    if (data.get()) {
        world = api.String(api.Invoke(n.worldName, data.get()));
        worldId = At<int>(data.get(), "id");
    }
    for (auto it = s.records.begin(); it != s.records.end(); ++it)
        if (it->infos.get() == infos) { s.records.erase(it); break; }
    if (s.records.size() >= RecordBudget) s.records.erase(s.records.begin());
    s.records.push_back(Record{Ref(infos), kind, std::move(world), worldId});
}
const Record* Lookup(Obj infos) {
    for (auto it = s.records.rbegin(); it != s.records.rend(); ++it)
        if (it->infos.get() == infos) return &*it;
    return nullptr;
}
std::string NoBreak(const std::string& text) {
    std::string joined;
    for (char c : text) {
        if (c == ' ') joined += NoBreakSpace;
        else joined += c;
    }
    return joined;
}
// The coordinates text of a tooltip, or empty for the game's own: on the
// mini-map the zone name above the coordinates, and the dimension with them
// when that map's choice shows it for this world.
std::string TooltipText(View view, const std::string& world, int worldId, Obj infos) {
    if (!At<bool>(infos, "showMapCoords")) return {}; // the game hides its coordinates here
    const auto& o = module->settings;
    const bool mini = view == View::Mini;
    const bool dimension = DimensionShown(mini ? o.coordsWorldMiniMapMode : o.coordsWorldMapMode, worldId) && !world.empty();
    std::string zone;
    if (mini && o.miniMapZoneName) {
        Ref subArea(api.Field(infos, "subArea"));
        if (subArea.get()) zone = api.String(api.Invoke(n.subAreaName, subArea.get()));
    }
    if (!dimension && zone.empty()) return {};
    const auto position = PositionText(At<int>(infos, "mapX"), At<int>(infos, "mapY"), dimension ? NoBreak(world) : std::string{});
    return zone.empty() ? position : zone + "\n" + position;
}
size_t LongestLine(const std::string& text) {
    size_t longest = 0, current = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') { longest = (std::max)(longest, current); current = 0; continue; }
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++current;
    }
    return longest;
}
// A label's text without wrapping, as UI Toolkit lays it out with the label's
// own font (TextElement.MeasureTextSize: the widest line); an estimate while the
// label cannot measure (no panel or font yet).
float TextWidth(Obj label, const std::string& text) {
    try {
        Ref value(api.string_new(text.c_str()));
        float width = 0, height = 0;
        int undefined = 0; // VisualElement.MeasureMode.Undefined
        const auto size = api.Value<Vec2>(api.Invoke(n.measure, label, {value.get(), &width, &undefined, &height, &undefined}));
        if (std::isfinite(size.x) && size.x > 0 && size.x < 4096) return size.x;
    } catch (const std::exception& e) { Report("measure", e); }
    return CharWidth * static_cast<float>(LongestLine(text));
}
// The row's width: the coordinates box, and the zaap price box beside it when
// the game shows one (zaapPrice >= 0, its label already written by Update).
// Unbounded: the root gets at most MaxWidth.
float RowWidth(Obj builder, Obj label, const std::string& shown, int zaapPrice) {
    float width = BoxPadding + TextWidth(label, shown);
    if (zaapPrice >= 0) {
        Ref price(api.Invoke(n.priceLabel, api.FieldAddress(builder, "m_binding")));
        const auto text = price.get() ? api.String(api.Invoke(n.getText, price.get())) : std::to_string(zaapPrice);
        width += BoxPadding + (price.get() ? TextWidth(price.get(), text) : CharWidth * static_cast<float>(LongestLine(text))) + KamasIcon;
    }
    return width + Margin;
}
// The tooltip template's root (#cartographyTooltip), an ancestor of its
// coordinates box: only the cartography tooltip owns it.
Obj TemplateRoot(Obj coords) {
    Obj node = coords;
    for (int depth = 0; node && depth < AncestorBudget; ++depth) {
        node = api.Invoke(n.parent, node);
        if (node && api.String(api.Invoke(n.name, node)) == "cartographyTooltip") return node;
    }
    return nullptr;
}
Obj StyleBox(Obj element, Method getter) {
    Ref style(api.Invoke(n.style, element));
    auto box = api.Invoke(getter, style.get());
    if (!box) throw std::runtime_error("Map info expected a boxed style value");
    return box;
}
void WriteStyle(Obj element, Method setter, Obj box) {
    Ref style(api.Invoke(n.style, element));
    api.Invoke(setter, style.get(), {api.object_unbox(box)});
}
void SetMinWidth(Obj element, float pixels) {
    int unit = 0;
    Ref length(api.New(n.length, {&pixels, &unit}));
    Ref box(StyleBox(element, n.getMinWidth));
    api.Invoke(n.setLength, api.object_unbox(box.get()), {api.object_unbox(length.get())});
    WriteStyle(element, n.setMinWidth, box.get());
}
void SetNoShrink(Obj element) {
    float none = 0;
    Ref box(StyleBox(element, n.getShrink));
    api.Invoke(n.setFloat, api.object_unbox(box.get()), {&none});
    WriteStyle(element, n.setShrink, box.get());
}
// DofusLabel.isSingleLine: its setter gives or drops the one-line height itself.
void SetSingleLine(Obj label, bool single) { api.Invoke(n.setSingleLine, label, {&single}); }
void GiveBack(const Adjusted& a) {
    WriteStyle(a.coords.get(), n.setShrink, a.shrink.get());
    if (a.root.get()) WriteStyle(a.root.get(), n.setMinWidth, a.width.get());
    if (a.label.get()) SetSingleLine(a.label.get(), a.singleLine);
}
void Release(Obj coords) {
    for (auto it = s.adjusted.begin(); it != s.adjusted.end(); ++it) {
        if (it->coords.get() != coords) continue;
        GiveBack(*it);
        s.adjusted.erase(it);
        return;
    }
}
// While this module writes the coordinates: the box keeps its width, the root
// gets the measured row (a short row keeps the template's own 154 px) and the
// label is single-line only for one line.
void Adjust(Obj coords, Obj root, Obj label, float width, bool lines) {
    auto found = std::find_if(s.adjusted.begin(), s.adjusted.end(), [&](const Adjusted& a) { return a.coords.get() == coords; });
    if (found == s.adjusted.end()) {
        if (s.adjusted.size() >= AdjustedBudget) {
            GiveBack(s.adjusted.front());
            s.adjusted.erase(s.adjusted.begin());
        }
        Adjusted entry;
        entry.coords.reset(coords);
        entry.shrink.reset(StyleBox(coords, n.getShrink));
        entry.label.reset(label);
        entry.singleLine = api.Value<bool>(api.Invoke(n.getSingleLine, label));
        if (root) { entry.root.reset(root); entry.width.reset(StyleBox(root, n.getMinWidth)); }
        s.adjusted.push_back(std::move(entry));
        found = s.adjusted.end() - 1;
    }
    SetNoShrink(coords);
    SetSingleLine(found->label.get(), found->singleLine && !lines);
    if (!found->root.get()) return;
    if (width > TemplateWidth) SetMinWidth(found->root.get(), width);
    else WriteStyle(found->root.get(), n.setMinWidth, found->width.get());
}
// After the game's CartographyTooltipBuilder.Update wrote « [x,y] »: the text
// of this tooltip's map, or the game's own text and template.
void Decorate(Obj builder, Obj infos) {
    const Record* found = infos ? Lookup(infos) : nullptr;
    if (!found && s.adjusted.empty()) return; // neither ours nor changed by us
    // Copies: the calls into the game below could run this hook again.
    const auto view = found ? found->view : View::None;
    const auto world = found ? found->world : std::string{};
    const int worldId = found ? found->worldId : 0;
    Ref label(api.Invoke(n.coordsLabel, api.FieldAddress(builder, "m_binding")));
    if (!label.get() || !api.class_is_assignable_from(n.label, api.object_get_class(label.get()))) return;
    Ref coords(api.Invoke(n.parent, label.get()));
    if (!coords.get() || api.String(api.Invoke(n.name, coords.get())) != "ctr_coords") return; // not the game's template
    const auto text = view == View::None ? std::string{} : TooltipText(view, world, worldId, infos);
    if (text.empty()) { Release(coords.get()); return; }
    const int x = At<int>(infos, "mapX"), y = At<int>(infos, "mapY"), price = At<int>(infos, "zaapPrice");
    Ref root(TemplateRoot(coords.get()));
    Ref value(api.string_new(text.c_str()));
    api.Invoke(n.setText, label.get(), {value.get()});
    // What the label shows (DofusLabel applies its text case, if any): the
    // cleanup compares with it and the width measures it.
    const auto shown = api.String(api.Invoke(n.getText, label.get()));
    s.label.reset(label.get());
    s.shown = shown;
    s.native = PositionText(x, y, {});
    const auto row = RowWidth(builder, label.get(), shown, price);
    const auto width = (std::min)(MaxWidth, row);
    // Several lines: the zone above the coordinates, or a row wider than its
    // root can grow (the template's 154 px without that root), which wraps.
    const bool lines = shown.find('\n') != std::string::npos || row > (root.get() ? MaxWidth : TemplateWidth);
    Adjust(coords.get(), root.get(), label.get(), width, lines);
    auto& logged = view == View::Mini ? s.loggedMini : s.loggedBig;
    if (!logged) {
        logged = true;
        Log(std::string("MAP_INFO_TOOLTIP view=") + (view == View::Mini ? "mini" : "big") +
            " width=" + (root.get() ? std::to_string(static_cast<int>(width)) : std::string("none")) +
            " lines=" + (lines ? "several" : "one"));
    }
}
bool Pending() { return !s.adjusted.empty() || !s.records.empty() || s.label.get(); }
// The template's own values back; the last rewritten label back to the game's
// text when it still shows ours. A failed restore stays for a retry.
void RestoreAll() {
    for (auto it = s.adjusted.begin(); it != s.adjusted.end();) {
        try { GiveBack(*it); it = s.adjusted.erase(it); }
        catch (const std::exception& e) { Report("restore", e); ++it; }
    }
    try {
        if (s.label.get() && api.Invoke(n.panel, s.label.get()) && api.String(api.Invoke(n.getText, s.label.get())) == s.shown) {
            Ref value(api.string_new(s.native.c_str()));
            api.Invoke(n.setText, s.label.get(), {value.get()});
        }
    } catch (const std::exception& e) { Report("restore", e); }
    s.label.reset();
    s.shown.clear();
    s.native.clear();
    s.records.clear();
}

void BerzHook(Obj view, Vec2 position, Obj infos, Method method) {
    if (Active() && Wanted()) {
        try { Remember(view, infos); }
        catch (const std::exception& e) { Report("hover", e); }
    }
    originalBerz(view, position, infos, method);
}
void UpdateHook(Obj builder, Obj infos, Method method) {
    originalUpdate(builder, infos, method);
    if (!Active()) return;
    try { Decorate(builder, infos); }
    catch (const std::exception& e) { Report("tooltip", e); }
}

void Install() {
    const auto view = api.Class("Core.dll", "", "fhw");
    n.bannerView = api.Class("Core.dll", ViewSpace, "CartographyBannerView");
    n.mapView = api.Class("Core.dll", ViewSpace, "CartographyMapView");
    if (!api.class_is_assignable_from(view, n.bannerView) || !api.class_is_assignable_from(view, n.mapView))
        throw std::runtime_error("Map info cartography views unavailable");
    RequireField(view, "<dugi>k__BackingField", 0x38);
    const auto builder = api.Class("Core.dll", "Core.UILogic.Components.Tooltips.Builder", "CartographyTooltipBuilder");
    const auto infos = api.NestedClass(builder, "CartographyTooltipInfos");
    RequireField(infos, "mapX", 0x10);
    RequireField(infos, "mapY", 0x14);
    RequireField(infos, "subArea", 0x18);
    RequireField(infos, "showMapCoords", 0x29);
    RequireField(infos, "zaapPrice", 0x2C);
    RequireField(builder, "m_binding", 0x10); // BaseTooltipBuilder<CartographyTooltipBinding>
    const auto binding = api.Class("Core.dll", "Core.UILogic.Bindings", "CartographyTooltipBinding");
    n.coordsLabel = api.FindVerified(binding, "get_lbl_coords", 0, 0x2EA53C0);
    n.priceLabel = api.FindVerified(binding, "get_lbl_zaapPrice", 0, 0x2EA55D0);
    n.label = api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel");
    n.setText = api.FindVerified(n.label, "set_text", 1, 0x1594570);
    n.getText = api.FindVerified(n.label, "get_text", 0, 0x1593FA0);
    n.getSingleLine = api.FindVerified(n.label, "get_isSingleLine", 0, 0x1593B70);
    n.setSingleLine = api.FindVerified(n.label, "set_isSingleLine", 1, 0x1593FB0);
    n.measure = api.FindVerified(api.Class(UiImage, UiSpace, "TextElement"), "MeasureTextSize", 5, 0x5645AF0);
    n.subAreaName = api.FindVerified(api.Class(DataImage, WorldSpace, "SubAreaData"), "get_name", 0, 0x7D0F60);
    // The shown world map: its name, and its id for « Oui, sauf dans le Monde des Douze ».
    const auto worldMap = api.Class(DataImage, WorldSpace, "WorldMapData");
    RequireField(worldMap, "id", 0x10);
    n.worldName = api.FindVerified(worldMap, "get_name", 0, 0x7D3DE0);
    const auto element = api.Class(UiImage, UiSpace, "VisualElement");
    n.parent = api.FindVerified(element, "get_parent", 0, 0x55F7180);
    n.name = api.FindVerified(element, "get_name", 0, 0x3158710);
    n.panel = api.FindVerified(element, "get_panel", 0, 0x55F7170);
    n.style = api.FindVerified(element, "get_style", 0, 0x55F74A0);
    const auto inlineStyle = api.Class(UiImage, UiSpace, "InlineStyleAccess");
    n.getMinWidth = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x55D1850);
    n.setMinWidth = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x55D4410);
    n.getShrink = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.get_flexShrink", 0, 0x55D1460);
    n.setShrink = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.set_flexShrink", 1, 0x55D3BC0);
    n.length = api.Class(UiImage, UiSpace, "Length");
    api.FindVerified(n.length, ".ctor", 2, 0x561D3D0);
    n.setLength = api.FindVerified(api.Class(UiImage, UiSpace, "StyleLength"), "set_value", 1, 0x5623940);
    n.setFloat = api.FindVerified(api.Class(UiImage, UiSpace, "StyleFloat"), "set_value", 1, 0x5622C10);
    // The tooltip of both maps: fhw.berz(mouse, infos) creates it for the
    // hovered cell; CartographyTooltipBuilder.Update writes its « [x,y] ».
    const auto berz = api.FindVerified(view, "berz", 2, 0x1BEF880);
    const auto update = api.FindVerified(builder, "Update", 1, 0x15EFCA0);
    AddHook(berz, 0x1BEF880, reinterpret_cast<void*>(&BerzHook), reinterpret_cast<void**>(&originalBerz));
    AddHook(update, 0x15EFCA0, reinterpret_cast<void*>(&UpdateHook), reinterpret_cast<void**>(&originalUpdate));
}
}
}

void InstallMapInfo() {
    using namespace mapInfo;
    if (s.installed) return;
    // Optional: a contract mismatch disables only this module. A hook prepared
    // before a failure stays a pass-through (installed stays false).
    try { Install(); }
    catch (const std::exception& e) { Log(std::string("MAP_INFO_DISABLED ") + e.what()); return; }
    s.installed = true;
    Log("MAP_INFO_READY");
}
void TickMapInfo() {
    using namespace mapInfo;
    if (!s.installed) return;
    // Inactive or every option off: nothing of this module stays in the maps.
    if ((!Active() || !Wanted()) && Pending()) RestoreAll();
}
void MapInfoFrame(unsigned long long) {} // the tooltip is rewritten as the game writes it: no per-frame work
void RestoreMapInfo() {
    using namespace mapInfo;
    if (s.installed) RestoreAll();
}
bool MapInfoCleanupPending() { return mapInfo::s.installed && mapInfo::Pending(); }
