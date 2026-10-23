// CalendarKraloveTests (offline): calendar-kralove.cpp on a fake IL2CPP
// runtime. No client binary is loaded, no process opened, no request sent:
// metamob-sync is replaced by a scripted view, metamob-data is the real one
// (local time through Windows' own time zone rules). Every native call the
// module makes must have been verified at install (FindVerified), except the
// generic List<T> and style value accessors every module uses.
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "metamob/metamob-data.hpp"
#include "metamob/metamob-sync.hpp"
#include "ui/native-click.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#pragma comment(lib, "normaliz.lib") // metamob-data.cpp (NameKey's NormalizeString), linked for LocalTime

namespace fixture {
using namespace managed;
struct Class { std::string name; };
struct MethodData { std::string type, name; int argc = 0; };
struct Rect { float x = 0, y = 0, width = 0, height = 0; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, std::array<char, 16>> styles; // inline styles written, by property
    std::vector<Obj> children;
    std::array<char, 16> bytes{};                        // a boxed value
    std::string text, tooltip, property;
    std::set<std::string> classes;
    Rect layout;
    bool rich = true;
    int picking = 0, size = -1, mainStyle = -1;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle, Obj> handles;
GCHandle handleId = 100;
// Field offsets as the pinned metadata has them (the generic binding at run time).
std::map<std::string, size_t> offsets{{"m_dayCards", 0x210}, {"<dayDate>k__BackingField", 0x2F8}, {"dzyi", 0x10}};
std::set<std::tuple<std::string, std::string, int>> verified;
std::vector<std::tuple<std::string, std::string, int, uintptr_t>> contracts;
// The game's own class chain, for the verified-call rule.
const std::map<std::string, std::string> bases{{"DofusLabel", "TextElement"}, {"TextElement", "VisualElement"},
    {"DofusButtonCustom", "VisualElement"}, {"CalendarDayCard", "VisualElement"}, {"ListView", "VisualElement"}};
std::vector<std::string> logs, urls;
using HookFn = void(*)(Obj, Method);
HookFn enableHook = nullptr, disableHook = nullptr;
int nativeEnables = 0, nativeDisables = 0;
bool (*clickHandler)(Obj) = nullptr;
int clickHandlers = 0;
Obj failDetach = nullptr;
// The scripted metamob-sync side.
metamob::KraloveView view;
int wants = 0, views = 0;
int64_t now = 0;

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake object"); return *static_cast<Object*>(obj); }
Class* Type(const std::string& name) { auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get(); }
Obj Make(const std::string& name) { auto o = std::make_unique<Object>(); o->klass = Type(name); auto p = o.get(); objects.push_back(std::move(o)); return p; }
template<class T> Obj Box(T value) {
    static_assert(sizeof(T) <= 16);
    auto o = Make("Box"); std::memcpy(Get(o).bytes.data(), &value, sizeof(value)); return o;
}
Obj String(const std::string& text) { auto o = Make("String"); Get(o).text = text; return o; }
void Add(Obj parent, Obj child) {
    Check(!Get(child).fields["parent"], "Element already attached");
    Get(parent).children.push_back(child); Get(child).fields["parent"] = parent;
}
void Insert(Obj parent, int index, Obj child) {
    Check(!Get(child).fields["parent"], "Element already attached");
    auto& children = Get(parent).children;
    Check(index >= 0 && index <= static_cast<int>(children.size()), "Insert index out of range");
    children.insert(children.begin() + index, child); Get(child).fields["parent"] = parent;
}
void Detach(Obj child) {
    if (auto parent = Get(child).fields["parent"]) { auto& v = Get(parent).children; v.erase(std::remove(v.begin(), v.end(), child), v.end()); }
    Get(child).fields["parent"] = nullptr;
}
Obj Panel(Obj o) { if (Get(o).fields["panel"]) return Get(o).fields["panel"]; auto p = Get(o).fields["parent"]; return p ? Panel(p) : nullptr; }
size_t Size(const std::string& property) {
    static const std::set<std::string> lengths{"left", "top", "width", "height", "minHeight", "fontSize"};
    return lengths.contains(property) ? 8 : 4;
}
std::string StyleKind(const std::string& property) {
    if (Size(property) == 8) return "StyleLength";
    return property == "flexShrink" || property == "opacity" ? "StyleFloat" : "StyleEnum";
}
bool Styled(Obj o, const std::string& property) { return Get(o).styles.contains(property); }
float Pixels(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; int unit = -1;
    std::memcpy(&value, Get(o).styles.at(property).data(), 4); std::memcpy(&unit, Get(o).styles.at(property).data() + 4, 4);
    Check(unit == 0, "Pixels expected: " + property);
    return value;
}
float PercentStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; int unit = -1;
    std::memcpy(&value, Get(o).styles.at(property).data(), 4); std::memcpy(&unit, Get(o).styles.at(property).data() + 4, 4);
    Check(unit == 1, "Percent expected: " + property);
    return value;
}
float FloatStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; std::memcpy(&value, Get(o).styles.at(property).data(), 4); return value;
}
int EnumStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    int value = 0; std::memcpy(&value, Get(o).styles.at(property).data(), 4); return value;
}
void RequireVerified(const std::string& type, const std::string& name, int argc) {
    // List<T>.get_Count/get_Item and the Style* value setters: generic, called
    // through runtime_invoke by every module (never a raw native address).
    static const std::set<std::string> generic{"get_Count", "get_Item", "set_value"};
    if (generic.contains(name)) return;
    for (std::string t = type; !t.empty(); t = bases.contains(t) ? bases.at(t) : std::string())
        if (verified.contains({t, name, argc})) return;
    throw std::runtime_error("Native call not verified at install: " + type + "." + name);
}
void NativeEnable(Obj, Method) { ++nativeEnables; }
void NativeDisable(Obj, Method) { ++nativeDisables; }
void Setup() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).klass; };
    api.class_is_valuetype = [](Klass k) {
        const auto& n = static_cast<Class*>(k)->name;
        return n == "Box" || n == "Length" || n.starts_with("Style");
    };
    api.object_unbox = [](Obj o) -> void* { return Get(o).bytes.data(); };
    api.string_new = [](const char* s) -> Obj { return String(s); };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++handleId] = o; return handleId; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Invalid GC release"); };
    api.class_get_field_from_name = [](Klass, const char* name) -> void* { const auto it = offsets.find(name); return it == offsets.end() ? nullptr : &it->second; };
    api.field_get_offset = [](void* f) { return *static_cast<size_t*>(f); };
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fixture::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto m = std::make_unique<fixture::MethodData>(); m->type = static_cast<fixture::Class*>(klass)->name; m->name = name; m->argc = argc;
    auto p = m.get(); fixture::methods.push_back(std::move(m)); return p;
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    const auto type = static_cast<fixture::Class*>(klass)->name;
    fixture::Check(rva != 0, "Verified method without RVA");
    fixture::verified.insert({type, name, argc});
    fixture::contracts.push_back({type, name, argc, rva});
    return Find(klass, name, argc);
}
void* Api::FieldInfo(Klass, const char* name) const {
    const auto it = fixture::offsets.find(name);
    if (it == fixture::offsets.end()) throw std::runtime_error(std::string("Field unavailable: ") + name);
    return &it->second;
}
Obj Api::Field(Obj obj, const char* name) const { return fixture::Get(obj).fields.at(name); }
std::string Api::String(Obj obj) const { return obj ? fixture::Get(obj).text : std::string(); }
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fixture;
    const auto name = static_cast<fixture::Class*>(klass)->name;
    RequireVerified(name, ".ctor", static_cast<int>(args.size()));
    auto o = Make(name);
    if (name == "Length") { size_t i = 0; for (auto arg : args) { std::memcpy(Get(o).bytes.data() + i * 4, arg, 4); ++i; } }
    return o;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    const auto& info = *static_cast<const fixture::MethodData*>(method);
    fixture::Check(info.argc == static_cast<int>(args.size()), "Managed invocation argument count");
    if (info.type == "Application" && info.name == "OpenURL") {
        fixture::Check(!self && args.size() == 1, "Static Application.OpenURL(string)");
        fixture::RequireVerified(info.type, info.name, info.argc);
        fixture::urls.push_back(fixture::Get(static_cast<Obj>(*args.begin())).text);
        return nullptr;
    }
    return Call(self, info.name.c_str(), args);
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    auto& o = Get(self);
    const std::string method = name;
    const std::vector<void*> args(arguments);
    RequireVerified(o.klass->name, method, static_cast<int>(args.size()));
    if (method == "get_ctr_days" || method == "get_gd_events") return o.fields.at(method.substr(4));
    if (method == "get_parent") return o.fields["parent"];
    if (method == "get_panel") return Panel(self);
    if (method == "IndexOf") {
        const auto& c = o.children;
        const auto it = std::find(c.begin(), c.end(), static_cast<Obj>(args[0]));
        return Box(it == c.end() ? -1 : static_cast<int>(it - c.begin()));
    }
    if (method == "Insert") { Insert(self, *static_cast<int*>(args[0]), static_cast<Obj>(args[1])); return nullptr; }
    if (method == "Add") { Add(self, static_cast<Obj>(args[0])); return nullptr; }
    if (method == "RemoveFromHierarchy") {
        if (self == failDetach) { failDetach = nullptr; throw std::runtime_error("Temporary detach failure"); }
        Detach(self); return nullptr;
    }
    if (method == "get_layout") return Box(o.layout);
    if (method == "get_style") { auto style = Make("InlineStyleAccess"); Get(style).fields["owner"] = self; return style; }
    if (o.klass->name == "InlineStyleAccess") {
        const std::string prefix = "UnityEngine.UIElements.IStyle.";
        Check(method.starts_with(prefix), "Inline styles through their explicit IStyle methods");
        const auto operation = method.substr(prefix.size()), property = operation.substr(4);
        auto& target = Get(o.fields.at("owner"));
        if (operation.starts_with("get_")) {
            auto box = Make(StyleKind(property)); Get(box).property = property;
            if (target.styles.contains(property)) Get(box).bytes = target.styles.at(property);
            return box;
        }
        Check(operation.starts_with("set_"), "Unexpected style method");
        std::memcpy(target.styles[property].data(), args.at(0), Size(property));
        return nullptr;
    }
    if (method == "set_value") { std::memcpy(o.bytes.data(), args.at(0), Size(o.property)); return nullptr; }
    if (method == "set_tooltip") { o.tooltip = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (method == "AddToClassList") { o.classes.insert(Get(static_cast<Obj>(args[0])).text); return nullptr; }
    if (method == "set_text") { o.text = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (method == "set_enableRichText") { o.rich = *static_cast<bool*>(args[0]); return nullptr; }
    if (method == "set_pickingMode") { o.picking = *static_cast<int*>(args[0]); return nullptr; }
    if (method == "set_size") { o.size = *static_cast<int*>(args[0]); return nullptr; }
    if (method == "set_mainStyle") { o.mainStyle = *static_cast<int*>(args[0]); return nullptr; }
    if (method == "get_Count") return Box(static_cast<int>(o.children.size()));
    if (method == "get_Item") return o.children.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected fake method " + method);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string& message) { fixture::logs.push_back(message); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& info = *static_cast<const fixture::MethodData*>(method);
    fixture::Check(info.type == "CalendarUI" && info.argc == 0, "Calendar hook ABI");
    if (rva == 0x2E065E0) {
        fixture::Check(info.name == "OnEnable", "OnEnable hook");
        fixture::enableHook = reinterpret_cast<fixture::HookFn>(replacement); *original = reinterpret_cast<void*>(fixture::NativeEnable);
    } else if (rva == 0x2E06550) {
        fixture::Check(info.name == "OnDisable", "OnDisable hook");
        fixture::disableHook = reinterpret_cast<fixture::HookFn>(replacement); *original = reinterpret_cast<void*>(fixture::NativeDisable);
    } else throw std::runtime_error("Unexpected hook");
}
namespace nativeClick { void AddHandler(bool (*handler)(managed::Obj)) { fixture::clickHandler = handler; ++fixture::clickHandlers; } }
namespace metamob {
void WantKralove() { ++fixture::wants; }
KraloveView MetamobKralove() { ++fixture::views; return fixture::view; }
}
#include "metamob/calendar-kralove.cpp"

namespace {
using namespace fixture;
// The visible texts (r111 design), written here independently of the module.
const std::string Loading = "Metamob : lecture des ouvertures…", Unreachable = "Metamob injoignable, nouvel essai automatique.",
    Busy = "Metamob : trop de demandes, nouvel essai automatique.", Refused = "Metamob refuse ta clé pour les ouvertures Kralamoure ; nouvel essai dans 10 minutes.",
    KeyUnreadable = "Clé Metamob illisible.", Unavailable = "Ouvertures Kralamoure indisponibles sur l’API Metamob.",
    Unreadable = "Réponse Metamob illisible, nouvel essai automatique.",
    NoQuest = "Serveur inconnu : aucune quête Ocre Dofus Unity sur ton compte Metamob.",
    SeveralQuests = "Serveur inconnu : plusieurs quêtes Ocre Dofus Unity, aucune n’indique ce personnage dans « Personnage à contacter ».",
    NoServer = "Serveur inconnu : ta quête Ocre sur Metamob n’indique pas de serveur.",
    PastWeek = "Metamob : les ouvertures des semaines passées ne sont pas lues.",
    Empty = "Aucune ouverture Kralamoure cette semaine sur Draconiros.",
    Banner = "Ouvertures Kralamoure planifiées sur Metamob pour Draconiros, à l’heure de ce PC. Survole une ouverture pour ses "
        "détails ; clique dessus pour l’ouvrir sur metamob.fr.",
    BannerUnknown = "Ouvertures Kralamoure planifiées sur Metamob pour le serveur de ta quête Ocre, à l’heure de ce PC. Survole une "
        "ouverture pour ses détails ; clique dessus pour l’ouvrir sur metamob.fr.";
// Private data planted in the openings: never in a log.
const std::string Creator = "Zorglubette", Marker = "Corps-Prive-Kralove";
constexpr int64_t SecretId = 987654321;

int64_t Utc(const char* text) { const auto at = metamob::UtcSeconds(text); Check(at.has_value(), std::string("Fixture date ") + text); return *at; }
int64_t Day(int y, int m, int d) { return metamob::DayNumber(y, m, d); }
std::optional<metamob::CivilTime> Paris(int64_t at) { return metamob::LocalTime(at, L"Romance Standard Time"); }
std::optional<metamob::CivilTime> Universal(int64_t at) { return metamob::LocalTime(at, L"UTC"); }
int64_t FakeNow() { return fixture::now; }
metamob::Kralove Opening(int64_t id, const char* at, int characters, std::string creator = {}, std::string description = {}) {
    metamob::Kralove opening;
    opening.id = id; opening.at = Utc(at); opening.serverId = 11; opening.server = "Draconiros";
    opening.creator = std::move(creator); opening.description = std::move(description);
    opening.participants = 4; opening.characters = characters; opening.messages = 3;
    return opening;
}

// The calendar window as the game builds it: ctr_days (7 day cards) and
// gd_events alone in one attached container.
struct Window { Obj owner, binding, container, days, events, list, panel; std::array<Obj, 7> cards; };
Window window;
uint64_t Ticks(int64_t day) { return (static_cast<uint64_t>(day + 719162) * 864000000000ULL) | (uint64_t(2) << 62); } // Kind Local
void ShowWeek(int64_t monday) {
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).fields["<dayDate>k__BackingField"] = Box(Ticks(monday + static_cast<int64_t>(i)));
}
Window Build(int64_t monday) {
    Window w;
    w.owner = Make("CalendarUI"); w.binding = Make("CalendarUIBinding"); w.container = Make("VisualElement");
    w.days = Make("VisualElement"); w.events = Make("ListView"); w.list = Make("List"); w.panel = Make("Panel");
    Get(w.container).fields["panel"] = w.panel;
    fixture::Add(w.container, w.days); fixture::Add(w.container, w.events);
    Get(w.owner).fields["dzyi"] = w.binding; Get(w.owner).fields["m_dayCards"] = w.list;
    Get(w.binding).fields["ctr_days"] = w.days; Get(w.binding).fields["gd_events"] = w.events;
    Get(w.days).layout = {0, 0, 1096, 74};
    for (size_t i = 0; i < 7; ++i) {
        w.cards[i] = Make("CalendarDayCard");
        Get(w.cards[i]).layout = {72.f + 144.f * static_cast<float>(i), 8, 136, 58};
        fixture::Add(w.days, w.cards[i]);
        Get(w.list).children.push_back(w.cards[i]);
    }
    window = w;
    ShowWeek(monday);
    return w;
}
Obj Row() { return state.row.get(); }
std::vector<Obj> Chips(size_t day) { return Get(state.cells[day].get()).children; }
size_t Logged(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
void Tick() { TickCalendarKralove(); }
void Ready(int64_t fromDay, std::vector<metamob::Kralove> openings) {
    view.usable = true; view.status = metamob::KraloveStatus::Ready; view.read = true; view.server = "Draconiros";
    view.fromDay = fromDay; view.openings = std::move(openings); ++view.version;
}
void Status(metamob::KraloveStatus status) {
    view.usable = true; view.status = status; view.read = false; view.server.clear(); view.openings.clear(); ++view.version;
}
void Untouched() {
    for (auto card : window.cards) Check(Get(card).styles.empty() && Get(card).classes.empty() && Get(card).tooltip.empty(), "Day cards untouched");
    for (auto element : {window.days, window.events, window.container})
        Check(Get(element).styles.empty() && Get(element).classes.empty() && Get(element).tooltip.empty(), "Native containers untouched");
    Check(Get(window.days).children.size() == 7, "ctr_days keeps its 7 cards");
}

void Helpers() {
    Check(EventLink(SecretId) == "https://www.metamob.fr/kralove/event/987654321" && EventLink(0).empty() && EventLink(-5).empty(),
        "Event page: digits of a positive id only");
    for (const auto& [status, text] : std::vector<std::pair<metamob::KraloveStatus, std::string>>{
        {metamob::KraloveStatus::Loading, Loading}, {metamob::KraloveStatus::Ready, ""}, {metamob::KraloveStatus::Unreachable, Unreachable},
        {metamob::KraloveStatus::Busy, Busy}, {metamob::KraloveStatus::Refused, Refused}, {metamob::KraloveStatus::KeyUnreadable, KeyUnreadable},
        {metamob::KraloveStatus::Unavailable, Unavailable}, {metamob::KraloveStatus::Unreadable, Unreadable},
        {metamob::KraloveStatus::NoQuest, NoQuest}, {metamob::KraloveStatus::SeveralQuests, SeveralQuests},
        {metamob::KraloveStatus::NoServer, NoServer}})
        Check(StatusText(status) == text, "Status text " + text);
    Check(EmptyText("Draconiros") == Empty && EmptyText({}) == "Aucune ouverture Kralamoure cette semaine sur ton serveur.", "Empty week text");
    Check(MoreText(1) == "+1 autre" && MoreText(2) == "+2 autres", "More text");

    // Bucketing by this PC's local days, around both 2026-2027 daylight saving changes in Paris.
    const std::vector<metamob::Kralove> october{
        Opening(1, "2026-10-18T21:59:00Z", 2),  // Sunday 18 23:59 CEST: the week before
        Opening(2, "2026-10-18T22:00:00Z", 2),  // Monday 19 00:00 CEST
        Opening(3, "2026-10-24T22:30:00Z", 1),  // Sunday 25 00:30 CEST
        Opening(4, "2026-10-25T00:30:00Z", 3),  // Sunday 25 02:30 CEST (the hour played twice, first time)
        Opening(5, "2026-10-25T01:30:00Z", 4),  // Sunday 25 02:30 CET (second time)
        Opening(6, "2026-10-25T22:59:00Z", 5),  // Sunday 25 23:59 CET
        Opening(7, "2026-10-25T23:00:00Z", 6)}; // Monday 26 00:00 CET: the week after
    std::array<int64_t, 7> week{};
    for (size_t i = 0; i < 7; ++i) week[i] = Day(2026, 10, 19) + static_cast<int64_t>(i);
    const auto paris = Bucket(october, week, Paris);
    Check(paris[0].size() == 1 && paris[0][0].opening->id == 2 && ChipText(paris[0][0]) == "00:00 · 2 persos", "Monday 00:00 CEST");
    for (size_t day = 1; day < 6; ++day) Check(paris[day].empty(), "No opening midweek");
    Check(paris[6].size() == 4 && ChipText(paris[6][0]) == "00:30 · 1 perso" && ChipText(paris[6][1]) == "02:30 · 3 persos" &&
        ChipText(paris[6][2]) == "02:30 · 4 persos" && ChipText(paris[6][3]) == "23:59 · 5 persos", "Sunday 25 October: summer then winter time");
    Check(LongDate(paris[6][1].local) == "dimanche 25 octobre 2026 à 02:30" && LongDate(paris[0][0].local) == "lundi 19 octobre 2026 à 00:00",
        "Long local dates");
    const auto utc = Bucket(october, week, Universal);
    Check(utc[0].empty() && utc[6].size() == 4 && utc[6][3].opening->id == 7 && ChipText(utc[6][0]) == "00:30 · 3 persos",
        "Another time zone, other days: the local days decide");
    const std::vector<metamob::Kralove> march{
        Opening(11, "2027-03-21T23:00:00Z", 1),  // Monday 22 00:00 CET
        Opening(12, "2027-03-28T00:30:00Z", 2),  // Sunday 28 01:30 CET
        Opening(13, "2027-03-28T01:00:00Z", 3),  // Sunday 28 03:00 CEST (02:00 skipped)
        Opening(14, "2027-03-28T21:59:00Z", 4),  // Sunday 28 23:59 CEST
        Opening(15, "2027-03-28T22:00:00Z", 5)}; // Monday 29 00:00 CEST: the week after
    for (size_t i = 0; i < 7; ++i) week[i] = Day(2027, 3, 22) + static_cast<int64_t>(i);
    const auto spring = Bucket(march, week, Paris);
    Check(spring[0].size() == 1 && ChipText(spring[0][0]) == "00:00 · 1 perso", "Monday 22 March 00:00 CET");
    Check(spring[6].size() == 3 && ChipText(spring[6][0]) == "01:30 · 2 persos" && ChipText(spring[6][1]) == "03:00 · 3 persos" &&
        ChipText(spring[6][2]) == "23:59 · 4 persos" && LongDate(spring[6][1].local) == "dimanche 28 mars 2027 à 03:00",
        "Sunday 28 March: winter then summer time");
    const auto unknown = Bucket(march, week, [](int64_t) { return std::optional<metamob::CivilTime>(); });
    for (const auto& day : unknown) Check(day.empty(), "No local time: no opening placed");
}

void Install() {
    // A field layout that is not the pinned one: disabled, the rest of the mod unaffected.
    offsets["m_dayCards"] = 0x218;
    InstallCalendarKralove();
    Check(!installed && !enableHook && !disableHook && !clickHandler && Logged("CALENDAR_KRALOVE_DISABLED") == 1,
        "A wrong contract disables the feature, before any hook");
    offsets["m_dayCards"] = 0x210;
    offsets["dzyi"] = 0x18;
    contracts.clear(); verified.clear();
    InstallCalendarKralove();
    Check(!installed && !enableHook && Logged("CALENDAR_KRALOVE_DISABLED") == 2, "The binding's offset is checked too");
    offsets["dzyi"] = 0x10;
    contracts.clear(); verified.clear();
    InstallCalendarKralove();
    Check(installed && enableHook && disableHook && clickHandler && clickHandlers == 1 && Logged("CALENDAR_KRALOVE_READY") == 1, "Installed");
    const std::vector<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"CalendarUIBinding", "get_ctr_days", 0, 0x2E9E140}, {"CalendarUIBinding", "get_gd_events", 0, 0x2E9E630},
        {"CalendarUI", "OnEnable", 0, 0x2E065E0}, {"CalendarUI", "OnDisable", 0, 0x2E06550},
        {"VisualElement", "Insert", 2, 0x55E5710}, {"VisualElement", "IndexOf", 1, 0x55E55B0}, {"VisualElement", "AddToClassList", 1, 0x55DF820},
        {"VisualElement", "RemoveFromHierarchy", 0, 0x55E7BD0}, {"VisualElement", "get_layout", 0, 0x55F6930},
        {"Application", "OpenURL", 1, 0x53ACA50}, {"DofusButtonCustom", ".ctor", 0, 0x170D6F0}, {"DofusLabel", ".ctor", 0, 0x1593940},
        {"TextElement", "set_enableRichText", 1, 0x564B1E0}, {"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_left", 1, 0x55D3EB0}};
    for (const auto& contract : expected)
        Check(std::find(contracts.begin(), contracts.end(), contract) != contracts.end(), "Contract verified: " + std::get<1>(contract));
    InstallCalendarKralove();
    Check(clickHandlers == 1 && Logged("CALENDAR_KRALOVE_READY") == 1, "Installed once");
}

