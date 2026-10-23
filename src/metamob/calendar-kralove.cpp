#include "metamob/calendar-kralove.hpp"
#include "metamob/metamob-data.hpp"
#include "metamob/metamob-sync.hpp"
#include "core/module.hpp"
#include "ui/native-click.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// The calendar window (CalendarUI): on its left « Mes calendriers », the
// game's calendars in a ListView (gd_calendarFilters); on its right the 7 day
// cards of a week (ctr_days) above the event lines (the ListView gd_events),
// both alone in one container. Built from the game's own templates
// (DofusUISettings: calendarFilterItem, calendarLine, calendarCard), and
// never touching the game's model, lists, filters or saved colours, the mod
// adds:
// - a « Kralamoure » entry right above the game's calendars, with its icon
//   and its colour; its checkbox is the option calendarKralamoure, changed
//   through module->Change so the options tab follows;
// - while it is checked, Kralamoure lines between the day cards and the
//   game's lines: the banner tinted with that colour, its icon, then a card
//   per opening on its day (« Ouverture Kralamoure », « Début : 21h00 »,
//   « 12 persos », past ones greyed), one more line for each further opening
//   of a day as the game does for overlapping events (3 lines at most, then a
//   « +N ouvertures » card). Native tooltips tell the local date and time,
//   the server, the organiser, the counts and the description; a click on a
//   card opens the event's page on metamob.fr (the « +N » one, Metamob's list
//   of openings). Without openings the line says why. The cards are drawn
//   again only when what they show changes.
// The game's components register themselves in DofusCursor's static caches
// of hand-cursor handlers, which never let them go: every clone is released
// from them at once, the mod's own hand cursors (cards, the entry's checkbox)
// when they go.
// The openings come from metamob-sync, asked only while the lines are shown
// (read at most every 10 min). Without a usable Metamob key there is neither
// entry nor line, and nothing is asked. Logs carry fixed codes and counts
// only: never an id, a name or a description.
namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* EventPage = "https://www.metamob.fr/kralove/event/";
constexpr const char* KralovePage = "https://www.metamob.fr/kralove"; // Metamob's list of every opening
constexpr size_t Days = 7, MaxLines = 3;
// The « Kralamoure » calendar: its colour (RRGGBB as ColorSquare.SetColor
// reads it) and its icon, FigmaIcons.waveMonsters (a wave and a monster), unlike
// the game's five calendars (icons celebration, almanax, krosmoz, crossedSwords,
// boots; default colours 47C3EB, EB71FF, FFD336, FF7C4C, 00A31E).
constexpr const char* KralamoureHex = "8A5CF6";
struct Color { float r, g, b, a; };
constexpr Color KralamoureColor{138 / 255.f, 92 / 255.f, 246 / 255.f, 1};
// CalendarCard.UpdateColor: the colour line opaque, the card at 30 %; a past
// opening takes the game's disabled grey (white 15 %), dimmed.
constexpr float CardAlpha = .3f, PastOpacity = .6f;
constexpr Color PastColor{1, 1, 1, .15f};
constexpr int KralamoureIcon = 329;
// Geometry (px) of the calendar's UXML and of CalendarCard.SetCardWidthAndPosition:
// a line 138 high (146 with its margin: gd_events' fixed item height), its
// banner 80; a one-day card 136 wide, one day every 144 px from the line's
// events; an entry of « Mes calendriers » 36 high (gd_calendarFilters).
constexpr float LineHeight = 138, StatusHeight = 80, CardWidth = 136, CardStep = 144, FilterHeight = 36;
// DisplayStyle, Position, FlexDirection, Align, WhiteSpace and LengthUnit values.
constexpr int DisplayFlex = 0, DisplayNone = 1, Absolute = 1, Column = 0, AlignCenter = 2, WrapText = 0, Pixel = 0;
// Tooltips placed as the calendar's own (pq.pcd: under their element), closed
// when the pointer leaves the element (Leave: MouseLeaveEvent, not the bubbling
// MouseOutEvent of its children); the game's hand cursor (CursorType 0x200000).
constexpr int TipBottomCenter = 7, TipTopCenter = 1, TipLeave = 0, CursorNormal = 0;
constexpr int64_t HandCursor = 0x200000;
// A .NET DateTime: 100 ns ticks since 0001-01-01 in its 62 low bits (its Kind
// above them); 1970-01-01 is its day 719162.
constexpr int64_t TicksPerDay = 864000000000, UnixEpochDay = 719162;
constexpr uint64_t TicksMask = (uint64_t(1) << 62) - 1;
struct Rect { float x, y, width, height; };
// A clone of a template that is not the one this module knows: refused (code 7).
struct TemplateError : std::runtime_error { using std::runtime_error::runtime_error; };

int64_t RealNow() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto ticks = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000) - 11644473600; // FILETIME counts 100 ns from 1601
}
std::optional<metamob::CivilTime> RealLocal(int64_t at) { return metamob::LocalTime(at); }
// Replaced by the tests only: the time (UTC Unix seconds) and the local time zone.
int64_t (*clockNow)() = RealNow;
std::optional<metamob::CivilTime> (*localTime)(int64_t) = RealLocal;

