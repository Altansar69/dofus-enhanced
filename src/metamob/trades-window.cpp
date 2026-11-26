#include "metamob/trades-window.hpp"
#include "metamob/trades-format.hpp"
#include "metamob/metamob-sync.hpp"
#include "ui/native-click.hpp"
#include "souls/soul-panel.hpp"
#include "ui/tools-ui.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* Components = "Core.UILogic.Components";
constexpr const char* Key = "trades";
constexpr const char* Title = "Échanges Metamob";
// Where a proposal is answered: Metamob's messages (its login page first when
// the browser is not signed in).
constexpr const char* MessagesPage = "https://www.metamob.fr/messages";
// FigmaIcons (values of the pinned snapshot): exchange for the window, its
// header and its top-bar button; reset for « Actualiser », bellOn and bellOff
// for « Notifications », external for « Ouvrir Metamob ».
constexpr int IconExchange = 129, IconRefresh = 249, IconBellOn = 56, IconBellOff = 55, IconExternal = 135;
// Enums of the pinned snapshot, as the statistics window's.
constexpr int DisplayNone = 1, RowDirection = 2, AlignCenter = 2, WrapLines = 1, WrapText = 0, Absolute = 1;
constexpr int HeaderLarge = 1, ButtonSmall = 0, StyleSecondary = 1, DividerSimple = 0, Horizontal = 0;
constexpr const char* RefreshHelp = "Relit tout de suite tes propositions d’échange sur Metamob (au plus une fois toutes les cinq secondes). Elles sont relues seules chaque minute.";
constexpr const char* NotifyOnHelp = "Notifications activées : une ligne dans le chat pour chaque nouvelle proposition d’échange reçue, et à la connexion le nombre de propositions en attente. Clique pour les désactiver.";
constexpr const char* NotifyOffHelp = "Notifications désactivées : les propositions ne sont lues que pendant que cette fenêtre est ouverte, sans ligne dans le chat. Clique pour les activer.";
constexpr const char* OpenHelp = "Ouvre tes messages Metamob dans le navigateur : c’est là qu’une proposition s’accepte ou se refuse. Le mod ne répond jamais à ta place.";
constexpr const char* SummaryHelp = "Les propositions d’échange que d’autres joueurs t’ont envoyées sur Metamob et qui attendent encore : la plus récente en premier.";
// The texts follow the sync once a second while the window is open; the
// proposals' ages are written again every half minute. The proposals are asked
// for from ten seconds after the character entered the world (the chat line of
// the first reading needs the chat); pumps three seconds apart are another entry.
constexpr ULONGLONG RefreshMs = 1000, AgesMs = 30000, EntryMs = 10000, GapMs = 3000;
constexpr float WindowWidth = 560, MinWidth = 380, MinHeight = 220, Padding = 10;

