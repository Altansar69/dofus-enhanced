#include "maps/map-info.cpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>

// The production map info against an offline cartography: the game's tooltip
// of the hovered cell (fhw.berz, then CartographyTooltipBuilder.Update writing
// « [x,y] » and the zaap price), its template, the layout of its coordinates
// box and the maps' dimensions. No client, no input.
namespace fake {
using namespace managed;
struct Type { std::string name; Type* parent = nullptr; };
struct MethodData { Type* type; std::string name; int argc; uintptr_t rva; };
// UnityEngine.UIElements.StyleLength: Length (value, unit) and its keyword;
// StyleFloat: value and keyword (0 Undefined: the value applies; 1 Null: no
// inline value, the template's own).
struct StyleLengthValue { float value; int unit; int keyword; };
struct StyleFloatValue { float value; int keyword; };
struct LengthValue { float value; int unit; };
struct Object {
    Type* type{};
    std::string text, name;
    std::map<std::string, Obj> fields;      // references read by Field
    std::map<std::string, int64_t> slots;   // FieldAddress storage
    std::array<unsigned char, 32> data{};   // boxed values
    Obj parent = nullptr, panel = nullptr;
    StyleLengthValue minWidth{0, 0, 1};
    StyleFloatValue flexShrink{0, 1};
    bool shown = true, upperCase = false;
    // DofusLabel: single-line by default, with the one-line height of its text
    // style inline (SetTextHeight); not single-line, no inline height.
    bool singleLine = true;
    StyleLengthValue height{0, 0, 1};
};
std::map<std::string, std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x3A1000000000ULL;
std::map<std::pair<std::string, std::string>, size_t> offsets;
std::map<std::string, uintptr_t> guards;
std::vector<uintptr_t> hookRvas;
std::vector<std::string> logs;
std::string failMethod, failVerified;
uintptr_t failHook = 0;
int berzCalls = 0, updateCalls = 0, textWrites = 0, singleLineWrites = 0;
bool measureWorks = true;
void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Type* Class(const std::string& name) {
    auto& type = types[name];
    if (!type) type = std::make_unique<Type>(Type{name});
    return type.get();
}
Object& Get(Obj value) { Check(value != nullptr, "Null fixture object"); return *static_cast<Object*>(value); }
Obj Make(const std::string& type) {
    auto value = std::make_unique<Object>();
    value->type = Class(type);
    auto result = value.get();
    objects.push_back(std::move(value));
    return result;
}
template<class T> Obj Box(T value, const char* type) {
    auto result = Make(type);
    static_assert(sizeof(T) <= 32);
    std::memcpy(Get(result).data.data(), &value, sizeof(value));
    return result;
}
Obj String(const std::string& text) { auto result = Make("String"); Get(result).text = text; return result; }
// The fixture's font: 7 px per code point, 18 px per line.
mapInfo::Vec2 Measure(const std::string& text) {
    float lines = 1, widest = 0, current = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') { ++lines; widest = (std::max)(widest, current); current = 0; continue; }
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) current += 7;
    }
    return {(std::max)(widest, current), 18 * lines};
}
// The text style's --line-height: the inline height of a single-line DofusLabel.
constexpr float LineHeight = 16;
StyleLengthValue OneLine() { return {LineHeight, 0, 0}; }

// ------------------------------------------------------------ the game
using BerzFn = void(*)(Obj, mapInfo::Vec2, Obj, Method);
using UpdateFn = void(*)(Obj, Obj, Method);
BerzFn berzHook = nullptr;
UpdateFn updateHook = nullptr;
Obj panel{}, tooltipRoot{}, templateRoot{}, tooltip{}, row{}, ctrCoords{}, label{}, ctrZaap{}, priceLabel{};
Obj mini{}, big{}, other{}, twelve{}, twelveEnglish{}, incarnam{}, nameless{}, amakna{}, longZone{};
Obj lastBuilder{};
// The tooltip template, as the game's UXML builds it: TooltipRoot > #cartographyTooltip
// (154 px) > #CartographyTooltip > row > #ctr_coords > #lbl_coords, and beside
// #ctr_coords, #ctr_zaapPrice > #lbl_zaapPrice.
void BuildTemplate(bool named = true, const char* coordsName = "ctr_coords") {
    tooltipRoot = Make("TooltipRoot");
    templateRoot = Make("TemplateContainer");
    Get(templateRoot).name = named ? "cartographyTooltip" : "";
    tooltip = Make("DofusVisualElement"); Get(tooltip).name = "CartographyTooltip";
    row = Make("DofusVisualElement");
    ctrCoords = Make("DofusVisualElement"); Get(ctrCoords).name = coordsName;
    label = Make("DofusLabel"); Get(label).name = "lbl_coords";
    ctrZaap = Make("DofusVisualElement"); Get(ctrZaap).name = "ctr_zaapPrice";
    priceLabel = Make("DofusLabel"); Get(priceLabel).name = "lbl_zaapPrice";
    Get(templateRoot).parent = tooltipRoot; Get(tooltip).parent = templateRoot; Get(row).parent = tooltip;
    Get(ctrCoords).parent = row; Get(label).parent = ctrCoords; Get(ctrZaap).parent = row; Get(priceLabel).parent = ctrZaap;
    for (auto element : {tooltipRoot, templateRoot, tooltip, row, ctrCoords, label, ctrZaap, priceLabel}) Get(element).panel = panel;
    Get(label).height = OneLine(); Get(priceLabel).height = OneLine(); // styled by the game: one line each
}
// fhw.berz: a new CartographyTooltipBuilder for these infos, set up by the
// tooltip service, which calls its Update (SetupViewWithContent).
void NativeBerz(Obj view, mapInfo::Vec2, Obj infos, Method) {
    ++berzCalls;
    Check(view && infos, "The game always creates its tooltip for a view and infos");
    lastBuilder = Make("CartographyTooltipBuilder");
    Get(lastBuilder).slots["m_binding"] = reinterpret_cast<int64_t>(label);
    updateHook(lastBuilder, infos, nullptr);
}
// CartographyTooltipBuilder.Update: « [{0},{1}] », ctr_coords per showMapCoords,
// « {0} » and ctr_zaapPrice per zaapPrice >= 0.
void NativeUpdate(Obj builder, Obj infos, Method) {
    ++updateCalls;
    auto target = reinterpret_cast<Obj>(Get(builder).slots.at("m_binding"));
    const auto& slots = Get(infos).slots;
    Get(target).text = "[" + std::to_string(static_cast<int>(slots.at("mapX"))) + "," + std::to_string(static_cast<int>(slots.at("mapY"))) + "]";
    Get(ctrCoords).shown = slots.at("showMapCoords") != 0;
    const auto price = static_cast<int>(slots.at("zaapPrice"));
    Get(priceLabel).text = std::to_string(price);
    Get(ctrZaap).shown = price >= 0;
}