// The openings of a week, by day card: each one's local date (this PC's time
// zone, daylight saving included) against the cards' 7 consecutive days.
struct Placed { const metamob::Kralove* opening = nullptr; metamob::CivilTime local; };
using Week = std::array<std::vector<Placed>, Days>;
Week Bucket(const std::vector<metamob::Kralove>& openings, const std::array<int64_t, Days>& days,
    std::optional<metamob::CivilTime> (*local)(int64_t)) {
    Week week;
    for (const auto& opening : openings) {
        const auto time = local(opening.at);
        if (!time) continue;
        const auto day = metamob::DayNumber(time->year, time->month, time->day) - days[0];
        if (day >= 0 && day < static_cast<int64_t>(Days)) week[static_cast<size_t>(day)].push_back({&opening, *time});
    }
    for (auto& day : week)
        std::stable_sort(day.begin(), day.end(), [](const Placed& a, const Placed& b) {
            return a.opening->at != b.opening->at ? a.opening->at < b.opening->at : a.opening->id < b.opening->id;
        });
    return week;
}
std::string Two(int value) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "%02d", value);
    return text;
}
// « 21h00 », as the game's own cards write their hours.
std::string Clock(const metamob::CivilTime& time) { return Two(time.hour) + "h" + Two(time.minute); }
std::string Counted(int count, const char* one, const char* many) { return std::to_string(count) + " " + (count > 1 ? many : one); }
constexpr const char* CardTitle = "Ouverture Kralamoure";
std::string StartText(const Placed& placed) { return "Début : " + Clock(placed.local); }
std::string CountText(const Placed& placed) { return Counted(placed.opening->characters, "perso", "persos"); }
std::string LongDate(const metamob::CivilTime& time) {
    static constexpr const char* Weekdays[] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
    static constexpr const char* Months[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre",
        "octobre", "novembre", "décembre"};
    return std::string(Weekdays[static_cast<size_t>(time.weekday) % 7]) + " " + (time.day == 1 ? std::string("1er") : std::to_string(time.day)) +
        " " + Months[static_cast<size_t>(time.month + 11) % 12] + " " + std::to_string(time.year) + " à " + Clock(time);
}
// Plain text only: every name and the description were made display-safe by
// the parser (no '<', '>', control or bidi character).
std::string Tooltip(const Placed& placed, const std::string& server, bool past) {
    const auto& opening = *placed.opening;
    const auto& name = opening.server.empty() ? server : opening.server;
    std::string text = past ? "Ouverture Kralamoure passée" : "Ouverture Kralamoure";
    if (!name.empty()) text += " · " + name;
    text += "\n" + LongDate(placed.local) + " (heure de ce PC)\nOrganisateur : " +
        (opening.creator.empty() ? std::string("inconnu") : opening.creator) + "\nInscrits : " +
        Counted(opening.participants, "joueur", "joueurs") + ", " + Counted(opening.characters, "personnage", "personnages") + " · " +
        Counted(opening.messages, "message", "messages");
    if (!opening.description.empty()) text += "\n« " + opening.description + " »";
    return text + "\nClique pour ouvrir l’événement sur metamob.fr.";
}
// More than 3 openings in a day (only far from Metamob's own time zone): the
// first two, then a card « +N ouvertures » telling the others.
std::string MoreText(size_t hidden) { return "+" + std::to_string(hidden) + (hidden > 1 ? " ouvertures" : " ouverture"); }
std::string MoreTooltip(const std::vector<Placed>& day, size_t from) {
    std::string text = "Autres ouvertures Kralamoure ce jour-là :";
    for (size_t i = from; i < day.size(); ++i) text += "\n" + Clock(day[i].local) + " · " + CountText(day[i]);
    return text + "\nClique pour voir toutes les ouvertures sur metamob.fr.";
}
std::string BannerTooltip(const std::string& server) {
    return "Ouvertures Kralamoure planifiées sur Metamob pour " + (server.empty() ? std::string("le serveur de ta quête Ocre") : server) +
        ", à l’heure de ce PC. Survole une ouverture pour ses détails ; clique dessus pour l’ouvrir sur metamob.fr.";
}
constexpr const char* FilterTooltip = "Kralamoure (Dofus Enhanced) : les ouvertures planifiées sur Metamob pour le serveur de ta quête "
    "Ocre, en lignes sous les jours. Décoché, rien n’est affiché ni lu sur Metamob. C’est l’option « Ouvertures Kralamoure "
    "dans le calendrier ».";
std::string EmptyText(const std::string& server) {
    return "Aucune ouverture Kralamoure cette semaine sur " + (server.empty() ? std::string("ton serveur") : server) + ".";
}
constexpr const char* PastWeekText = "Metamob : les ouvertures des semaines passées ne sont pas lues.";
// The line's text while it shows no opening, by the state of the last reading.
std::string StatusText(metamob::KraloveStatus status) {
    using metamob::KraloveStatus;
    switch (status) {
    case KraloveStatus::Loading: return "Metamob : lecture des ouvertures…";
    case KraloveStatus::Ready: return {};
    case KraloveStatus::Unreachable: return "Metamob injoignable, nouvel essai automatique.";
    case KraloveStatus::Busy: return "Metamob : trop de demandes, nouvel essai automatique.";
    case KraloveStatus::Refused: return "Metamob refuse ta clé pour les ouvertures Kralamoure ; nouvel essai dans 10 minutes.";
    case KraloveStatus::KeyUnreadable: return "Clé Metamob illisible.";
    case KraloveStatus::Unavailable: return "Ouvertures Kralamoure indisponibles sur l’API Metamob.";
    case KraloveStatus::Unreadable: return "Réponse Metamob illisible, nouvel essai automatique.";
    case KraloveStatus::NoQuest: return "Serveur inconnu : aucune quête Ocre Dofus Unity sur ton compte Metamob.";
    case KraloveStatus::SeveralQuests:
        return "Serveur inconnu : plusieurs quêtes Ocre Dofus Unity, aucune n’indique ce personnage dans « Personnage à contacter ».";
    case KraloveStatus::NoServer: return "Serveur inconnu : ta quête Ocre sur Metamob n’indique pas de serveur.";
    }
    return {};
}
// The event's page: its id in digits only (std::to_string of a positive id).
std::string EventLink(int64_t id) { return id > 0 ? EventPage + std::to_string(id) : std::string(); }
// An error's own text for the log, at most 160 bytes (never cut inside a UTF-8 character).
std::string Short(const char* text) {
    std::string value = text ? text : "";
    size_t size = (std::min)(value.size(), size_t(160));
    while (size && size < value.size() && (static_cast<unsigned char>(value[size]) & 0xC0) == 0x80) --size;
    value.resize(size);
    return value;
}

// What the lines show for a week: compared at each pump, the cards are drawn
// again only when it changes (not at each reading Metamob gives unchanged).
struct CardPlan {
    size_t line = 0, day = 0;
    std::string title, start, end, tip;
    int64_t id = 0;
    bool past = false, more = false;
    bool operator==(const CardPlan&) const = default;
};
struct Plan {
    std::string status, server; // the status text when no card; the server for the banner's tooltip
    size_t lines = 1, shown = 0;
    std::vector<CardPlan> cards;
    bool operator==(const Plan&) const = default;
};
Plan MakePlan(const metamob::KraloveView& view, const std::array<int64_t, Days>& week, int64_t now) {
    Plan plan;
    plan.server = view.read ? view.server : std::string();
    if (!view.read) plan.status = StatusText(view.status);
    else if (week[0] <= view.fromDay) plan.status = PastWeekText; // a day before the first one asked is shown
    else {
        const auto days = Bucket(view.openings, week, localTime);
        for (size_t day = 0; day < Days; ++day) {
            const auto& openings = days[day];
            plan.shown += openings.size();
            const auto slots = (std::min)(openings.size(), MaxLines);
            plan.lines = (std::max)(plan.lines, slots);
            for (size_t slot = 0; slot < slots; ++slot) {
                CardPlan card;
                card.line = slot; card.day = day;
                if (slot + 1 == MaxLines && openings.size() > MaxLines) {
                    card.title = "Kralamoure"; card.start = MoreText(openings.size() - slot);
                    card.tip = MoreTooltip(openings, slot); card.more = true;
                } else {
                    const auto& placed = openings[slot];
                    card.past = placed.opening->at <= now;
                    card.title = CardTitle; card.start = StartText(placed); card.end = CountText(placed);
                    card.tip = Tooltip(placed, view.server, card.past); card.id = placed.opening->id;
                }
                plan.cards.push_back(std::move(card));
            }
        }
        if (!plan.shown) plan.status = EmptyText(view.server);
    }
    if (!plan.status.empty()) { plan.lines = 1; plan.cards.clear(); }
    return plan;
}