// The option off: the hooks pass through, nothing is read, asked or drawn.
void OptionOff() {
    module->settings.calendarKralamoure = false;
    view = {}; view.usable = true;
    const auto enables = nativeEnables;
    enableHook(window.owner, nullptr);
    Check(nativeEnables == enables + 1 && !pending.handle, "Option off: OnEnable passes through, nothing kept");
    for (int i = 0; i < 3; ++i) Tick();
    Check(nativeEnables == enables + 1 && !pending.handle && !Row() && views == 0 && wants == 0, "Option off: no row, no reading");
    auto foreign = Make("DofusButtonCustom");
    Check(!clickHandler(foreign) && urls.empty(), "Option off: the game's buttons keep their clicks");
    disableHook(window.owner, nullptr);
    Check(nativeDisables == 1 && Get(window.container).children.size() == 2, "Option off: nothing placed");
    module->settings.calendarKralamoure = true;
}

// No usable key: no row and no request; the open calendar gets its row once a key is usable.
void WithoutKey() {
    view = {};
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Row() && pending.handle && wants == 0 && views == 2 && Get(window.container).children.size() == 2, "No usable key: no row, nothing asked");
    view.usable = true;
    Tick();
    Check(Row() && wants == 1 && Get(window.container).children == std::vector<Obj>{window.days, Row(), window.events},
        "A usable key: the row between the day cards and the event list");
}

