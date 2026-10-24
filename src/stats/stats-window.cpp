#include "stats/stats-window.hpp"
#include "stats/stats-format.hpp"
#include "stats/stats.hpp"
#include "ui/native-click.hpp"
#include "souls/soul-panel.hpp"
#include "ui/tools-ui.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* Components = "Core.UILogic.Components";
constexpr const char* Title = "Statistiques";
// FigmaIcons (values of the pinned snapshot): chart for the window, its header
// and its top-bar button; one per section: hourglass (temps et déplacements),
// experience, kamas, job (récolte et artisanat), crossedSwords, spell, zaap
// (téléportations); the reset's: reset, then trash for Confirmer, as the
// options' reset in 1.15.0.
constexpr int IconChart = 77, IconReset = 249, IconTrash = 316;
constexpr int PartIcons[stats::PartCount] = {172, 134, 180, 179, 103, 284, 336};
// Enums of the pinned snapshot: DisplayStyle, FlexDirection.Row, Align.FlexStart
// and Center, Justify.FlexEnd, Wrap.Wrap, WhiteSpace.Normal, Position.Absolute,
// TextAnchor.MiddleRight; HeaderWidget.ComponentSizeEnum.large, DofusTab.TabSize.medium
// and TextCaseEnum.fullCaps, DofusButtonCustom small, secondary, normal or
// negative, Divider simple and horizontal.
constexpr int DisplayFlex = 0, DisplayNone = 1, RowDirection = 2, AlignStart = 1, AlignCenter = 2, JustifyEnd = 2,
    WrapLines = 1, WrapText = 0, Absolute = 1, MiddleRight = 5;
constexpr int HeaderLarge = 1, TabMedium = 1, TabCaps = 1, ButtonSmall = 0, StyleSecondary = 1, StatusNormal = 0,
    StatusNegative = 1, DividerSimple = 0, Horizontal = 0;
// What is shown: « Ce personnage » (0) or « Tous mes personnages » (1), as the
// options' choice in 1.15.0.
constexpr const char* Scopes[] = {"Ce personnage", "Tous mes personnages"};
constexpr const char* ScopeHelps[] = {"Les statistiques du personnage connecté dans ce jeu.",
    "Les statistiques de tous tes personnages comptés sur ce PC, additionnées ; les records gardent la plus grande valeur."};
constexpr const char* WhoHelp = "Le personnage dont les statistiques sont affichées, ou le nombre de tes personnages additionnés, et le jour où leur comptage a commencé.";
constexpr const char* ResetHelp = "Efface les statistiques affichées : celles de ce personnage, ou celles de tous tes personnages avec « Tous mes personnages ». Clique sur Réinitialiser, puis sur Confirmer dans les 10 secondes ; sinon rien n’est effacé. Des statistiques effacées ne peuvent pas être récupérées.";
// The figures follow the store once a second while the window is open. The
// reset waits 10 s for Confirmer, taken only from 1 s after the question (the
// same button: a double click never erases); its outcome stays 6 s.
constexpr ULONGLONG RefreshMs = 1000, ConfirmMs = 10000, ConfirmWaitMs = 1000, ResultMs = 6000;
// The window opens as wide as three columns (narrower on a small screen) and
// is never narrower than two.
constexpr float WindowWidth = 900, MinWidth = 560, MinHeight = 360, Padding = 10, ColumnGap = 14;
// Three columns from this width of their box, else two. Each section has its
// column, in reading order: with two, the short sections, then the fights and
// the spells; with three, two or three sections each.
constexpr float ThreeColumns = 780;
constexpr size_t MaxColumns = 3;
constexpr size_t ColumnOf[2][stats::PartCount] = {{0, 0, 0, 0, 1, 1, 0}, {0, 0, 0, 1, 1, 2, 2}};
static_assert(std::size(Scopes) == 2 && std::size(ScopeHelps) == 2);