// A card on a line: an opening (id > 0) or the « +N » of a crowded day, with
// the native tooltip and clickable the mod gave it (its hand cursor is in the
// cursor caches, released with its tree).
struct Card { Ref root, tip, click; size_t day = 0; int64_t id = 0; bool more = false; };
// A line: the clone of calendarLine and its parts.
struct Line { Ref root, line, banner, border, icon, events; };
struct State {
    Ref owner, container, days, events, filters, filterList, cached, compositor, tooltips; // tooltips: the game's tooltip service (fou)
    // The « Kralamoure » entry before gd_calendarFilters, its checkbox and tooltip.
    Ref filter, checkbox, filterTip;
    bool filterValue = false;               // the option as the checkbox shows it
    // The block of lines at index 1 of the day cards' container.
    Ref block, status, bannerTip;
    std::vector<Line> lines;
    std::vector<Card> cards;
    std::string bannerText;
    Plan plan;                              // what the lines show
    std::array<float, Days * 2> placed{};   // each day's card left and width as last written
    int waits = 0;                          // pumps the day cards were seen without their days
    bool drawn = false, refused = false, cleanup = false;
} state;
Ref pending; // the calendar enabled, its layout not checked yet
bool installed = false;
uint64_t nextCleanupLog = 0;
struct Natives {
    Klass element = nullptr, dayCard = nullptr, icon = nullptr, label = nullptr, checkbox = nullptr, square = nullptr,
        tooltip = nullptr, clickable = nullptr, cursor = nullptr;
    Method settings{}, lineAsset{}, cardAsset{}, filterAsset{}, cloneTree{}, setColor{}, tooltipCtor{}, hideTooltip{},
        addManipulator{}, removeManipulator{}, clickableCtor{}, enableCursor{}, removeCursor{}, openUrl{};
} n;
void (*originalEnable)(Obj, Method) = nullptr;
void (*originalDisable)(Obj, Method) = nullptr;

bool Session() { return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested; }
bool Enabled() { return module && module->settings.calendarKralamoure; }
Obj StyleBox(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    Obj box = api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str());
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Calendar style unavailable");
    return box;
}
void WriteStyle(Obj element, const char* property, void* value) {
    Ref box(StyleBox(element, property));
    api.Call(box.get(), "set_value", {value});
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box.get())});
}
void Enum(Obj element, const char* property, int value) { WriteStyle(element, property, &value); }
void Float(Obj element, const char* property, float value) { WriteStyle(element, property, &value); }
void Paint(Obj element, const char* property, Color color) { WriteStyle(element, property, &color); }
void Length(Obj element, const char* property, float value) {
    int unit = Pixel;
    Ref length(api.New(api.Class(Ui, UiSpace, "Length"), {&value, &unit}));
    WriteStyle(element, property, api.object_unbox(length.get()));
}
void Text(Obj element, const std::string& text, const char* setter = "set_text") {
    Ref value(api.string_new(text.c_str()));
    api.Call(element, setter, {value.get()});
}
void Class(Obj element, const char* name) { Text(element, name, "AddToClassList"); }
void Add(Obj parent, Obj child) { api.Call(parent, "Add", {child}); }
int Index(Obj parent, Obj child) { return api.Value<int>(api.Call(parent, "IndexOf", {child})); }
Obj NewElement() { return api.New(n.element); }
void Detach(Obj element) { api.Call(element, "RemoveFromHierarchy"); }
// The game's label, never rich text; added to its parent.
Obj NewLabel(Obj parent, const char* textClass, const char* colorClass) {
    Ref label(api.New(n.label));
    bool rich = false;
    api.Call(label.get(), "set_enableRichText", {&rich});
    Class(label.get(), textClass);
    Class(label.get(), colorClass);
    Add(parent, label.get());
    return label.get();
}
void LayoutRefused(int code) { Log("CALENDAR_KRALOVE_LAYOUT code=" + std::to_string(code)); }

// The elements of a tree, root first (a tree of the mod: a few dozen elements).
std::vector<Obj> Tree(Obj root) {
    std::vector<Obj> found, stack{root};
    while (!stack.empty()) {
        if (found.size() >= 1024) throw TemplateError("Calendar tree too large");
        const Obj element = stack.back();
        stack.pop_back();
        found.push_back(element);
        const int count = api.Value<int>(api.Call(element, "get_childCount"));
        if (count < 0 || count > 64) throw TemplateError("Calendar template layout");
        for (int i = count - 1; i >= 0; --i)
            if (Obj child = api.Call(element, "ElementAt", {&i})) stack.push_back(child);
    }
    return found;
}
// DofusCursor's caches of hand-cursor handlers: EnableCursorHandler (and the
// AddCursorHandler of DofusButtonCustom, CheckboxCustom, ColorSquare… in their
// constructors) keys them by element; RemoveCursorHandler only unregisters the
// element's callbacks. Every element of the tree that is a key is released:
// its handler unregistered, then both keys removed, the enter one last (a
// retry still finds it).
void Uncache(Obj root) {
    Ref enter(api.StaticField(n.cursor, "s_mouseEnterCursorMap")), leave(api.StaticField(n.cursor, "s_mouseLeaveCursorMap"));
    if (!enter.get() || !leave.get()) throw std::runtime_error("Calendar cursor cache unavailable");
    const auto enterType = api.object_get_class(enter.get()), leaveType = api.object_get_class(leave.get());
    if (std::string(api.class_get_name(enterType)) != "Dictionary`2" || std::string(api.class_get_name(leaveType)) != "Dictionary`2")
        throw std::runtime_error("Calendar cursor cache type");
    const auto has = api.FindVerified(enterType, "ContainsKey", 1, 0x36EEAC0);
    const auto enterRemove = api.FindVerified(enterType, "Remove", 1, 0x36F0D60), leaveRemove = api.FindVerified(leaveType, "Remove", 1, 0x36F0D60);
    for (const auto element : Tree(root)) {
        if (!api.Value<bool>(api.Invoke(has, enter.get(), {element}))) continue;
        api.Invoke(n.removeCursor, nullptr, {element});
        api.Invoke(leaveRemove, leave.get(), {element});
        api.Invoke(enterRemove, enter.get(), {element});
    }
}
// The game's hand cursor on one of the mod's elements (released by Uncache).
void HandOn(Obj element) {
    int64_t hand = HandCursor;
    int normal = CursorNormal;
    api.Invoke(n.enableCursor, nullptr, {element, &hand, &normal});
}
// A new element holding a clone of one of the game's calendar templates. Its
// caller owns it before releasing it from the cursor caches (Uncache), so a
// failure from there on is still cleaned up.
Obj Clone(Method asset) {
    Ref settings(api.Invoke(n.settings, nullptr));
    if (!settings.get()) throw TemplateError("Calendar templates unavailable");
    Ref tree(api.Invoke(asset, settings.get()));
    if (!tree.get()) throw TemplateError("Calendar template unavailable");
    Ref target(NewElement());
    api.Invoke(n.cloneTree, tree.get(), {target.get()});
    return target.get();
}
// Named elements of a clone, found as UQuery finds them (the first in depth
// first order); a missing one refuses the template.
template<size_t N> std::array<Obj, N> Elements(Obj root, const std::array<const char*, N>& names) {
    std::array<Obj, N> found{};
    for (const auto element : Tree(root)) {
        const auto name = api.String(api.Call(element, "get_name"));
        for (size_t i = 0; i < N; ++i)
            if (!found[i] && name == names[i]) found[i] = element;
    }
    for (size_t i = 0; i < N; ++i)
        if (!found[i]) throw TemplateError(std::string("Calendar template: ") + names[i]);
    return found;
}
Obj Typed(Obj element, Klass klass) {
    if (api.object_get_class(element) != klass) throw TemplateError("Calendar template component");
    return element;
}