void Loads() {
    const auto row = Row();
    Check(Pixels(row, "height") == 72 && FloatStyle(row, "flexShrink") == 0 && Get(row).children.size() == 9, "Row: 72 px, banner, 7 cells, status");
    const auto banner = state.banner.get();
    Check(EnumStyle(banner, "position") == 1 && Pixels(banner, "left") == 0 && Pixels(banner, "width") == 64 && Pixels(banner, "height") == 64 &&
        Get(banner).classes.contains("backgroundColor_black_black25") && Get(banner).tooltip == BannerUnknown, "Banner of 64 px");
    const auto& labels = Get(banner).children;
    Check(labels.size() == 2 && Get(labels[0]).text == "Kralamoure" && Get(labels[1]).text == "Metamob" && !Get(labels[0]).rich &&
        !Get(labels[1]).rich && Get(labels[0]).picking == 1 && Get(labels[1]).picking == 1 && Pixels(labels[0], "fontSize") == 11 &&
        Get(labels[0]).classes.contains("textShort_smallBold"), "Banner labels: plain text, the banner keeps the hover");
    for (size_t i = 0; i < 7; ++i)
        Check(Pixels(state.cells[i].get(), "left") == 72.f + 144.f * static_cast<float>(i) && Pixels(state.cells[i].get(), "width") == 136 &&
            Pixels(state.cells[i].get(), "height") == 64, "Cells on the day cards");
    Check(Get(state.status.get()).text == Loading && EnumStyle(state.statusBox.get(), "display") == 0 && !Get(state.status.get()).rich,
        "Loading text");
    Check(Pixels(state.statusBox.get(), "left") == 72 && Pixels(state.statusBox.get(), "width") == 1000, "Status across the week");
    const auto renders = Logged("CALENDAR_KRALOVE_WEEK");
    Tick(); Tick();
    Check(wants == 3 && Logged("CALENDAR_KRALOVE_WEEK") == renders, "Asked at every pump, drawn again only on a change");
    Untouched();
}

