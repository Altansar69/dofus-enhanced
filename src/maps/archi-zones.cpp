#include "maps/archi-zones.hpp"
#include "souls/chat-soul-data.hpp"
#include "souls/chat-soul-status.hpp"
#include "metamob/metamob-sync.hpp"
#include "ui/native-click.hpp"
#include "maps/resource-map-ui.hpp"
#include "souls/soul-panel.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace archiZones {
// ===================================================================== model
// No game access below this line until the natives: the missing monsters, the
// zones, filters, sorting, colours and texts (archi-zones-tests covers them).
enum class Kind : uint8_t { Archimonster, Boss };
// The panel's type choice; the mini-map follows it too.
enum class Show : int { All = 0, Archimonsters = 1, Bosses = 2 };
// Quest 439: steps 863 to 865 hold the 51 dungeon bosses, 866 to 880 the 286 archimonsters.
constexpr int16_t FirstArchimonsterStep = 866;
constexpr size_t None = std::numeric_limits<size_t>::max();
struct Goal { int monster = 0; Kind kind = Kind::Archimonster; std::string name, folded; std::vector<size_t> zones; };
struct Zone {
    int id = 0, level = 0, world = 0, dungeon = 0;
    bool shape = false; // shown on the world map with an outline: it can be coloured there
    bool read = false;
    std::string name, area, folded;
    std::vector<size_t> goals;
};
struct Counts { int archimonsters = 0, bosses = 0; bool operator==(const Counts&) const = default; };
struct Filters {
    std::string search;             // folded, see Fold
    int area = 0;                   // 0: every territory; n: areas[n - 1]
    int minLevel = 0, maxLevel = 0; // 0: unbounded
    Show show = Show::All;
    bool operator==(const Filters&) const = default;
};
enum class Column : int { Name = 0, Level = 1, Missing = 2 };
struct Sort { Column column = Column::Missing; bool descending = true; bool operator==(const Sort&) const = default; };
// What the readers know, for the panel's legend. metamob: 0 not used (no usable
// key), 1 not read yet, 2 read.
struct Sources {
    bool ocreKnown = false, ocreComplete = false, bagKnown = false;
    int metamob = 0;
    bool operator==(const Sources&) const = default;
};

// Lower case without accents, for the search field: « Écume » finds « ecu ».
const char* Plain(unsigned code) {
    if ((code >= 0xC0 && code <= 0xC5) || (code >= 0xE0 && code <= 0xE5)) return "a";
    if (code == 0xC6 || code == 0xE6) return "ae";
    if (code == 0xC7 || code == 0xE7) return "c";
    if ((code >= 0xC8 && code <= 0xCB) || (code >= 0xE8 && code <= 0xEB)) return "e";
    if ((code >= 0xCC && code <= 0xCF) || (code >= 0xEC && code <= 0xEF)) return "i";
    if (code == 0xD1 || code == 0xF1) return "n";
    if ((code >= 0xD2 && code <= 0xD6) || code == 0xD8 || (code >= 0xF2 && code <= 0xF6) || code == 0xF8) return "o";
    if ((code >= 0xD9 && code <= 0xDC) || (code >= 0xF9 && code <= 0xFC)) return "u";
    if (code == 0xDD || code == 0xFD || code == 0xFF) return "y";
    if (code == 0x152 || code == 0x153) return "oe";
    if (code == 0x2018 || code == 0x2019) return "'";
    return nullptr;
}
std::string Fold(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
        if (length == 1 || i + length > text.size()) {
            out += static_cast<char>(std::tolower(lead));
            ++i;
            continue;
        }
        unsigned code = lead & (0x7F >> length);
        for (size_t k = 1; k < length; ++k) code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        if (const char* plain = Plain(code)) out += plain;
        else out.append(text, i, length);
        i += length;
    }
    return out;
}
std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
// A level field: digits only, 0 when empty (unbounded), at most 999.
int Level(const std::string& text) {
    int value = 0;
    for (char c : Trim(text)) {
        if (c < '0' || c > '9') return 0;
        value = std::min(999, value * 10 + (c - '0'));
    }
    return value;
}
// Missing-count colour scale: 1, 2, 3, 4 and 5, 6 and more. Zero: no shape.
struct Rgb { float r, g, b; };
constexpr std::array<Rgb, 5> Scale{{
    {1.000f, .835f, .290f}, // #FFD54A
    {1.000f, .651f, .169f}, // #FFA62B
    {.949f, .420f, .129f},  // #F26B21
    {.843f, .149f, .239f},  // #D7263D
    {.557f, .106f, .353f},  // #8E1B5A
}};
constexpr std::array<const char*, 5> ScaleLabels{{"1", "2", "3", "4–5", "6+"}};
int Band(int missing) { return missing <= 0 ? -1 : missing <= 3 ? missing - 1 : missing <= 5 ? 3 : 4; }
struct Color { float r = 0, g = 0, b = 0, a = 0; };
// The game's own shape colours (fill, outline); the mini-map's fill is lighter.
Color Fill(int band, bool mini) { const auto& c = Scale.at(band); return {c.r, c.g, c.b, mini ? .25f : .35f}; }
Color Line(int band) { const auto& c = Scale.at(band); return {c.r, c.g, c.b, .8f}; }

struct Model {
    std::vector<Goal> goals;
    std::vector<Zone> zones;
    std::vector<std::string> areas; // sorted, distinct
    std::vector<uint8_t> missing;   // per goal
    std::vector<Counts> counts;     // per zone
    Sources sources;
    uint64_t version = 0;           // changes with missing, counts or sources
    static bool Shown(const Goal& goal, Show show) {
        return show == Show::All || (show == Show::Bosses) == (goal.kind == Kind::Boss);
    }
    int Count(size_t zone, Show show) const {
        const auto& c = counts.at(zone);
        return show == Show::Archimonsters ? c.archimonsters : show == Show::Bosses ? c.bosses : c.archimonsters + c.bosses;
    }
    // Missing = not validated in L’éternelle moisson (still to do, or quest not
    // started) AND not in the bag AND not in the Metamob quest. Unknown Ocre:
    // never missing (nothing is coloured); unknown bag: not in the bag; Metamob
    // without a usable key, or not read yet: ignored.
    void Recount() {
        std::vector<uint8_t> next(goals.size(), 0);
        Sources found;
        size_t ocreKnown = 0, ocreDone = 0, used = 0, unread = 0;
        auto list = metamob::OcreList::NotRead; // why the unread ones have no quantity
        for (size_t i = 0; i < goals.size(); ++i) {
            const auto ocre = OcreProgress(goals[i].monster);
            const auto bag = BagProgress(goals[i].monster);
            if (ocre != SoulState::Unknown) ++ocreKnown;
            if (ocre == SoulState::Done) ++ocreDone;
            if (bag != SoulState::Unknown) found.bagKnown = true;
            if ((ocre != SoulState::Missing && ocre != SoulState::NotStarted) || bag == SoulState::Done) continue;
            const auto metamob = metamob::MetamobOcreQuantity(goals[i].monster);
            if (metamob.usable) {
                ++used;
                if (metamob.quantity < 0) { ++unread; list = metamob.list; }
                else if (metamob.quantity > 0) continue;
            }
            next[i] = 1;
        }
        found.ocreKnown = ocreKnown > 0;
        found.ocreComplete = !goals.empty() && ocreDone == goals.size();
        found.metamob = used == 0 ? 0 : unread == 0 ? 2 : list == metamob::OcreList::Failed ? 3 :
            list == metamob::OcreList::NoQuest ? 4 : list == metamob::OcreList::SeveralQuests ? 5 : 1;
        std::vector<Counts> zoneCounts(zones.size());
        for (size_t z = 0; z < zones.size(); ++z)
            for (const auto g : zones[z].goals)
                if (next[g]) ++(goals[g].kind == Kind::Boss ? zoneCounts[z].bosses : zoneCounts[z].archimonsters);
        if (next != missing || zoneCounts != counts || !(found == sources)) ++version;
        missing = std::move(next);
        counts = std::move(zoneCounts);
        sources = found;
    }
    bool Passes(size_t index, const Filters& filters) const {
        const auto& zone = zones.at(index);
        if (filters.area > 0 && (filters.area > static_cast<int>(areas.size()) || zone.area != areas[filters.area - 1])) return false;
        if (filters.minLevel > 0 && zone.level < filters.minLevel) return false;
        if (filters.maxLevel > 0 && zone.level > filters.maxLevel) return false;
        if (filters.search.empty() || zone.folded.find(filters.search) != std::string::npos) return true;
        // A missing monster of the zone found by its name.
        return std::any_of(zone.goals.begin(), zone.goals.end(), [&](size_t g) {
            return missing.at(g) && Shown(goals[g], filters.show) && goals[g].folded.find(filters.search) != std::string::npos;
        });
    }
    // The panel's rows: zones still holding a missing monster of the chosen
    // type and passing the filters, sorted as the column headers say.
    std::vector<size_t> Rows(const Filters& filters, const Sort& sort) const {
        std::vector<size_t> rows;
        for (size_t z = 0; z < zones.size(); ++z)
            if (Count(z, filters.show) > 0 && Passes(z, filters)) rows.push_back(z);
        std::sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
            const auto& za = zones[a];
            const auto& zb = zones[b];
            const int ca = Count(a, filters.show), cb = Count(b, filters.show);
            const auto nameA = Fold(za.name), nameB = Fold(zb.name);
            if (sort.column == Column::Name && nameA != nameB) return sort.descending ? nameA > nameB : nameA < nameB;
            if (sort.column == Column::Level && za.level != zb.level) return sort.descending ? za.level > zb.level : za.level < zb.level;
            if (sort.column == Column::Missing && ca != cb) return sort.descending ? ca > cb : ca < cb;
            if (ca != cb) return ca > cb;
            if (za.level != zb.level) return za.level < zb.level;
            if (nameA != nameB) return nameA < nameB;
            return za.id < zb.id;
        });
        return rows;
    }
    // Distinct missing monsters of each kind, and zones holding one, for a type choice.
    std::array<int, 3> Totals(Show show) const {
        std::array<int, 3> totals{};
        for (size_t g = 0; g < goals.size(); ++g)
            if (missing.at(g) && Shown(goals[g], show)) ++totals[goals[g].kind == Kind::Boss ? 1 : 0];
        for (size_t z = 0; z < zones.size(); ++z) if (Count(z, show) > 0) ++totals[2];
        return totals;
    }
};