// The calendar's own kind of tooltip and clickable on the mod's elements.
void AttachTip(Ref& slot, Obj element, const std::string& text) {
    const auto service = state.tooltips.get();
    if (!service) throw std::runtime_error("Calendar tooltip service unavailable");
    Ref value(api.string_new(text.c_str()));
    slot.reset(api.object_new(n.tooltip));
    int origin = TipBottomCenter, source = TipTopCenter, behavior = TipLeave;
    bool front = false;
    api.Invoke(n.tooltipCtor, slot.get(), {service, value.get(), &origin, &source, &behavior, &front});
    api.Invoke(n.addManipulator, nullptr, {element, slot.get()});
}
void ReleaseTip(Ref& slot, Obj element) {
    if (!slot.handle) return;
    api.Invoke(n.hideTooltip, slot.get());
    api.Invoke(n.removeManipulator, nullptr, {element, slot.get()});
    slot.reset();
}
// A clickable without action (ExtendedClickable): its clicks reach CardClick
// through the shared native button hook (native-click.cpp).
void AttachClick(Ref& slot, Obj element) {
    slot.reset(api.object_new(n.clickable));
    Obj none = nullptr;
    api.Invoke(n.clickableCtor, slot.get(), {none});
    api.Invoke(n.addManipulator, nullptr, {element, slot.get()});
}
void ReleaseClick(Ref& slot, Obj element) {
    if (!slot.handle) return;
    api.Invoke(n.removeManipulator, nullptr, {element, slot.get()});
    slot.reset();
}

// The game's 7 day cards (CalendarUI.m_dayCards, in ctr_days): their days and
// their layout. Dated: 7 days in a row; Undated: all on one day (not dated
// yet); Bad: not that layout (never drawn on).
enum class Cards { Dated, Undated, Bad };
Cards ReadWeek(Obj owner, Obj days, std::array<int64_t, Days>& week, std::array<Rect, Days>& cards) {
    Ref list(api.Field(owner, "m_dayCards"));
    if (!list.get() || api.Value<int>(api.Call(list.get(), "get_Count")) != static_cast<int>(Days)) return Cards::Bad;
    bool row = true, same = true;
    for (int i = 0; i < static_cast<int>(Days); ++i) {
        Ref card(api.Call(list.get(), "get_Item", {&i}));
        if (!card.get() || api.object_get_class(card.get()) != n.dayCard || api.Call(card.get(), "get_parent") != days) return Cards::Bad;
        const auto ticks = api.Value<uint64_t>(api.Field(card.get(), "<dayDate>k__BackingField")) & TicksMask;
        week[static_cast<size_t>(i)] = static_cast<int64_t>(ticks / TicksPerDay) - UnixEpochDay;
        cards[static_cast<size_t>(i)] = api.Value<Rect>(api.Call(card.get(), "get_layout"));
        row = row && week[static_cast<size_t>(i)] == week[0] + i;
        same = same && week[static_cast<size_t>(i)] == week[0];
    }
    return row ? Cards::Dated : same ? Cards::Undated : Cards::Bad;
}
enum class Built { Done, Retry, Refused };
// The calendar as this module knows it: ctr_days (index 0) and gd_events
// (index 1) alone in one container, gd_calendarFilters in another; any other
// layout is refused with its code.
Built Resolve(Obj owner) {
    Ref binding(api.Field(owner, "dzyi"));
    if (!binding.get()) { LayoutRefused(1); return Built::Refused; }
    Ref days(api.Call(binding.get(), "get_ctr_days")), events(api.Call(binding.get(), "get_gd_events")),
        filterList(api.Call(binding.get(), "get_gd_calendarFilters"));
    if (!days.get() || !events.get() || !filterList.get()) { LayoutRefused(2); return Built::Refused; }
    Ref container(api.Call(days.get(), "get_parent")), filters(api.Call(filterList.get(), "get_parent"));
    if (!container.get() || api.Call(events.get(), "get_parent") != container.get() || !filters.get() || filters.get() == container.get()) {
        LayoutRefused(3);
        return Built::Refused;
    }
    if (Index(container.get(), days.get()) != 0 || Index(container.get(), events.get()) != 1) { LayoutRefused(4); return Built::Refused; }
    if (!api.Call(container.get(), "get_panel") || !api.Call(filters.get(), "get_panel")) return Built::Retry; // not shown yet: the next pump
    state = State{};
    state.owner.reset(owner);
    state.container = std::move(container); state.days = std::move(days); state.events = std::move(events);
    state.filters = std::move(filters); state.filterList = std::move(filterList);
    state.cached.reset(module->cached.get());
    state.compositor.reset(module->compositor.get());
    state.tooltips.reset(module->FindService("fou"));
    return Built::Done;
}
// The mod's elements in place in the window they were made for, in this session.
bool Current() {
    if (!Session() || state.cleanup || !state.owner.handle) return false;
    if (state.cached.get() != module->cached.get() || state.compositor.get() != module->compositor.get()) return false;
    if (state.block.handle) {
        const auto container = state.container.get();
        if (api.Call(state.block.get(), "get_parent") != container || !api.Call(state.block.get(), "get_panel") ||
            Index(container, state.days.get()) != 0 || Index(container, state.block.get()) != 1 || Index(container, state.events.get()) != 2)
            return false;
    }
    if (state.filter.handle) {
        const auto filters = state.filters.get();
        if (api.Call(state.filter.get(), "get_parent") != filters || !api.Call(state.filter.get(), "get_panel")) return false;
        const int index = Index(filters, state.filter.get());
        if (index < 0 || Index(filters, state.filterList.get()) != index + 1) return false;
    }
    return true;
}