void Openings() {
    now = Utc("2026-10-21T19:00:00Z"); // Wednesday 21:00 in Paris
    Ready(Day(2026, 10, 12), {
        Opening(SecretId, "2026-10-19T19:00:00Z", 12, Creator, "Rendez-vous au zaap " + Marker),
        Opening(108, "2026-10-18T22:00:00Z", 2),
        Opening(109, "2026-10-21T18:00:00Z", 0, Creator),
        Opening(102, "2026-10-24T22:30:00Z", 1),
        Opening(103, "2026-10-25T00:30:00Z", 3),
        Opening(104, "2026-10-25T01:30:00Z", 4),
        Opening(105, "2026-10-25T22:59:00Z", 5),
        Opening(106, "2026-10-25T23:00:00Z", 6),
        Opening(107, "2026-10-18T21:59:00Z", 7)});
    Tick();
    Check(EnumStyle(state.statusBox.get(), "display") == 1 && Get(state.banner.get()).tooltip == Banner, "Openings: no status text");
    const auto monday = Chips(0), wednesday = Chips(2), sunday = Chips(6);
    Check(monday.size() == 2 && wednesday.size() == 1 && sunday.size() == 3 && Chips(1).empty() && Chips(3).empty() && Chips(4).empty() &&
        Chips(5).empty(), "Openings by local day, the weeks before and after left out");
    Check(Get(monday[0]).text == "00:00 · 2 persos" && Get(monday[1]).text == "21:00 · 12 persos" && Get(wednesday[0]).text == "20:00 · 0 perso" &&
        Get(sunday[0]).text == "00:30 · 1 perso" && Get(sunday[1]).text == "02:30 · 3 persos" && Get(sunday[2]).text == "+2 autres",
        "Chip captions: local time and characters, 3 at most");
    for (auto chip : {monday[0], monday[1], wednesday[0], sunday[0], sunday[1]}) {
        Check(Get(chip).klass->name == "DofusButtonCustom" && Get(chip).size == 0 && Get(chip).mainStyle == 1, "The game's small button");
        Check(EnumStyle(chip, "position") == 1 && Pixels(chip, "left") == 0 && PercentStyle(chip, "width") == 100 && Pixels(chip, "height") == 20 &&
            Pixels(chip, "minHeight") == 20, "20 px across the cell");
    }
    Check(Pixels(monday[0], "top") == 0 && Pixels(monday[1], "top") == 22 && Pixels(sunday[2], "top") == 44, "Stacked in the 64 px band");
    Check(FloatStyle(monday[0], "opacity") < .5f && FloatStyle(monday[1], "opacity") < .5f && FloatStyle(wednesday[0], "opacity") < .5f &&
        !Styled(sunday[0], "opacity") && !Styled(sunday[1], "opacity"), "Past openings dimmed");
    Check(Get(monday[1]).tooltip == "Ouverture Kralamoure passée · Draconiros\nlundi 19 octobre 2026 à 21:00 (heure de ce PC)\n"
        "Organisateur : " + Creator + "\nInscrits : 4 joueurs, 12 personnages · 3 messages\n« Rendez-vous au zaap " + Marker + " »\n"
        "Clique pour ouvrir l’événement sur metamob.fr.", "Tooltip of a past opening");
    Check(Get(sunday[0]).tooltip == "Ouverture Kralamoure · Draconiros\ndimanche 25 octobre 2026 à 00:30 (heure de ce PC)\n"
        "Organisateur : inconnu\nInscrits : 4 joueurs, 1 personnage · 3 messages\nClique pour ouvrir l’événement sur metamob.fr.",
        "Tooltip without organiser nor description");
    Check(Get(sunday[2]).klass->name == "DofusLabel" && !Get(sunday[2]).rich && Get(sunday[2]).tooltip ==
        "Autres ouvertures Kralamoure ce jour-là :\n02:30 · 4 persos\n23:59 · 5 persos\nToutes les ouvertures : metamob.fr, rubrique Kralamoure.",
        "The others of a crowded day");
    // Clicks: the event's page for an opening, nothing for the « +N » label, the game's own buttons untouched.
    Check(clickHandler(monday[1]) && urls == std::vector<std::string>{"https://www.metamob.fr/kralove/event/987654321"}, "Opening clicked");
    Check(clickHandler(sunday[2]) && urls.size() == 1, "« +2 autres » clicked: no page");
    Check(!clickHandler(Make("DofusButtonCustom")) && !clickHandler(window.cards[0]) && urls.size() == 1, "Foreign buttons keep their clicks");
    Check(Logged("CALENDAR_KRALOVE_OPEN") == 1, "Opening logged without its id");
    // Time passes: dimmed when its time comes.
    const auto renders = Logged("CALENDAR_KRALOVE_WEEK");
    now = Utc("2026-10-24T22:29:59Z");
    Tick();
    Check(Logged("CALENDAR_KRALOVE_WEEK") == renders && !Styled(Chips(6)[0], "opacity"), "Not drawn again before an opening's time");
    now = Utc("2026-10-24T22:30:00Z");
    Tick();
    Check(Logged("CALENDAR_KRALOVE_WEEK") == renders + 1 && FloatStyle(Chips(6)[0], "opacity") < .5f && !Styled(Chips(6)[1], "opacity") &&
        Get(Chips(6)[0]).tooltip.starts_with("Ouverture Kralamoure passée · Draconiros\n"), "Dimmed at its time");
    Check(!Get(monday[0]).fields["parent"] && Get(monday[0]).tooltip.size(), "Previous buttons detached");
    Untouched();
}