// ===================================================================== texts
constexpr const char* SquareTooltip = "Ocre : les archimonstres et boss de donjon qu’il te manque, zone par zone";
constexpr const char* PanelTitle = "Ocre";
constexpr const char* AllAreas = "Tous les territoires";
constexpr std::array<const char*, 3> ShowLabels{{"Tous", "Archimonstres", "Boss de donjon"}};
constexpr std::array<const char*, 3> HeaderLabels{{"Territoire", "Niveau", "Manquants"}};
constexpr const char* LoadingText = "Lecture des zones de L’éternelle moisson…";
constexpr const char* OcreUnknownText = "L’avancement de L’éternelle moisson n’est pas encore connu : ouvre le journal de quêtes "
    "pour le charger. Aucune zone n’est colorée en attendant.";
constexpr const char* CompleteText = "L’éternelle moisson est terminée : il ne te manque rien.";
constexpr const char* NoMatchText = "Aucune zone ne correspond à ta recherche.";
std::string Plural(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count > 1 ? plural : singular);
}
std::string NothingMissingText(Show show) {
    switch (show) {
    case Show::Archimonsters: return "Il ne te manque aucun archimonstre.";
    case Show::Bosses: return "Il ne te manque aucun boss de donjon.";
    default: return "Il ne te manque aucun archimonstre ni boss de donjon.";
    }
}
std::string SourcesText(const Sources& sources, bool metamobShown) {
    std::string text = sources.ocreKnown ? "Ocre : lu" : "Ocre : ?";
    text += sources.bagKnown ? " · Sac : lu" : " · Sac : ? (compté comme absent)";
    if (metamobShown) switch (sources.metamob) {
    case 0: text += " · Metamob : non utilisé"; break;
    case 2: text += " · Metamob : lu"; break;
    case 3: text += " · Metamob : ? (lecture impossible, nouvel essai automatique)"; break;
    case 4: text += " · Metamob : aucune quête Ocre Dofus Unity sur ton compte"; break;
    case 5: text += " · Metamob : plusieurs quêtes Ocre, aucune n’indique ce personnage"; break;
    default: text += " · Metamob : ? (pas encore lu)"; break;
    }
    return text;
}
std::string StatusText(const Model& model, Show show, bool ready) {
    if (!ready) return LoadingText;
    const auto& sources = model.sources;
    if (!sources.ocreKnown) return OcreUnknownText;
    if (sources.ocreComplete) return CompleteText;
    const auto totals = model.Totals(show);
    std::string summary;
    if (totals[0] + totals[1] == 0) summary = NothingMissingText(show);
    else {
        if (show != Show::Bosses) summary = Plural(totals[0], "archimonstre", "archimonstres");
        if (show == Show::All) summary += " et ";
        if (show != Show::Archimonsters) summary += Plural(totals[1], "boss de donjon", "boss de donjon");
        summary += totals[0] + totals[1] > 1 ? " manquants" : " manquant";
        summary += ", dans " + Plural(totals[2], "zone", "zones") + ".";
    }
    return summary + "\n" + SourcesText(sources, totals[0] + totals[1] > 0 || sources.metamob != 0);
}
std::string NoResultText(const Model& model, Show show, bool ready) {
    if (!ready) return LoadingText;
    if (!model.sources.ocreKnown) return OcreUnknownText;
    if (model.sources.ocreComplete) return CompleteText;
    const auto totals = model.Totals(show);
    return totals[2] == 0 ? NothingMissingText(show) : NoMatchText;
}
std::string RowTooltip(const Model& model, size_t index, Show show) {
    const auto& zone = model.zones.at(index);
    std::string text = zone.name;
    if (!zone.area.empty()) text += " · " + zone.area;
    if (zone.level > 0) text += " · niveau " + std::to_string(zone.level);
    std::string names;
    for (const auto g : zone.goals) {
        const auto& goal = model.goals[g];
        if (!model.missing.at(g) || !Model::Shown(goal, show)) continue;
        if (!names.empty()) names += ", ";
        names += goal.name.empty() ? "monstre " + std::to_string(goal.monster) : goal.name;
        if (goal.kind == Kind::Boss) names += " (boss)";
    }
    if (!names.empty()) text += "\nManquants ici : " + names + ".";
    if (!zone.shape) text += zone.dungeon > 0 ? "\nDonjon sans contour sur la carte : un clic centre la carte sur son entrée."
                                              : "\nZone sans contour sur la carte : un clic centre la carte sur elle.";
    return text;
}

// =================================================================== natives
using namespace managed;
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* DataImage = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
constexpr const char* ShapePrefix = "dofus_miniatures_ocre_";
constexpr const char* LayerName = "dofus_miniatures_ocre";
// FigmaIcons: archimonster, boss, magnifier.
constexpr int IconArchimonster = 10, IconBoss = 61, IconMagnifier = 194;
// Work per pump: monsters and zones read, shapes created and removed, rows built.
constexpr size_t GoalBudget = 48, ZoneBudget = 24, CreateBudget = 12, RemoveBudget = 32, RowBudget = 24;
// A shape asked for appears in the view's registry on a following frame; one
// that never does is given up after this delay.
constexpr uint64_t ShapeWaitMs = 3000;
constexpr size_t MiniView = 0, BigView = 1; // keys of the cartography service's views
struct Natives {
    Klass window{}, binding{}, view{}, service{}, worldMap{}, shape{}, shapeBase{}, marker{};
    Klass monster{}, dataCenter{}, subRoot{}, subArea{}, area{}, dungeonRoot{}, dungeon{}, mapInfo{}, point{};
    Klass element{}, label{}, scroll{}, button{}, figmaWindow{}, input{}, header{}, row{}, rowBase{}, dropdown{};
    Method leaveMode{}, moveTo{}, switchWorld{}, worldButtons{}, conquestButton{};
    Method addShape{}, layer{}, fill{}, line{}, hover{}, reparent{}, dispose{};
    Method monsterById{}, monsterName{}, subAreas{}, subAreaById{}, subAreaName{}, subAreaWorld{}, subAreaCenter{}, subAreaArea{}, areaName{};
    Method dungeons{}, dungeonByMap{}, mapInfoById{}, mapInfoSubArea{};
    Method elementCtor{}, add{}, insert{}, indexOf{}, removeFromHierarchy{}, parent{}, panel{}, pickingMode{}, setName{}, tooltip{}, addClass{};
    Method labelCtor{}, labelText{}, scrollCtor{}, scrollContent{}, scrollHorizontal{};
    Method buttonCtor{}, buttonIcon{}, buttonStyle{}, buttonRichText{};
    Method windowCtor{}, windowTitle{}, windowHeaderStyle{}, windowClose{};
    Method inputCtor{}, inputIcon{}, inputText{}, inputPlaceholder{}, inputMax{}, inputNumbers{};
    Method headerCtor{}, headerText{}, headerState{}, headerSetState{};
    Method rowCtor{}, rowStyle{}, rowText{}, rowIcon{}, rowActivated{};
    Method dropdownCtor{}, dropdownAdd{}, dropdownSelect{}, dropdownSelected{}, dropdownChoice{};
} n;

// One coloured zone of a view: asked with the game's AddAreaShape, then found
// in the view's registry and moved into the mod's layer.
struct Shape {
    Ref shape, name;
    int band = -1;
    uint64_t since = 0;
    bool ready = false, failed = false;
};
struct View {
    Ref owner, root, layer;
    int worldId = 0;
    uint64_t shownAt = 0; // last time the shapes were shown again (the game's besm hides every shape)
    std::map<int, Shape> shapes; // by zone id
    bool retiring = false;
    uint64_t retiredAt = 0;
};
struct Row {
    Ref element, level, count, swatch;
    size_t zone = None;
    std::string name, levelText, countText, tooltip;
    int band = -2, icon = -1;
    bool shown = false, activated = false;
};
struct Panel {
    Ref root, window, close, search, areas, minLevel, maxLevel, type, noResult, noResultLabel, content, list, status;
    std::array<Ref, 3> headers;
    std::array<int, 3> states{};
    std::vector<Row> rows;
    std::vector<size_t> zones; // shown rows, model indices
    size_t areaItems = 0;      // territories added to the dropdown
    int areaChoice = 0, showChoice = 0, activeZone = 0;
    std::string statusText, noResultText;
    int noResultShown = -1;
    Filters shownFilters;
    Sort shownSort;
    uint64_t shownVersion = 0;
    bool dirty = true;
};
struct Square { Ref window, buttons, button; };
struct Build {
    std::map<int, size_t> zoneIndex;
    size_t nextGoal = 0, nextZone = 0;
    bool started = false, ready = false, counted = false;
    uint64_t revision = 0, countedAt = 0, nextCount = 0;
};
struct Clicks { bool square = false, close = false; int row = -1; };
bool installed = false, active = false, needsAsked = false;
Clicks pendingClicks;
uint64_t nextLog = 0, retryUi = 0, retryMap = 0;
Filters filters;
Sort sort;
Model model;
Build build;
Square square;
std::unique_ptr<Panel> panel;
std::array<View, 2> views;