Obj Invoke(Method method, Obj self, std::initializer_list<void*> arguments) {
    const auto& m = *static_cast<const MethodData*>(method);
    const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size()) == m.argc, "Managed argument count mismatch: " + m.name);
    if (!failMethod.empty() && failMethod == m.name) { failMethod.clear(); throw std::runtime_error("Injected native failure: " + m.name); }
    const auto& t = m.type->name;
    if (t == "CartographyTooltipBinding") {
        // A value type: the receiver is the binding inside its builder.
        if (m.name == "get_lbl_coords" && m.rva == 0x2EA53C0) return *static_cast<Obj*>(self);
        if (m.name == "get_lbl_zaapPrice" && m.rva == 0x2EA55D0) return priceLabel;
        throw std::runtime_error("Only the verified binding getters");
    }
    if (t == "StyleLength") {
        Check(m.name == "set_value" && m.rva == 0x5623940, "Only the verified StyleLength.set_value");
        auto& value = *static_cast<StyleLengthValue*>(self);
        const auto& length = *static_cast<LengthValue*>(args[0]);
        value = {length.value, length.unit, 0};
        return nullptr;
    }
    if (t == "StyleFloat") {
        Check(m.name == "set_value" && m.rva == 0x5622C10, "Only the verified StyleFloat.set_value");
        *static_cast<StyleFloatValue*>(self) = {*static_cast<float*>(args[0]), 0};
        return nullptr;
    }
    auto& o = Get(self);
    if (t == "TextElement") {
        Check(m.name == "MeasureTextSize" && m.rva == 0x5645AF0 && *static_cast<int*>(args[2]) == 0 && *static_cast<int*>(args[4]) == 0,
            "Text is measured by the verified MeasureTextSize, without width or height limit");
        Check(o.type->name == "DofusLabel", "Each label measures its own text");
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        return Box(measureWorks ? Measure(Get(static_cast<Obj>(args[0])).text) : mapInfo::Vec2{nan, nan}, "Vector2");
    }
    if (m.name == "get_isSingleLine" || m.name == "set_isSingleLine") {
        const bool getter = m.name == "get_isSingleLine";
        Check(t == "DofusLabel" && m.rva == (getter ? 0x1593B70u : 0x1593FB0u), "Only the verified DofusLabel.isSingleLine");
        if (getter) return Box(o.singleLine, "Boolean");
        // DofusLabel.set_isSingleLine: a change gives the one-line height
        // (single-line) or drops it (SetTextHeight).
        const bool single = *static_cast<bool*>(args[0]);
        if (single != o.singleLine) { o.singleLine = single; o.height = single ? OneLine() : StyleLengthValue{0, 0, 1}; ++singleLineWrites; }
        return nullptr;
    }
    if (m.name == "get_parent") return o.parent;
    if (m.name == "get_name") return String(t == "VisualElement" ? o.name : o.text);
    if (m.name == "get_panel") return o.panel;
    if (m.name == "get_style") { auto style = Make("InlineStyleAccess"); Get(style).fields["element"] = self; return style; }
    if (m.name == "UnityEngine.UIElements.IStyle.get_minWidth") return Box(Get(o.fields.at("element")).minWidth, "StyleLength");
    if (m.name == "UnityEngine.UIElements.IStyle.set_minWidth") {
        Get(o.fields.at("element")).minWidth = *static_cast<StyleLengthValue*>(args[0]);
        return nullptr;
    }
    if (m.name == "UnityEngine.UIElements.IStyle.get_flexShrink") return Box(Get(o.fields.at("element")).flexShrink, "StyleFloat");
    if (m.name == "UnityEngine.UIElements.IStyle.set_flexShrink") {
        Get(o.fields.at("element")).flexShrink = *static_cast<StyleFloatValue*>(args[0]);
        return nullptr;
    }
    if (m.name == "set_text") {
        // DofusLabel.set_text applies the label's text case (here Upper) when it has one.
        ++textWrites;
        o.text = Get(static_cast<Obj>(args[0])).text;
        if (o.upperCase) for (auto& c : o.text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return nullptr;
    }
    if (m.name == "get_text") return String(o.text);
    throw std::runtime_error("Unexpected fake method " + t + "." + m.name);
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Class(name); }
Klass Api::NestedClass(Klass, const char* name) const { return fake::Class(name); }
Method Api::Find(Klass type, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type), name, argc, 0});
    auto result = method.get();
    fake::methods.push_back(std::move(method));
    return result;
}
Method Api::FindVerified(Klass type, const char* name, int argc, uintptr_t rva) const {
    if (fake::failVerified == name) throw std::runtime_error(std::string("Verified method unavailable: ") + name);
    fake::guards[static_cast<fake::Type*>(type)->name + "." + name] = rva;
    auto method = Find(type, name, argc);
    const_cast<fake::MethodData*>(static_cast<const fake::MethodData*>(method))->rva = rva;
    return method;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const { return fake::Invoke(method, self, args); }
Obj Api::New(Klass type, std::initializer_list<void*> args) const {
    fake::Check(static_cast<fake::Type*>(type)->name == "Length" && args.size() == 2, "Only the native Length constructor");
    const std::vector<void*> values(args);
    return fake::Box(fake::LengthValue{*static_cast<float*>(values[0]), *static_cast<int*>(values[1])}, "Length");
}
Obj Api::Field(Obj self, const char* name) const { return self ? fake::Get(self).fields[name] : nullptr; }
void* Api::FieldAddress(Obj self, const char* name) const { return &fake::Get(self).slots[name]; }
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string{}; }
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method method, uintptr_t rva, void* hook, void** original) {
    using namespace fake;
    const auto& m = *static_cast<const MethodData*>(method);
    Check(m.rva == rva, "A hook goes through its verified method");
    if (failHook == rva) throw std::runtime_error("Injected hook failure");
    hookRvas.push_back(rva);
    if (rva == 0x1BEF880) { Check(m.type->name == "fhw" && m.name == "berz" && m.argc == 2, "fhw.berz contract"); berzHook = reinterpret_cast<BerzFn>(hook); *original = reinterpret_cast<void*>(&NativeBerz); }
    else if (rva == 0x15EFCA0) { Check(m.type->name == "CartographyTooltipBuilder" && m.name == "Update" && m.argc == 1, "Update contract"); updateHook = reinterpret_cast<UpdateFn>(hook); *original = reinterpret_cast<void*>(&NativeUpdate); }
    else throw std::runtime_error("Unexpected map info hook");
}

