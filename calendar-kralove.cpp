#include "calendar-kralove.hpp"
#include "metamob-data.hpp"
#include "metamob-sync.hpp"
#include "module.hpp"
#include "native-click.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// The calendar window (CalendarUI) shows the 7 day cards of a week (ctr_days)
// above the list of its events (gd_events), both alone in one container. The
// mod's own row goes between them, only once that layout is checked: a 64 px
// band whose « Kralamoure · Metamob » banner sits above the event lines' type
// banners and whose 7 cells sit on the day cards' own layout. A cell shows the
// day's openings as the game's buttons (« 21:00 · 12 persos », past ones
// dimmed, 3 at most); their tooltip tells the local date and time, the server,
// the organiser, the counts and the description, as plain text; their click
// opens the event's page on metamob.fr. Without openings the row says why. The
// openings come from metamob-sync, asked only while the row is shown (read at
// most every 10 min); nothing of the game's own elements is changed. Logs
// carry fixed codes and counts only: never an id, a name or a description.
namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* EventPage = "https://www.metamob.fr/kralove/event/";
constexpr size_t Days = 7, MaxChips = 3;
// Geometry (px) from the calendar's UXML until the day cards are laid out:
// cards 136 wide every 144 px after ctr_days' 72 px left padding, the event
// lines' 64 px type banner and its 8 px margin. Three 20 px openings in 64 px.
constexpr float BandHeight = 64, RowHeight = 72, BannerWidth = 64, CardLeft = 72, CardWidth = 136, CardStep = 144,
    ChipHeight = 20, ChipStep = 22, PastOpacity = .45f;
// DisplayStyle, Position, Align, Justify, WhiteSpace, LengthUnit and
// PickingMode values; DofusButtonCustom SizeEnum.small, ComponentStyleEnum.secondary.
constexpr int DisplayFlex = 0, DisplayNone = 1, Absolute = 1, AlignCenter = 2, JustifyCenter = 1, WrapText = 0, Pixel = 0,
    Percent = 1, PickIgnore = 1, ButtonSmall = 0, StyleSecondary = 1;