void Weeks() {
    // The game's arrows: the cards' days change, the row follows.
    ShowWeek(Day(2026, 10, 26));
    Tick();
    Check(Chips(0).size() == 1 && Get(Chips(0)[0]).text == "00:00 · 6 persos" && EnumStyle(state.statusBox.get(), "display") == 1,
        "Next week: its own openings");
    ShowWeek(Day(2026, 11, 2));
    Tick();
    for (size_t i = 0; i < 7; ++i) Check(Chips(i).empty(), "An empty week");
    Check(Get(state.status.get()).text == Empty && EnumStyle(state.statusBox.get(), "display") == 0, "Empty week: told, with the server");
    ShowWeek(Day(2026, 10, 12));
    Tick();
    Check(Get(state.status.get()).text == PastWeek && Chips(0).empty(), "A week before the first day asked: not read");
    ShowWeek(Day(2026, 10, 19));
    Tick();
    Check(Chips(0).size() == 2 && EnumStyle(state.statusBox.get(), "display") == 1, "Back to this week");
    // The cards' layout: the cells follow, the row's own place included.
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).layout.x += 10;
    Get(Row()).layout.x = -4;
    Tick();
    for (size_t i = 0; i < 7; ++i)
        Check(Pixels(state.cells[i].get(), "left") == 86.f + 144.f * static_cast<float>(i), "Cells follow the cards' layout");
    Check(Pixels(state.statusBox.get(), "left") == 86 && Pixels(state.statusBox.get(), "width") == 1000, "Status box follows");
    Get(window.cards[3]).layout.x = NAN;
    Tick();
    Check(Pixels(state.cells[3].get(), "left") == 86.f + 144.f * 3, "An unresolved layout moves nothing");
    Get(window.cards[3]).layout.x = 72.f + 144.f * 3 + 10;
    Get(Row()).layout.x = 0;
    for (size_t i = 0; i < 7; ++i) Get(window.cards[i]).layout.x -= 10;
    Tick();
    Check(Pixels(state.cells[0].get(), "left") == 72, "Back on the UXML's places");
    // Every state of the reading, with nothing read yet.
    for (const auto& [status, text] : std::vector<std::pair<metamob::KraloveStatus, std::string>>{
        {metamob::KraloveStatus::Unreachable, Unreachable}, {metamob::KraloveStatus::Busy, Busy}, {metamob::KraloveStatus::Unavailable, Unavailable},
        {metamob::KraloveStatus::Unreadable, Unreadable}, {metamob::KraloveStatus::NoQuest, NoQuest},
        {metamob::KraloveStatus::SeveralQuests, SeveralQuests}, {metamob::KraloveStatus::NoServer, NoServer},
        {metamob::KraloveStatus::Loading, Loading}}) {
        Status(status);
        Tick();
        Check(Get(state.status.get()).text == text && EnumStyle(state.statusBox.get(), "display") == 0 && Chips(0).empty() &&
            Get(state.banner.get()).tooltip == BannerUnknown, "State text: " + text);
    }
    // A passing failure after a reading keeps what was read.
    Ready(Day(2026, 10, 12), {Opening(SecretId, "2026-10-22T19:00:00Z", 8, Creator, Marker)});
    view.status = metamob::KraloveStatus::Unreachable;
    Tick();
    Check(Chips(3).size() == 1 && EnumStyle(state.statusBox.get(), "display") == 1, "Read openings kept through a passing failure");
    Untouched();
}

