#include "notebook/notebook-native.hpp"
#include "core/module.hpp"
#include "travel/chat-travel.hpp"
#include "souls/soul-panel.hpp"
#include <algorithm>
#include <cstring>
#include <map>

namespace notebook {
namespace {
using namespace managed;
Method currentMap{}, mapInfo{}, subarea{}, world{}, inputGetter{}, lengthValid{}, append{}, trySend{}, worldById{}, worldName{};
Method zoneName{}; // Optional: without it, a position added from a map is titled with its coordinates.
Klass characterClass{}, windowsClass{}, chatClass{}, inputClass{}, bindingClass{};
// Map pins: the game's flag messages (ccn adds or moves a flag, cco removes
// one), handled by its cartography controller fhk (the fkc service) exactly as
// when the message bus delivers them; fhk.beop gives its map views.
Klass cartographyClass{}, flagClass{}, unflagClass{};
Method flagCtor{}, unflagCtor{}, addFlag{}, removeFlag{}, mapView{};
bool pinsReady = false;
// Row menus: the game's right-click menu on the panel's rows.
Klass manipulatorClass{};
Method manipulatorCtor{}, addManipulator{}, removeManipulator{}, menuOf{}, appendTitle{}, appendItem{}, itemCallback{}, stopPropagation{};
bool rowMenusReady = false;
// FigmaIcons of the row menu: zaap (travel), flag (show the pin), eyeSlashed (hide it).
constexpr int IconTravel = 336, IconShow = 145, IconHide = 137;
// System.ReadOnlySpan<char>: a managed char pointer and a length, passed by value.
struct CharSpan { const wchar_t* data; int32_t length; };
void Ready() {
    if (!module || !module->ready || !module->servicesReady || module->stopped || module->stopRequested)
        throw std::runtime_error("Personnage indisponible.");
}
void Field(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error("Notebook native field unavailable");
}
std::string Coordinates(int x, int y) { return "[" + std::to_string(x) + "," + std::to_string(y) + "]"; }
// The user's text in the game's labels that read rich text (flag tooltip, menu
// title): shown as written, a '<' never opens a tag (it becomes '‹').
std::string Plain(std::string text) {
    for (auto at = text.find('<'); at != std::string::npos; at = text.find('<', at + 3)) text.replace(at, 1, "\xE2\x80\xB9");
    return text;
}
// The entry as a valid position (title aside), as the trip checks it.
void CheckPosition(const Entry& entry) {
    Entry checked = entry; checked.kind = Kind::Position; checked.title = "Position"; checked.body.clear(); checked.channel = 0;
    Validate(checked);
}
void InstallPins() {
    cartographyClass = api.Class("Core.dll", "", "fhk");
    flagClass = api.Class("Core.dll", "", "ccn"); unflagClass = api.Class("Core.dll", "", "cco");
    // fhw.duiw: the flags of a map view, by world map id then flag id; fhw.duix:
    // the Phoenix flag of a ghost, which fhw.beuw then uses for any new flag.
    Field(api.Class("Core.dll", "", "fhw"), "duiw", 0x1F8);
    Field(api.Class("Core.dll", "", "fhw"), "duix", 0x200);
    flagCtor = api.FindVerified(flagClass, ".ctor", 10, 0x267D110);
    unflagCtor = api.FindVerified(unflagClass, ".ctor", 2, 0x267D290);
    addFlag = api.FindVerified(cartographyClass, "beoo", 1, 0x1C49250);
    removeFlag = api.FindVerified(cartographyClass, "beon", 1, 0x1C49100);
    mapView = api.FindVerified(cartographyClass, "beop", 1, 0x1C49400);
}
bool PopulateRowMenu(Obj owner, Obj event);
bool RowMenuCallback(Obj callback, Obj view);
void InstallRowMenus() {
    constexpr const char* menus = "Core.UILogic.Components.ContextMenu";
    constexpr const char* ui = "UnityEngine.UIElementsModule.dll";
    constexpr const char* uiSpace = "UnityEngine.UIElements";
    manipulatorClass = api.Class("Core.dll", menus, "DofusContextualMenuManipulator");
    manipulatorCtor = api.FindVerified(manipulatorClass, ".ctor", 4, 0x16D0BB0);
    auto extensions = api.Class(ui, uiSpace, "VisualElementExtensions");
    addManipulator = api.FindVerified(extensions, "AddManipulator", 2, 0x56755F0);
    removeManipulator = api.FindVerified(extensions, "RemoveManipulator", 2, 0x5676A90);
    menuOf = api.FindVerified(api.Class("Core.dll", menus, "DofusContextualMenuPopulateEvent"), "get_menu", 0, 0x11E7EC0);
    auto menuClass = api.Class("Core.dll", menus, "DofusContextualMenu");
    appendTitle = api.FindVerified(menuClass, "AppendTitle", 1, 0x16D7F70);
    appendItem = api.FindVerified(menuClass, "AppendDefaultItem", 13, 0x16D6C60);
    itemCallback = api.FindVerified(api.Class("Core.dll", menus, "ContextMenuItem"), "get_m_callback", 0, 0x16CF040);
    stopPropagation = api.FindVerified(api.Class(ui, uiSpace, "EventBase"), "StopPropagation", 0, 0x576D9C0);
    soul::SetAuxiliaryPanelMenus(PopulateRowMenu, RowMenuCallback);
}
}
void InstallNative() {
    characterClass = api.Class("Core.dll", "", "esg");
    windowsClass = api.Class("Core.dll", "", "ghz");
    chatClass = api.Class("Core.dll", "Core.UILogic.Chat", "ChatView");
    inputClass = api.Class("Core.dll", "Core.UILogic.Components.Figma", "TextInput");
    bindingClass = api.Class("Core.dll", "Core.UILogic.Bindings", "ChatviewBinding");
    Field(windowsClass, "dyvx", 0x258);
    currentMap = api.FindVerified(characterClass, "zeo", 0, 0x185C1E0);
    constexpr const char* image = "Ankama.Dofus.Core.DataCenter.dll";
    constexpr const char* space = "Core.DataCenter.Metadata.World";
    auto info = api.Class(image, space, "MapInformationData");
    Field(info, "posX", 0x18); Field(info, "posY", 0x1A);
    Field(api.Class(image, space, "WorldPoint"), "m_mapId", 0x10);
    mapInfo = api.FindVerified(info, "GetMapInformationById", 1, 0x7C4200);
    subarea = api.FindVerified(info, "get_subArea", 0, 0x7C44B0);
    world = api.FindVerified(api.Class(image, space, "SubAreaData"), "get_worldmapId", 0, 0x7D1040);
    auto worldClass = api.Class(image, space, "WorldMapData");
    worldById = api.FindVerified(worldClass, "GetWorldMapById", 1, 0x7D3D50);
    worldName = api.FindVerified(worldClass, "get_name", 0, 0x7D3DE0);
    inputGetter = api.FindVerified(bindingClass, "get_ti_chatInput", 0, 0x2EB8FD0);
    lengthValid = api.FindVerified(chatClass, "IsTextLengthValid", 1, 0x2D92630);
    append = api.FindVerified(chatClass, "AppendTextToInputAndFocus", 1, 0x2D8E010);
    // The channel ChatView's own SendToGuild/Party/TeamShortcut set before
    // validating the input, read back by TrySendMessage.
    Field(chatClass, "m_sendingChannel", 0x1EC);
    trySend = api.FindVerified(chatClass, "TrySendMessage", 1, 0x2D9D300);
    // Optional parts: each one that does not match disables only itself.
    try { zoneName = api.FindVerified(api.Class(image, space, "SubAreaData"), "get_name", 0, 0x7D0F60); }
    catch (const std::exception& error) { zoneName = nullptr; Log(std::string("NOTEBOOK_ZONE_NAME_UNAVAILABLE ") + error.what()); }
    try { InstallPins(); pinsReady = true; }
    catch (const std::exception& error) { pinsReady = false; Log(std::string("NOTEBOOK_PIN_UNAVAILABLE ") + error.what()); }
    try { InstallRowMenus(); rowMenusReady = true; }
    catch (const std::exception& error) { rowMenusReady = false; Log(std::string("NOTEBOOK_ROW_MENU_UNAVAILABLE ") + error.what()); }
}
namespace {
// The live chat view and its input, as the user sees them; throws otherwise.
struct Chat { Ref compositor, service, view, input; };
Chat OpenChat(const char* closed) {
    Chat chat;
    chat.compositor.reset(module->compositor.get()); chat.service.reset(module->FindService("fqw"));
    if (!chat.service.get() || api.object_get_class(chat.service.get()) != windowsClass) throw std::runtime_error("Chat indisponible.");
    chat.view.reset(api.Field(chat.service.get(), "dyvx"));
    if (!chat.view.get() || api.object_get_class(chat.view.get()) != chatClass) throw std::runtime_error("Chat indisponible.");
    Ref binding(api.Field(chat.view.get(), "dzyi"));
    if (!binding.get() || api.object_get_class(binding.get()) != bindingClass) throw std::runtime_error("Chat indisponible.");
    chat.input.reset(api.Invoke(inputGetter, api.object_unbox(binding.get())));
    if (!chat.input.get() || api.object_get_class(chat.input.get()) != inputClass || !api.Call(chat.input.get(), "get_panel") ||
        !api.Value<bool>(api.Call(chat.input.get(), "get_enabledInHierarchy")))
        throw std::runtime_error(closed);
    return chat;
}
void SameChat(const Chat& chat) {
    Ready();
    if (module->compositor.get() != chat.compositor.get() || module->FindService("fqw") != chat.service.get() ||
        api.Field(chat.service.get(), "dyvx") != chat.view.get() || !api.Call(chat.input.get(), "get_panel"))
        throw std::runtime_error("Le chat a changé. Réessaie.");
}
}
Entry CurrentPosition() {
    Ready();
    Ref compositor(module->compositor.get()), character(module->FindService("fng"));
    if (!character.get() || api.object_get_class(character.get()) != characterClass)
        throw std::runtime_error("Position actuelle indisponible.");
    Ref location(api.Invoke(currentMap, character.get()));
    if (!location.get()) throw std::runtime_error("Position actuelle indisponible.");
    auto map = managed::Read<int64_t>(location.get(), 0x10);
    if (map <= 0) throw std::runtime_error("Carte actuelle indisponible.");
    Ref info(api.Invoke(mapInfo, nullptr, {&map}));
    if (!info.get()) throw std::runtime_error("Cette carte ne possède pas de coordonnées publiques.");
    Ref area(api.Invoke(subarea, info.get()));
    if (!area.get()) throw std::runtime_error("Monde de cette carte indisponible.");
    Entry result;
    result.map = map; result.x = managed::Read<int16_t>(info.get(), 0x18); result.y = managed::Read<int16_t>(info.get(), 0x1A);
    result.world = api.Value<int>(api.Invoke(world, area.get()));
    Ready();
    Ref after(api.Invoke(currentMap, character.get()));
    if (module->compositor.get() != compositor.get() || module->FindService("fng") != character.get() ||
        !after.get() || managed::Read<int64_t>(after.get(), 0x10) != map)
        throw std::runtime_error("La carte a changé. Réessaie.");
    return result;
}
std::vector<World> Worlds() {
    Ready();
    std::vector<World> result;
    // World map ids are small; the game's lookup returns nothing for unused ids.
    for (int id = 1; id <= 64 && result.size() < 32; ++id) {
        Ref data;
        try { data.reset(api.Invoke(worldById, nullptr, {&id})); } catch (const std::exception&) { continue; }
        if (!data.get()) continue;
        Ref name(api.Invoke(worldName, data.get()));
        const auto text = api.String(name.get());
        if (text.empty()) continue;
        result.push_back({id, text});
    }
    if (result.empty()) result.push_back({1, "Monde des Douze"});
    return result;
}
void Travel(const Entry& entry) {
    Ready();
    if (module->InCombat()) throw std::runtime_error("Trajet indisponible pendant un combat.");
    CheckPosition(entry);
    StartChatTravel({entry.x, entry.y, entry.world});
}
void PrepareMessage(const std::string& text) {
    Ready();
    Entry checked; checked.kind = Kind::Message; checked.title = "Message"; checked.body = text; Validate(checked);
    auto chat = OpenChat("Ouvre le chat pour préparer le message.");
    Ref message(api.string_new(text.c_str()));
    if (!api.Value<bool>(api.Invoke(lengthValid, chat.view.get(), {message.get()})))
        throw std::runtime_error("Le brouillon du chat est trop long pour ajouter ce message.");
    SameChat(chat);
    // Native append/focus only. The user sends their draft themselves.
    api.Invoke(append, chat.view.get(), {message.get()});
}
void SendChatMessage(const std::string& text, int channel) {
    Ready();
    if (!ValidChannel(channel)) throw std::runtime_error("Canal invalide.");
    Entry checked; checked.kind = Kind::Message; checked.title = "Message"; checked.body = text; checked.channel = channel; Validate(checked);
    auto chat = OpenChat("Ouvre le chat pour envoyer le message.");
    Ref message(api.string_new(text.c_str()));
    if (!api.Value<bool>(api.Invoke(lengthValid, chat.view.get(), {message.get()})))
        throw std::runtime_error("Message trop long pour le chat.");
    SameChat(chat);
    // ChatView.TrySendMessage is what Enter does in the chat input: channel
    // shortcuts, commands, receiver, history and the request itself are the
    // game's. The chosen channel goes through m_sendingChannel exactly like the
    // view's own SendToGuildShortcut, and the previous value is put back.
    auto* sending = static_cast<int*>(api.FieldAddress(chat.view.get(), "m_sendingChannel"));
    if (!sending) throw std::runtime_error("Chat indisponible.");
    const int previous = *sending;
    if (channel) *sending = channel;
    CharSpan span{api.string_chars(message.get()), api.string_length(message.get())};
    bool accepted = false;
    try { accepted = api.Value<bool>(api.Invoke(trySend, chat.view.get(), {&span})); }
    catch (...) { *sending = previous; throw; }
    *sending = previous;
    if (!accepted) throw std::runtime_error("Le chat n’a pas accepté ce message.");
}
void Copy(const std::string& text) {
    if (text.empty()) throw std::runtime_error("Aucun texte à copier.");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Texte invalide.");
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<size_t>(count) + 1) * sizeof(wchar_t));
    if (!memory) throw std::runtime_error("Copie impossible.");
    auto* content = static_cast<wchar_t*>(GlobalLock(memory));
    if (!content) {GlobalFree(memory); throw std::runtime_error("Copie impossible.");}
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), content, count);
    content[count] = L'\0'; GlobalUnlock(memory);
    const auto window = GetActiveWindow();
    if (!window || !OpenClipboard(window)) {GlobalFree(memory); throw std::runtime_error("Presse-papiers occupé. Réessaie.");}
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) {GlobalFree(memory); throw std::runtime_error("Copie impossible.");}
}
namespace {
// The flag id of a position: the "flag_" prefix gives it the game's own flag
// menu (remove, travel); nothing in it is read as a chat, custom (saved by the
// game), quest or player-position flag.
std::string PinId(uint64_t id) { return "flag_dofusenhanced_notebook_" + std::to_string(id); }
// Light blue, told apart from the player's own flags.
constexpr float PinColor[4] = {0.25f, 0.7f, 1.f, 1.f};
// The game's cartography controller, as registered for its map views.
Obj Cartography() {
    Ready();
    if (!pinsReady) throw std::runtime_error("Repères de carte indisponibles.");
    const auto service = module->FindService("fkc");
    if (!service || api.object_get_class(service) != cartographyClass) throw std::runtime_error("Carte du jeu indisponible.");
    return service;
}
// The flags this module placed, by entry id, with the position as placed.
std::map<uint64_t, Entry> placed;
void RemoveFlag(Obj cartography, uint64_t id) {
    Ref text(api.string_new(PinId(id).c_str())), message(api.object_new(unflagClass));
    if (!message.get()) throw std::runtime_error("Repère de carte indisponible.");
    int everyWorld = -1;
    api.Invoke(unflagCtor, message.get(), {text.get(), &everyWorld});
    api.Invoke(removeFlag, cartography, {message.get()});
    placed.erase(id);
}
}
bool PinsAvailable() { return pinsReady; }
bool PinShown(const Entry& entry) {
    if (!entry.id) return false;
    Ref cartography(Cartography());
    Ref id(api.string_new(PinId(entry.id).c_str()));
    // The mini-map (0) and the big map (1) hold the same flags; either will do.
    for (int kind = 0; kind < 2; ++kind) {
        Ref view(api.Invoke(mapView, cartography.get(), {&kind}));
        if (!view.get()) continue;
        Ref worlds(api.Field(view.get(), "duiw"));
        int key = entry.world;
        if (!worlds.get() || !api.Value<bool>(api.Call(worlds.get(), "ContainsKey", {&key}))) continue;
        Ref flags(api.Call(worlds.get(), "get_Item", {&key}));
        if (flags.get() && api.Value<bool>(api.Call(flags.get(), "ContainsKey", {id.get()}))) return true;
    }
    return false;
}
void ShowPin(const Entry& entry) {
    if (!entry.id) throw std::runtime_error("Enregistre d’abord cette position.");
    CheckPosition(entry);
    Ref cartography(Cartography());
    // A ghost's map shows its Phoenix flag, and the game then draws any new flag
    // with the Phoenix's own object (fhw.beuw, fhw.duix): no pin meanwhile.
    for (int kind = 0; kind < 2; ++kind) {
        Ref view(api.Invoke(mapView, cartography.get(), {&kind}));
        if (view.get() && api.Field(view.get(), "duix"))
            throw std::runtime_error("Repère impossible tant que ton personnage est un fantôme (drapeau du Phénix sur la carte).");
    }
    // One flag per position: a flag left on another world (edited entry) goes first.
    RemoveFlag(cartography.get(), entry.id);
    const auto title = entry.title.empty() ? std::string("Position") : entry.title;
    const auto legend = "Carnet : " + Plain(title) + " (" + std::to_string(entry.x) + "," + std::to_string(entry.y) + ")";
    Ref id(api.string_new(PinId(entry.id).c_str())), text(api.string_new(legend.c_str())), message(api.object_new(flagClass));
    if (!message.get()) throw std::runtime_error("Repère de carte indisponible.");
    int worldId = entry.world, x = entry.x, y = entry.y;
    float color[4]; std::memcpy(color, PinColor, sizeof(color));
    // As a chat position link places its flag, without its toggle (shown again,
    // the flag moves) and without opening the map; the player can remove it
    // from the flag's own menu.
    bool toggle = false, openMap = false, removable = true, last = false;
    api.Invoke(flagCtor, message.get(), {id.get(), text.get(), &worldId, &x, &y, color, &toggle, &openMap, &removable, &last});
    api.Invoke(addFlag, cartography.get(), {message.get()});
    // The game keeps flags only for the worlds it draws.
    if (!PinShown(entry)) throw std::runtime_error("La carte du jeu ne connaît pas ce monde : repère impossible.");
    placed[entry.id] = entry;
}
void HidePin(uint64_t id) {
    if (!id) return;
    Ref cartography(Cartography());
    RemoveFlag(cartography.get(), id);
}
std::vector<Entry> PlacedPins() {
    std::vector<Entry> result;
    for (auto pin = placed.begin(); pin != placed.end();) {
        // Gone from the map (its own menu, a new session): forgotten, never put back.
        if (!PinShown(pin->second)) { pin = placed.erase(pin); continue; }
        result.push_back(pin->second); ++pin;
    }
    return result;
}
namespace {
// The zone of the clicked map (the name the game shows for its subarea), else
// the coordinates.
std::string ZoneTitle(const Entry& entry) {
    const auto fallback = "Position " + Coordinates(entry.x, entry.y);
    if (entry.map <= 0 || !zoneName) return fallback;
    try {
        int64_t map = entry.map;
        Ref info(api.Invoke(mapInfo, nullptr, {&map}));
        if (!info.get()) return fallback;
        Ref area(api.Invoke(subarea, info.get()));
        if (!area.get()) return fallback;
        Ref name(api.Invoke(zoneName, area.get()));
        Entry probe = entry; probe.title = api.String(name.get());
        Validate(probe); // Empty, too long or with control characters: the fallback.
        return probe.title;
    } catch (const std::exception&) { return fallback; }
}
}
Added AddMapPosition(int x, int y, int worldId, int64_t map) {
    Ready();
    Entry entry; entry.kind = Kind::Position; entry.x = x; entry.y = y; entry.world = worldId; entry.map = map > 0 ? map : 0;
    entry.title = "Position"; Validate(entry);
    entry.title = ZoneTitle(entry);
    // The duplicate check and the save run under the notebook's writers' lock.
    Added result;
    result.entry = AddPosition(DataPath(module->settingsPath), std::move(entry), result.created);
    return result;
}
namespace {
struct RowItem { Ref item, callback; int action = 0; };
enum RowAction { TravelAction, ShowAction, HideAction };
struct RowMenus {
    RowMenu handlers;
    std::vector<std::pair<Ref, Ref>> rows; // Each row and its right-button manipulator.
    Ref menu, cached, compositor;
    Entry entry;
    std::vector<RowItem> items;
    bool armed = false;
} menus;
Obj MenuRoot(Obj menu) {
    if (!menu) return nullptr;
    const auto root = api.Field(menu, "dofusContextualMenuRoot");
    return root && api.Call(root, "get_menuData") == menu ? root : nullptr;
}
bool Present(Obj menu, Obj item) {
    Ref rows(api.Call(menu, "get_menuItems"));
    const int count = rows.get() ? api.Value<int>(api.Call(rows.get(), "get_Count")) : 0;
    if (count < 0 || count > 64) return false;
    for (int i = 0; i < count; ++i) if (api.Call(rows.get(), "get_Item", {&i}) == item) return true;
    return false;
}
void CloseRowMenu() {
    menus.armed = false; menus.items.clear();
    Ref menu(menus.menu.get()); menus.menu.reset();
    Ref root(MenuRoot(menu.get()));
    if (root.get() && api.Call(root.get(), "get_panel")) { bool delayed = false; api.Call(root.get(), "RemoveMenu", {&delayed}); }
}
void AppendRow(Obj menu, const char* text, const char* help, int icon, int action) {
    Ref rows(api.Call(menu, "get_menuItems"));
    int index = api.Value<int>(api.Call(rows.get(), "get_Count"));
    if (index < 0 || index > 64) throw std::runtime_error("Notebook row menu bounds");
    Ref label(api.string_new(text)), tooltip(api.string_new(help)), empty(api.string_new(""));
    Ref asset(api.object_new(api.class_from_type(api.method_get_param(appendItem, 7))));
    Ref color(api.object_new(api.class_from_type(api.method_get_param(appendItem, 10))));
    bool disabled = false, close = true; unsigned delay = 500; int style = 1, figma = icon;
    // A null managed action: only this module's callback router acts on the row.
    api.Invoke(appendItem, menu, {label.get(), nullptr, &disabled, &close, tooltip.get(), &delay, empty.get(),
        api.object_unbox(asset.get()), &style, &figma, api.object_unbox(color.get()), empty.get(), empty.get()});
    RowItem item; item.item.reset(api.Call(rows.get(), "get_Item", {&index}));
    item.callback.reset(api.Invoke(itemCallback, item.item.get()));
    if (!item.callback.get()) throw std::runtime_error("Notebook row menu callback unavailable");
    item.action = action; menus.items.push_back(std::move(item));
}
bool PopulateRowMenu(Obj owner, Obj event) {
    const auto found = std::find_if(menus.rows.begin(), menus.rows.end(), [&](const std::pair<Ref, Ref>& row) { return owner && row.second.get() == owner; });
    if (found == menus.rows.end()) return false;
    try {
        CloseRowMenu();
        if (!event) return true;
        Entry entry;
        Ref menu(api.Invoke(menuOf, event));
        const bool ready = module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
        if (!menu.get() || !ready || !menus.handlers.entry || !menus.handlers.entry(found->first.get(), entry) || entry.kind != Kind::Position) {
            // Nothing to offer: an empty menu must not be displayed (the game
            // would add a null root); a stopped populate event is not shown.
            api.Invoke(stopPropagation, event);
            return true;
        }
        menus.menu.reset(menu.get()); menus.entry = entry;
        menus.cached.reset(module->cached.get()); menus.compositor.reset(module->compositor.get());
        Ref title(api.string_new(Plain(entry.title + " " + Coordinates(entry.x, entry.y)).c_str()));
        api.Invoke(appendTitle, menu.get(), {title.get()});
        AppendRow(menu.get(), "Voyager", "Lancer le trajet du mod vers cette position : zaaps, bateaux puis /travel.", IconTravel, TravelAction);
        if (pinsReady) {
            bool shown = false;
            try { shown = PinShown(entry); } catch (const std::exception&) {}
            if (shown) AppendRow(menu.get(), "Retirer de la carte", "Retirer le repère de cette position de la grande carte et de la mini-carte.", IconHide, HideAction);
            else AppendRow(menu.get(), "Afficher sur la carte", "Placer un repère sur cette position, visible sur la grande carte et la mini-carte.", IconShow, ShowAction);
        }
        menus.armed = true;
    } catch (const std::exception& error) {
        menus.armed = false;
        Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what());
        try { CloseRowMenu(); if (event) api.Invoke(stopPropagation, event); } catch (const std::exception&) {}
    }
    return true;
}
bool RowMenuCallback(Obj callback, Obj view) {
    if (!callback) return false;
    const auto found = std::find_if(menus.items.begin(), menus.items.end(), [&](const RowItem& item) { return item.callback.get() == callback; });
    if (found == menus.items.end()) return false;
    const int action = found->action;
    const Entry entry = menus.entry;
    std::string status;
    try {
        Ref item(found->item.get());
        if (!menus.armed || !view || !api.Call(view, "get_panel") || api.Call(view, "get_itemData") != item.get() ||
            api.Value<bool>(api.Call(item.get(), "get_disabled")) || !Present(menus.menu.get(), item.get()) ||
            !module || module->cached.get() != menus.cached.get() || module->compositor.get() != menus.compositor.get()) return true;
        Ref root(MenuRoot(menus.menu.get()));
        if (!root.get() || !api.Call(root.get(), "get_panel")) return true;
        CloseRowMenu(); // Disarmed before the action, including a reentrant one.
        if (action == TravelAction) { Travel(entry); status = "Trajet demandé vers " + Coordinates(entry.x, entry.y) + "."; }
        else if (action == ShowAction) { ShowPin(entry); status = "Repère placé sur la carte en " + Coordinates(entry.x, entry.y) + " : grande carte et mini-carte."; }
        else { HidePin(entry.id); status = "Repère retiré de la carte."; }
        Log("NOTEBOOK_ROW_MENU action=" + std::string(action == TravelAction ? "travel" : action == ShowAction ? "show" : "hide"));
    } catch (const std::exception& error) { status = error.what(); Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what()); }
    if (!status.empty() && menus.handlers.done) {
        try { menus.handlers.done(entry, status); }
        catch (const std::exception& error) { Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what()); }
    }
    return true;
}
// The rows' populate event reaches this module only through the soul panel's
// hook on DofusContextualMenuManipulator.OnContextualMenuEvent (0x16D04D0).
// Without that hook a right-click would bring an empty menu (the game then
// throws adding its null root): the rows get no menu.
bool PopulateHooked() { return soul::PanelMenusReady(); }
}
void SetRowMenu(RowMenu menu) { menus.handlers = menu; }
bool AttachRowMenu(Obj row) {
    if (!rowMenusReady || !row) return false;
    if (!PopulateHooked()) {
        static bool told = false;
        if (!told) { told = true; Log("NOTEBOOK_ROW_MENU_UNAVAILABLE populate hook inactive"); }
        return false;
    }
    try {
        Ready();
        Ref service(module->FindService("fpz"));
        if (!service.get()) throw std::runtime_error("Notebook row menu service unavailable");
        Ref manipulator(api.object_new(manipulatorClass));
        if (!manipulator.get()) throw std::runtime_error("Notebook row menu unavailable");
        // The game's contextual menu on the right button only (MouseButton 1).
        int right = 1; bool other = false;
        api.Invoke(manipulatorCtor, manipulator.get(), {service.get(), nullptr, &right, &other});
        api.Invoke(addManipulator, nullptr, {row, manipulator.get()});
        menus.rows.emplace_back(Ref(row), std::move(manipulator));
        return true;
    } catch (const std::exception& error) { Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what()); return false; }
}
void ReleaseRowMenus() {
    try { CloseRowMenu(); } catch (const std::exception& error) { Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what()); }
    for (auto& [row, manipulator] : menus.rows) {
        try { if (row.get() && manipulator.get()) api.Invoke(removeManipulator, nullptr, {row.get(), manipulator.get()}); }
        catch (const std::exception& error) { Log(std::string("NOTEBOOK_ROW_MENU_ERROR ") + error.what()); }
    }
    menus.rows.clear(); menus.cached.reset(); menus.compositor.reset();
}
}