// A proposal's block: its first line is written again as it ages.
struct Row { Ref box, title; metamob::TradeProposal proposal; std::string titleShown; };
struct State {
    Ref root, window, header, dragger, dependencies, cached, compositor, closeButton;
    Ref summary, refresh, notify, open, list;
    std::array<Ref, 4> inputEvents;
    std::vector<Ref> manipulators;
    std::vector<Row> rows;
    std::string summaryShown;
    uint64_t version = 0;      // the sync's reading the rows show
    bool listed = false;       // rows were built for that reading
    int notifyShown = -1;      // 1 on, 0 off; -1 before
    ULONGLONG nextRefresh = 0, nextAges = 0;
    bool cleanup = false;
} state;
bool installed = false, wanted = false;
ULONGLONG enteredAt = 0, askedAt = 0; // this entry into the world; the last pump that could ask
Method unsubscribe = nullptr, openUrl = nullptr;

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
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Trades style unavailable");
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
void Write(Obj element, std::string& written, const std::string& text) {
    if (written == text) return;
    Text(element, text);
    written = text;
}
Obj Element(Obj parent) {
    Ref element(api.New(api.Class(Ui, UiSpace, "VisualElement")));
    Add(parent, element.get()); return element.get();
}
// A text in the notebook's colours: light for what matters, grey for the
// rest; never rich text (a user's name shows as it is).
Obj Label(Obj parent, bool light, float size = 13) {
    Ref label(api.New(api.Class(Ui, UiSpace, "Label")));
    bool rich = false; api.Call(label.get(), "set_enableRichText", {&rich});
    Enum(label.get(), "whiteSpace", WrapText); Pixels(label.get(), "fontSize", size);
    for (const auto* property : {"paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) Pixels(label.get(), property, 0);
    if (light) Color(label.get(), .9f, .9f, .9f); else Color(label.get(), .72f, .72f, .68f);
    Add(parent, label.get()); return label.get();
}
// The game's button component (native hover, pressed and disabled states).
Obj Button(Obj parent, const char* text, int icon, const char* tooltip) {
    Ref button(api.New(api.Class("Core.dll", Figma, "DofusButtonCustom")));
    int size = ButtonSmall, style = StyleSecondary;
    api.Call(button.get(), "set_size", {&size}); api.Call(button.get(), "set_mainStyle", {&style}); api.Call(button.get(), "set_icon", {&icon});
    Text(button.get(), text); Text(button.get(), tooltip, "set_tooltip"); Float(button.get(), "flexShrink", 0);
    Add(parent, button.get()); return button.get();
}
void Divider(Obj parent) {
    Ref divider(api.New(api.Class("Core.dll", Figma, "Divider")));
    int style = DividerSimple, orientation = Horizontal;
    api.Call(divider.get(), "set_componentStyle", {&style}); api.Call(divider.get(), "set_orientation", {&orientation});
    Add(parent, divider.get());
}
// The game's layer of ordinary windows (RootMedium), as the statistics window's.
Obj Root() {
    Ref hud(soul::GetUiRoot()); if (!hud.get()) return nullptr;
    Ref parent(api.Call(hud.get(), "get_parent"));
    Ref hierarchy(api.Call(parent.get(), "get_hierarchy"));
    const int count = api.Value<int>(api.Call(hierarchy.get(), "get_childCount"));
    if (count < 0 || count > 64) throw std::runtime_error("Trades UI layer bounds");
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
int64_t UnixNow() { return static_cast<int64_t>(std::time(nullptr)); }
// The proposals of the sync's last reading, one block each: the rows of the
// previous one are taken out first.
void ShowAges() {
    const auto now = UnixNow();
    for (auto& row : state.rows) Write(row.title.get(), row.titleShown, metamob::TradeTitle(row.proposal, now));
}
void ShowProposals(const metamob::TradesView& view) {
    for (auto& row : state.rows) api.Call(row.box.get(), "RemoveFromHierarchy");
    state.rows.clear();
    for (const auto& proposal : view.proposals) {
        Row row;
        row.box.reset(Element(state.list.get()));
        row.proposal = proposal;
        Obj box = row.box.get();
        Pixels(box, "paddingTop", 8); Pixels(box, "paddingBottom", 2);
        row.title.reset(Label(box, true, 14));
        Obj where = Label(box, false); Text(where, metamob::TradeStateText(proposal)); Pixels(where, "paddingBottom", 4);
        Text(Label(box, true), metamob::TradeReceiveText(proposal));
        Obj gives = Label(box, true); Text(gives, metamob::TradeGiveText(proposal)); Pixels(gives, "paddingBottom", 8);
        Divider(box);
        state.rows.push_back(std::move(row));
    }
    ShowAges();
    state.version = view.version;
    state.listed = true;
}
void ShowNotify() {
    const int on = module->settings.metamobTradesNotify ? 1 : 0;
    if (on == state.notifyShown) return;
    int icon = on ? IconBellOn : IconBellOff;
    Text(state.notify.get(), on ? "Notifications : oui" : "Notifications : non");
    api.Call(state.notify.get(), "set_icon", {&icon});
    Text(state.notify.get(), on ? NotifyOnHelp : NotifyOffHelp, "set_tooltip");
    state.notifyShown = on;
}
// What the sync holds now, into every text of the window.
void Refresh(ULONGLONG now) {
    state.nextRefresh = now + RefreshMs;
    const auto view = metamob::MetamobTrades();
    Write(state.summary.get(), state.summaryShown, metamob::TradesSummary(view, now));
    // Built again only for another reading: a list being scrolled stays in place.
    if (!state.listed || view.version != state.version) { ShowProposals(view); state.nextAges = now + AgesMs; }
    else if (now >= state.nextAges) { ShowAges(); state.nextAges = now + AgesMs; }
    ShowNotify();
}
float Width(Obj root) {
    Ref resolved(api.Call(root, "get_resolvedStyle"));
    const float available = api.Value<float>(api.Call(resolved.get(), "get_width")) - 32;
    return std::isfinite(available) ? std::clamp(available, MinWidth, WindowWidth) : WindowWidth;
}
// The game's click on the window's own buttons (nativeClick).
bool Action(Obj owner) {
    if (!owner || (owner != state.closeButton.get() && owner != state.refresh.get() && owner != state.notify.get() &&
        owner != state.open.get())) return false;
    try {
        if (!wanted || !Current() || !api.Value<bool>(api.Call(owner, "get_enabledInHierarchy"))) return true;
        if (owner == state.closeButton.get()) wanted = false;
        else if (owner == state.refresh.get()) { metamob::RefreshTrades(); Log("METAMOB_TRADES_WINDOW_REFRESH"); }
        else if (owner == state.notify.get()) {
            Settings next = module->settings;
            next.metamobTradesNotify = !next.metamobTradesNotify;
            module->Change(next);
            Log(std::string("METAMOB_TRADES_NOTIFY ") + (next.metamobTradesNotify ? "on" : "off"));
            ShowNotify();
        } else {
            Ref url(api.string_new(MessagesPage));
            api.Invoke(openUrl, nullptr, {url.get()});
            Log("METAMOB_TRADES_WINDOW_SITE");
        }
    } catch (const std::exception& error) { LogLimited(std::string("METAMOB_TRADES_WINDOW_ACTION_ERROR ") + error.what()); }
    return true;
}
void Retain(const char* field) {
    Ref list(api.Field(state.window.get(), field));
    const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
    if (count < 0 || count > 32) throw std::runtime_error("Trades native manipulator bounds");
    for (int i = 0; i < count; ++i) state.manipulators.emplace_back(api.Call(list.get(), "get_Item", {&i}));
}
void Create(Obj root) {
    state.root.reset(root); state.dependencies.reset(module->FindService("fpt"));
    if (!state.dependencies.get() || api.object_get_class(state.dependencies.get()) != api.Class("Core.dll", "", "goc"))
        throw std::runtime_error("Trades native dependencies unavailable");
    state.cached.reset(api.Call(state.dependencies.get(), "bfri")); state.compositor.reset(module->compositor.get());
    Ref input(api.Call(state.dependencies.get(), "bfre"));
    if (state.cached.get() != module->cached.get() || !input.get() || api.object_get_class(input.get()) != api.Class("Core.dll", "", "eqo"))
        throw std::runtime_error("Trades native session mismatch");
    state.window.reset(api.New(api.Class("Core.dll", Components, "MagneticDragWindow")));
    Obj window = state.window.get();
    Text(window, "", "set_name"); Text(window, Title, "set_headerTitle");
    bool yes = true; int icon = IconExchange, size = HeaderLarge;
    api.Call(window, "set_headerIcon", {&icon}); api.Call(window, "set_headerSize", {&size});
    api.Call(window, "set_headerShowCloseButton", {&yes});
    for (const auto* setter : {"set_verticalResizable", "set_horizontalResizable", "set_cornerResizable"}) api.Call(window, setter, {&yes});
    api.Call(window, "set_hasBackground", {&yes});
    Enum(window, "position", Absolute); Float(window, "flexGrow", 0);
    Pixels(window, "width", Width(root)); Pixels(window, "minWidth", MinWidth); Pixels(window, "minHeight", MinHeight);
    state.header.reset(api.Call(window, "get_headerWidget"));
    state.closeButton.reset(api.Field(state.header.get(), "closeButton"));
    if (!state.closeButton.get()) throw std::runtime_error("Trades native close button unavailable");
    toolsUi::AttachHeader(Key, window);
    Ref draggers(api.Field(window, "m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(), "get_Item", {state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[] = {"ydf", "ydh", "ydj", "ydl"};
    for (size_t i = 0; i < 4; ++i) state.inputEvents[i].reset(api.Call(input.get(), getters[i]));
    api.Call(window, "Init", {state.dependencies.get()});
    Ref content(toolsUi::ScrollBody(window, root, true));
    Float(content.get(), "flexGrow", 1);
    for (const auto* property : {"paddingLeft", "paddingRight", "paddingTop", "paddingBottom"}) Pixels(content.get(), property, Padding);
    // What the last reading found, then the window's three buttons on their own line.
    state.summary.reset(Label(content.get(), true));
    Pixels(state.summary.get(), "paddingBottom", 8); Text(state.summary.get(), SummaryHelp, "set_tooltip");
    Ref buttons(Element(content.get()));
    Enum(buttons.get(), "flexDirection", RowDirection); Enum(buttons.get(), "alignItems", AlignCenter); Enum(buttons.get(), "flexWrap", WrapLines);
    Pixels(buttons.get(), "paddingBottom", 8);
    const auto gap = [&] { Pixels(Element(buttons.get()), "width", 6); };
    state.refresh.reset(Button(buttons.get(), "Actualiser", IconRefresh, RefreshHelp));
    gap();
    state.notify.reset(Button(buttons.get(), "Notifications", IconBellOn, NotifyOnHelp));
    gap();
    state.open.reset(Button(buttons.get(), "Ouvrir Metamob", IconExternal, OpenHelp));
    Divider(content.get());
    state.list.reset(Element(content.get()));
    Refresh(GetTickCount64());
    Add(root, window);
    Log("METAMOB_TRADES_WINDOW_OPEN native=true");
}
// The native parts of the window, checked against the pinned client; the
// window itself, its header, drag and input contracts are the soul panel's,
// checked by InstallSoulHelper first.
void Verify() {
    auto button = api.Class("Core.dll", Figma, "DofusButtonCustom");
    api.FindVerified(button, ".ctor", 0, 0x16FF0E0); api.FindVerified(button, "set_icon", 1, 0x16FF9D0); api.FindVerified(button, "set_text", 1, 0x16FFFE0);
    api.FindVerified(button, "set_mainStyle", 1, 0x16FFC00); api.FindVerified(button, "set_size", 1, 0x16FFD20);
    auto divider = api.Class("Core.dll", Figma, "Divider");
    api.FindVerified(divider, ".ctor", 0, 0x16FD600); api.FindVerified(divider, "set_componentStyle", 1, 0x16FD7B0); api.FindVerified(divider, "set_orientation", 1, 0x16FDB50);
    api.FindVerified(api.Class("Core.dll", Components, "ResizableWindow"), "set_headerShowCloseButton", 1, 0x15B1750);
    auto element = api.Class(Ui, UiSpace, "VisualElement");
    api.FindVerified(element, ".ctor", 0, 0x55BC890); api.FindVerified(element, "Add", 1, 0x55A72B0); api.FindVerified(element, "RemoveFromHierarchy", 0, 0x55AF3B0);
    api.FindVerified(element, "BringToFront", 0, 0x55A80C0); api.FindVerified(element, "get_parent", 0, 0x55BE960); api.FindVerified(element, "get_panel", 0, 0x55BE950);
    api.FindVerified(element, "get_style", 0, 0x55BEC80); api.FindVerified(element, "get_resolvedStyle", 0, 0x55BEAC0);
    api.FindVerified(element, "set_name", 1, 0x55C0E50); api.FindVerified(element, "set_tooltip", 1, 0x55C1290);
    api.FindVerified(api.Class(Ui, UiSpace, "Label"), ".ctor", 0, 0x56F0F40);
    auto text = api.Class(Ui, UiSpace, "TextElement");
    api.FindVerified(text, "set_text", 1, 0x5612F60); api.FindVerified(text, "set_enableRichText", 1, 0x56129C0);
    api.FindVerified(api.Class(Ui, UiSpace, "ResolvedStyleAccess"), "get_width", 0, 0x55A1C40);
    // The inline styles written: each property's getter (its current value) and setter.
    auto styleAccess = api.Class(Ui, UiSpace, "InlineStyleAccess");
    const struct { const char* name; int argc; uintptr_t rva; } styles[] = {
        {"UnityEngine.UIElements.IStyle.get_display", 0, 0x5598A50}, {"UnityEngine.UIElements.IStyle.set_display", 1, 0x559AFD0},
        {"UnityEngine.UIElements.IStyle.get_flexDirection", 0, 0x5598BA0}, {"UnityEngine.UIElements.IStyle.set_flexDirection", 1, 0x559B260},
        {"UnityEngine.UIElements.IStyle.get_alignItems", 0, 0x5598440}, {"UnityEngine.UIElements.IStyle.set_alignItems", 1, 0x559A050},
        {"UnityEngine.UIElements.IStyle.get_flexWrap", 0, 0x5598C50}, {"UnityEngine.UIElements.IStyle.set_flexWrap", 1, 0x559B430},
        {"UnityEngine.UIElements.IStyle.get_whiteSpace", 0, 0x5599E90}, {"UnityEngine.UIElements.IStyle.set_whiteSpace", 1, 0x559DB50},
        {"UnityEngine.UIElements.IStyle.get_position", 0, 0x5599210}, {"UnityEngine.UIElements.IStyle.set_position", 1, 0x559C090},
        {"UnityEngine.UIElements.IStyle.get_flexGrow", 0, 0x5598C30}, {"UnityEngine.UIElements.IStyle.set_flexGrow", 1, 0x559B310},
        {"UnityEngine.UIElements.IStyle.get_flexShrink", 0, 0x5598C40}, {"UnityEngine.UIElements.IStyle.set_flexShrink", 1, 0x559B3A0},
        {"UnityEngine.UIElements.IStyle.get_fontSize", 0, 0x5598CE0}, {"UnityEngine.UIElements.IStyle.set_fontSize", 1, 0x559B4E0},
        {"UnityEngine.UIElements.IStyle.get_color", 0, 0x55989B0}, {"UnityEngine.UIElements.IStyle.set_color", 1, 0x559ADB0},
        {"UnityEngine.UIElements.IStyle.get_width", 0, 0x5599F20}, {"UnityEngine.UIElements.IStyle.set_width", 1, 0x559DBD0},
        {"UnityEngine.UIElements.IStyle.get_minWidth", 0, 0x5599030}, {"UnityEngine.UIElements.IStyle.set_minWidth", 1, 0x559BBF0},
        {"UnityEngine.UIElements.IStyle.get_minHeight", 0, 0x5598FF0}, {"UnityEngine.UIElements.IStyle.set_minHeight", 1, 0x559BB50},
        {"UnityEngine.UIElements.IStyle.get_paddingLeft", 0, 0x5599150}, {"UnityEngine.UIElements.IStyle.set_paddingLeft", 1, 0x559BEB0},
        {"UnityEngine.UIElements.IStyle.get_paddingRight", 0, 0x5599190}, {"UnityEngine.UIElements.IStyle.set_paddingRight", 1, 0x559BF50},
        {"UnityEngine.UIElements.IStyle.get_paddingTop", 0, 0x55991D0}, {"UnityEngine.UIElements.IStyle.set_paddingTop", 1, 0x559BFF0},
        {"UnityEngine.UIElements.IStyle.get_paddingBottom", 0, 0x5599110}, {"UnityEngine.UIElements.IStyle.set_paddingBottom", 1, 0x559BE10}};
    for (const auto& style : styles) api.FindVerified(styleAccess, style.name, style.argc, style.rva);
    unsubscribe = api.FindVerified(api.Class("Core.dll", "Core.Engine.Inputs.Events", "InputCustomEvent"), "UnSubscribe", 1, 0x20E65D0);
    openUrl = api.FindVerified(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"), "OpenURL", 1, 0x5374230);
}
// The option is on, or the window is open: the sync keeps the proposals fresh.
void Ask() {
    if (!Session()) return;
    const auto now = GetTickCount64();
    if (!enteredAt || now - askedAt >= GapMs) enteredAt = now; // off the world, the pumps do not come here
    askedAt = now;
    if (now - enteredAt < EntryMs) return;
    const bool notify = module->settings.metamobTradesNotify;
    if (notify || wanted) metamob::WantTrades(notify);
}
}

void InstallTradesWindow() {
    if (installed) return;
    try { Verify(); }
    catch (const std::exception& error) { Log(std::string("METAMOB_TRADES_WINDOW_DISABLED ") + error.what()); return; }
    nativeClick::AddHandler(Action);
    // The top-bar button, after the statistics', while a usable Metamob key is
    // stored; the ⋯ menu's « Masquer le module » closes the window.
    toolsUi::Register(Key, Title, IconExchange, [] { return wanted; }, [] { wanted = !wanted; return true; }, {},
        [] { return metamob::MetamobKeyUsable(); });
    installed = true;
    Log("METAMOB_TRADES_WINDOW_READY");
}
void RestoreTradesWindow() {
    wanted = false;
    if (!state.window.get()) { state = State{}; return; }
    state.cleanup = true;
    try {
        Enum(state.window.get(), "display", DisplayNone);
        toolsUi::DetachHeader(Key);
        const auto mouse = api.Class(Ui, UiSpace, "MouseCaptureController");
        if (state.header.get() && api.Value<bool>(api.Invoke(api.Find(mouse, "HasMouseCapture", 1), nullptr, {state.header.get()}))) {
            *static_cast<bool*>(api.FieldAddress(state.dragger.get(), "m_active")) = false;
            api.Invoke(api.Find(mouse, "ReleaseMouse", 1), nullptr, {state.header.get()});
            // Only the window that still owns capture may clear the drag flag.
            reinterpret_cast<void(*)(bool, Method)>(api.NativeAt(0x165F920))(false, nullptr);
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
            if (count < 0 || count > 16384) throw std::runtime_error("Trades input subscriber bounds");
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
    } catch (const std::exception& error) { LogLimited(std::string("METAMOB_TRADES_WINDOW_CLEANUP_ERROR ") + error.what()); }
}
bool TradesWindowCleanupPending() { return state.cleanup; }
void TickTradesWindow() {
    // The chat notices need no window: asked for even when its contracts do not hold.
    try { Ask(); } catch (const std::exception& error) { LogLimited(std::string("METAMOB_TRADES_ASK_ERROR ") + error.what()); }
    if (!installed) return;
    if (!wanted || !Session() || state.cleanup || (state.window.get() && !Current())) { RestoreTradesWindow(); return; }
    if (!state.window.get()) {
        try {
            Ref root(Root());
            if (!root.get() || !api.Call(root.get(), "get_panel")) return;
            Create(root.get());
        } catch (const std::exception& error) { Log(std::string("METAMOB_TRADES_WINDOW_CREATE_ERROR ") + error.what()); RestoreTradesWindow(); }
        return;
    }
    try {
        const auto now = GetTickCount64();
        if (now >= state.nextRefresh) Refresh(now);
    } catch (const std::exception& error) { LogLimited(std::string("METAMOB_TRADES_WINDOW_ERROR ") + error.what()); }
}