void Layouts() {
    // OnDisable: the row goes before the game's own OnDisable.
    const auto row = Row(), chip = Chips(3)[0];
    disableHook(window.owner, nullptr);
    Check(!state.row.get() && !Get(row).fields["parent"] && Get(window.container).children == std::vector<Obj>{window.days, window.events} &&
        !pending.handle, "OnDisable removes the row");
    Check(!clickHandler(chip) && urls.size() == 1, "A removed button is no longer ours");
    const auto asked = wants, read = views;
    Tick(); Tick();
    Check(wants == asked && views == read, "Closed: nothing read nor asked");
    // A layout other than ctr_days then gd_events in one container: refused with its code, nothing placed.
    auto foreign = Make("VisualElement");
    fixture::Insert(window.container, 1, foreign);
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=4") == 1 && !pending.handle && Get(window.container).children.size() == 3,
        "gd_events not at index 1: refused once");
    fixture::Detach(foreign);
    fixture::Detach(window.events);
    auto elsewhere = Make("VisualElement"); fixture::Add(elsewhere, window.events);
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=3") == 1, "gd_events in another container: refused");
    fixture::Detach(window.events); fixture::Add(window.container, window.events);
    Get(window.binding).fields["gd_events"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=2") == 1, "No gd_events: refused");
    Get(window.binding).fields["gd_events"] = window.events;
    Get(window.owner).fields["dzyi"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=1") == 1, "No binding: refused");
    Get(window.owner).fields["dzyi"] = window.binding;
    // Six days in the list, or days not in a row: refused.
    auto last = Get(window.list).children.back();
    Get(window.list).children.pop_back();
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 1, "Six day cards: refused");
    Get(window.list).children.push_back(last);
    Get(window.cards[4]).fields["<dayDate>k__BackingField"] = Box(Ticks(Day(2026, 10, 30)));
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 2, "Days not in a row: refused");
    // Cards not dated yet (all on one day): waited for, then placed once dated; refused after 5 s.
    for (auto card : window.cards) Get(card).fields["<dayDate>k__BackingField"] = Box(uint64_t(0));
    enableHook(window.owner, nullptr);
    for (int i = 0; i < 25; ++i) Tick();
    Check(!Row() && pending.handle && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 2, "Undated cards: waited for, untold");
    Tick();
    Check(!Row() && !pending.handle && Logged("CALENDAR_KRALOVE_LAYOUT code=5") == 3, "Still undated after 5 s: refused");
    for (auto card : window.cards) Get(card).fields["<dayDate>k__BackingField"] = Box(Ticks(Day(2026, 10, 19)));
    enableHook(window.owner, nullptr);
    Tick();
    Check(!Row() && pending.handle, "Undated again (one day for all): waits");
    ShowWeek(Day(2026, 10, 19));
    Tick();
    Check(Row(), "Placed once dated");
    disableHook(window.owner, nullptr);
    Check(!Row(), "Closed");
    // Not shown yet: tried again at the next pump, untold.
    Get(window.container).fields["panel"] = nullptr;
    enableHook(window.owner, nullptr);
    Tick(); Tick();
    Check(!Row() && pending.handle && Logged("CALENDAR_KRALOVE_LAYOUT") == 7, "Not attached yet: waits");
    Get(window.container).fields["panel"] = window.panel;
    Tick();
    Check(Row() && Get(window.container).children == std::vector<Obj>{window.days, Row(), window.events}, "Placed once shown");
    // The game moves things around the row: it goes, until the next OnEnable.
    auto intruder = Make("VisualElement");
    fixture::Insert(window.container, 1, intruder);
    const auto moved = Row();
    Tick();
    Check(!state.row.get() && !Get(moved).fields["parent"] && Get(window.container).children ==
        std::vector<Obj>{window.days, intruder, window.events}, "A changed layout: the row goes, the game's element stays");
    Tick();
    Check(!Row(), "Not placed again without an OnEnable");
    fixture::Detach(intruder);
    enableHook(window.owner, nullptr);
    Tick();
    Check(Row(), "Placed again at the next OnEnable");
    // A day card leaving the list while shown: the row goes with code 6.
    Get(window.list).children.pop_back();
    Tick();
    Check(!Row() && Logged("CALENDAR_KRALOVE_LAYOUT code=6") == 1 && Get(window.container).children.size() == 2, "Cards changed while shown");
    Get(window.list).children.push_back(last);
    // Another character session: the row goes.
    enableHook(window.owner, nullptr);
    Tick();
    Check(Row(), "Placed");
    module->cached.reset(Make("OtherCache"));
    Tick();
    Check(!Row() && Get(window.container).children.size() == 2, "Another session: the row goes");
    Untouched();
}