bool Session() {
    return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested &&
        module->compositor.get();
}
void Report(const char* area, const std::exception& error) {
    const auto now = GetTickCount64();
    if (now < nextLog) return;
    nextLog = now + 10000;
    Log(std::string("ARCHI_ZONES_ERROR ") + area + ": " + error.what());
}
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Ocre zones field mismatch: ") + name);
}
template<class T> T& At(Obj owner, const char* name) { return *static_cast<T*>(api.FieldAddress(owner, name)); }
std::string Str(Obj value) { return api.String(value); }
int ListCount(Obj list, int maximum) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > maximum) throw std::runtime_error("Ocre zones list outside bounds");
    return count;
}

// ------------------------------------------------------------- UI helpers
Obj Parent(Obj element) { return api.Invoke(n.parent, element); }
Obj StyleBox(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    auto value = api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str());
    if (!value || !api.class_is_valuetype(api.object_get_class(value))) throw std::runtime_error("Ocre panel expected a boxed style");
    return value;
}
void WriteStyle(Obj element, const char* property, Obj box) {
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box)});
}
template<class T> void Style(Obj element, const char* property, T value) {
    Ref box(StyleBox(element, property));
    api.Call(box.get(), "set_value", {&value});
    WriteStyle(element, property, box.get());
}
void Length(Obj element, const char* property, float value, int unit) {
    Ref length(api.New(api.Class(UiImage, UiSpace, "Length"), {&value, &unit}));
    Ref box(StyleBox(element, property));
    api.Call(box.get(), "set_value", {api.object_unbox(length.get())});
    WriteStyle(element, property, box.get());
}
void Pixels(Obj element, const char* property, float value) { Length(element, property, value, 0); }
void Percent(Obj element, const char* property, float value) { Length(element, property, value, 1); }
void Display(Obj element, bool shown) { Style(element, "display", shown ? 0 : 1); }
void Text(Obj element, Method setter, const std::string& text) {
    Ref value(api.string_new(text.c_str()));
    api.Invoke(setter, element, {value.get()});
}
void Classes(Obj element, std::initializer_list<const char*> names) {
    for (const auto* name : names) Text(element, n.addClass, name);
}
void Ignore(Obj element) { int ignore = 1; api.Invoke(n.pickingMode, element, {&ignore}); }
void Add(Obj parent, Obj child) { api.Invoke(n.add, parent, {child}); }
void Padding(Obj element, float vertical, float horizontal) {
    Pixels(element, "paddingTop", vertical); Pixels(element, "paddingBottom", vertical);
    Pixels(element, "paddingLeft", horizontal); Pixels(element, "paddingRight", horizontal);
}
Obj New(Klass type, Method ctor) {
    Ref value(api.object_new(type));
    if (!value.get()) throw std::runtime_error("Ocre panel allocation failed");
    api.Invoke(ctor, value.get());
    return value.get();
}
Obj Element(Obj parent = nullptr) {
    Ref element(New(n.element, n.elementCtor));
    if (parent) Add(parent, element.get());
    return element.get();
}
Obj Label(Obj parent, const std::string& text, std::initializer_list<const char*> classes) {
    Ref label(New(n.label, n.labelCtor));
    Classes(label.get(), classes);
    Text(label.get(), n.labelText, text);
    if (parent) Add(parent, label.get());
    return label.get();
}
Obj Input(Obj parent, const char* hint, int maxLength, float width) {
    Ref input(New(n.input, n.inputCtor));
    Text(input.get(), n.inputPlaceholder, hint);
    api.Invoke(n.inputMax, input.get(), {&maxLength});
    Pixels(input.get(), "width", width);
    Add(parent, input.get());
    return input.get();
}
Obj Dropdown(Obj parent, const std::vector<std::string>& items, int selected, float width) {
    Ref dropdown(New(n.dropdown, n.dropdownCtor));
    At<bool>(dropdown.get(), "closeOnSelection") = true;
    for (size_t i = 0; i < items.size(); ++i) {
        Ref label(api.string_new(items[i].c_str()));
        int style = 0, icon = 0, id = static_cast<int>(i);
        api.Invoke(n.dropdownAdd, dropdown.get(), {label.get(), nullptr, &style, &icon, &icon, &id});
    }
    api.Invoke(n.dropdownSelect, dropdown.get(), {&selected});
    Text(dropdown.get(), n.dropdownChoice, items.at(selected));
    Pixels(dropdown.get(), "width", width);
    Add(parent, dropdown.get());
    return dropdown.get();
}
// The choice of a native dropdown (the item id), -1 without one.
int Selected(Obj dropdown) {
    Ref selected(api.Invoke(n.dropdownSelected, dropdown));
    return selected.get() ? At<int>(selected.get(), "id") : -1;
}
void Remove(Ref& element) {
    if (element.get() && Parent(element.get())) api.Invoke(n.removeFromHierarchy, element.get());
    element.reset();
}

// ------------------------------------------------------------------- data
Obj SubAreaRoot() { return api.Invoke(n.subAreas, nullptr); }
size_t ZoneOf(int id) {
    const auto found = build.zoneIndex.find(id);
    if (found != build.zoneIndex.end()) return found->second;
    model.zones.push_back({});
    model.zones.back().id = id;
    model.zones.back().name = "Zone " + std::to_string(id);
    build.zoneIndex.emplace(id, model.zones.size() - 1);
    return model.zones.size() - 1;
}
// The monster's own zones (an archimonster's are a subset of its normal monster's).
void ReadGoal(size_t index) {
    auto& goal = model.goals[index];
    int64_t id = goal.monster;
    Ref monster(api.Invoke(n.monsterById, nullptr, {&id}));
    if (!monster.get()) return;
    Ref name(api.Invoke(n.monsterName, monster.get()));
    goal.name = Str(name.get());
    goal.folded = Fold(goal.name);
    Ref list(api.Field(monster.get(), "subareas"));
    if (!list.get()) return;
    for (int i = 0, count = ListCount(list.get(), 256); i < count; ++i) {
        const auto subarea = static_cast<int>(api.Value<uint32_t>(api.Call(list.get(), "get_Item", {&i})));
        if (subarea <= 0) continue;
        const auto zone = ZoneOf(subarea);
        if (std::find(goal.zones.begin(), goal.zones.end(), zone) != goal.zones.end()) continue;
        goal.zones.push_back(zone);
        model.zones[zone].goals.push_back(index);
    }
}
void ReadZone(Obj root, size_t index) {
    auto& zone = model.zones[index];
    int id = zone.id;
    Ref sub(api.Invoke(n.subAreaById, root, {&id}));
    if (!sub.get()) return;
    Ref name(api.Invoke(n.subAreaName, sub.get()));
    if (!Str(name.get()).empty()) zone.name = Str(name.get());
    zone.level = static_cast<int>(std::min<uint32_t>(At<uint32_t>(sub.get(), "level"), 9999));
    zone.world = api.Value<int>(api.Invoke(n.subAreaWorld, sub.get()));
    zone.dungeon = At<int>(sub.get(), "dungeonId");
    Ref outline(api.Field(sub.get(), "shape"));
    zone.shape = At<bool>(sub.get(), "displayOnWorldMap") && outline.get() && ListCount(outline.get(), 1 << 20) > 0;
    Ref area(api.Invoke(n.subAreaArea, sub.get()));
    if (area.get()) { Ref areaName(api.Invoke(n.areaName, area.get())); zone.area = Str(areaName.get()); }
    zone.folded = Fold(zone.name + " " + zone.area);
    zone.read = true;
}
// Built once, from the game's own data (MonsterData.subareas, SubAreaData),
// within a budget per pump; a monster or zone that cannot be read is skipped.
void BuildData() {
    if (build.ready) return;
    if (!build.started) {
        model = {};
        for (const auto& goal : chat_souls::OcreGoals) {
            model.goals.push_back({});
            model.goals.back().monster = goal.monster;
            model.goals.back().kind = goal.step < FirstArchimonsterStep ? Kind::Boss : Kind::Archimonster;
        }
        build.started = true;
    }
    for (size_t budget = GoalBudget; budget > 0 && build.nextGoal < model.goals.size(); --budget) {
        const auto index = build.nextGoal++;
        try { ReadGoal(index); } catch (const std::exception& e) { Report("monster", e); }
    }
    if (build.nextGoal < model.goals.size()) return;
    if (build.nextZone < model.zones.size()) {
        Ref root(SubAreaRoot());
        if (!root.get()) throw std::runtime_error("Subarea metadata unavailable");
        for (size_t budget = ZoneBudget; budget > 0 && build.nextZone < model.zones.size(); --budget) {
            const auto index = build.nextZone++;
            try { ReadZone(root.get(), index); } catch (const std::exception& e) { Report("zone", e); }
        }
        if (build.nextZone < model.zones.size()) return;
    }
    std::vector<std::string> areas;
    for (const auto& zone : model.zones) if (!zone.area.empty()) areas.push_back(zone.area);
    std::sort(areas.begin(), areas.end(), [](const std::string& a, const std::string& b) {
        const auto fa = Fold(a), fb = Fold(b);
        return fa != fb ? fa < fb : a < b;
    });
    areas.erase(std::unique(areas.begin(), areas.end()), areas.end());
    model.areas = std::move(areas);
    model.missing.assign(model.goals.size(), 0);
    model.counts.assign(model.zones.size(), {});
    build.ready = true;
    const auto drawable = std::count_if(model.zones.begin(), model.zones.end(), [](const Zone& z) { return z.shape; });
    Log("ARCHI_ZONES_DATA_READY goals=" + std::to_string(model.goals.size()) + " zones=" + std::to_string(model.zones.size()) +
        " outlined=" + std::to_string(drawable) + " territories=" + std::to_string(model.areas.size()));
}
// At most once per second, when a reader published something new, and every
// two seconds anyway (the Metamob list and the bag's validity change on their own).
void Count(uint64_t now) {
    const auto revision = SoulStatusRevision();
    if (build.counted && (now < build.nextCount || (revision == build.revision && now < build.countedAt + 2000))) return;
    model.Recount();
    build.counted = true;
    build.revision = revision;
    build.countedAt = now;
    build.nextCount = now + 1000;
}