// The « Kralamoure » entry: the game's filter row (calendarFilterItem) with its
// checkbox, the Kralamoure icon and colour, right above the game's calendars.
void CreateFilter() {
    if (api.Call(state.filterList.get(), "get_parent") != state.filters.get()) throw TemplateError("Calendar filters moved");
    state.filter.reset(Clone(n.filterAsset)); // owned before anything else
    const auto row = state.filter.get();
    Uncache(row);
    Text(row, "dofusEnhancedKralamoure", "set_name");
    Length(row, "height", FilterHeight);
    Float(row, "flexShrink", 0);
    const auto parts = Elements<3>(row, {"chbx_filterName", "tx_drag", "colorSquare"});
    state.checkbox.reset(Typed(parts[0], n.checkbox));
    const auto checkbox = state.checkbox.get();
    Text(checkbox, "Kralamoure", "set_rightText");
    int icon = KralamoureIcon;
    api.Call(checkbox, "set_iconValue", {&icon});
    bool value = Enabled();
    api.Call(checkbox, "SetValueWithoutNotify", {&value});
    state.filterValue = value;
    Enum(Typed(parts[1], n.icon), "display", DisplayNone); // as the game's own entries: not reordered
    const auto square = Typed(parts[2], n.square);
    bool selectable = false; // no colour picker: the colour is the mod's
    api.Call(square, "set_selectable", {&selectable});
    Ref hex(api.string_new(KralamoureHex));
    api.Invoke(n.setColor, square, {hex.get()});
    HandOn(checkbox); // the hand of the game's own entries, over the checkbox only
    AttachTip(state.filterTip, row, FilterTooltip);
    int index = Index(state.filters.get(), state.filterList.get());
    api.Call(state.filters.get(), "Insert", {&index, row});
    Log("CALENDAR_KRALOVE_FILTER");
}
void RemoveFilter() {
    if (const auto row = state.filter.get()) {
        ReleaseTip(state.filterTip, row);
        Uncache(row);
        Detach(row);
    }
    state.filter.reset();
    state.checkbox.reset();
}
// The checkbox shows the option, as the settings hold it.
void ShowOption(bool option) {
    bool value = option;
    api.Call(state.checkbox.get(), "SetValueWithoutNotify", {&value});
    if (option && !state.filterValue) { state.refused = false; state.waits = 0; } // checked again: the lines may be placed again
    state.filterValue = option;
}
// The entry's checkbox is the option: a click on it changes the option (and
// the options tab), the options tab or the shared settings file change it.
// Module::Change sets the settings before it refreshes, syncs and saves: after
// a failure, the checkbox shows what the settings now hold, whichever step failed.
void PollFilter() {
    const bool option = Enabled();
    if (option != state.filterValue) { ShowOption(option); return; }
    const bool value = api.Value<bool>(api.Call(state.checkbox.get(), "get_value"));
    if (value == option) return;
    auto next = module->settings;
    next.calendarKralamoure = value;
    try {
        module->Change(std::move(next));
        Log(std::string("CALENDAR_KRALOVE_TOGGLE ") + (value ? "on" : "off"));
    } catch (const std::exception& error) {
        Log("CALENDAR_KRALOVE_TOGGLE_ERROR " + Short(error.what()));
    } catch (...) {
        Log("CALENDAR_KRALOVE_TOGGLE_ERROR");
    }
    ShowOption(Enabled());
}

// A line: the game's calendarLine, its banner tinted and with the Kralamoure
// icon; the following lines hide their banner, as the game's additional lines.
void AddLine() {
    const bool first = state.lines.empty();
    state.lines.push_back(Line{});
    auto& line = state.lines.back();
    line.root.reset(Clone(n.lineAsset)); // owned before anything else
    const auto root = line.root.get();
    Uncache(root);
    Float(root, "flexShrink", 0);
    const auto parts = Elements<5>(root, {"calendarLine", "ctr_evenType", "tx_borderBanner", "tx_eventType", "ctr_events"});
    line.line.reset(parts[0]); line.banner.reset(parts[1]); line.border.reset(parts[2]);
    line.icon.reset(Typed(parts[3], n.icon)); line.events.reset(parts[4]);
    Paint(line.border.get(), "unityBackgroundImageTintColor", KralamoureColor);
    int icon = KralamoureIcon;
    api.Call(line.icon.get(), "set_icon", {&icon});
    Length(line.line.get(), "height", LineHeight);
    if (first) {
        // The first line says why it shows no opening.
        Enum(line.events.get(), "alignItems", AlignCenter);
        Ref status(NewLabel(line.events.get(), "italicShort_medium", "textColor_white_white80"));
        Enum(status.get(), "whiteSpace", WrapText);
        Enum(status.get(), "display", DisplayNone);
        state.status = std::move(status);
    } else {
        bool visible = false;
        api.Call(line.banner.get(), "set_visible", {&visible});
    }
    Add(state.block.get(), root);
}
void DropLine() {
    if (const auto root = state.lines.back().root.get()) {
        Uncache(root);
        Detach(root);
    }
    state.lines.pop_back();
}
void Lines(size_t count) {
    while (state.lines.size() < count) AddLine();
    while (state.lines.size() > count && state.lines.size() > 1) DropLine();
}
// The block of lines, built with its first line before it is placed, at index 1
// of the container of ctr_days and gd_events; only once the day cards are dated.
Built CreateBlock() {
    std::array<int64_t, Days> week{};
    std::array<Rect, Days> cards{};
    const auto dated = ReadWeek(state.owner.get(), state.days.get(), week, cards);
    if (dated == Cards::Undated && ++state.waits <= 25) return Built::Retry; // dated within 5 s, or refused
    if (dated != Cards::Dated) { LayoutRefused(5); return Built::Refused; }
    const auto container = state.container.get();
    if (Index(container, state.days.get()) != 0 || Index(container, state.events.get()) != 1) { LayoutRefused(4); return Built::Refused; }
    Ref block(NewElement());
    Text(block.get(), "dofusEnhancedKralamoureLines", "set_name");
    Float(block.get(), "flexShrink", 0);
    state.block = std::move(block); // owned before it is placed
    for (size_t i = 0; i < Days; ++i) { state.placed[2 * i] = CardStep * static_cast<float>(i); state.placed[2 * i + 1] = CardWidth; }
    state.drawn = false;
    state.plan = Plan{};
    AddLine();
    int index = 1;
    api.Call(container, "Insert", {&index, state.block.get()});
    Log("CALENDAR_KRALOVE_LINES");
    return Built::Done;
}
void ClearCards() {
    while (!state.cards.empty()) {
        auto& card = state.cards.back();
        if (const auto root = card.root.get()) {
            ReleaseTip(card.tip, root);
            ReleaseClick(card.click, root);
            Uncache(root);
            Detach(root);
        }
        state.cards.pop_back();
    }
}
void RemoveBlock() {
    ClearCards();
    if (!state.lines.empty()) ReleaseTip(state.bannerTip, state.lines.front().banner.get());
    while (!state.lines.empty()) DropLine();
    if (state.block.handle) Detach(state.block.get());
    state.block.reset(); state.status.reset(); state.bannerTip.reset();
    state.bannerText.clear();
    state.plan = Plan{};
    state.drawn = false;
}