// A figure's line: its label at the left, its value at the right (on a line of
// its own, still at the right, when both do not fit), and what they show.
struct LineView {
    Ref line, label, value;
    std::string labelShown, valueShown, helpShown;
    int display = -1;  // as last written; -1 before
};
struct PartView { Ref section; std::vector<LineView> lines; };
struct State {
    Ref root, window, header, dragger, dependencies, cached, compositor, closeButton;
    Ref who, columnsBox, footer, reset;
    std::array<Ref, 2> tabs;
    std::array<Ref, MaxColumns> columns;
    std::array<Ref, 4> inputEvents;
    std::array<PartView, stats::PartCount> parts;
    std::vector<Ref> manipulators;
    stats::Totals totals;  // as last read from the store
    std::string whoShown, footerShown, captionShown;
    size_t columnCount = 0;  // 2 or 3 in use
    ULONGLONG nextRefresh = 0;
    bool cleanup = false;
} state;
// What the window shows and its reset question. The choice stays while the
// window closes and opens again, and is « Ce personnage » at each launch of
// the game; a question or an outcome goes with the window.
struct Shown {
    int scope = 0;
    bool confirm = false, confirmAll = false;  // asked, for what was shown then
    int64_t confirmCharacter = 0;
    int result = 0;  // 1 erased, 2 impossible
    ULONGLONG confirmDeadline = 0, confirmFrom = 0, resultUntil = 0;
} shown;
bool installed = false, wanted = false;
Method unsubscribe = nullptr;
unsigned long long (*testClock)() = nullptr;  // tests only (UseStatsWindowClock)
ULONGLONG Now() { return testClock ? testClock() : GetTickCount64(); }