// ------------------------------------------------------------- the window
bool CurrentWindow(Obj window) {
    return window && Session() && api.object_get_class(window) == n.window &&
        api.Field(window, "dtzy") == module->FindService("fng") && api.Field(window, "dtzz") == module->FindService("fkc") &&
        At<bool>(window, "dtzx");
}
int Mode(Obj window) { return At<int>(window, "dubo"); }
void DetachSquare() {
    Remove(square.button);
    square = {};
}
void DetachPanel() {
    if (!panel) return;
    Remove(panel->window);
    panel.reset();
}
void DetachUi() {
    try { DetachPanel(); } catch (const std::exception& e) { Report("panel cleanup", e); }
    try { DetachSquare(); } catch (const std::exception& e) { Report("square cleanup", e); }
}
// The « Ocre » square: the game's own mode button (Anomalies, Conquête), right
// under « Conquête » in the map's mode column.
void EnsureSquare(Obj window) {
    if (square.window.get() == window && square.button.get() && Parent(square.button.get()) == square.buttons.get()) return;
    DetachSquare();
    Ref binding(api.Field(window, "dzyi"));
    if (!binding.get() || api.object_get_class(binding.get()) != n.binding) throw std::runtime_error("Cartography binding unavailable");
    auto* data = api.object_unbox(binding.get());
    Ref buttons(api.Invoke(n.worldButtons, data)), conquest(api.Invoke(n.conquestButton, data));
    if (!buttons.get() || !conquest.get() || Parent(conquest.get()) != buttons.get()) throw std::runtime_error("Cartography mode column unavailable");
    const int index = api.Value<int>(api.Invoke(n.indexOf, buttons.get(), {conquest.get()}));
    if (index < 0) throw std::runtime_error("Conquest square not found");
    Ref button(New(n.button, n.buttonCtor));
    bool rich = false;
    int secondary = 1, icon = IconArchimonster;
    api.Invoke(n.buttonRichText, button.get(), {&rich});
    api.Invoke(n.buttonStyle, button.get(), {&secondary});
    api.Invoke(n.buttonIcon, button.get(), {&icon});
    Text(button.get(), n.tooltip, SquareTooltip);
    Pixels(button.get(), "marginTop", 4);
    int at = index + 1;
    api.Invoke(n.insert, buttons.get(), {&at, button.get()});
    square.window.reset(window);
    square.buttons = std::move(buttons);
    square.button = std::move(button);
    Log("ARCHI_ZONES_SQUARE_READY");
}
// Entering the Ocre mode leaves the game's anomaly or conquest mode the way its
// windows' close buttons do: one panel on the right at a time.
void Toggle(Obj window) {
    active = !active;
    if (!active) return;
    if (Mode(window) != 0) { int none = 0; api.Invoke(n.leaveMode, window, {&none}); }
    Log("ARCHI_ZONES_MODE on");
}