// A card: the game's calendarCard, small and starting on its day, with the
// Kralamoure icon and colour (grey once past); no rewards, offering nor
// buttons; the game's hand cursor over it, as over its own cards.
void AddCard(const CardPlan& spec) {
    state.cards.push_back(Card{});
    auto& owned = state.cards.back();
    owned.day = spec.day; owned.id = spec.id; owned.more = spec.more;
    owned.root.reset(Clone(n.cardAsset)); // owned before anything else
    const auto root = owned.root.get();
    Uncache(root); // its buttons registered hand cursors: none of them is shown
    // As the CalendarCard constructor: the card's class, absolutely placed on its day.
    Class(root, "calendarCard");
    Enum(root, "position", Absolute);
    Length(root, "left", state.placed[2 * spec.day]);
    Length(root, "width", state.placed[2 * spec.day + 1]);
    const auto parts = Elements<10>(root, {"ctr_calendarCard", "colorLine", "tx_icon", "lbl_title", "ctr_duration", "lbl_start",
        "lbl_end", "ctr_rewards", "tx_offering", "ctr_buttons"});
    const auto card = parts[0];
    Text(card, "almanax", "RemoveFromClassList"); // the template's sample; « small start » are the card's own
    const Color color = spec.past ? PastColor : KralamoureColor;
    Paint(parts[1], "backgroundColor", color);
    Paint(card, "backgroundColor", spec.past ? PastColor : Color{color.r, color.g, color.b, CardAlpha});
    int icon = KralamoureIcon;
    api.Call(Typed(parts[2], n.icon), "set_icon", {&icon});
    for (const auto& [element, text] : {std::pair{parts[3], &spec.title}, std::pair{parts[5], &spec.start}, std::pair{parts[6], &spec.end}}) {
        Typed(element, n.label);
        bool rich = false;
        api.Call(element, "set_enableRichText", {&rich});
        Text(element, *text);
    }
    // Start then registrants, one under the other, centred as the small card's.
    Enum(parts[4], "flexDirection", Column);
    Enum(parts[4], "alignItems", AlignCenter);
    if (spec.end.empty()) Enum(parts[6], "display", DisplayNone);
    for (size_t i : {7u, 8u, 9u}) Enum(parts[i], "display", DisplayNone);
    if (spec.past) Float(root, "opacity", PastOpacity);
    Add(state.lines[spec.line].events.get(), root);
    AttachTip(owned.tip, root, spec.tip);
    AttachClick(owned.click, root);
    HandOn(root);
}
void BannerTip(const std::string& server) {
    const auto text = BannerTooltip(server);
    if (text == state.bannerText && state.bannerTip.handle) return;
    const auto banner = state.lines.front().banner.get();
    ReleaseTip(state.bannerTip, banner);
    AttachTip(state.bannerTip, banner, text);
    state.bannerText = text;
}
// The week as planned: the cards of the shown days, otherwise why there is none.
void Render(const Plan& plan, metamob::KraloveStatus reading) {
    ClearCards();
    state.drawn = false;
    Lines(plan.lines);
    for (auto& line : state.lines) Length(line.line.get(), "height", plan.status.empty() ? LineHeight : StatusHeight);
    Text(state.status.get(), plan.status);
    Enum(state.status.get(), "display", plan.status.empty() ? DisplayNone : DisplayFlex);
    for (const auto& card : plan.cards) AddCard(card);
    BannerTip(plan.server);
    state.plan = plan;
    state.drawn = true;
    Log("CALENDAR_KRALOVE_WEEK openings=" + std::to_string(plan.shown) + " lines=" + std::to_string(state.lines.size()) +
        " state=" + std::to_string(static_cast<int>(reading)));
}
// The cards on the day cards' own layout (ctr_days and the block share their
// container). Until the block and its first line are laid out (a new block,
// a new line), the game's own places stay.
void Align(const std::array<Rect, Days>& cards) {
    for (const auto& card : cards)
        if (!std::isfinite(card.x) || !std::isfinite(card.width) || card.width < 16 || card.width > 1024 || std::abs(card.x) > 8192) return;
    const auto& line = state.lines.front();
    float origin = 0;
    for (const auto element : {state.block.get(), line.root.get(), line.line.get(), line.events.get()}) {
        const auto layout = api.Value<Rect>(api.Call(element, "get_layout"));
        if (!std::isfinite(layout.x) || !std::isfinite(layout.width) || layout.width <= 0 || std::abs(layout.x) > 8192) return;
        origin += layout.x;
    }
    const auto days = api.Value<Rect>(api.Call(state.days.get(), "get_layout"));
    if (!std::isfinite(days.x) || !std::isfinite(days.width) || days.width <= 0 || std::abs(days.x) > 8192) return;
    const float shift = days.x - origin;
    for (size_t i = 0; i < Days; ++i) {
        const float left = cards[i].x + shift, width = cards[i].width;
        if (std::abs(left - state.placed[2 * i]) < .5f && std::abs(width - state.placed[2 * i + 1]) < .5f) continue;
        state.placed[2 * i] = left; state.placed[2 * i + 1] = width;
        for (const auto& card : state.cards)
            if (card.day == i) { Length(card.root.get(), "left", left); Length(card.root.get(), "width", width); }
    }
}
// Each pump while shown: the week shown (the game's arrows change the cards'
// days), the cards' layout, the openings read, and the time (greying).
void Update(const metamob::KraloveView& view) {
    std::array<int64_t, Days> week{};
    std::array<Rect, Days> cards{};
    if (ReadWeek(state.owner.get(), state.days.get(), week, cards) != Cards::Dated) {
        LayoutRefused(6);
        RestoreCalendarKralove();
        return;
    }
    auto plan = MakePlan(view, week, clockNow());
    if (!state.drawn || !(plan == state.plan)) Render(plan, view.status);
    Align(cards);
}
void EnableHook(Obj owner, Method method) {
    originalEnable(owner, method);
    try {
        if (installed && owner && Session()) {
            RestoreCalendarKralove();
            if (!state.cleanup) pending.reset(owner);
        }
    } catch (const std::exception& error) { Log("CALENDAR_KRALOVE_ENABLE_ERROR " + Short(error.what())); }
    catch (...) { Log("CALENDAR_KRALOVE_ENABLE_ERROR"); }
}
void DisableHook(Obj owner, Method method) {
    try {
        if (owner && ((pending.handle && pending.get() == owner) || (state.owner.handle && state.owner.get() == owner))) RestoreCalendarKralove();
    } catch (const std::exception& error) { Log("CALENDAR_KRALOVE_DISABLE_ERROR " + Short(error.what())); }
    catch (...) { Log("CALENDAR_KRALOVE_DISABLE_ERROR"); }
    originalDisable(owner, method);
}
// A click on one of the mod's cards (native-click.cpp): the event's page in
// the browser, or Metamob's list of openings for a « +N » card. Any other
// element keeps the game's own handling.
bool CardClick(Obj target) noexcept {
    try {
        if (!target || state.cards.empty()) return false;
        const auto card = std::find_if(state.cards.begin(), state.cards.end(), [&](const Card& item) { return item.root.get() == target; });
        if (card == state.cards.end()) return false;
        if (!Enabled() || !Current()) return true;
        const auto link = card->id > 0 ? EventLink(card->id) : card->more ? std::string(KralovePage) : std::string();
        if (link.empty()) return true;
        Ref url(api.string_new(link.c_str()));
        api.Invoke(n.openUrl, nullptr, {url.get()});
        Log("CALENDAR_KRALOVE_OPEN");
    } catch (const std::exception& error) { Log("CALENDAR_KRALOVE_OPEN_ERROR " + Short(error.what())); }
    catch (...) { Log("CALENDAR_KRALOVE_OPEN_ERROR"); }
    return true;
}
void RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Calendar field layout: ") + name);
}
}