void KeyAndOption() {
    enableHook(window.owner, nullptr);
    Tick();
    Check(Row(), "Placed");
    // The key no longer usable (forgotten, refused, unreadable): no row, nothing asked; usable again: back without reopening.
    view.usable = false;
    const auto asked = wants;
    Tick(); Tick();
    Check(!Row() && pending.handle && wants == asked && Get(window.container).children.size() == 2, "No usable key: the row goes, the calendar waits");
    view.usable = true;
    Tick();
    Check(Row() && wants == asked + 1, "Usable again: back");
    // The option turned off while shown: the row goes, nothing more is read or asked.
    module->settings.calendarKralamoure = false;
    const auto read = views;
    Tick();
    Check(!Row() && !pending.handle && views == read && wants == asked + 1, "Option off: removed at once");
    module->settings.calendarKralamoure = true;
    Tick();
    Check(!Row() && views == read, "Option on again: at the next OnEnable");
    enableHook(window.owner, nullptr);
    Tick();
    Check(Row(), "Placed at the next OnEnable");
    // A detach that fails: the cleanup stays pending (clicks consumed, no page), then completes.
    Ready(Day(2026, 10, 12), {Opening(SecretId, "2026-10-22T19:00:00Z", 8, Creator, Marker)});
    Tick();
    const auto row = Row(), chip = Chips(3)[0];
    failDetach = row;
    disableHook(window.owner, nullptr);
    Check(CalendarKraloveCleanupPending() && Get(row).fields["parent"], "Detach failure: cleanup pending");
    Check(clickHandler(chip) && urls.size() == 1, "While cleaning up: the click is consumed, no page");
    Tick();
    Check(!CalendarKraloveCleanupPending() && !Get(row).fields["parent"] && !state.row.get(), "Cleanup completed at the next pump");
    // Stop: Restore releases everything.
    enableHook(window.owner, nullptr);
    Tick();
    Check(Row(), "Placed");
    RestoreCalendarKralove();
    Check(!Row() && !pending.handle && Get(window.container).children.size() == 2 && !CalendarKraloveCleanupPending(), "Restored");
    Untouched();
}