// -------------------------------------------------------------- the panel
Obj Header(Obj parent, int column, float width) {
    Ref header(New(n.header, n.headerCtor));
    Text(header.get(), n.headerText, HeaderLabels.at(column));
    if (width > 0) {
        Pixels(header.get(), "width", width);
        Style(header.get(), "justifyContent", 2);
    }
    Add(parent, header.get());
    return header.get();
}
// The anomaly panel's layout (CartographyMapUI ctr_locTreeAnomaly) and the
// game's own components and USS classes: search, territories, level range,
// type; sortable columns; the rows; a footer with the legend.
void CreatePanel(Obj root) {
    auto next = std::make_unique<Panel>();
    auto& p = *next;
    p.root.reset(root);
    p.window.reset(New(n.figmaWindow, n.windowCtor));
    Obj window = p.window.get();
    Text(window, n.windowTitle, PanelTitle);
    int secondary = 1;
    api.Invoke(n.windowHeaderStyle, window, {&secondary});
    p.close.reset(api.Invoke(n.windowClose, window));
    Style(window, "position", 1);
    Percent(window, "top", 10);
    Pixels(window, "right", 16);
    Pixels(window, "width", 560);
    Pixels(window, "height", 700);
    Percent(window, "maxHeight", 86);
    Style(window, "flexShrink", 0.0f);
    Ref body(Element(window));
    Style(body.get(), "flexGrow", 1.0f);
    Ref top(Element(body.get()));
    Classes(top.get(), {"backgroundColor_background_medium90"});
    Style(top.get(), "flexShrink", 0.0f);
    Ref filtersBox(Element(top.get()));
    Ignore(filtersBox.get());
    Style(filtersBox.get(), "flexGrow", 1.0f);
    Padding(filtersBox.get(), 16, 16);
    p.search.reset(Input(filtersBox.get(), "Rechercher", 40, 254));
    int magnifier = IconMagnifier;
    api.Invoke(n.inputIcon, p.search.get(), {&magnifier});
    Text(p.search.get(), n.tooltip, "Une zone, un territoire ou un monstre manquant");
    Pixels(p.search.get(), "marginBottom", 8);
    p.areas.reset(Dropdown(filtersBox.get(), {AllAreas}, 0, 254));
    Ref level(Element(filtersBox.get()));
    Ignore(level.get());
    Style(level.get(), "position", 1);
    Pixels(level.get(), "left", 307);
    Pixels(level.get(), "top", 14);
    Pixels(level.get(), "width", 240);
    Style(level.get(), "flexDirection", 2);
    Style(level.get(), "alignItems", 2);
    Style(level.get(), "justifyContent", 2);
    Ref from(Label(level.get(), "Niveau", {"normal2_p"}));
    Pixels(from.get(), "paddingRight", 8);
    p.minLevel.reset(Input(level.get(), "1", 3, 60));
    bool numbers = true;
    api.Invoke(n.inputNumbers, p.minLevel.get(), {&numbers});
    Style(p.minLevel.get(), "flexShrink", 0.0f);
    Ref to(Label(level.get(), "à", {"normal2_p"}));
    Pixels(to.get(), "paddingLeft", 8);
    Pixels(to.get(), "paddingRight", 8);
    p.maxLevel.reset(Input(level.get(), "200", 3, 60));
    api.Invoke(n.inputNumbers, p.maxLevel.get(), {&numbers});
    Ref typeRow(Element(filtersBox.get()));
    Ignore(typeRow.get());
    Style(typeRow.get(), "position", 1);
    Pixels(typeRow.get(), "left", 307);
    Pixels(typeRow.get(), "top", 55);
    Pixels(typeRow.get(), "width", 240);
    Style(typeRow.get(), "flexDirection", 2);
    Style(typeRow.get(), "justifyContent", 2);
    p.showChoice = static_cast<int>(filters.show);
    p.type.reset(Dropdown(typeRow.get(), {ShowLabels[0], ShowLabels[1], ShowLabels[2]}, p.showChoice, 200));
    Text(p.type.get(), n.tooltip, "Archimonstres, boss de donjon, ou les deux (la mini-carte suit ce choix)");
    p.noResult.reset(Element(body.get()));
    Percent(p.noResult.get(), "height", 100);
    Padding(p.noResult.get(), 16, 24);
    Style(p.noResult.get(), "alignItems", 2);
    Style(p.noResult.get(), "justifyContent", 1);
    Display(p.noResult.get(), false);
    p.noResultLabel.reset(Label(p.noResult.get(), "", {"italicShort_medium", "textColor_highlight_secondary80"}));
    Style(p.noResultLabel.get(), "whiteSpace", 0);
    p.content.reset(Element(body.get()));
    Style(p.content.get(), "flexGrow", 1.0f);
    Pixels(p.content.get(), "marginTop", 16);
    Pixels(p.content.get(), "paddingLeft", 16);
    Ref heads(Element(p.content.get()));
    Style(heads.get(), "flexDirection", 2);
    Pixels(heads.get(), "paddingRight", 24);
    Pixels(heads.get(), "paddingLeft", 16);
    Pixels(heads.get(), "marginBottom", 8);
    p.headers[0].reset(Header(heads.get(), 0, 0));
    Ref right(Element(heads.get()));
    Style(right.get(), "flexDirection", 2);
    Style(right.get(), "justifyContent", 2);
    Style(right.get(), "flexGrow", 1.0f);
    p.headers[1].reset(Header(right.get(), 1, 100));
    p.headers[2].reset(Header(right.get(), 2, 100));
    for (size_t i = 0; i < 3; ++i) {
        p.states[i] = sort.column == static_cast<Column>(i) ? (sort.descending ? 2 : 1) : 0;
        if (p.states[i]) api.Invoke(n.headerSetState, p.headers[i].get(), {&p.states[i]});
    }
    p.list.reset(api.object_new(n.scroll));
    int vertical = 0, hidden = 2;
    api.Invoke(n.scrollCtor, p.list.get(), {&vertical});
    api.Invoke(n.scrollHorizontal, p.list.get(), {&hidden});
    Classes(p.list.get(), {"scrollbarPadding"});
    Style(p.list.get(), "flexGrow", 1.0f);
    Add(p.content.get(), p.list.get());
    Ref footer(Element(window));
    Classes(footer.get(), {"Footer"});
    Padding(footer.get(), 12, 16);
    Style(footer.get(), "flexShrink", 0.0f);
    Ref legend(Element(footer.get()));
    Style(legend.get(), "flexDirection", 2);
    Style(legend.get(), "alignItems", 2);
    Style(legend.get(), "flexWrap", 1);
    Pixels(legend.get(), "marginBottom", 6);
    Ref legendTitle(Label(legend.get(), "Manquants par zone :", {"textColor_white_white80"}));
    Pixels(legendTitle.get(), "marginRight", 8);
    for (size_t band = 0; band < Scale.size(); ++band) {
        Ref swatch(Element(legend.get()));
        Pixels(swatch.get(), "width", 14);
        Pixels(swatch.get(), "height", 14);
        Pixels(swatch.get(), "marginRight", 4);
        const auto fill = Fill(static_cast<int>(band), false);
        Style(swatch.get(), "backgroundColor", Color{fill.r, fill.g, fill.b, .9f});
        Ref text(Label(legend.get(), ScaleLabels[band], {"textColor_white_white80"}));
        Pixels(text.get(), "marginRight", 12);
    }
    p.status.reset(Label(footer.get(), LoadingText, {"textColor_white_white80"}));
    Style(p.status.get(), "whiteSpace", 0);
    p.statusText = LoadingText;
    Add(root, window);
    panel = std::move(next);
    Log("ARCHI_ZONES_PANEL_READY");
}
Row CreateRow(Obj list) {
    Row row;
    row.element.reset(New(n.row, n.rowCtor));
    int choice = 0;
    api.Invoke(n.rowStyle, row.element.get(), {&choice});
    Pixels(row.element.get(), "minHeight", 40);
    Pixels(row.element.get(), "marginBottom", 4);
    Display(row.element.get(), false);
    row.level.reset(Label(row.element.get(), "", {"textShort_largeRegular", "textColor_white_white80"}));
    Ignore(row.level.get());
    Pixels(row.level.get(), "width", 100);
    Style(row.level.get(), "flexShrink", 0.0f);
    Style(row.level.get(), "unityTextAlign", 5);
    Ref box(Element(row.element.get()));
    Ignore(box.get());
    Pixels(box.get(), "width", 100);
    Style(box.get(), "flexShrink", 0.0f);
    Style(box.get(), "flexDirection", 2);
    Style(box.get(), "alignItems", 2);
    Style(box.get(), "justifyContent", 2);
    row.swatch.reset(Element(box.get()));
    Ignore(row.swatch.get());
    Pixels(row.swatch.get(), "width", 12);
    Pixels(row.swatch.get(), "height", 12);
    Pixels(row.swatch.get(), "marginRight", 6);
    row.count.reset(Label(box.get(), "", {"textShort_largeRegular", "textColor_white_white80"}));
    Ignore(row.count.get());
    Add(list, row.element.get());
    return row;
}
void RenderRow(Row& row, size_t index) {
    const auto& zone = model.zones.at(index);
    const int count = model.Count(index, filters.show);
    const auto& counts = model.counts.at(index);
    const int icon = filters.show == Show::Bosses || (filters.show == Show::All && counts.archimonsters == 0) ? IconBoss : IconArchimonster;
    const auto levelText = zone.level > 0 ? std::to_string(zone.level) : std::string("–");
    const auto countText = std::to_string(count);
    const auto tooltip = RowTooltip(model, index, filters.show);
    const int band = Band(count);
    const bool activated = panel->activeZone == zone.id;
    Obj element = row.element.get();
    if (row.name != zone.name) { Text(element, n.rowText, zone.name); row.name = zone.name; }
    if (row.icon != icon) { int value = icon; api.Invoke(n.rowIcon, element, {&value}); row.icon = icon; }
    if (row.levelText != levelText) { Text(row.level.get(), n.labelText, levelText); row.levelText = levelText; }
    if (row.countText != countText) { Text(row.count.get(), n.labelText, countText); row.countText = countText; }
    if (row.band != band && band >= 0) {
        const auto fill = Fill(band, false);
        Style(row.swatch.get(), "backgroundColor", Color{fill.r, fill.g, fill.b, .9f});
        row.band = band;
    }
    if (row.tooltip != tooltip) { Text(element, n.tooltip, tooltip); row.tooltip = tooltip; }
    if (row.activated != activated) { bool value = activated; api.Invoke(n.rowActivated, element, {&value}); row.activated = activated; }
    if (!row.shown) { Display(element, true); row.shown = true; }
    row.zone = index;
}
// The panel's controls, read at each pump (native components keep their own
// input; no managed callback is created).
void ReadControls(Panel& p) {
    Filters next = filters;
    next.search = Fold(Trim(Str(api.Invoke(n.inputText, p.search.get()))));
    next.minLevel = Level(Str(api.Invoke(n.inputText, p.minLevel.get())));
    next.maxLevel = Level(Str(api.Invoke(n.inputText, p.maxLevel.get())));
    const int area = Selected(p.areas.get());
    if (area >= 0 && area <= static_cast<int>(p.areaItems) && area != p.areaChoice) {
        p.areaChoice = area;
        Text(p.areas.get(), n.dropdownChoice, area == 0 ? std::string(AllAreas) : model.areas.at(area - 1));
    }
    next.area = p.areaChoice;
    const int show = Selected(p.type.get());
    if (show >= 0 && show < 3 && show != p.showChoice) {
        p.showChoice = show;
        Text(p.type.get(), n.dropdownChoice, ShowLabels[show]);
    }
    next.show = static_cast<Show>(p.showChoice);
    // One sorted column at a time: the header just clicked (its state moved)
    // wins, the others go back to inactive.
    int clicked = -1, clickedState = 0;
    for (int i = 0; i < 3; ++i) {
        const int state = api.Value<int>(api.Invoke(n.headerState, p.headers[i].get()));
        if (state != p.states[i] && state != 0) { clicked = i; clickedState = state; }
    }
    if (clicked >= 0) {
        for (int i = 0; i < 3; ++i) {
            if (i == clicked || p.states[i] == 0) continue;
            int inactive = 0;
            api.Invoke(n.headerSetState, p.headers[i].get(), {&inactive});
            p.states[i] = 0;
        }
        p.states[clicked] = clickedState;
        sort = {static_cast<Column>(clicked), clickedState == 2};
    }
    filters = next;
}
void UpdatePanel(Panel& p) {
    ReadControls(p);
    if (!build.ready) {
        if (p.statusText != LoadingText) { Text(p.status.get(), n.labelText, LoadingText); p.statusText = LoadingText; }
        return;
    }
    while (p.areaItems < model.areas.size()) {
        Ref label(api.string_new(model.areas[p.areaItems].c_str()));
        int style = 0, icon = 0, id = static_cast<int>(p.areaItems + 1);
        api.Invoke(n.dropdownAdd, p.areas.get(), {label.get(), nullptr, &style, &icon, &icon, &id});
        ++p.areaItems;
    }
    if (p.dirty || !(filters == p.shownFilters) || !(sort == p.shownSort) || model.version != p.shownVersion) {
        p.zones = model.Rows(filters, sort);
        p.shownFilters = filters;
        p.shownSort = sort;
        p.shownVersion = model.version;
        p.dirty = false;
    }
    for (size_t created = 0; p.rows.size() < p.zones.size() && created < RowBudget; ++created)
        p.rows.push_back(CreateRow(api.Invoke(n.scrollContent, p.list.get())));
    for (size_t i = 0; i < p.rows.size(); ++i) {
        auto& row = p.rows[i];
        if (i < p.zones.size()) RenderRow(row, p.zones[i]);
        else if (row.shown) { Display(row.element.get(), false); row.shown = false; row.zone = None; }
    }
    const auto status = StatusText(model, filters.show, true);
    if (status != p.statusText) { Text(p.status.get(), n.labelText, status); p.statusText = status; }
    const bool empty = p.zones.empty();
    if (empty) {
        const auto text = NoResultText(model, filters.show, true);
        if (text != p.noResultText) { Text(p.noResultLabel.get(), n.labelText, text); p.noResultText = text; }
    }
    if (p.noResultShown != (empty ? 1 : 0)) {
        Display(p.noResult.get(), empty);
        Display(p.content.get(), !empty);
        p.noResultShown = empty ? 1 : 0;
    }
}
// A row click centres the big map on the zone like the anomaly rows: a dungeon
// on its entrance, another zone on its centre, after switching the map's world
// when the zone is in another one.
void Center(Obj window, size_t index) {
    const auto& zone = model.zones.at(index);
    Ref root(SubAreaRoot());
    if (!root.get()) return;
    int id = zone.id;
    Ref sub(api.Invoke(n.subAreaById, root.get(), {&id}));
    if (!sub.get()) return;
    int x = 0, y = 0, world = zone.world;
    bool found = false;
    if (zone.dungeon > 0) {
        Ref maps(api.Field(sub.get(), "mapIds"));
        if (maps.get() && ListCount(maps.get(), 4096) > 0) {
            int first = 0;
            int map = api.Value<int>(api.Call(maps.get(), "get_Item", {&first}));
            Ref dungeons(api.Invoke(n.dungeons, nullptr));
            Ref dungeon(dungeons.get() ? api.Invoke(n.dungeonByMap, dungeons.get(), {&map}) : nullptr);
            if (dungeon.get()) {
                int64_t entrance = At<int>(dungeon.get(), "entranceMapId");
                Ref info(api.Invoke(n.mapInfoById, nullptr, {&entrance}));
                if (info.get()) {
                    x = At<int16_t>(info.get(), "posX");
                    y = At<int16_t>(info.get(), "posY");
                    Ref place(api.Invoke(n.mapInfoSubArea, info.get()));
                    if (place.get()) world = api.Value<int>(api.Invoke(n.subAreaWorld, place.get()));
                    found = true;
                }
            }
        }
    }
    if (!found) {
        Ref point(api.Invoke(n.subAreaCenter, sub.get()));
        if (!point.get()) return;
        x = At<int>(point.get(), "x");
        y = At<int>(point.get(), "y");
    }
    Ref view(api.Field(window, "ducm"));
    if (view.get() && world > 0) {
        Ref map(api.Field(view.get(), "<dugi>k__BackingField"));
        if (map.get() && At<int>(map.get(), "id") != world) api.Invoke(n.switchWorld, window, {&world});
    }
    api.Invoke(n.moveTo, window, {&x, &y});
    panel->activeZone = zone.id;
}
void EnsurePanel(Obj window) {
    Ref root(api.Field(window, "dzyj"));
    if (!root.get() || !api.Invoke(n.panel, root.get())) { DetachPanel(); return; }
    if (panel && (panel->root.get() != root.get() || Parent(panel->window.get()) != root.get())) DetachPanel();
    if (!panel) CreatePanel(root.get());
}