void InstallCalendarKralove() {
    if (installed) return;
    try {
        const auto calendar = api.Class("Core.dll", "Core.UILogic.Calendar", "CalendarUI");
        const auto binding = api.Class("Core.dll", "Core.UILogic.Bindings", "CalendarUIBinding");
        n.dayCard = api.Class("Core.dll", "Core.UILogic.Calendar.CalendarItem", "CalendarDayCard");
        RequireField(calendar, "m_dayCards", 0x210);
        RequireField(n.dayCard, "<dayDate>k__BackingField", 0x2F8);
        // The binding struct of the generic window base gob<CalendarUIBinding>:
        // the generic definition's metadata has no offset; CalendarUI.OnEnable
        // and OnDisable pass this + 0x10 to its getters.
        if (api.field_get_offset(api.FieldInfo(calendar, "dzyi")) != 0x10) throw std::runtime_error("Calendar binding layout");
        api.FindVerified(binding, "get_ctr_days", 0, 0x2E9E140);
        api.FindVerified(binding, "get_gd_events", 0, 0x2E9E630);
        api.FindVerified(binding, "get_gd_calendarFilters", 0, 0x2E9E580);
        // The calendar's own templates, as CalendarUI.MakeItem, MakeFilterItem and the CalendarCard constructor clone them.
        const auto settings = api.Class("Core.dll", "Core.UILogic.Components", "DofusUISettings");
        n.settings = api.FindVerified(settings, "GetSettings", 0, 0x1597FD0);
        n.lineAsset = api.FindVerified(settings, "get_calendarLine", 0, 0x1599AB0);
        n.cardAsset = api.FindVerified(settings, "get_calendarCard", 0, 0x15999F0);
        n.filterAsset = api.FindVerified(settings, "get_calendarFilterItem", 0, 0x1599A70);
        n.cloneTree = api.FindVerified(api.Class(Ui, UiSpace, "VisualTreeAsset"), "CloneTree", 1, 0x567E440);
        n.element = api.Class(Ui, UiSpace, "VisualElement");
        api.FindVerified(n.element, ".ctor", 0, 0x55F50B0);
        api.FindVerified(n.element, "Add", 1, 0x55DFAD0);
        api.FindVerified(n.element, "Insert", 2, 0x55E5710);
        api.FindVerified(n.element, "IndexOf", 1, 0x55E55B0);
        api.FindVerified(n.element, "get_parent", 0, 0x55F7180);
        api.FindVerified(n.element, "get_panel", 0, 0x55F7170);
        api.FindVerified(n.element, "get_layout", 0, 0x55F6930);
        api.FindVerified(n.element, "RemoveFromHierarchy", 0, 0x55E7BD0);
        api.FindVerified(n.element, "get_style", 0, 0x55F74A0);
        api.FindVerified(n.element, "AddToClassList", 1, 0x55DF820);
        api.FindVerified(n.element, "RemoveFromClassList", 1, 0x55E7AB0);
        api.FindVerified(n.element, "get_childCount", 0, 0x55F5970);
        api.FindVerified(n.element, "ElementAt", 1, 0x55E2DE0);
        api.FindVerified(n.element, "get_name", 0, 0x3158710);
        api.FindVerified(n.element, "set_name", 1, 0x55F9670);
        api.FindVerified(n.element, "set_visible", 1, 0x55FA170);
        const auto length = api.Class(Ui, UiSpace, "Length");
        api.FindVerified(length, ".ctor", 2, 0x561D3D0);
        const auto style = api.Class(Ui, UiSpace, "InlineStyleAccess");
        for (const auto& [property, get, set] : std::array<std::tuple<const char*, uintptr_t, uintptr_t>, 12>{{
            {"position", 0x55D1A30, 0x55D48B0}, {"left", 0x55D1610, 0x55D3EB0}, {"width", 0x55D2740, 0x55D63F0},
            {"height", 0x55D1540, 0x55D3D60}, {"flexShrink", 0x55D1460, 0x55D3BC0}, {"opacity", 0x55D1890, 0x55D44B0},
            {"display", 0x55D1270, 0x55D37F0}, {"alignItems", 0x55D0C60, 0x55D2870}, {"flexDirection", 0x55D13C0, 0x55D3A80},
            {"whiteSpace", 0x55D26B0, 0x55D6370}, {"backgroundColor", 0x55D0D90, 0x55D2BA0},
            {"unityBackgroundImageTintColor", 0x55D1FC0, 0x55D5530}}}) {
            api.FindVerified(style, (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str(), 0, get);
            api.FindVerified(style, (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), 1, set);
        }
        n.icon = api.Class("Core.dll", Figma, "DofusIcon");
        api.FindVerified(n.icon, "set_icon", 1, 0x170F480);
        n.label = api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel");
        api.FindVerified(n.label, ".ctor", 0, 0x1593940);
        api.FindVerified(n.label, "set_text", 1, 0x1594570);
        api.FindVerified(api.Class(Ui, UiSpace, "TextElement"), "set_enableRichText", 1, 0x564B1E0);
        n.checkbox = api.Class("Core.dll", Figma, "CheckboxCustom");
        api.FindVerified(n.checkbox, "get_value", 0, 0x1705050);
        api.FindVerified(n.checkbox, "SetValueWithoutNotify", 1, 0x17044F0);
        api.FindVerified(n.checkbox, "set_rightText", 1, 0x1705590);
        api.FindVerified(n.checkbox, "set_iconValue", 1, 0x1705240);
        n.square = api.Class("Core.dll", Figma, "ColorSquare");
        api.FindVerified(n.square, "set_selectable", 1, 0x17094E0);
        n.setColor = api.FindVerified(n.square, "SetColor", 1, 0x1708AF0); // SetColor(string), not SetColor(Color)
        const auto tooltips = "Core.UILogic.Components.Tooltips";
        n.tooltip = api.Class("Core.dll", tooltips, "TooltipManipulator");
        n.tooltipCtor = api.FindVerified(n.tooltip, ".ctor", 6, 0x15DFAE0);
        n.hideTooltip = api.FindVerified(api.Class("Core.dll", tooltips, "BaseTooltipManipulator"), "HideTooltip", 0, 0x15CD030);
        const auto extensions = api.Class(Ui, UiSpace, "VisualElementExtensions");
        n.addManipulator = api.FindVerified(extensions, "AddManipulator", 2, 0x56755F0);
        n.removeManipulator = api.FindVerified(extensions, "RemoveManipulator", 2, 0x5676A90);
        n.clickable = api.Class("Core.dll", Figma, "ExtendedClickable");
        n.clickableCtor = api.FindVerified(n.clickable, ".ctor", 1, 0x172B830); // ExtendedClickable(Action), not (Action<EventBase>)
        n.cursor = api.Class("Core.dll", "Core.UILogic", "DofusCursor");
        n.enableCursor = api.FindVerified(n.cursor, "EnableCursorHandler", 3, 0x2808CD0);
        n.removeCursor = api.FindVerified(n.cursor, "RemoveCursorHandler", 1, 0x2809CD0);
        RequireField(n.cursor, "s_mouseEnterCursorMap", 0x40);
        RequireField(n.cursor, "s_mouseLeaveCursorMap", 0x48);
        n.openUrl = api.FindVerified(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"), "OpenURL", 1, 0x53ACA50);
        const auto enable = api.FindVerified(calendar, "OnEnable", 0, 0x2E065E0);
        const auto disable = api.FindVerified(calendar, "OnDisable", 0, 0x2E06550);
        AddHook(enable, 0x2E065E0, reinterpret_cast<void*>(EnableHook), reinterpret_cast<void**>(&originalEnable));
        AddHook(disable, 0x2E06550, reinterpret_cast<void*>(DisableHook), reinterpret_cast<void**>(&originalDisable));
        nativeClick::AddHandler(CardClick);
        installed = true;
        Log("CALENDAR_KRALOVE_READY");
    } catch (const std::exception& error) {
        // Optional: the rest of the mod runs without it (the hooks pass through).
        Log(std::string("CALENDAR_KRALOVE_DISABLED ") + error.what());
    }
}
void TickCalendarKralove() {
    if (!installed) return;
    try {
        if (!Session() || state.cleanup) {
            if (state.cleanup || state.owner.handle || pending.handle) RestoreCalendarKralove();
            return;
        }
        if (state.owner.handle && !Current()) { RestoreCalendarKralove(); return; } // closed, moved, another session: until the next OnEnable
        if (!state.owner.handle) {
            if (!pending.handle) return; // the calendar is not open
            Ref owner(pending.get());
            const auto built = owner.get() ? Resolve(owner.get()) : Built::Refused;
            if (built == Built::Retry) return;
            pending.reset();
            if (built == Built::Refused) return;
        }
        const auto view = metamob::MetamobKralove();
        if (!view.usable) { RemoveBlock(); RemoveFilter(); return; } // no usable key: no entry, no line, no request
        if (!state.filter.handle) CreateFilter();
        PollFilter();
        if (!Enabled()) { if (state.block.handle) RemoveBlock(); return; } // unchecked: no line, no request
        if (!state.block.handle) {
            if (state.refused) return;
            const auto built = CreateBlock();
            if (built == Built::Retry) return;
            if (built == Built::Refused) { state.refused = true; return; }
        }
        metamob::WantKralove();
        Update(view);
    } catch (const TemplateError&) {
        LayoutRefused(7);
        RestoreCalendarKralove();
    } catch (const std::exception& error) {
        Log("CALENDAR_KRALOVE_ERROR " + Short(error.what()));
        RestoreCalendarKralove();
    } catch (...) {
        Log("CALENDAR_KRALOVE_ERROR");
        RestoreCalendarKralove();
    }
}
void RestoreCalendarKralove() {
    try {
        pending.reset();
        if (!state.block.handle && !state.filter.handle && state.cards.empty() && state.lines.empty() && !state.cleanup) {
            state = State{};
            return;
        }
        state.cleanup = true;
        RemoveBlock();
        RemoveFilter();
        state = State{};
    } catch (...) {
        // Retried at each pump: told once every 10 s at most.
        const auto now = GetTickCount64();
        if (now >= nextCleanupLog) { Log("CALENDAR_KRALOVE_CLEANUP_ERROR"); nextCleanupLog = now + 10000; }
    }
}
bool CalendarKraloveCleanupPending() { return state.cleanup; }