bool Session() {
    return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
// A failure repeated at each refresh: one line per message every ten seconds.
void LogLimited(const std::string& line) {
    static std::string previous;
    static ULONGLONG next = 0;
    const auto now = GetTickCount64();
    if (line == previous && now < next) return;
    previous = line; next = now + 10000; Log(line);
}
Obj Style(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    Obj box = api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str());
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Statistics style unavailable");
    return box;
}
void WriteStyle(Obj element, const char* property, Obj box) {
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box)});
}
void Enum(Obj element, const char* property, int value) {
    Ref box(Style(element, property)); api.Call(box.get(), "set_value", {&value}); WriteStyle(element, property, box.get());
}
void Float(Obj element, const char* property, float value) {
    Ref box(Style(element, property)); api.Call(box.get(), "set_value", {&value}); WriteStyle(element, property, box.get());
}
void Pixels(Obj element, const char* property, float value) {
    int unit = 0;
    Ref length(api.New(api.Class(Ui, UiSpace, "Length"), {&value, &unit}));
    Ref box(Style(element, property)); api.Call(box.get(), "set_value", {api.object_unbox(length.get())}); WriteStyle(element, property, box.get());
}
void Color(Obj element, float r, float g, float b) {
    float a = 1;
    Ref color(api.New(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Color"), {&r, &g, &b, &a}));
    Ref box(Style(element, "color")); api.Call(box.get(), "set_value", {api.object_unbox(color.get())}); WriteStyle(element, "color", box.get());
}
void Text(Obj element, const std::string& text, const char* setter = "set_text") {
    Ref value(api.string_new(text.c_str())); api.Call(element, setter, {value.get()});
}
void Add(Obj parent, Obj child) { api.Call(parent, "Add", {child}); }
// A text written only when it changes.
void Write(Obj element, std::string& written, const std::string& text, const char* setter = "set_text") {
    if (written == text) return;
    Text(element, text, setter);
    written = text;
}
// A box laying its children side by side.
Obj Box(Obj parent) {
    Ref box(api.New(api.Class(Ui, UiSpace, "VisualElement")));
    Enum(box.get(), "flexDirection", RowDirection); Enum(box.get(), "alignItems", AlignCenter);
    Add(parent, box.get()); return box.get();
}
// A text in the notebook's colours: light for a value or a sentence, grey for
// a figure's label; never rich text (a character's name shows as it is).
Obj Label(Obj parent, bool light) {
    Ref label(api.New(api.Class(Ui, UiSpace, "Label")));
    bool rich = false; api.Call(label.get(), "set_enableRichText", {&rich});
    Enum(label.get(), "whiteSpace", WrapText); Pixels(label.get(), "fontSize", 13);
    for (const auto* property : {"paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) Pixels(label.get(), property, 0);
    if (light) Color(label.get(), .9f, .9f, .9f); else Color(label.get(), .72f, .72f, .68f);
    Add(parent, label.get()); return label.get();
}
// The game's button component, as the notebook's (native hover, pressed and
// disabled states, icon and caption).
Obj Button(Obj parent, const char* text, int icon, const char* tooltip) {
    Ref button(api.New(api.Class("Core.dll", Figma, "DofusButtonCustom")));
    int size = ButtonSmall, style = StyleSecondary;
    api.Call(button.get(), "set_size", {&size}); api.Call(button.get(), "set_mainStyle", {&style}); api.Call(button.get(), "set_icon", {&icon});
    Text(button.get(), text); Text(button.get(), tooltip, "set_tooltip"); Float(button.get(), "flexShrink", 0);
    Add(parent, button.get()); return button.get();
}
// The game's tab, in the notebook's capitalised medium style.
Obj Tab(Obj parent, const char* text, const char* tooltip) {
    Ref tab(api.New(api.Class("Core.dll", Figma, "DofusTab")));
    int size = TabMedium, caps = TabCaps; api.Call(tab.get(), "set_size", {&size}); api.Call(tab.get(), "set_fontCase", {&caps});
    Text(tab.get(), text); Text(tab.get(), tooltip, "set_tooltip"); Float(tab.get(), "flexGrow", 1); Pixels(tab.get(), "minWidth", 0);
    Add(parent, tab.get()); return tab.get();
}
void Divider(Obj parent) {
    Ref divider(api.New(api.Class("Core.dll", Figma, "Divider")));
    int style = DividerSimple, orientation = Horizontal;
    api.Call(divider.get(), "set_componentStyle", {&style}); api.Call(divider.get(), "set_orientation", {&orientation});
    Add(parent, divider.get());
}
// The game's titled section (the notebook's and the map resource drawer's
// SectionHeader), opened then locked; returns its content. The native
// set_isOpen keeps the previous (closed) state while canBeOpened is off, which
// it is after the constructor: opened while openable, then locked.
Obj Section(Obj parent, const char* title, int icon, Ref& header) {
    bool subContent = true, folderButton = true, no = false, hasIcon = true, open = true, openable = true, locked = false;
    Ref section(api.New(api.Class("Core.dll", Figma, "SectionHeader"), {&subContent, &folderButton, &no, &hasIcon, &no}));
    Text(section.get(), title, "set_title"); api.Call(section.get(), "set_icon", {&icon});
    api.Call(section.get(), "set_canBeOpened", {&openable}); api.Call(section.get(), "set_isOpen", {&open}); api.Call(section.get(), "set_canBeOpened", {&locked});
    Ref content(api.New(api.Class(Ui, UiSpace, "VisualElement")));
    for (const auto* property : {"paddingLeft", "paddingRight"}) Pixels(content.get(), property, 4);
    Pixels(content.get(), "paddingTop", 6); Pixels(content.get(), "paddingBottom", 10);
    api.Call(section.get(), "AddSubContent", {content.get()});
    Add(parent, section.get()); header.reset(section.get()); return content.get();
}
// The game's layer of ordinary windows (RootMedium); its tooltips and higher
// layers keep their priority.
Obj Root() {
    Ref hud(soul::GetUiRoot()); if (!hud.get()) return nullptr;
    Ref parent(api.Call(hud.get(), "get_parent"));
    Ref hierarchy(api.Call(parent.get(), "get_hierarchy"));
    const int count = api.Value<int>(api.Call(hierarchy.get(), "get_childCount"));
    if (count < 0 || count > 64) throw std::runtime_error("Statistics UI layer bounds");
    Obj result = nullptr;
    for (int i = 0; i < count; ++i) {
        Obj child = api.Call(hierarchy.get(), "get_Item", {&i});
        if (api.String(api.Call(child, "get_name")) == "RootMedium") {
            if (result) throw std::runtime_error("Ambiguous native window layer");
            result = child;
        }
    }
    return result;
}
bool Current() {
    return Session() && state.window.get() && !state.cleanup && state.cached.get() == module->cached.get() &&
        state.compositor.get() == module->compositor.get() && state.dependencies.get() == module->FindService("fpt") &&
        api.Call(state.window.get(), "get_parent") == state.root.get() && api.Call(state.window.get(), "get_panel");
}
// A section's lines, as many as it can show (stats::PartSize), filled by ShowFigures.
void Build(size_t index, Obj column) {
    auto& part = state.parts[index];
    const auto kind = static_cast<stats::Part>(index);
    Ref content(Section(column, stats::PartTitle(kind), PartIcons[index], part.section));
    part.lines.resize(stats::PartSize(kind));
    for (auto& view : part.lines) {
        view.line.reset(Box(content.get()));
        Enum(view.line.get(), "flexWrap", WrapLines); Enum(view.line.get(), "justifyContent", JustifyEnd); Pixels(view.line.get(), "paddingBottom", 3);
        view.label.reset(Label(view.line.get(), false));
        Float(view.label.get(), "flexGrow", 1); Float(view.label.get(), "flexShrink", 1); Pixels(view.label.get(), "paddingRight", 8);
        view.value.reset(Label(view.line.get(), true));
        Enum(view.value.get(), "unityTextAlign", MiddleRight);
    }
}
// Each line as the store's totals are now: hidden while it has nothing to
// show, its label, value and hover text written when they change.
void ShowFigures() {
    for (size_t index = 0; index < stats::PartCount; ++index) {
        auto& part = state.parts[index];
        const auto lines = stats::Lines(static_cast<stats::Part>(index), state.totals, stats::KeyName);
        for (size_t i = 0; i < part.lines.size(); ++i) {
            auto& view = part.lines[i];
            const bool show = i < lines.size() && lines[i].shown;
            const int display = show ? DisplayFlex : DisplayNone;
            if (view.display != display) { Enum(view.line.get(), "display", display); view.display = display; }
            if (i >= lines.size()) continue;
            Write(view.label.get(), view.labelShown, lines[i].label);
            Write(view.value.get(), view.valueShown, lines[i].value);
            Write(view.line.get(), view.helpShown, lines[i].help, "set_tooltip");
        }
    }
}
// The reset's line and button: Réinitialiser, the question with Confirmer, then its outcome.
stats::Reset Step() {
    if (shown.confirm) return stats::Reset::Asking;
    return shown.result == 1 ? stats::Reset::Erased : shown.result == 2 ? stats::Reset::Impossible : stats::Reset::Ready;
}
void ShowFooter() {
    const auto step = Step();
    Write(state.footer.get(), state.footerShown, stats::ResetText(step, shown.confirm ? shown.confirmAll : shown.scope == 1, state.totals));
    const std::string caption = stats::ResetCaption(step);
    if (caption == state.captionShown) return;
    const bool asking = step == stats::Reset::Asking;
    int icon = asking ? IconTrash : IconReset, status = asking ? StatusNegative : StatusNormal;
    Text(state.reset.get(), caption); api.Call(state.reset.get(), "set_icon", {&icon}); api.Call(state.reset.get(), "set_status", {&status});
    state.captionShown = caption;
}
// What is shown, read from the store (the played character read again there,
// another client's writes followed), into every text of the window.
void Refresh(ULONGLONG now) {
    state.nextRefresh = now + RefreshMs;
    state.totals = stats::Shown(shown.scope == 1);
    Write(state.who.get(), state.whoShown, stats::Who(state.totals, shown.scope == 1));
    ShowFigures();
    ShowFooter();
}
void ShowTabs() {
    for (size_t i = 0; i < state.tabs.size(); ++i) {
        bool on = static_cast<int>(i) == shown.scope;
        api.Call(state.tabs[i].get(), "SetValueWithoutNotify", {&on});
    }
}
// The native tabs toggle themselves on click; the window follows them at each
// pump. Another choice drops a question or an outcome and shows at once; the
// tab shown stays selected when clicked again.
bool PollTabs() {
    for (size_t i = 0; i < state.tabs.size(); ++i) {
        if (static_cast<int>(i) == shown.scope || !api.Value<bool>(api.Call(state.tabs[i].get(), "get_isOn"))) continue;
        shown.scope = static_cast<int>(i);
        shown.confirm = false; shown.result = 0;
        ShowTabs();
        return true;
    }
    if (!api.Value<bool>(api.Call(state.tabs[static_cast<size_t>(shown.scope)].get(), "get_isOn"))) ShowTabs();
    return false;
}
// An unanswered question goes after 10 s, an outcome after 6 s.
void Expire(ULONGLONG now) {
    bool changed = false;
    if (shown.confirm && now >= shown.confirmDeadline) { shown.confirm = false; changed = true; }
    if (shown.result && now >= shown.resultUntil) { shown.result = 0; changed = true; }
    if (changed) ShowFooter();
}
// « Réinitialiser » asks first: the line names what would be erased and the
// button becomes Confirmer; Confirmer within 10 s erases what was shown when
// asked, for the same character, and the figures follow at once. A click in
// the first second after the question is the question's gesture: ignored.
void ResetClick(ULONGLONG now) {
    if (!shown.confirm || now >= shown.confirmDeadline) {
        shown.confirm = true; shown.confirmAll = shown.scope == 1; shown.confirmCharacter = stats::Current();
        shown.confirmDeadline = now + ConfirmMs; shown.confirmFrom = now + ConfirmWaitMs; shown.result = 0;
        ShowFooter();
        return;
    }
    if (now < shown.confirmFrom) return;
    shown.confirm = false;
    const bool same = shown.confirmAll || (shown.confirmCharacter > 0 && stats::Current() == shown.confirmCharacter);
    shown.result = same && stats::Clear(shown.confirmAll) ? 1 : 2;
    shown.resultUntil = now + ResultMs;
    Refresh(now);
}
// The sections in two or three columns, by the width of their box.
size_t ColumnsFor(float width) { return width >= ThreeColumns ? 3 : 2; }
void ShowColumns(size_t count) {
    Enum(state.columns[2].get(), "display", count == 3 ? DisplayFlex : DisplayNone);
    Pixels(state.columns[1].get(), "paddingRight", count == 3 ? ColumnGap : 0);
    state.columnCount = count;
}
void Arrange(size_t count) {
    if (count == state.columnCount) return;
    for (size_t index = 0; index < stats::PartCount; ++index) {
        Obj section = state.parts[index].section.get();
        api.Call(section, "RemoveFromHierarchy");
        Add(state.columns[ColumnOf[count - 2][index]].get(), section);
    }
    ShowColumns(count);
}
// The user resizes the window with its native resizers: the columns follow
// once the box has its new width (none before its first layout).
void FollowWidth() {
    Ref resolved(api.Call(state.columnsBox.get(), "get_resolvedStyle"));
    const float width = api.Value<float>(api.Call(resolved.get(), "get_width"));
    if (std::isfinite(width) && width > 0) Arrange(ColumnsFor(width));
}
// As wide as three columns, narrower on a small screen, never under two.
float Width(Obj root) {
    Ref resolved(api.Call(root, "get_resolvedStyle"));
    const float available = api.Value<float>(api.Call(resolved.get(), "get_width")) - 32;
    return std::isfinite(available) ? std::clamp(available, MinWidth, WindowWidth) : WindowWidth;
}
// The game's click on the window's own buttons (nativeClick): its close
// button and the reset. The tabs switch by themselves (PollTabs).
bool Action(Obj owner) {
    if (!owner || (owner != state.closeButton.get() && owner != state.reset.get())) return false;
    try {
        if (!wanted || !Current() || !api.Value<bool>(api.Call(owner, "get_enabledInHierarchy"))) return true;
        if (owner == state.closeButton.get()) wanted = false;
        else ResetClick(Now());
    } catch (const std::exception& error) { LogLimited(std::string("STATS_WINDOW_ACTION_ERROR ") + error.what()); }
    return true;
}
void Retain(const char* field) {
    Ref list(api.Field(state.window.get(), field));
    const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
    if (count < 0 || count > 32) throw std::runtime_error("Statistics native manipulator bounds");
    for (int i = 0; i < count; ++i) state.manipulators.emplace_back(api.Call(list.get(), "get_Item", {&i}));
}
void Create(Obj root) {
    state.root.reset(root); state.dependencies.reset(module->FindService("fpt"));
    if (!state.dependencies.get() || api.object_get_class(state.dependencies.get()) != api.Class("Core.dll", "", "goc"))
        throw std::runtime_error("Statistics native dependencies unavailable");
    state.cached.reset(api.Call(state.dependencies.get(), "bfri")); state.compositor.reset(module->compositor.get());
    Ref input(api.Call(state.dependencies.get(), "bfre"));
    if (state.cached.get() != module->cached.get() || !input.get() || api.object_get_class(input.get()) != api.Class("Core.dll", "", "eqo"))
        throw std::runtime_error("Statistics native session mismatch");
    state.window.reset(api.New(api.Class("Core.dll", Components, "MagneticDragWindow")));
    Obj window = state.window.get();
    Text(window, "", "set_name"); Text(window, Title, "set_headerTitle");
    bool yes = true; int icon = IconChart, size = HeaderLarge;
    api.Call(window, "set_headerIcon", {&icon}); api.Call(window, "set_headerSize", {&size});
    // The window's own close button (the panel handles its click: the game
    // calls nothing for a window it does not manage) and the native resizers.
    api.Call(window, "set_headerShowCloseButton", {&yes});
    for (const auto* setter : {"set_verticalResizable", "set_horizontalResizable", "set_cornerResizable"}) api.Call(window, setter, {&yes});
    api.Call(window, "set_hasBackground", {&yes});
    Enum(window, "position", Absolute); Float(window, "flexGrow", 0);
    const float width = Width(root);
    Pixels(window, "width", width); Pixels(window, "minWidth", MinWidth); Pixels(window, "minHeight", MinHeight);
    state.header.reset(api.Call(window, "get_headerWidget"));
    state.closeButton.reset(api.Field(state.header.get(), "closeButton"));
    if (!state.closeButton.get()) throw std::runtime_error("Statistics native close button unavailable");
    toolsUi::AttachHeader("stats", window);
    Ref draggers(api.Field(window, "m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(), "get_Item", {state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[] = {"ydf", "ydh", "ydj", "ydl"};
    for (size_t i = 0; i < 4; ++i) state.inputEvents[i].reset(api.Call(input.get(), getters[i]));
    api.Call(window, "Init", {state.dependencies.get()});
    Ref content(toolsUi::ScrollBody(window, root, true));
    Float(content.get(), "flexGrow", 1);
    for (const auto* property : {"paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) Pixels(content.get(), property, Padding);
    // What is shown: the game's tabs, then whose statistics they are.
    Ref tabs(Box(content.get()));
    for (size_t i = 0; i < state.tabs.size(); ++i) state.tabs[i].reset(Tab(tabs.get(), Scopes[i], ScopeHelps[i]));
    ShowTabs();
    Divider(content.get());
    state.who.reset(Label(content.get(), true));
    Pixels(state.who.get(), "paddingTop", 4); Pixels(state.who.get(), "paddingBottom", 10); Text(state.who.get(), WhoHelp, "set_tooltip");
    // The sections, side by side in two or three columns of equal width.
    state.columnsBox.reset(Box(content.get()));
    Enum(state.columnsBox.get(), "alignItems", AlignStart);
    for (size_t i = 0; i < MaxColumns; ++i) {
        Ref column(api.New(api.Class(Ui, UiSpace, "VisualElement")));
        Pixels(column.get(), "width", 0); Float(column.get(), "flexGrow", 1); Float(column.get(), "flexShrink", 1); Pixels(column.get(), "minWidth", 0);
        Pixels(column.get(), "paddingRight", i == 0 ? ColumnGap : 0);
        Add(state.columnsBox.get(), column.get()); state.columns[i].reset(column.get());
    }
    const auto columns = ColumnsFor(width - 2 * Padding);
    for (size_t index = 0; index < stats::PartCount; ++index) Build(index, state.columns[ColumnOf[columns - 2][index]].get());
    ShowColumns(columns);
    // The reset, at the bottom: what it would erase, then its button.
    Divider(content.get());
    Ref footer(Box(content.get()));
    Pixels(footer.get(), "paddingTop", 6);
    state.footer.reset(Label(footer.get(), false));
    Float(state.footer.get(), "flexGrow", 1); Float(state.footer.get(), "flexShrink", 1); Pixels(state.footer.get(), "paddingRight", 8);
    state.reset.reset(Button(footer.get(), stats::ResetCaption(stats::Reset::Ready), IconReset, ResetHelp));
    Refresh(Now());
    Add(root, window);
    Log("STATS_WINDOW_OPEN native=true");
}
}

void InstallStatsWindow() {
    // The native parts of the window, checked against the pinned client; the
    // window itself, its header, drag and input contracts are the soul
    // panel's, checked by InstallSoulHelper first.
    auto tab = api.Class("Core.dll", Figma, "DofusTab");
    api.FindVerified(tab, ".ctor", 0, 0x1712DC0); api.FindVerified(tab, "set_text", 1, 0x17136F0); api.FindVerified(tab, "set_size", 1, 0x1713550);
    api.FindVerified(tab, "set_fontCase", 1, 0x1712FC0); api.FindVerified(tab, "get_isOn", 0, 0x1712F50); api.FindVerified(tab, "SetValueWithoutNotify", 1, 0x1711970);
    auto button = api.Class("Core.dll", Figma, "DofusButtonCustom");
    api.FindVerified(button, ".ctor", 0, 0x170D6F0); api.FindVerified(button, "set_icon", 1, 0x170DFE0); api.FindVerified(button, "set_text", 1, 0x170E5F0);
    api.FindVerified(button, "set_mainStyle", 1, 0x170E210); api.FindVerified(button, "set_size", 1, 0x170E330); api.FindVerified(button, "set_status", 1, 0x170E400);
    auto section = api.Class("Core.dll", Figma, "SectionHeader");
    api.FindVerified(section, ".ctor", 5, 0x1745F00); api.FindVerified(section, "set_title", 1, 0x17467B0); api.FindVerified(section, "set_icon", 1, 0x1746200);
    api.FindVerified(section, "set_isOpen", 1, 0x17462F0); api.FindVerified(section, "set_canBeOpened", 1, 0x1746090); api.FindVerified(section, "AddSubContent", 1, 0x1744360);
    auto divider = api.Class("Core.dll", Figma, "Divider");
    api.FindVerified(divider, ".ctor", 0, 0x170BC10); api.FindVerified(divider, "set_componentStyle", 1, 0x170BDC0); api.FindVerified(divider, "set_orientation", 1, 0x170C160);
    api.FindVerified(api.Class("Core.dll", Components, "ResizableWindow"), "set_headerShowCloseButton", 1, 0x15BFB50);
    auto element = api.Class(Ui, UiSpace, "VisualElement");
    api.FindVerified(element, ".ctor", 0, 0x55F50B0); api.FindVerified(element, "Add", 1, 0x55DFAD0); api.FindVerified(element, "RemoveFromHierarchy", 0, 0x55E7BD0);
    api.FindVerified(element, "BringToFront", 0, 0x55E08E0); api.FindVerified(element, "get_parent", 0, 0x55F7180); api.FindVerified(element, "get_panel", 0, 0x55F7170);
    api.FindVerified(element, "get_style", 0, 0x55F74A0); api.FindVerified(element, "get_resolvedStyle", 0, 0x55F72E0);
    api.FindVerified(element, "set_name", 1, 0x55F9670); api.FindVerified(element, "set_tooltip", 1, 0x55F9AB0);
    api.FindVerified(api.Class(Ui, UiSpace, "Label"), ".ctor", 0, 0x5729760);
    auto text = api.Class(Ui, UiSpace, "TextElement");
    api.FindVerified(text, "set_text", 1, 0x564B780); api.FindVerified(text, "set_enableRichText", 1, 0x564B1E0);
    api.FindVerified(api.Class(Ui, UiSpace, "ResolvedStyleAccess"), "get_width", 0, 0x55DA460);
    // The inline styles written: each property's getter (its current value) and setter.
    auto styleAccess = api.Class(Ui, UiSpace, "InlineStyleAccess");
    const struct { const char* name; int argc; uintptr_t rva; } styles[] = {
        {"UnityEngine.UIElements.IStyle.get_display", 0, 0x55D1270}, {"UnityEngine.UIElements.IStyle.set_display", 1, 0x55D37F0},
        {"UnityEngine.UIElements.IStyle.get_flexDirection", 0, 0x55D13C0}, {"UnityEngine.UIElements.IStyle.set_flexDirection", 1, 0x55D3A80},
        {"UnityEngine.UIElements.IStyle.get_alignItems", 0, 0x55D0C60}, {"UnityEngine.UIElements.IStyle.set_alignItems", 1, 0x55D2870},
        {"UnityEngine.UIElements.IStyle.get_justifyContent", 0, 0x55D1580}, {"UnityEngine.UIElements.IStyle.set_justifyContent", 1, 0x55D3E00},
        {"UnityEngine.UIElements.IStyle.get_flexWrap", 0, 0x55D1470}, {"UnityEngine.UIElements.IStyle.set_flexWrap", 1, 0x55D3C50},
        {"UnityEngine.UIElements.IStyle.get_whiteSpace", 0, 0x55D26B0}, {"UnityEngine.UIElements.IStyle.set_whiteSpace", 1, 0x55D6370},
        {"UnityEngine.UIElements.IStyle.get_unityTextAlign", 0, 0x55D23B0}, {"UnityEngine.UIElements.IStyle.set_unityTextAlign", 1, 0x55D5ED0},
        {"UnityEngine.UIElements.IStyle.get_position", 0, 0x55D1A30}, {"UnityEngine.UIElements.IStyle.set_position", 1, 0x55D48B0},
        {"UnityEngine.UIElements.IStyle.get_flexGrow", 0, 0x55D1450}, {"UnityEngine.UIElements.IStyle.set_flexGrow", 1, 0x55D3B30},
        {"UnityEngine.UIElements.IStyle.get_flexShrink", 0, 0x55D1460}, {"UnityEngine.UIElements.IStyle.set_flexShrink", 1, 0x55D3BC0},
        {"UnityEngine.UIElements.IStyle.get_fontSize", 0, 0x55D1500}, {"UnityEngine.UIElements.IStyle.set_fontSize", 1, 0x55D3D00},
        {"UnityEngine.UIElements.IStyle.get_color", 0, 0x55D11D0}, {"UnityEngine.UIElements.IStyle.set_color", 1, 0x55D35D0},
        {"UnityEngine.UIElements.IStyle.get_width", 0, 0x55D2740}, {"UnityEngine.UIElements.IStyle.set_width", 1, 0x55D63F0},
        {"UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x55D1850}, {"UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x55D4410},
        {"UnityEngine.UIElements.IStyle.get_minHeight", 0, 0x55D1810}, {"UnityEngine.UIElements.IStyle.set_minHeight", 1, 0x55D4370},
        {"UnityEngine.UIElements.IStyle.get_paddingLeft", 0, 0x55D1970}, {"UnityEngine.UIElements.IStyle.set_paddingLeft", 1, 0x55D46D0},
        {"UnityEngine.UIElements.IStyle.get_paddingRight", 0, 0x55D19B0}, {"UnityEngine.UIElements.IStyle.set_paddingRight", 1, 0x55D4770},
        {"UnityEngine.UIElements.IStyle.get_paddingTop", 0, 0x55D19F0}, {"UnityEngine.UIElements.IStyle.set_paddingTop", 1, 0x55D4810},
        {"UnityEngine.UIElements.IStyle.get_paddingBottom", 0, 0x55D1930}, {"UnityEngine.UIElements.IStyle.set_paddingBottom", 1, 0x55D4630}};
    for (const auto& style : styles) api.FindVerified(styleAccess, style.name, style.argc, style.rva);
    unsubscribe = api.FindVerified(api.Class("Core.dll", "Core.Engine.Inputs.Events", "InputCustomEvent"), "UnSubscribe", 1, 0x20FF620);
    nativeClick::AddHandler(Action);
    // The top-bar button, after the notebook's, while « Bouton dans la barre
    // du haut » is ticked; the ⋯ menu's « Masquer le module » closes the window.
    toolsUi::Register("stats", Title, IconChart, [] { return wanted; }, [] { wanted = !wanted; return true; }, {},
        [] { return module && module->settings.statsButton; });
    installed = true;
}
void OpenStatsWindow() {
    if (!installed || !Session()) return;
    wanted = true;
    try { if (Current()) api.Call(state.window.get(), "BringToFront"); }
    catch (const std::exception& error) { LogLimited(std::string("STATS_WINDOW_FRONT_ERROR ") + error.what()); }
}
void RestoreStatsWindow() {
    wanted = false;
    shown.confirm = false; shown.result = 0;
    if (!state.window.get()) { state = State{}; return; }
    state.cleanup = true;
    try {
        Enum(state.window.get(), "display", DisplayNone);
        toolsUi::DetachHeader("stats");
        const auto mouse = api.Class(Ui, UiSpace, "MouseCaptureController");
        if (state.header.get() && api.Value<bool>(api.Invoke(api.Find(mouse, "HasMouseCapture", 1), nullptr, {state.header.get()}))) {
            *static_cast<bool*>(api.FieldAddress(state.dragger.get(), "m_active")) = false;
            api.Invoke(api.Find(mouse, "ReleaseMouse", 1), nullptr, {state.header.get()});
            // Only the window that still owns capture may clear the drag flag.
            reinterpret_cast<void(*)(bool, Method)>(api.NativeAt(0x166DF30))(false, nullptr);
        }
        if (state.dragger.get()) api.Call(state.dragger.get(), "RemoveSnapPreview");
        bool locked = true; int none = 0;
        api.Call(state.window.get(), "set_isUILocked", {&locked});
        api.Call(state.window.get(), "set_dragPosition", {&none});
        // Init subscribed the window's draggers and resizers to the game's input events.
        for (const auto& event : state.inputEvents) {
            if (!event.get()) continue;
            Ref list(api.Field(event.get(), "m_input")); if (!list.get()) continue;
            const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
            if (count < 0 || count > 16384) throw std::runtime_error("Statistics input subscriber bounds");
            for (int i = count; i-- > 0;) {
                Ref callback(api.Call(list.get(), "get_Item", {&i}));
                if (!callback.get()) continue;
                Obj target = api.Call(callback.get(), "get_Target");
                if (std::any_of(state.manipulators.begin(), state.manipulators.end(), [&](const Ref& item) { return item.get() == target; }))
                    api.Invoke(unsubscribe, event.get(), {callback.get()});
            }
        }
        api.Call(state.window.get(), "RemoveFromHierarchy");
        state = State{};
    } catch (const std::exception& error) { LogLimited(std::string("STATS_WINDOW_CLEANUP_ERROR ") + error.what()); }
}
bool StatsWindowCleanupPending() { return state.cleanup; }
void UseStatsWindowClock(unsigned long long (*now)()) { testClock = now; }
void TickStatsWindow() {
    if (!installed) return;
    if (!wanted || !Session() || state.cleanup || (state.window.get() && !Current())) { RestoreStatsWindow(); return; }
    if (!state.window.get()) {
        try {
            Ref root(Root());
            if (!root.get() || !api.Call(root.get(), "get_panel")) return;
            Create(root.get());
        } catch (const std::exception& error) { Log(std::string("STATS_WINDOW_CREATE_ERROR ") + error.what()); RestoreStatsWindow(); }
        return;
    }
    // Nothing is read while the window is closed; open, the figures follow
    // once a second, at once after another choice.
    try {
        const auto now = Now();
        const bool changed = PollTabs();
        Expire(now);
        FollowWidth();
        if (changed || now >= state.nextRefresh) Refresh(now);
    } catch (const std::exception& error) { LogLimited(std::string("STATS_WINDOW_ERROR ") + error.what()); }
}