// -------------------------------------------------------------- the shapes
Obj Registry(Obj owner, int world) {
    Ref all(api.Field(owner, "<duic>k__BackingField"));
    if (!all.get()) return nullptr;
    Obj inner = nullptr;
    return api.Value<bool>(api.Call(all.get(), "TryGetValue", {&world, &inner})) ? inner : nullptr;
}
Obj Lookup(Obj registry, Obj name) {
    if (!registry || !name) return nullptr;
    Obj shape = nullptr;
    if (!api.Value<bool>(api.Call(registry, "TryGetValue", {name, &shape})) || !shape) return nullptr;
    return api.object_get_class(shape) == n.shape ? shape : nullptr;
}
void Paint(Obj shape, int band, bool mini) {
    auto fill = Fill(band, mini);
    auto line = Line(band);
    api.Invoke(n.fill, shape, {&fill});
    api.Invoke(n.line, shape, {&line});
}
// Disposes our shape and takes its name out of the registry (only ours: the
// registry entry must still be this shape).
void Dispose(View& v, Shape& s) {
    if (!s.shape.get() || !v.owner.get()) return;
    Ref registry(Registry(v.owner.get(), v.worldId));
    if (registry.get() && Lookup(registry.get(), s.name.get()) == s.shape.get()) {
        api.Invoke(n.dispose, s.shape.get());
        api.Call(registry.get(), "Remove", {s.name.get()});
    }
    s.shape.reset();
}
// The game hides every registered shape when it refreshes the world (fhw.besm,
// end of berh): ours are shown again.
void Show(Obj shape) {
    Ref root(api.Field(shape, "<dtwe>k__BackingField"));
    if (!root.get()) throw std::runtime_error("Ocre shape root unavailable");
    Display(root.get(), true);
}
// A pending shape now in the registry: into the mod's layer (the game's zoom
// rescale keeps it there), shown, no map info hover on the mini-map, current colour.
void Adopt(View& v, Shape& s, Obj shape, bool mini) {
    Show(shape);
    api.Invoke(n.reparent, shape, {v.layer.get()});
    if (mini) { bool hover = false; api.Invoke(n.hover, shape, {&hover}); }
    s.shape.reset(shape);
    s.ready = true;
    s.failed = false;
}
void StartRetire(View& v, uint64_t now) {
    if (!v.owner.get() && !v.layer.get() && v.shapes.empty()) return;
    if (!v.retiring) { v.retiring = true; v.retiredAt = now; }
}
// AddAreaShape registers a shape under the world shown when it resumes (one
// frame after the call): asked for just before a world change, it lands under
// the new world. Disposed there; false when it is not there either.
bool DisposeLanded(View& v, Shape& s) {
    if (!v.owner.get()) return false;
    Ref world(api.Field(v.owner.get(), "<dugi>k__BackingField"));
    if (!world.get()) return false;
    const int id = At<int>(world.get(), "id");
    if (id == v.worldId) return false;
    Ref registry(Registry(v.owner.get(), id));
    Obj landed = Lookup(registry.get(), s.name.get());
    if (!landed) return false;
    api.Invoke(n.dispose, landed);
    api.Call(registry.get(), "Remove", {s.name.get()});
    s.shape.reset();
    return true;
}
// Removes every shape of a view, waiting a little for the ones asked for but
// not registered yet (never left behind in a native layer), then the layer.
void Retire(View& v, uint64_t now) {
    Ref registry(v.owner.get() ? Registry(v.owner.get(), v.worldId) : nullptr);
    for (auto it = v.shapes.begin(); it != v.shapes.end();) {
        auto& s = it->second;
        if (!s.ready && !s.shape.get()) {
            Obj found = Lookup(registry.get(), s.name.get());
            if (found) s.shape.reset(found);
            else if (DisposeLanded(v, s)) { it = v.shapes.erase(it); continue; }
            else if (now < s.since + ShapeWaitMs && now < v.retiredAt + ShapeWaitMs) { ++it; continue; }
        }
        Dispose(v, s);
        it = v.shapes.erase(it);
    }
    if (!v.shapes.empty()) return;
    Remove(v.layer);
    v = View{};
}
struct Frame { Obj root = nullptr; int worldId = 0; };
bool ReadFrame(Obj owner, Frame& f) {
    if (!owner || !api.class_is_assignable_from(n.view, api.object_get_class(owner))) return false;
    if (api.Field(owner, "dugv") != module->FindService("fkc") || api.Field(owner, "dugy") != module->FindService("fng")) return false;
    f.root = api.Field(owner, "dugq");
    Ref world(api.Field(owner, "<dugi>k__BackingField"));
    if (!f.root || !world.get() || !api.Invoke(n.panel, f.root)) return false;
    f.worldId = At<int>(world.get(), "id");
    return f.worldId > 0;
}
// The mod's layer: the native layers' geometry, just below the first native
// layer (fia 0): above the map picture, under the icons.
void CreateLayer(View& v, Obj owner, const Frame& f) {
    int first = 0;
    Ref anchor(api.Invoke(n.layer, owner, {&first}));
    const int index = anchor.get() ? api.Value<int>(api.Invoke(n.indexOf, f.root, {anchor.get()})) : -1;
    if (index < 0) throw std::runtime_error("Ocre layer anchor unavailable");
    Ref layer(Element());
    Text(layer.get(), n.setName, LayerName);
    Ignore(layer.get());
    Style(layer.get(), "position", 1);
    Pixels(layer.get(), "left", 0);
    Pixels(layer.get(), "top", 0);
    Percent(layer.get(), "width", 100);
    Percent(layer.get(), "height", 100);
    int at = index;
    api.Invoke(n.insert, f.root, {&at, layer.get()});
    v.owner.reset(owner);
    v.root.reset(f.root);
    v.layer = std::move(layer);
    v.worldId = f.worldId;
}
// The view's shapes become the wanted ones: (zone id, colour band) of the zones
// outlined on the view's current world.
void SyncView(View& v, Obj owner, const std::map<int, int>& wanted, bool mini, uint64_t now) {
    if (v.retiring) { Retire(v, now); if (v.retiring || !v.shapes.empty()) return; }
    Frame f;
    if (!ReadFrame(owner, f)) { StartRetire(v, now); Retire(v, now); return; }
    if (v.owner.get() != owner || v.root.get() != f.root || v.worldId != f.worldId || !v.layer.get() || Parent(v.layer.get()) != f.root) {
        if (v.owner.get() || v.layer.get() || !v.shapes.empty()) { StartRetire(v, now); Retire(v, now); if (v.retiring) return; }
        CreateLayer(v, owner, f);
    }
    Ref registry(Registry(owner, v.worldId));
    size_t removed = 0, created = 0;
    for (auto it = v.shapes.begin(); it != v.shapes.end();) {
        auto& s = it->second;
        const auto want = wanted.find(it->first);
        if (!s.ready) {
            if (Obj found = Lookup(registry.get(), s.name.get())) {
                Adopt(v, s, found, mini);
                if (want != wanted.end()) { s.band = want->second; Paint(found, s.band, mini); }
            } else if (now >= s.since + ShapeWaitMs && !s.failed) {
                s.failed = true;
                Log("ARCHI_ZONES_SHAPE_MISSING zone=" + std::to_string(it->first));
            }
        }
        if (want == wanted.end()) {
            if (!s.ready && !s.failed) { ++it; continue; } // wait for it, never leave it behind
            if (removed >= RemoveBudget) { ++it; continue; }
            Dispose(v, s);
            it = v.shapes.erase(it);
            ++removed;
            continue;
        }
        if (s.ready && s.band != want->second) { s.band = want->second; Paint(s.shape.get(), s.band, mini); }
        ++it;
    }
    if (now >= v.shownAt + 2000) {
        v.shownAt = now;
        for (auto& [zone, s] : v.shapes) if (s.ready && s.shape.get()) Show(s.shape.get());
    }
    for (const auto& [zone, band] : wanted) {
        if (v.shapes.contains(zone)) continue;
        if (created >= CreateBudget) break;
        Shape s;
        s.name.reset(api.string_new((std::string(ShapePrefix) + std::to_string(zone)).c_str()));
        s.band = band;
        s.since = now;
        if (Obj existing = Lookup(registry.get(), s.name.get())) {
            Adopt(v, s, existing, mini);
            Paint(existing, band, mini);
        } else {
            int id = zone, layer = -1, bonus = -1;
            auto fill = Fill(band, mini);
            auto line = Line(band);
            float thickness = 2;
            bool flag = false;
            api.Invoke(n.addShape, owner, {&id, &layer, &fill, &line, &thickness, s.name.get(), &flag, &bonus});
        }
        v.shapes.emplace(zone, std::move(s));
        ++created;
    }
}
// Zones of the view's world with a colour band: the panel's rows on the big
// map, every zone of the type choice on the mini-map.
std::map<int, int> Wanted(const std::vector<size_t>& indices, Obj owner) {
    std::map<int, int> wanted;
    Frame f;
    if (!ReadFrame(owner, f)) return wanted;
    for (const auto index : indices) {
        const auto& zone = model.zones.at(index);
        const int band = Band(model.Count(index, filters.show));
        if (band >= 0 && zone.shape && zone.world == f.worldId) wanted.emplace(zone.id, band);
    }
    return wanted;
}
Obj MiniOwner() {
    Ref service(module->FindService("fkc"));
    if (!service.get() || api.object_get_class(service.get()) != n.service) return nullptr;
    Ref keyed(api.Field(service.get(), "dueo"));
    if (!keyed.get()) return nullptr;
    int key = static_cast<int>(MiniView);
    Obj owner = nullptr;
    return api.Value<bool>(api.Call(keyed.get(), "TryGetValue", {&key, &owner})) ? owner : nullptr;
}