// A .NET DateTime: 100 ns ticks since 0001-01-01 in its 62 low bits (its Kind
// above them); 1970-01-01 is its day 719162.
constexpr int64_t TicksPerDay = 864000000000, UnixEpochDay = 719162;
constexpr uint64_t TicksMask = (uint64_t(1) << 62) - 1;
struct Rect { float x, y, width, height; };

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
std::string Clock(const metamob::CivilTime& time) { return Two(time.hour) + ":" + Two(time.minute); }
std::string Counted(int count, const char* one, const char* many) { return std::to_string(count) + " " + (count > 1 ? many : one); }
std::string ChipText(const Placed& placed) { return Clock(placed.local) + " · " + Counted(placed.opening->characters, "perso", "persos"); }
std::string LongDate(const metamob::CivilTime& time) {
    static constexpr const char* Weekdays[] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
    static constexpr const char* Months[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre",
        "octobre", "novembre", "décembre"};
    return std::string(Weekdays[static_cast<size_t>(time.weekday) % 7]) + " " + std::to_string(time.day) + " " +
        Months[static_cast<size_t>(time.month + 11) % 12] + " " + std::to_string(time.year) + " à " + Clock(time);
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
// first two, then « +N autres » telling the others.
std::string MoreText(size_t hidden) { return "+" + std::to_string(hidden) + (hidden > 1 ? " autres" : " autre"); }
std::string MoreTooltip(const std::vector<Placed>& day, size_t from) {
    std::string text = "Autres ouvertures Kralamoure ce jour-là :";
    for (size_t i = from; i < day.size(); ++i) text += "\n" + ChipText(day[i]);
    return text + "\nToutes les ouvertures : metamob.fr, rubrique Kralamoure.";
}
std::string BannerTooltip(const std::string& server) {
    return "Ouvertures Kralamoure planifiées sur Metamob pour " + (server.empty() ? std::string("le serveur de ta quête Ocre") : server) +
        ", à l’heure de ce PC. Survole une ouverture pour ses détails ; clique dessus pour l’ouvrir sur metamob.fr.";
}
std::string EmptyText(const std::string& server) {
    return "Aucune ouverture Kralamoure cette semaine sur " + (server.empty() ? std::string("ton serveur") : server) + ".";
}
constexpr const char* PastWeekText = "Metamob : les ouvertures des semaines passées ne sont pas lues.";
// The row's text while it shows no opening, by the state of the last reading.
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

// An opening's button (id: its event), or the « +N » label of a day (id 0).
struct Chip { Ref element; int64_t id = 0, at = 0; };
struct State {
    Ref owner, container, days, events, row, banner, statusBox, status, cached, compositor;
    std::array<Ref, Days> cells;
    std::vector<Chip> chips;
    std::array<int64_t, Days> week{};       // the day cards' days (metamob::DayNumber)
    std::array<float, Days * 2> placed{};   // each cell's left and width as last written
    uint64_t version = 0;                   // the KraloveView drawn
    size_t past = 0;                        // its openings already past when drawn (dimmed)
    bool drawn = false, cleanup = false;
} state;
Ref pending; // the calendar enabled, its row not placed yet
int waits = 0; // pumps its day cards were seen without their days (UpdateCalendarEvents dates them)
bool installed = false;
Klass dayCardClass = nullptr;
Method openUrl = nullptr;
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
void Length(Obj element, const char* property, float value, int unit = Pixel) {
    Ref length(api.New(api.Class(Ui, UiSpace, "Length"), {&value, &unit}));
    WriteStyle(element, property, api.object_unbox(length.get()));
}
// Absolutely placed in its parent (the row, or a cell).
void Place(Obj element, float left, float top, float width, float height, int widthUnit = Pixel) {
    Enum(element, "position", Absolute);
    Length(element, "left", left);
    Length(element, "top", top);
    Length(element, "width", width, widthUnit);
    Length(element, "height", height);
}
void Text(Obj element, const std::string& text, const char* setter = "set_text") {
    Ref value(api.string_new(text.c_str()));
    api.Call(element, setter, {value.get()});
}
// The game's text and colour classes (its font size option included).
void Class(Obj element, const char* name) { Text(element, name, "AddToClassList"); }
void Add(Obj parent, Obj child) { api.Call(parent, "Add", {child}); }
int Index(Obj parent, Obj child) { return api.Value<int>(api.Call(parent, "IndexOf", {child})); }
Obj NewElement() { return api.New(api.Class(Ui, UiSpace, "VisualElement")); }
// The game's label, never rich text; fontSize 0 keeps its class's size. A
// label that is not picked leaves the hover (and its tooltip) to its parent.
Obj NewLabel(Obj parent, const std::string& text, const char* textClass, float fontSize, bool picked = true) {
    Ref label(api.New(api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel")));
    bool rich = false;
    api.Call(label.get(), "set_enableRichText", {&rich});
    if (!picked) { int ignore = PickIgnore; api.Call(label.get(), "set_pickingMode", {&ignore}); }
    Class(label.get(), textClass);
    if (fontSize > 0) Length(label.get(), "fontSize", fontSize);
    Text(label.get(), text);
    Add(parent, label.get());
    return label.get();
}
void LayoutRefused(int code) { Log("CALENDAR_KRALOVE_LAYOUT code=" + std::to_string(code)); }
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
        if (!card.get() || api.object_get_class(card.get()) != dayCardClass || api.Call(card.get(), "get_parent") != days) return Cards::Bad;
        const auto ticks = api.Value<uint64_t>(api.Field(card.get(), "<dayDate>k__BackingField")) & TicksMask;
        week[static_cast<size_t>(i)] = static_cast<int64_t>(ticks / TicksPerDay) - UnixEpochDay;
        cards[static_cast<size_t>(i)] = api.Value<Rect>(api.Call(card.get(), "get_layout"));
        row = row && week[static_cast<size_t>(i)] == week[0] + i;
        same = same && week[static_cast<size_t>(i)] == week[0];
    }
    return row ? Cards::Dated : same ? Cards::Undated : Cards::Bad;
}
enum class Built { Done, Retry, Refused };
// The row, built whole before it is placed (a failure never leaves half of it
// in the window), at index 1 of the container of ctr_days (index 0) and
// gd_events (index 1); any other layout is refused with its code.
Built Create(Obj owner) {
    Ref binding(api.Field(owner, "dzyi"));
    if (!binding.get()) { LayoutRefused(1); return Built::Refused; }
    Ref days(api.Call(binding.get(), "get_ctr_days")), events(api.Call(binding.get(), "get_gd_events"));
    if (!days.get() || !events.get()) { LayoutRefused(2); return Built::Refused; }
    Ref container(api.Call(days.get(), "get_parent"));
    if (!container.get() || api.Call(events.get(), "get_parent") != container.get()) { LayoutRefused(3); return Built::Refused; }
    if (Index(container.get(), days.get()) != 0 || Index(container.get(), events.get()) != 1) { LayoutRefused(4); return Built::Refused; }
    if (!api.Call(container.get(), "get_panel")) return Built::Retry; // not shown yet: the next pump
    std::array<int64_t, Days> week{};
    std::array<Rect, Days> cards{};
    const auto dated = ReadWeek(owner, days.get(), week, cards);
    if (dated == Cards::Undated && ++waits <= 25) return Built::Retry; // dated within 5 s, or refused
    if (dated != Cards::Dated) { LayoutRefused(5); return Built::Refused; }
    Ref row(NewElement());
    Length(row.get(), "height", RowHeight);
    Float(row.get(), "flexShrink", 0);
    Ref banner(NewElement());
    Place(banner.get(), 0, 0, BannerWidth, BandHeight);
    Enum(banner.get(), "alignItems", AlignCenter);
    Enum(banner.get(), "justifyContent", JustifyCenter);
    Class(banner.get(), "backgroundColor_black_black25");
    Text(banner.get(), BannerTooltip({}), "set_tooltip");
    Ref title(NewLabel(banner.get(), "Kralamoure", "textShort_smallBold", 11, false));
    Class(title.get(), "textColor_white_white100");
    Ref source(NewLabel(banner.get(), "Metamob", "italicShort_small", 10, false));
    Float(source.get(), "opacity", .75f);
    Add(row.get(), banner.get());
    std::array<Ref, Days> cells;
    std::array<float, Days * 2> placed{};
    for (size_t i = 0; i < Days; ++i) {
        cells[i].reset(NewElement());
        placed[2 * i] = CardLeft + CardStep * static_cast<float>(i);
        placed[2 * i + 1] = CardWidth;
        Place(cells[i].get(), placed[2 * i], 0, placed[2 * i + 1], BandHeight);
        Add(row.get(), cells[i].get());
    }
    Ref statusBox(NewElement());
    Place(statusBox.get(), CardLeft, 0, CardStep * static_cast<float>(Days - 1) + CardWidth, BandHeight);
    Enum(statusBox.get(), "justifyContent", JustifyCenter);
    Enum(statusBox.get(), "display", DisplayNone);
    Ref status(NewLabel(statusBox.get(), {}, "italicShort_small", 0, false));
    Enum(status.get(), "whiteSpace", WrapText);
    Class(status.get(), "textColor_white_white80");
    Add(row.get(), statusBox.get());
    // Owned before it is placed: an Insert that fails half-way is still undone.
    state = State{};
    state.owner.reset(owner);
    state.container = std::move(container); state.days = std::move(days); state.events = std::move(events);
    state.row = std::move(row); state.banner = std::move(banner); state.statusBox = std::move(statusBox); state.status = std::move(status);
    state.cells = std::move(cells);
    state.placed = placed;
    state.cached.reset(module->cached.get());
    state.compositor.reset(module->compositor.get());
    int index = 1;
    api.Call(state.container.get(), "Insert", {&index, state.row.get()});
    Log("CALENDAR_KRALOVE_ROW");
    return Built::Done;
}
// The row in place in the window it was made for, in this session.
bool Current() {
    if (!Session() || !state.row.get() || state.cleanup) return false;
    if (state.cached.get() != module->cached.get() || state.compositor.get() != module->compositor.get()) return false;
    const auto container = state.container.get();
    return api.Call(state.row.get(), "get_parent") == container && api.Call(state.row.get(), "get_panel") &&
        Index(container, state.days.get()) == 0 && Index(container, state.row.get()) == 1 && Index(container, state.events.get()) == 2;
}
// The cells on the day cards' own layout (ctr_days and the row share their
// container); before the cards are laid out, the UXML's places stay.
void Align(const std::array<Rect, Days>& cards) {
    for (const auto& card : cards)
        if (!std::isfinite(card.x) || !std::isfinite(card.width) || card.width < 16 || card.width > 1024 || std::abs(card.x) > 8192) return;
    const auto days = api.Value<Rect>(api.Call(state.days.get(), "get_layout"));
    const auto row = api.Value<Rect>(api.Call(state.row.get(), "get_layout"));
    const float shift = std::isfinite(days.x) && std::isfinite(row.x) && std::abs(days.x - row.x) < 8192 ? days.x - row.x : 0.f;
    bool moved = false;
    for (size_t i = 0; i < Days; ++i) {
        const float left = cards[i].x + shift, width = cards[i].width;
        if (std::abs(left - state.placed[2 * i]) < .5f && std::abs(width - state.placed[2 * i + 1]) < .5f) continue;
        Length(state.cells[i].get(), "left", left);
        Length(state.cells[i].get(), "width", width);
        state.placed[2 * i] = left; state.placed[2 * i + 1] = width;
        moved = true;
    }
    if (!moved) return;
    Length(state.statusBox.get(), "left", state.placed[0]);
    Length(state.statusBox.get(), "width", state.placed[2 * Days - 2] + state.placed[2 * Days - 1] - state.placed[0]);
}
void ClearChips() {
    for (auto& chip : state.chips)
        if (chip.element.get()) api.Call(chip.element.get(), "RemoveFromHierarchy");
    state.chips.clear();
}
size_t PastCount(int64_t now) {
    return static_cast<size_t>(std::count_if(state.chips.begin(), state.chips.end(), [&](const Chip& chip) { return chip.id && chip.at <= now; }));
}
void AddChip(size_t day, size_t slot, const Placed& placed, const std::string& server, int64_t now) {
    Ref button(api.New(api.Class("Core.dll", Figma, "DofusButtonCustom")));
    int size = ButtonSmall, style = StyleSecondary;
    api.Call(button.get(), "set_size", {&size});
    api.Call(button.get(), "set_mainStyle", {&style});
    const bool past = placed.opening->at <= now;
    Text(button.get(), ChipText(placed));
    Text(button.get(), Tooltip(placed, server, past), "set_tooltip");
    Place(button.get(), 0, ChipStep * static_cast<float>(slot), 100, ChipHeight, Percent);
    Length(button.get(), "minHeight", ChipHeight);
    if (past) Float(button.get(), "opacity", PastOpacity);
    Add(state.cells[day].get(), button.get());
    state.chips.push_back({Ref(button.get()), placed.opening->id, placed.opening->at});
}
void AddMore(size_t day, const std::vector<Placed>& openings, size_t from) {
    Ref more(NewLabel(state.cells[day].get(), MoreText(openings.size() - from), "italicShort_small", 0));
    Place(more.get(), 0, ChipStep * static_cast<float>(from), 100, ChipHeight, Percent);
    Text(more.get(), MoreTooltip(openings, from), "set_tooltip");
    state.chips.push_back({Ref(more.get()), 0, 0});
}
// The week as Metamob's last reading tells it: the openings of the shown days
// when that week was read, otherwise why there is none.
void Render(const metamob::KraloveView& view, const std::array<int64_t, Days>& week, int64_t now) {
    ClearChips();
    state.version = view.version; state.week = week; state.drawn = true;
    std::string status;
    size_t shown = 0;
    if (!view.read) status = StatusText(view.status);
    else if (week[0] <= view.fromDay) status = PastWeekText; // a day before the first one asked is shown
    else {
        const auto plan = Bucket(view.openings, week, localTime);
        for (size_t day = 0; day < Days; ++day) {
            const auto& openings = plan[day];
            const size_t buttons = openings.size() > MaxChips ? MaxChips - 1 : openings.size();
            for (size_t slot = 0; slot < buttons; ++slot) AddChip(day, slot, openings[slot], view.server, now);
            if (openings.size() > MaxChips) AddMore(day, openings, buttons);
            shown += openings.size();
        }
        if (!shown) status = EmptyText(view.server);
    }
    Text(state.status.get(), status);
    Enum(state.statusBox.get(), "display", status.empty() ? DisplayNone : DisplayFlex);
    Text(state.banner.get(), BannerTooltip(view.read ? view.server : std::string()), "set_tooltip");
    state.past = PastCount(now);
    Log("CALENDAR_KRALOVE_WEEK openings=" + std::to_string(shown) + " state=" + std::to_string(static_cast<int>(view.status)));
}
// Each pump while shown: the week shown (the game's arrows change the cards'
// days), the cards' layout, the openings read, and the time (dimming).
void Update(const metamob::KraloveView& view) {
    std::array<int64_t, Days> week{};
    std::array<Rect, Days> cards{};
    if (ReadWeek(state.owner.get(), state.days.get(), week, cards) != Cards::Dated) {
        LayoutRefused(6);
        RestoreCalendarKralove();
        return;
    }
    Align(cards);
    const auto now = clockNow();
    if (!state.drawn || view.version != state.version || week != state.week || PastCount(now) != state.past) Render(view, week, now);
}
// No usable key any more: the row goes, the open calendar waits for one.
void Withdraw() {
    Ref owner(state.owner.get());
    RestoreCalendarKralove();
    if (!state.cleanup && owner.get()) pending = std::move(owner);
}
void EnableHook(Obj owner, Method method) {
    originalEnable(owner, method);
    try {
        if (installed && owner && Session() && Enabled()) {
            RestoreCalendarKralove();
            waits = 0;
            if (!state.cleanup) pending.reset(owner);
        }
    } catch (...) { Log("CALENDAR_KRALOVE_ENABLE_ERROR"); }
}
void DisableHook(Obj owner, Method method) {
    try {
        if (owner && ((pending.handle && pending.get() == owner) || (state.owner.handle && state.owner.get() == owner))) RestoreCalendarKralove();
    } catch (...) { Log("CALENDAR_KRALOVE_DISABLE_ERROR"); }
    originalDisable(owner, method);
}
// The game's click on one of the row's buttons (native-click.cpp): the event's
// page in the browser. Any other button keeps the game's own handling.
bool ChipClick(Obj button) noexcept {
    try {
        if (!button || state.chips.empty()) return false;
        const auto chip = std::find_if(state.chips.begin(), state.chips.end(), [&](const Chip& item) { return item.element.get() == button; });
        if (chip == state.chips.end()) return false;
        if (!Enabled() || !Current()) return true;
        const auto link = EventLink(chip->id);
        if (link.empty()) return true;
        Ref url(api.string_new(link.c_str()));
        api.Invoke(openUrl, nullptr, {url.get()});
        Log("CALENDAR_KRALOVE_OPEN");
    } catch (...) { Log("CALENDAR_KRALOVE_OPEN_ERROR"); }
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
        dayCardClass = api.Class("Core.dll", "Core.UILogic.Calendar.CalendarItem", "CalendarDayCard");
        RequireField(calendar, "m_dayCards", 0x210);
        RequireField(dayCardClass, "<dayDate>k__BackingField", 0x2F8);
        // The binding struct of the generic window base gob<CalendarUIBinding>:
        // the generic definition's metadata has no offset; CalendarUI.OnEnable
        // and OnDisable pass this + 0x10 to its getters.
        if (api.field_get_offset(api.FieldInfo(calendar, "dzyi")) != 0x10) throw std::runtime_error("Calendar binding layout");
        api.FindVerified(binding, "get_ctr_days", 0, 0x2E9E140);
        api.FindVerified(binding, "get_gd_events", 0, 0x2E9E630);
        const auto element = api.Class(Ui, UiSpace, "VisualElement");
        api.FindVerified(element, ".ctor", 0, 0x55F50B0);
        api.FindVerified(element, "Add", 1, 0x55DFAD0);
        api.FindVerified(element, "Insert", 2, 0x55E5710);
        api.FindVerified(element, "IndexOf", 1, 0x55E55B0);
        api.FindVerified(element, "get_parent", 0, 0x55F7180);
        api.FindVerified(element, "get_panel", 0, 0x55F7170);
        api.FindVerified(element, "get_layout", 0, 0x55F6930);
        api.FindVerified(element, "RemoveFromHierarchy", 0, 0x55E7BD0);
        api.FindVerified(element, "get_style", 0, 0x55F74A0);
        api.FindVerified(element, "set_tooltip", 1, 0x55F9AB0);
        api.FindVerified(element, "set_pickingMode", 1, 0x55F97B0);
        api.FindVerified(element, "AddToClassList", 1, 0x55DF820);
        const auto length = api.Class(Ui, UiSpace, "Length");
        api.FindVerified(length, ".ctor", 2, 0x561D3D0);
        const auto style = api.Class(Ui, UiSpace, "InlineStyleAccess");
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_position", 0, 0x55D1A30);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_position", 1, 0x55D48B0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_left", 0, 0x55D1610);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_left", 1, 0x55D3EB0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_top", 0, 0x55D1CD0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_top", 1, 0x55D4F70);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_width", 0, 0x55D2740);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_width", 1, 0x55D63F0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_height", 0, 0x55D1540);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_height", 1, 0x55D3D60);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_minHeight", 0, 0x55D1810);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_minHeight", 1, 0x55D4370);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_flexShrink", 0, 0x55D1460);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_flexShrink", 1, 0x55D3BC0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_opacity", 0, 0x55D1890);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_opacity", 1, 0x55D44B0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_justifyContent", 0, 0x55D1580);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_justifyContent", 1, 0x55D3E00);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_fontSize", 0, 0x55D1500);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_fontSize", 1, 0x55D3D00);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_display", 0, 0x55D1270);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_display", 1, 0x55D37F0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_alignItems", 0, 0x55D0C60);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_alignItems", 1, 0x55D2870);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.get_whiteSpace", 0, 0x55D26B0);
        api.FindVerified(style, "UnityEngine.UIElements.IStyle.set_whiteSpace", 1, 0x55D6370);
        const auto button = api.Class("Core.dll", Figma, "DofusButtonCustom");
        api.FindVerified(button, ".ctor", 0, 0x170D6F0);
        api.FindVerified(button, "set_size", 1, 0x170E330);
        api.FindVerified(button, "set_mainStyle", 1, 0x170E210);
        api.FindVerified(button, "set_text", 1, 0x170E5F0);
        const auto label = api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel");
        api.FindVerified(label, ".ctor", 0, 0x1593940);
        api.FindVerified(label, "set_text", 1, 0x1594570);
        api.FindVerified(api.Class(Ui, UiSpace, "TextElement"), "set_enableRichText", 1, 0x564B1E0);
        openUrl = api.FindVerified(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"), "OpenURL", 1, 0x53ACA50);
        const auto enable = api.FindVerified(calendar, "OnEnable", 0, 0x2E065E0);
        const auto disable = api.FindVerified(calendar, "OnDisable", 0, 0x2E06550);
        AddHook(enable, 0x2E065E0, reinterpret_cast<void*>(EnableHook), reinterpret_cast<void**>(&originalEnable));
        AddHook(disable, 0x2E06550, reinterpret_cast<void*>(DisableHook), reinterpret_cast<void**>(&originalDisable));
        nativeClick::AddHandler(ChipClick);
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
        if (!Session() || !Enabled() || state.cleanup) {
            if (state.cleanup || state.row.get() || pending.handle) RestoreCalendarKralove();
            return;
        }
        if (state.row.get() && !Current()) { RestoreCalendarKralove(); return; } // closed, moved, another session: until the next OnEnable
        if (!state.row.get() && !pending.handle) return;                       // the calendar is not open
        const auto view = metamob::MetamobKralove();
        if (!view.usable) { if (state.row.get()) Withdraw(); return; }         // no usable key: no row, no request
        if (!state.row.get()) {
            Ref owner(pending.get());
            const auto built = owner.get() ? Create(owner.get()) : Built::Refused;
            if (built == Built::Retry) return;
            pending.reset();
            if (built == Built::Refused) return;
        }
        metamob::WantKralove();
        Update(view);
    } catch (...) {
        Log("CALENDAR_KRALOVE_ERROR");
        RestoreCalendarKralove();
    }
}
void RestoreCalendarKralove() {
    try {
        pending.reset();
        if (!state.row.get()) { state = State{}; return; }
        state.cleanup = true;
        api.Call(state.row.get(), "RemoveFromHierarchy");
        state = State{};
    } catch (...) { Log("CALENDAR_KRALOVE_CLEANUP_ERROR"); }
}
bool CalendarKraloveCleanupPending() { return state.cleanup; }