namespace fake {
const std::string Nbsp = "\xC2\xA0";
// An ordinary space before the dimension (the only break opportunity), no-break inside its name.
const std::string TwelveCoords = "[3,-20, Monde" + Nbsp + "des" + Nbsp + "Douze]";
const std::string Village = "Village d\xE2\x80\x99" "Amakna";
constexpr float Row = 20 + 2;       // the coordinates box padding and the margin
constexpr float Zaap = 20 + 18;     // the price box padding and the kamas icon
void Init() {
    api.object_get_class = [](Obj value) -> Klass { return Get(value).type; };
    api.class_get_parent = [](Klass type) -> Klass { return static_cast<Type*>(type)->parent; };
    api.class_is_assignable_from = [](Klass base, Klass type) {
        for (auto t = static_cast<Type*>(type); t; t = t->parent) if (t == base) return true;
        return false;
    };
    api.object_unbox = [](Obj value) -> void* { return Get(value).data.data(); };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj value, bool) -> GCHandle { const auto handle = ++nextHandle; handles.emplace(handle, value); return handle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC reference double free"); };
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        const auto found = offsets.find({static_cast<Type*>(type)->name, name});
        return found == offsets.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
    Class("CartographyBannerView")->parent = Class("fhw");
    Class("CartographyMapView")->parent = Class("fhw");
    Class("OtherCartographyView")->parent = Class("fhw");
    Class("CartographyTooltipBuilder")->parent = Class("BaseTooltipBuilder`1");
    Class("DofusLabel")->parent = Class("Label");
    Class("Label")->parent = Class("TextElement");
    Class("TextElement")->parent = Class("VisualElement");
    for (const auto* name : {"TooltipRoot", "TemplateContainer", "DofusVisualElement"}) Class(name)->parent = Class("VisualElement");
    offsets = {
        {{"fhw", "<dugi>k__BackingField"}, 0x38}, {{"CartographyTooltipInfos", "mapX"}, 0x10}, {{"CartographyTooltipInfos", "mapY"}, 0x14},
        {{"CartographyTooltipInfos", "subArea"}, 0x18}, {{"CartographyTooltipInfos", "showMapCoords"}, 0x29},
        {{"CartographyTooltipInfos", "zaapPrice"}, 0x2C}, {{"BaseTooltipBuilder`1", "m_binding"}, 0x10}, {{"WorldMapData", "id"}, 0x10},
    };
    panel = Make("Panel");
    // The game's world maps: their name (get_name) and id (WorldMapData.id), the
    // Monde des Douze being world 1 whatever its name in the game's language.
    auto world = [](const char* name, int id) { auto w = Make("WorldMapData"); Get(w).text = name; Get(w).slots["id"] = id; return w; };
    twelve = world("Monde des Douze", 1); twelveEnglish = world("World of Twelve", 1); incarnam = world("Incarnam", 2); nameless = world("", 3);
    amakna = Make("SubAreaData"); Get(amakna).text = Village;
    longZone = Make("SubAreaData"); Get(longZone).text = "Lisi\xC3\xA8re de la for\xC3\xAAt mal\xC3\xA9" "fique sombre";
    mini = Make("CartographyBannerView"); Get(mini).fields["<dugi>k__BackingField"] = twelve;
    big = Make("CartographyMapView"); Get(big).fields["<dugi>k__BackingField"] = twelve;
    other = Make("OtherCartographyView"); Get(other).fields["<dugi>k__BackingField"] = twelve;
    BuildTemplate();
}
// The game's hover of a cell: its tooltip infos, then fhw.berz.
Obj Hover(Obj view, int x, int y, Obj subArea = nullptr, bool shown = true, int zaapPrice = -1) {
    auto infos = Make("CartographyTooltipInfos");
    Get(infos).slots["mapX"] = x;
    Get(infos).slots["mapY"] = y;
    Get(infos).slots["showMapCoords"] = shown ? 1 : 0;
    Get(infos).slots["zaapPrice"] = zaapPrice;
    Get(infos).fields["subArea"] = subArea;
    berzHook(view, {12.5f, 40.0f}, infos, nullptr);
    return infos;
}
std::string Text() { return Get(label).text; }
StyleLengthValue MinWidth() { return Get(templateRoot).minWidth; }
bool OwnWidth() { return MinWidth().keyword == 1; }                 // the template's own: no inline minimum width
bool NoShrink() { const auto s = Get(ctrCoords).flexShrink; return s.keyword == 0 && s.value == 0; }
bool OwnShrink() { return Get(ctrCoords).flexShrink.keyword == 1; }
bool Widened(float width) { return MinWidth().keyword == 0 && MinWidth().unit == 0 && MinWidth().value == width; }
// The fixture's layout of the row: the lines #lbl_coords shows at the width its
// box gets (the root's 154 px or its minimum width, less the box padding and the
// zaap price box), and the lines its black box (#ctr_coords' background) holds:
// one for a single-line label (its fixed one-line height), every line otherwise.
int ShownLines() {
    const auto root = MinWidth();
    float room = (std::max)(154.0f, root.keyword == 0 ? root.value : 0.0f) - 20;
    if (Get(ctrZaap).shown) room -= Zaap + Measure(Get(priceLabel).text).x;
    const auto text = Text();
    int lines = 0;
    for (size_t start = 0;;) {
        const auto end = text.find('\n', start);
        const auto width = Measure(text.substr(start, end == std::string::npos ? std::string::npos : end - start)).x;
        lines += (std::max)(1, static_cast<int>(std::ceil(width / room)));
        if (end == std::string::npos) return lines;
        start = end + 1;
    }
}
bool Encloses() { return (Get(label).singleLine ? 1 : ShownLines()) == ShownLines(); }
// The label's own setting, as the game styled it: single-line, one line high.
bool OwnLine() { return Get(label).singleLine && Get(label).height.keyword == 0 && Get(label).height.value == LineHeight; }
// The zone name, then each map's dimension choice: 0 « Non », 1 « Oui, sauf
// dans le Monde des Douze », 2 « Oui ».
void Options(bool zone, int bigMode, int miniMode) {
    auto& o = module->settings;
    o.miniMapZoneName = zone; o.coordsWorldMapMode = bigMode; o.coordsWorldMiniMapMode = miniMode;
}
void Reset() {
    RestoreMapInfo();
    mapInfo::s = mapInfo::State{};
    mapInfo::originalBerz = nullptr; mapInfo::originalUpdate = nullptr;
    berzHook = nullptr; updateHook = nullptr; hookRvas.clear(); guards.clear();
    failMethod.clear(); failVerified.clear(); failHook = 0; measureWorks = true;
    module->stopRequested = false; module->ready = true; module->servicesReady = true;
    Options(true, 2, 2);
    BuildTemplate();
}

void Contracts() {
    InstallMapInfo();
    Check(mapInfo::s.installed && !logs.empty() && logs.back() == "MAP_INFO_READY", "Map info installs");
    Check(hookRvas == std::vector<uintptr_t>{0x1BEF880, 0x15EFCA0}, "Exactly the tooltip creation and its Update are hooked");
    Check(guards.at("fhw.berz") == 0x1BEF880 && guards.at("CartographyTooltipBuilder.Update") == 0x15EFCA0 &&
        guards.at("CartographyTooltipBinding.get_lbl_coords") == 0x2EA53C0 && guards.at("CartographyTooltipBinding.get_lbl_zaapPrice") == 0x2EA55D0 &&
        guards.at("DofusLabel.set_text") == 0x1594570 && guards.at("DofusLabel.get_text") == 0x1593FA0 &&
        guards.at("DofusLabel.get_isSingleLine") == 0x1593B70 && guards.at("DofusLabel.set_isSingleLine") == 0x1593FB0 &&
        guards.at("TextElement.MeasureTextSize") == 0x5645AF0 && guards.at("SubAreaData.get_name") == 0x7D0F60 &&
        guards.at("WorldMapData.get_name") == 0x7D3DE0 && guards.at("VisualElement.get_parent") == 0x55F7180 &&
        guards.at("VisualElement.get_name") == 0x3158710 && guards.at("VisualElement.get_panel") == 0x55F7170 &&
        guards.at("VisualElement.get_style") == 0x55F74A0 &&
        guards.at("InlineStyleAccess.UnityEngine.UIElements.IStyle.get_minWidth") == 0x55D1850 &&
        guards.at("InlineStyleAccess.UnityEngine.UIElements.IStyle.set_minWidth") == 0x55D4410 &&
        guards.at("InlineStyleAccess.UnityEngine.UIElements.IStyle.get_flexShrink") == 0x55D1460 &&
        guards.at("InlineStyleAccess.UnityEngine.UIElements.IStyle.set_flexShrink") == 0x55D3BC0 &&
        guards.at("Length..ctor") == 0x561D3D0 && guards.at("StyleLength.set_value") == 0x5623940 &&
        guards.at("StyleFloat.set_value") == 0x5622C10, "Exact verified native contracts");
    for (const auto& [name, rva] : guards)
        Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& g) { return g.rva == rva; }),
            "Every verified method has its 16-byte guard: " + name);
    InstallMapInfo();
    Check(hookRvas.size() == 2, "A second install adds nothing");
    MapInfoFrame(1);
}
void DefaultOptions() {
    Check(module->settings.miniMapZoneName && module->settings.coordsWorldMapMode == 1 && module->settings.coordsWorldMiniMapMode == 1,
        "By default: the zone name, and on both maps the dimension except in the Monde des Douze");
    // By default a cell of the Monde des Douze keeps the game's coordinates: the
    // zone above them on the mini-map, the game's own tooltip on the big map.
    Hover(mini, 3, -20, amakna);
    Check(berzCalls == 1 && updateCalls == 1, "The game's own tooltip is created and updated once");
    Check(Text() == Village + "\n[3,-20]" && OwnWidth() && NoShrink() && !Get(label).singleLine && ShownLines() == 2 && Encloses(),
        "Mini-map in the Monde des Douze by default: the zone above the game's coordinates, both lines in the box");
    Hover(big, 3, -20, amakna);
    Check(Text() == "[3,-20]" && OwnWidth() && OwnShrink() && OwnLine(), "Big map in the Monde des Douze by default: the game's own tooltip and template");
    // Any other dimension shows its name by default, on both maps.
    Get(mini).fields["<dugi>k__BackingField"] = incarnam; Get(big).fields["<dugi>k__BackingField"] = incarnam;
    Hover(big, 4, -2, amakna);
    Check(Text() == "[4,-2, Incarnam]" && OwnWidth() && NoShrink() && OwnLine(), "Big map in Incarnam by default: [x,y, dimension] on one line");
    Hover(mini, 4, -2, amakna);
    Check(Text() == Village + "\n[4,-2, Incarnam]" && !Get(label).singleLine && ShownLines() == 2 && Encloses(),
        "Mini-map in Incarnam by default: the zone above [x,y, dimension]");
    Get(mini).fields["<dugi>k__BackingField"] = twelve; Get(big).fields["<dugi>k__BackingField"] = twelve;
    Check(std::count(logs.begin(), logs.end(), "MAP_INFO_TOOLTIP view=mini width=134 lines=several") == 1 &&
        std::count(logs.begin(), logs.end(), "MAP_INFO_TOOLTIP view=big width=134 lines=one") == 1,
        "The first rewritten tooltip of each map is logged once");
    // « Oui » on both maps: the Monde des Douze shows its name too.
    Options(true, 2, 2);
    Hover(mini, 3, -20, amakna);
    Check(Text() == Village + "\n" + TwelveCoords, "Mini-map: the zone above [x,y, dimension]");
    Check(Widened(7 * 24 + Row) && NoShrink(), "The coordinates box keeps its width and the root gets the measured row");
    Check(!Get(label).singleLine && Get(label).height.keyword == 1 && ShownLines() == 2 && Encloses(),
        "Two lines: the label is not single-line, its black box holds both");
    Check(Get(tooltipRoot).minWidth.keyword == 1 && Get(label).minWidth.keyword == 1 && Get(ctrZaap).flexShrink.keyword == 1 &&
        Get(priceLabel).singleLine && Get(priceLabel).height.value == LineHeight,
        "Only the cartography template's root, coordinates box and label change");
    Hover(big, 3, -20, amakna);
    Check(Text() == TwelveCoords && Widened(7 * 24 + Row) && OwnLine(), "Big map: [x,y, dimension], never the zone name, on the label's own line");
    Hover(big, -1, 13);
    Check(Text() == "[-1,13, Monde" + Nbsp + "des" + Nbsp + "Douze]", "Big map without a zone keeps its dimension");
    Get(big).fields["<dugi>k__BackingField"] = incarnam;
    Hover(big, 4, -2);
    Check(Text() == "[4,-2, Incarnam]" && OwnWidth() && NoShrink(), "The shown world names the dimension; a short row keeps the template's width");
    Get(big).fields["<dugi>k__BackingField"] = twelve;
    Check(std::count_if(logs.begin(), logs.end(), [](const std::string& l) { return l.starts_with("MAP_INFO_TOOLTIP "); }) == 2,
        "Only the first rewritten tooltip of each map is logged");
}
// A zaap cell: the game shows its price box beside the coordinates; the row's
// width counts both boxes, measured, and the coordinates box never shrinks.
void ZaapPrice() {
    Hover(mini, 3, -20, amakna, true, 1234);
    Check(Text() == Village + "\n" + TwelveCoords && Get(priceLabel).text == "1234" && Get(ctrZaap).shown, "The game shows the zaap price");
    Check(Widened(7 * 24 + Row + Zaap + 7 * 4) && NoShrink(), "The row holds the coordinates and the price box");
    Check(!Get(label).singleLine && ShownLines() == 2 && Encloses() && Get(priceLabel).singleLine, "Beside the price, the coordinates' box holds both lines");
    Hover(big, 3, -20, nullptr, true, 0);
    Check(Text() == TwelveCoords && Widened(7 * 24 + Row + Zaap + 7 * 1) && OwnLine(), "A free zaap is a price box too");
    Get(big).fields["<dugi>k__BackingField"] = incarnam;
    Hover(big, 4, -2, nullptr, true, 250);
    Check(Text() == "[4,-2, Incarnam]" && Widened(7 * 16 + Row + Zaap + 7 * 3) && OwnLine() && Encloses(), "Even a short text needs the room of both boxes");
    Get(big).fields["<dugi>k__BackingField"] = twelve;
    // The zaap prices arrive later: the game updates the same tooltip again.
    auto infos = Hover(mini, 3, -20, amakna);
    Check(Widened(7 * 24 + Row), "Without a price, the coordinates box alone");
    Get(infos).slots["zaapPrice"] = 98765;
    updateHook(lastBuilder, infos, nullptr);
    Check(Text() == Village + "\n" + TwelveCoords && Widened(7 * 24 + Row + Zaap + 7 * 5) && Encloses(), "A later price Update widens the same tooltip");
    // Text the label cannot measure yet (no panel or font): the estimate, 8 px a code point.
    measureWorks = false;
    Hover(mini, 3, -20, amakna, true, 1234);
    Check(Widened(8 * 24 + Row + Zaap + 8 * 4) && NoShrink(), "Unmeasured text: an estimate, the box still never shrinks");
    measureWorks = true;
    // DofusLabel's own text case: the label shows its case, the cleanup and the width follow what it shows.
    Get(label).upperCase = true;
    Hover(mini, 3, -20, amakna);
    Check(Text() == "VILLAGE D\xE2\x80\x99" "AMAKNA\n[3,-20, MONDE" + Nbsp + "DES" + Nbsp + "DOUZE]" && mapInfo::s.shown == Text(),
        "A text case of the label is what this module remembers as shown");
    RestoreMapInfo();
    Check(Text() == "[3,-20]" && OwnWidth() && OwnShrink() && OwnLine(), "With a text case the cleanup still gives the game's text back");
    Get(label).upperCase = false;
}
void EachOption() {
    Options(false, 2, 2);
    Hover(mini, 3, -20, amakna);
    Check(Text() == TwelveCoords && Widened(7 * 24 + Row) && OwnLine(), "Zone name off: the mini-map keeps its dimension, on one line");
    Options(true, 2, 0);
    Hover(mini, 3, -20, amakna);
    Check(Text() == Village + "\n[3,-20]" && OwnWidth() && NoShrink() && !Get(label).singleLine && Encloses(),
        "Mini-map dimension off: the zone above the game's coordinates, both in the box");
    Hover(big, 3, -20, amakna);
    Check(Text() == TwelveCoords, "The big map follows its own option");
    Options(false, 2, 0);
    Hover(mini, 3, -20, amakna);
    Check(Text() == "[3,-20]" && OwnWidth() && OwnShrink() && OwnLine(), "Both mini-map options off: the game's own tooltip and template");
    Options(true, 0, 2);
    Hover(big, 3, -20, amakna);
    Check(Text() == "[3,-20]" && OwnWidth() && OwnShrink() && OwnLine(), "Big map option off: the game's own tooltip and template");
    Hover(mini, 3, -20, longZone);
    Check(Text() == Get(longZone).text + "\n" + TwelveCoords && Widened(7 * 36 + Row) && ShownLines() == 2 && Encloses(),
        "A long zone name widens the row to its line");
    auto huge = Make("SubAreaData"); Get(huge).text = std::string(80, 'z');
    Hover(mini, 3, -20, huge);
    Check(Widened(480) && NoShrink() && ShownLines() == 3 && !Get(label).singleLine && Encloses(),
        "The root's width is bounded: the zone wraps, its box holds every line");
    const auto calls = berzCalls;
    Options(false, 0, 0);
    auto quiet = Hover(mini, 3, -20, amakna);
    Check(berzCalls == calls + 1 && Text() == "[3,-20]" && OwnWidth() && OwnShrink() && OwnLine() && !mapInfo::Lookup(quiet) &&
        mapInfo::s.records.size() <= 8, "All off: the hooks pass the game's tooltip through, remember nothing and give the template back");
    TickMapInfo();
    Check(!MapInfoCleanupPending() && mapInfo::s.records.empty(), "All off: nothing of this module stays");
    Options(true, 2, 2);
}
// « Dimension avec les coordonnées » of each map, three choices: « Non »
// never, « Oui, sauf dans le Monde des Douze » everywhere but in the game's
// world 1 (known by its id, whatever its name), « Oui » everywhere. Each map
// follows its own choice, with or without the zone above, and keeps the
// game's own tooltip and template where it shows neither.
void EachMode() {
    const std::pair<Obj, std::string> worlds[] = {{twelve, "Monde" + Nbsp + "des" + Nbsp + "Douze"},
        {twelveEnglish, "World" + Nbsp + "of" + Nbsp + "Twelve"}, {incarnam, "Incarnam"}};
    for (int bigMode = 0; bigMode <= 2; ++bigMode)
        for (int miniMode = 0; miniMode <= 2; ++miniMode)
            for (bool zone : {false, true})
                for (const auto& entry : worlds) {
                    const auto world = entry.first;
                    const auto& name = entry.second;
                    Options(zone, bigMode, miniMode);
                    Get(mini).fields["<dugi>k__BackingField"] = world; Get(big).fields["<dugi>k__BackingField"] = world;
                    const auto coords = [&](int mode) {
                        const bool shown = mode == 2 || (mode == 1 && world == incarnam);
                        return shown ? "[3,-20, " + name + "]" : std::string("[3,-20]");
                    };
                    const auto what = "Big map " + std::to_string(bigMode) + ", mini-map " + std::to_string(miniMode) +
                        (zone ? ", zone" : "") + ", " + Get(world).text + ": ";
                    const auto infos = Hover(big, 3, -20, amakna);
                    Check(Text() == coords(bigMode), what + "the big map follows its own choice");
                    Check(Text() != "[3,-20]" || (OwnWidth() && OwnShrink() && OwnLine()), what + "the big map's own tooltip and template");
                    const auto record = mapInfo::Lookup(infos);
                    Check((record != nullptr) == (zone || bigMode != 0 || miniMode != 0),
                        what + "the hooks remember a tooltip while an option is on, even one showing nothing here");
                    Check(!record || record->worldId == Get(world).slots.at("id"), what + "the shown world's id is remembered");
                    Hover(mini, 3, -20, amakna);
                    Check(Text() == (zone ? Village + "\n" : std::string()) + coords(miniMode), what + "the mini-map follows its own choice");
                    Check((Text() != "[3,-20]" || (OwnWidth() && OwnShrink() && OwnLine())) && Encloses(), what + "the mini-map's box holds every line");
                }
    Options(true, 2, 2);
    Get(mini).fields["<dugi>k__BackingField"] = twelve; Get(big).fields["<dugi>k__BackingField"] = twelve;
}
void EdgeCases() {
    Hover(mini, 3, -20, amakna, false);
    Check(Text() == "[3,-20]" && !Get(ctrCoords).shown && OwnShrink() && OwnLine(), "Coordinates the game hides stay the game's");
    Hover(other, 3, -20, amakna);
    Check(Text() == "[3,-20]", "An unknown cartography view keeps the game's tooltip");
    Hover(mini, 7, 7);
    Check(Text() == "[7,7, Monde" + Nbsp + "des" + Nbsp + "Douze]", "No zone under the cell: the dimension only");
    Get(mini).fields["<dugi>k__BackingField"] = nameless;
    Hover(mini, 3, -20, amakna);
    Check(Text() == Village + "\n[3,-20]" && !Get(label).singleLine && Encloses(), "A world without a name: the zone and the game's coordinates");
    Get(mini).fields["<dugi>k__BackingField"] = nullptr;
    const auto writes = textWrites;
    Hover(mini, 3, -20);
    Check(Text() == "[3,-20]" && textWrites == writes && OwnShrink(), "No world and no zone: the game's own text, left untouched");
    Get(mini).fields["<dugi>k__BackingField"] = twelve;
    // The game updates the same tooltip again (zaap prices): rewritten again.
    auto infos = Hover(mini, 3, -20, amakna);
    Get(label).text = "[3,-20]";
    updateHook(lastBuilder, infos, nullptr);
    Check(Text() == Village + "\n" + TwelveCoords, "A later Update of the same tooltip is rewritten again");
    // Eight more tooltips: the oldest infos is forgotten and stays the game's.
    for (int i = 0; i < 8; ++i) Hover(big, i, i);
    Get(label).text = "[3,-20]";
    updateHook(lastBuilder, infos, nullptr);
    Check(Text() == "[3,-20]" && mapInfo::s.records.size() == 8 && OwnWidth() && OwnShrink() && OwnLine(),
        "Bounded memory: a forgotten tooltip keeps the game's text and template");
    // A template without the 154 px root: the text, the box and its label, nothing else.
    BuildTemplate(false);
    Hover(mini, 3, -20, amakna);
    Check(Text() == Village + "\n" + TwelveCoords && MinWidth().keyword == 1 && NoShrink() && !mapInfo::s.adjusted.back().root.get() &&
        !Get(label).singleLine && Encloses(), "Without the template root, only the coordinates box and its label change");
    // A label that is not in the game's coordinates box: left to the game.
    BuildTemplate(true, "ctr_other");
    Hover(mini, 3, -20, amakna);
    Check(Text() == "[3,-20]" && OwnShrink() && OwnWidth() && OwnLine(), "Another template layout is left to the game");
    // Several pooled templates: at most four stay changed, the oldest is given back.
    std::vector<std::array<Obj, 3>> templates;
    for (int i = 0; i < 6; ++i) { BuildTemplate(); templates.push_back({templateRoot, ctrCoords, label}); Hover(mini, 3, -20, amakna); }
    Check(mapInfo::s.adjusted.size() == 4 && Get(templates[0][0]).minWidth.keyword == 1 && Get(templates[0][1]).flexShrink.keyword == 1 &&
        Get(templates[0][2]).singleLine && Get(templates[0][2]).height.value == LineHeight &&
        Get(templates[1][0]).minWidth.keyword == 1 && Get(templates[1][1]).flexShrink.keyword == 1 && Get(templates[1][2]).singleLine &&
        Get(templates[5][0]).minWidth.keyword == 0 && Get(templates[5][1]).flexShrink.keyword == 0 && !Get(templates[5][2]).singleLine,
        "At most four changed templates; older ones get their own values back");
    // A native failure while rewriting leaves the game's tooltip and logs once.
    failMethod = "get_lbl_coords";
    Hover(mini, 3, -20, amakna);
    Check(Text() == "[3,-20]" && std::any_of(logs.begin(), logs.end(), [](const std::string& l) { return l.starts_with("MAP_INFO_ERROR tooltip: "); }),
        "A failure keeps the game's tooltip and is reported");
    failMethod = "get_name";
    Hover(mini, 3, -20, amakna);
    Check(Text() == "[3,-20]", "A failure while remembering the map keeps the game's tooltip");
    // A failed measure falls back to the estimate.
    failMethod = "MeasureTextSize";
    Hover(mini, 3, -20, amakna);
    Check(Text() == Village + "\n" + TwelveCoords && Widened(8 * 24 + Row) && Encloses(), "A failed measure falls back to the estimate");
    // The label's own setting cannot be read: nothing of a new template changes, the failure is reported.
    BuildTemplate();
    mapInfo::s.nextLog = 0;
    failMethod = "get_isSingleLine";
    Hover(mini, 3, -20, amakna);
    Check(OwnLine() && OwnShrink() && OwnWidth() && logs.back() == "MAP_INFO_ERROR tooltip: Injected native failure: get_isSingleLine",
        "A label whose setting cannot be read keeps the template as the game made it, and is reported");
    Hover(mini, 3, -20, amakna);
    Check(!Get(label).singleLine && Encloses(), "The next update of the tooltip adjusts it");
}
// The black box of the coordinates (#ctr_coords' background) holds every line
// the label shows. The reported mini-map tooltip: its zone name above the
// coordinates overflowed a box kept on one line by the single-line label.
void EveryLine() {
    Hover(mini, 3, -20, amakna);
    Check(ShownLines() == 2 && !Get(label).singleLine && Get(label).height.keyword == 1 && Encloses(),
        "The zone above the coordinates: the label is not single-line, its box holds both lines");
    Get(label).singleLine = true;
    Check(!Encloses(), "The fixture sees the reported tooltip: a single-line label keeps its box on one line");
    Get(label).singleLine = false;
    // One line on the same template: the label's own height again (the game's look).
    Hover(big, 3, -20, amakna);
    Check(Text() == TwelveCoords && ShownLines() == 1 && OwnLine() && Encloses(), "The big map's line on the same template: its own height again");
    Options(false, 2, 2);
    Hover(mini, 3, -20, amakna);
    Check(Text() == TwelveCoords && OwnLine() && Encloses(), "Zone name off: one line, the label's own height");
    // Every combination of the options (the zone name, each map's three
    // choices) on each map, in the Monde des Douze and in Incarnam, with a
    // zone, a long one or none, with or without a zaap price: the box holds
    // every line the label shows, and the label leaves its own single line
    // only for several.
    const auto writes = singleLineWrites;
    for (int mask = 0; mask < 18; ++mask) {
        Options((mask & 1) != 0, mask / 2 % 3, mask / 6);
        for (auto world : {twelve, incarnam}) {
            Get(mini).fields["<dugi>k__BackingField"] = world; Get(big).fields["<dugi>k__BackingField"] = world;
            for (auto view : {mini, big})
                for (auto zone : {amakna, longZone, static_cast<Obj>(nullptr)})
                    for (int price : {-1, 1234}) {
                        Hover(view, 3, -20, zone, true, price);
                        const auto lines = ShownLines();
                        Check(Encloses() && (lines == 1 ? OwnLine() : !Get(label).singleLine),
                            "Options " + std::to_string(mask) + ": the box holds the label's " + std::to_string(lines) + " line(s)");
                    }
        }
    }
    Get(mini).fields["<dugi>k__BackingField"] = twelve; Get(big).fields["<dugi>k__BackingField"] = twelve;
    Check(singleLineWrites > writes, "The combinations switch the label both ways");
    Options(true, 2, 2);
    // One line wider than the tooltip can grow (480 px), without a zone: it wraps, both lines in the box.
    auto vast = Make("WorldMapData"); Get(vast).text = std::string(70, 'w'); Get(vast).slots["id"] = 9;
    Get(big).fields["<dugi>k__BackingField"] = vast;
    Hover(big, 3, -20);
    Check(Widened(480) && ShownLines() == 2 && !Get(label).singleLine && Encloses(), "A wrapped line of coordinates: both lines in the box");
    Get(big).fields["<dugi>k__BackingField"] = twelve;
    // Without the template's root the row keeps its 154 px: a line of coordinates that wraps there.
    BuildTemplate(false);
    Hover(big, 3, -20);
    Check(Text() == TwelveCoords && ShownLines() == 2 && !Get(label).singleLine && Encloses(), "A row wider than its 154 px wraps: both lines in the box");
    BuildTemplate();
    // The game's own text back, by the game or by the options: the label's own line.
    Hover(mini, 3, -20, amakna);
    Hover(mini, 3, -20, amakna, false);
    Check(Text() == "[3,-20]" && OwnLine(), "Coordinates the game hides: the label's own line back");
    Hover(mini, 3, -20, amakna);
    Options(false, 2, 0);
    Hover(mini, 3, -20, amakna);
    Check(Text() == "[3,-20]" && OwnLine() && Encloses(), "Mini-map options off: the label's own line back");
    Options(true, 2, 2);
    // A label already multi-line keeps its own setting, for one line or after.
    Get(label).singleLine = false; Get(label).height = {0, 0, 1};
    Hover(big, 3, -20);
    Check(Text() == TwelveCoords && !Get(label).singleLine, "A multi-line label stays multi-line on one line");
    Hover(mini, 3, -20, amakna);
    RestoreMapInfo();
    Check(Text() == "[3,-20]" && !Get(label).singleLine && Get(label).height.keyword == 1 && !MapInfoCleanupPending(), "Its own setting is given back");
    BuildTemplate();
    // Text the label cannot measure yet: the estimate decides, the box still holds every line.
    measureWorks = false;
    Hover(mini, 3, -20, amakna);
    Check(!Get(label).singleLine && Encloses(), "Unmeasured text: several lines, all in the box");
    Hover(big, 3, -20);
    Check(OwnLine() && Encloses(), "Unmeasured, one line: the label's own height");
    measureWorks = true;
}
void Cleanup() {
    auto shown = Hover(mini, 3, -20, amakna);
    Check(MapInfoCleanupPending() && MinWidth().keyword == 0 && NoShrink() && !Get(label).singleLine, "Shown: cleanup is pending");
    const auto rooted = handles.size();
    module->stopRequested = true;
    Hover(mini, 4, 4, amakna);
    Check(Text() == "[4,4]", "Stopping: the hooks pass the game's tooltip through");
    const auto builder = lastBuilder;
    Get(builder).slots["m_binding"] = reinterpret_cast<int64_t>(label);
    updateHook(builder, shown, nullptr); // the game updates a tooltip remembered before the stop
    Check(Text() == "[3,-20]", "Stopping: even a remembered tooltip keeps the game's text");
    Get(label).text = Village + "\n" + TwelveCoords; // still showing a rewritten text
    mapInfo::s.shown = Text();
    failMethod = "UnityEngine.UIElements.IStyle.set_minWidth";
    TickMapInfo();
    Check(MapInfoCleanupPending() && !mapInfo::s.adjusted.empty(), "A failed template restore is retained for a retry");
    TickMapInfo();
    Check(!MapInfoCleanupPending() && OwnWidth() && OwnShrink() && OwnLine() && Text() == "[3,-20]" && handles.size() < rooted,
        "The retry gives back the template, the game's text and every reference");
    module->stopRequested = false;
    // The label's own setting fails to come back: retained, then given back.
    Hover(mini, 3, -20, amakna);
    failMethod = "set_isSingleLine";
    RestoreMapInfo();
    Check(MapInfoCleanupPending() && !Get(label).singleLine && Text() == "[3,-20]", "A failed label restore is retained for a retry");
    RestoreMapInfo();
    Check(!MapInfoCleanupPending() && OwnLine() && OwnWidth() && OwnShrink(), "The retry gives the label its own line back");
    // A label the game has rewritten since is left alone.
    Hover(mini, 3, -20, amakna);
    Get(label).text = "[9,9]";
    RestoreMapInfo();
    Check(Text() == "[9,9]" && OwnWidth() && OwnShrink() && OwnLine() && !MapInfoCleanupPending(), "Cleanup restores only its own text");
    Hover(mini, 3, -20, amakna);
    Get(label).panel = nullptr;
    RestoreMapInfo();
    Check(Text() != "[3,-20]" && OwnLine() && !MapInfoCleanupPending(), "A detached label keeps its text and gets its own line back");
    Get(label).panel = panel;
    module->servicesReady = false;
    TickMapInfo();
    Check(!MapInfoCleanupPending(), "Services gone: nothing pending");
    Check(handles.empty(), "Every managed reference is released");
}
void InstallFailures() {
    Reset();
    mapInfo::s = mapInfo::State{};
    for (const char* missing : {"get_lbl_coords", "get_isSingleLine", "set_isSingleLine"}) {
        failVerified = missing;
        InstallMapInfo();
        Check(!mapInfo::s.installed && hookRvas.empty() && logs.back() == std::string("MAP_INFO_DISABLED Verified method unavailable: ") + missing,
            std::string("A missing contract disables only this module, before any hook: ") + missing);
    }
    failVerified.clear();
    // The world id behind « Oui, sauf dans le Monde des Douze »: moved, only this module is disabled, before any hook.
    offsets[{"WorldMapData", "id"}] = 0x14;
    InstallMapInfo();
    Check(!mapInfo::s.installed && hookRvas.empty() && logs.back() == "MAP_INFO_DISABLED Map info field mismatch: id",
        "A moved world id disables only this module, before any hook");
    offsets[{"WorldMapData", "id"}] = 0x10;
    offsets[{"CartographyTooltipInfos", "zaapPrice"}] = 0x30;
    InstallMapInfo();
    Check(!mapInfo::s.installed && hookRvas.empty(), "A moved field disables only this module");
    offsets[{"CartographyTooltipInfos", "zaapPrice"}] = 0x2C;
    // The second hook fails: the first one stays a pass-through.
    failHook = 0x15EFCA0;
    InstallMapInfo();
    Check(!mapInfo::s.installed && hookRvas == std::vector<uintptr_t>{0x1BEF880}, "A failed second hook disables the module");
    const auto calls = berzCalls;
    auto infos = Make("CartographyTooltipInfos");
    Get(infos).slots = {{"mapX", 1}, {"mapY", 2}, {"showMapCoords", 1}, {"zaapPrice", -1}};
    updateHook = [](Obj builder, Obj value, Method method) { NativeUpdate(builder, value, method); };
    berzHook(mini, {0, 0}, infos, nullptr);
    Check(berzCalls == calls + 1 && Text() == "[1,2]" && mapInfo::s.records.empty() && !MapInfoCleanupPending(),
        "Not installed: the prepared hook only calls the game");
    failHook = 0;
    Reset();
    InstallMapInfo();
    Check(mapInfo::s.installed, "Installs again once the contracts match");
}
}

int main() {
    try {
        fake::Init();
        Module owner("offline");
        module = &owner;
        owner.ready = true;
        owner.servicesReady = true;
        fake::Contracts();
        fake::DefaultOptions();
        fake::ZaapPrice();
        fake::EachOption();
        fake::EachMode();
        fake::EveryLine();
        fake::EdgeCases();
        fake::Cleanup();
        fake::InstallFailures();
        RestoreMapInfo();
        module = nullptr;
        std::cout << "PASS: map info on the game's cartography tooltip: zone name on the mini-map, [x,y, dimension] on each map "
                     "per its choice (Non, Oui sauf dans le Monde des Douze by the world's id, Oui), measured row with the zaap price, non-shrinking coordinates box, its black box around every "
                     "line (label single-line only for one), template given back, bounded memory, failures and cleanup\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