// ----------------------------------------------------------------- clicks
// Native button clicks (shared hook): the square and the panel's close button.
bool ButtonClick(Obj button) {
    if (!button) return false;
    if (square.button.get() == button) { pendingClicks.square = true; return true; }
    if (panel && panel->close.get() == button) { pendingClicks.close = true; return true; }
    return false;
}
// Native list rows (shared ListElement action hook): a zone row.
bool RowClick(Obj row) {
    if (!row || !panel) return false;
    for (size_t i = 0; i < panel->rows.size(); ++i)
        if (panel->rows[i].element.get() == row) { pendingClicks.row = static_cast<int>(i); return true; }
    return false;
}
void ReleaseNeeds() {
    if (!needsAsked) return;
    needsAsked = false;
    WantSoulStatus(SoulClient::Map, false, false);
}
bool Pending() {
    if (square.button.get() || panel) return true;
    for (const auto& v : views) if (v.owner.get() || v.layer.get() || !v.shapes.empty()) return true;
    return false;
}
void RestoreAll(uint64_t now) {
    active = false;
    pendingClicks = {};
    DetachUi();
    for (auto& v : views) {
        try { StartRetire(v, now); Retire(v, now); }
        catch (const std::exception& e) { Report("shape cleanup", e); }
    }
    ReleaseNeeds();
}

void Tick(uint64_t now) {
    if (!Session()) { RestoreAll(now); return; }
    const auto clicks = pendingClicks;
    pendingClicks = {};
    const bool mapOption = module->settings.archiZonesMap, miniOption = module->settings.archiZonesMiniMap;
    if (!mapOption && !miniOption && !Pending() && !needsAsked) { active = false; return; } // off: inert
    // The big map window resource-map-ui follows, when it is the current one.
    Obj window = nullptr;
    if (mapOption) {
        Obj candidate = resourceMap::CartographyWindow();
        if (candidate && CurrentWindow(candidate)) window = candidate;
    } else active = false;
    if (window && now >= retryUi) {
        try {
            EnsureSquare(window);
            if (clicks.square) Toggle(window);
            if (clicks.close) active = false;
            // The game's own anomaly or conquest mode chosen: the Ocre mode leaves.
            if (active && Mode(window) != 0) active = false;
        } catch (const std::exception& e) { Report("square", e); DetachUi(); retryUi = now + 5000; window = nullptr; }
    } else if (!window) {
        // Closed: the Ocre mode ends with it, as the game's own modes do (fhi.OnDisable → bejb(0)).
        DetachUi();
        active = false;
    }
    const bool big = window && active;
    if (!big) { try { DetachPanel(); } catch (const std::exception& e) { Report("panel cleanup", e); } }
    Obj mini = miniOption ? MiniOwner() : nullptr;
    for (const auto key : {BigView, MiniView}) {
        auto& v = views[key];
        if ((key == BigView ? !big : !mini) || v.retiring) {
            try { StartRetire(v, now); Retire(v, now); } catch (const std::exception& e) { Report("shape cleanup", e); v = View{}; }
        }
    }
    if (!big && !mini) { ReleaseNeeds(); return; }
    // Shown: the readers (bag, quest) and the Metamob list stay fresh.
    WantSoulStatus(SoulClient::Map, true, true);
    needsAsked = true;
    metamob::WantMetamobCatalog();
    try { BuildData(); } catch (const std::exception& e) { Report("data", e); }
    if (build.ready) Count(now);
    std::vector<size_t> rows;
    if (big && now >= retryUi) {
        try {
            EnsurePanel(window);
            if (panel && clicks.row >= 0 && static_cast<size_t>(clicks.row) < panel->rows.size()) {
                const auto& row = panel->rows[clicks.row];
                if (row.shown && row.zone != None) Center(window, row.zone);
            }
            if (panel) { UpdatePanel(*panel); rows = panel->zones; }
        } catch (const std::exception& e) { Report("panel", e); DetachPanel(); retryUi = now + 5000; }
    }
    if (!build.ready || now < retryMap) return;
    if (big) {
        if (!panel) rows = model.Rows(filters, sort);
        try {
            Ref owner(api.Field(window, "ducm"));
            SyncView(views[BigView], owner.get(), Wanted(rows, owner.get()), false, now);
        } catch (const std::exception& e) { Report("map", e); StartRetire(views[BigView], now); retryMap = now + 5000; }
    }
    if (mini) {
        try {
            std::vector<size_t> all;
            for (size_t z = 0; z < model.zones.size(); ++z) all.push_back(z);
            SyncView(views[MiniView], mini, Wanted(all, mini), true, now);
        } catch (const std::exception& e) { Report("mini-map", e); StartRetire(views[MiniView], now); retryMap = now + 5000; }
    }
}