void Private() {
    for (const auto& line : logs) {
        Check(line.find("987654321") == std::string::npos && line.find("Zorglub") == std::string::npos && line.find("Corps-Prive") == std::string::npos &&
            line.find("Draconiros") == std::string::npos && line.find("metamob.fr") == std::string::npos, "A log quotes an opening: " + line);
    }
}
}

int main() {
    try {
        Setup();
        clockNow = FakeNow;
        localTime = Paris;
        Module mod({});
        module = &mod;
        mod.ready = mod.servicesReady = true;
        mod.cached.reset(Make("Cache"));
        mod.compositor.reset(Make("Compositor"));
        Helpers();
        Build(Day(2026, 10, 19));
        Install();
        OptionOff();
        WithoutKey();
        Loads();
        Openings();
        Weeks();
        Layouts();
        mod.cached.reset(Make("Cache"));
        KeyAndOption();
        Private();
        mod.cached.reset(); mod.compositor.reset();
        module = nullptr;
        Check(handles.empty(), "No managed reference kept after Restore");
        std::cout << "PASS: Kralamoure calendar row: inert with the option off or without a usable key, placed only between ctr_days "
            "and gd_events (layout codes 1-6, retried until shown, undated cards waited for 5 s), 7 cells on the day cards' layout, the game's buttons "
            "« 21:00 · 12 persos » by local day (DST 2026-10-25 and 2027-03-28), 3 a day then « +N autres », past ones dimmed at "
            "their time, plain-text tooltips, event page on click (digits only), week changes followed, state texts, "
            "removed on OnDisable / layout change / another session / no key / option off, retryable cleanup, every native "
            "call verified at install, no private data in logs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fixture::logs) std::cerr << "  log: " << line << '\n';
        return 1;
    }
}