void Install() {
    n.window = api.Class("Core.dll", "", "fhi");
    n.binding = api.Class("Core.dll", "Core.UILogic.Bindings", "CartographyMapUIBinding");
    n.view = api.Class("Core.dll", "", "fhw");
    n.service = api.Class("Core.dll", "", "fhk");
    n.shape = api.Class("Core.dll", "", "fgf");
    n.shapeBase = api.Class("Core.dll", "", "fgg");
    n.marker = api.Class("Core.dll", "", "fgh");
    n.worldMap = api.Class(DataImage, WorldSpace, "WorldMapData");
    RequireField(n.window, "dtzx", 0x408);
    RequireField(n.window, "dtzy", 0x410);
    RequireField(n.window, "dtzz", 0x418);
    RequireField(n.window, "dubo", 0x52C);
    RequireField(n.window, "ducm", 0x5D0);
    RequireField(n.view, "dugq", 0x68);
    RequireField(n.view, "dugv", 0x90);
    RequireField(n.view, "dugy", 0xA8);
    RequireField(n.view, "<dugi>k__BackingField", 0x38);
    RequireField(n.view, "<duic>k__BackingField", 0x160);
    RequireField(n.service, "dueo", 0x98);
    RequireField(n.worldMap, "id", 0x10);
    RequireField(n.marker, "<dtwe>k__BackingField", 0x10);
    // The map window: leaving the game's anomaly/conquest mode (its windows'
    // close buttons), MoveTo and the world switch of its anomaly rows, the mode column.
    n.leaveMode = api.FindVerified(n.window, "bejv", 1, 0x1BB4090);
    n.moveTo = api.FindVerified(n.window, "beki", 2, 0x1BB5E70);
    n.switchWorld = api.FindVerified(n.window, "belb", 1, 0x1BB8D40);
    n.worldButtons = api.FindVerified(n.binding, "get_ctr_worldmapButtons", 0, 0x2EA2830);
    n.conquestButton = api.FindVerified(n.binding, "get_btn_conquest", 0, 0x2EA0A00);
    // The game's zone shapes (AddAreaShape, as the anomalies and conquest use it).
    n.addShape = api.FindVerified(n.view, "besn", 8, 0x1BF0FE0);
    n.layer = api.FindVerified(n.view, "bexb", 1, 0x1C04C70);
    n.fill = api.FindVerified(n.shape, "befi", 1, 0x1BAC490);
    n.line = api.FindVerified(n.shape, "befk", 1, 0x1BAC570);
    n.hover = api.FindVerified(n.shape, "befo", 1, 0x1BAC7A0);
    n.reparent = api.FindVerified(n.shapeBase, "bege", 1, 0x1BAE1D0);
    n.dispose = api.FindVerified(n.shapeBase, "begd", 0, 0x1BAE0A0);
    // The game's data: monsters, zones, territories, dungeons, maps.
    n.monster = api.Class(DataImage, "Core.DataCenter.Metadata.Monster", "MonsterData");
    RequireField(n.monster, "subareas", 0x50);
    n.monsterById = api.FindVerified(n.monster, "GetById", 1, 0x7E76B0);
    n.monsterName = api.FindVerified(n.monster, "get_name", 0, 0x7E7A30);
    n.dataCenter = api.Class(DataImage, "Core.DataCenter", "DataCenterModule");
    n.subAreas = api.FindVerified(n.dataCenter, "get_subAreasDataRoot", 0, 0x7C1A40);
    n.dungeons = api.FindVerified(n.dataCenter, "get_dungeonsDataRoot", 0, 0x7BC7C0);
    n.subRoot = api.Class(DataImage, WorldSpace, "SubAreasDataRoot");
    n.subAreaById = api.FindVerified(n.subRoot, "GetSubAreaById", 1, 0x7D1060);
    n.subArea = api.Class(DataImage, WorldSpace, "SubAreaData");
    RequireField(n.subArea, "mapIds", 0x20);
    RequireField(n.subArea, "shape", 0x30);
    RequireField(n.subArea, "level", 0x40);
    RequireField(n.subArea, "displayOnWorldMap", 0x46);
    RequireField(n.subArea, "dungeonId", 0x98);
    n.subAreaName = api.FindVerified(n.subArea, "get_name", 0, 0x7D0F60);
    n.subAreaWorld = api.FindVerified(n.subArea, "get_worldmapId", 0, 0x7D1040);
    n.subAreaCenter = api.FindVerified(n.subArea, "Center", 0, 0x7D0820);
    n.subAreaArea = api.FindVerified(n.subArea, "GetArea", 0, 0x7D0A50);
    n.area = api.Class(DataImage, WorldSpace, "AreaData");
    n.areaName = api.FindVerified(n.area, "get_name", 0, 0x7B8660);
    n.dungeonRoot = api.Class(DataImage, WorldSpace, "DungeonsDataRoot");
    n.dungeonByMap = api.FindVerified(n.dungeonRoot, "GetDungeonByMapId", 1, 0x7C26B0);
    n.dungeon = api.Class(DataImage, WorldSpace, "DungeonData");
    RequireField(n.dungeon, "entranceMapId", 0x28);
    n.mapInfo = api.Class(DataImage, WorldSpace, "MapInformationData");
    RequireField(n.mapInfo, "posX", 0x18);
    RequireField(n.mapInfo, "posY", 0x1A);
    n.mapInfoById = api.FindVerified(n.mapInfo, "GetMapInformationById", 1, 0x7C4200);
    n.mapInfoSubArea = api.FindVerified(n.mapInfo, "get_subArea", 0, 0x7C44B0);
    n.point = api.Class(DataImage, "Core.DataCenter.Types", "Point");
    RequireField(n.point, "x", 0x10);
    RequireField(n.point, "y", 0x14);
    // The game's UI components (those of the anomaly panel).
    n.element = api.Class(UiImage, UiSpace, "VisualElement");
    n.elementCtor = api.FindVerified(n.element, ".ctor", 0, 0x55F50B0);
    n.add = api.FindVerified(n.element, "Add", 1, 0x55DFAD0);
    n.insert = api.FindVerified(n.element, "Insert", 2, 0x55E5710);
    n.indexOf = api.FindVerified(n.element, "IndexOf", 1, 0x55E55B0);
    n.removeFromHierarchy = api.FindVerified(n.element, "RemoveFromHierarchy", 0, 0x55E7BD0);
    n.parent = api.FindVerified(n.element, "get_parent", 0, 0x55F7180);
    n.panel = api.FindVerified(n.element, "get_panel", 0, 0x55F7170);
    n.pickingMode = api.FindVerified(n.element, "set_pickingMode", 1, 0x55F97B0);
    n.setName = api.FindVerified(n.element, "set_name", 1, 0x55F9670);
    n.tooltip = api.FindVerified(n.element, "set_tooltip", 1, 0x55F9AB0);
    n.addClass = api.FindVerified(n.element, "AddToClassList", 1, 0x55DF820);
    api.FindVerified(n.element, "get_style", 0, 0x55F74A0);
    n.label = api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel");
    n.labelCtor = api.FindVerified(n.label, ".ctor", 0, 0x1593940);
    n.labelText = api.FindVerified(n.label, "set_text", 1, 0x1594570);
    n.scroll = api.Class(UiImage, UiSpace, "ScrollView");
    n.scrollCtor = api.FindVerified(n.scroll, ".ctor", 1, 0x5741C70);
    n.scrollContent = api.FindVerified(n.scroll, "get_contentContainer", 0, 0x5742BE0);
    n.scrollHorizontal = api.FindVerified(n.scroll, "set_horizontalScrollerVisibility", 1, 0x5743280);
    n.button = api.Class("Core.dll", Figma, "DofusButtonCustom");
    n.buttonCtor = api.FindVerified(n.button, ".ctor", 0, 0x170D6F0);
    n.buttonIcon = api.FindVerified(n.button, "set_icon", 1, 0x170DFE0);
    n.buttonStyle = api.FindVerified(n.button, "set_mainStyle", 1, 0x170E210);
    n.buttonRichText = api.FindVerified(n.button, "set_enableRichText", 1, 0x170DEE0);
    n.figmaWindow = api.Class("Core.dll", Figma, "WindowFigma");
    n.windowCtor = api.FindVerified(n.figmaWindow, ".ctor", 0, 0x17987B0);
    n.windowTitle = api.FindVerified(n.figmaWindow, "set_title", 1, 0x1799210);
    n.windowHeaderStyle = api.FindVerified(n.figmaWindow, "set_headerStyle", 1, 0x1798F30);
    n.windowClose = api.FindVerified(n.figmaWindow, "get_closeButton", 0, 0x1798CF0);
    n.input = api.Class("Core.dll", Figma, "TextInput");
    n.inputCtor = api.FindVerified(n.input, ".ctor", 0, 0x178DC90);
    n.inputIcon = api.FindVerified(n.input, "set_icon", 1, 0x178E110);
    n.inputText = api.FindVerified(n.input, "get_text", 0, 0x178DDA0);
    n.inputPlaceholder = api.FindVerified(n.input, "set_placeholderText", 1, 0x178EAA0);
    n.inputMax = api.FindVerified(n.input, "set_maxLength", 1, 0x178E9B0);
    n.inputNumbers = api.FindVerified(n.input, "set_isNumbersOnly", 1, 0x178E8E0);
    n.header = api.Class("Core.dll", Figma, "TableColumnHeader");
    n.headerCtor = api.FindVerified(n.header, ".ctor", 0, 0x17853B0);
    n.headerText = api.FindVerified(n.header, "set_headerText", 1, 0x1785460);
    n.headerState = api.FindVerified(n.header, "get_state", 0, 0x15BF880);
    n.headerSetState = api.FindVerified(n.header, "set_state", 1, 0x1785650);
    n.row = api.Class("Core.dll", Figma, "ListElementBasic");
    n.rowCtor = api.FindVerified(n.row, ".ctor", 0, 0x1734630);
    n.rowStyle = api.FindVerified(n.row, "set_listElementStyle", 1, 0x1734700);
    n.rowBase = api.Class("Core.dll", Figma, "ListElementBase");
    RequireField(n.rowBase, "id", 0x310);
    n.rowText = api.FindVerified(n.rowBase, "set_text", 1, 0x1733CA0);
    n.rowIcon = api.FindVerified(n.rowBase, "set_icon", 1, 0x1733BD0);
    n.rowActivated = api.FindVerified(n.rowBase, "set_isActivated", 1, 0x1733BF0);
    n.dropdown = api.Class("Core.dll", Figma, "Dropdown");
    RequireField(n.dropdown, "closeOnSelection", 0x3D8);
    n.dropdownCtor = api.FindVerified(n.dropdown, ".ctor", 0, 0x17282C0);
    n.dropdownAdd = api.FindVerified(n.dropdown, "AddListElementBasic", 6, 0x1724840);
    n.dropdownSelect = api.FindVerified(n.dropdown, "SelectItemIdWithoutNotify", 1, 0x17265B0);
    n.dropdownSelected = api.FindVerified(n.dropdown, "get_selectedElement", 0, 0x155DB30);
    n.dropdownChoice = api.FindVerified(n.dropdown, "set_choiceText", 1, 0x1728550);
}
}

void InstallArchiZones() {
    using namespace archiZones;
    if (installed) return;
    // Optional: a contract mismatch disables only the Ocre zones, never the mod.
    try { Install(); }
    catch (const std::exception& e) { Log(std::string("ARCHI_ZONES_DISABLED ") + e.what()); return; }
    nativeClick::AddHandler(ButtonClick);
    soul::SetAuxiliaryPanelAction(RowClick);
    installed = true;
}
void TickArchiZones() {
    using namespace archiZones;
    if (!installed) return;
    try { Tick(GetTickCount64()); }
    catch (const std::exception& e) { Report("tick", e); }
}
void RestoreArchiZones() {
    using namespace archiZones;
    if (!installed) return;
    RestoreAll(GetTickCount64());
}
bool ArchiZonesCleanupPending() { return archiZones::installed && archiZones::Pending(); }
