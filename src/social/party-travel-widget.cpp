#include "social/party-travel-widget.hpp"
#include "social/party-slot-layout.hpp"
#include "social/spouse-party-menu.hpp"
#include "travel/chat-travel.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cstdlib>
#include <vector>

// « Bouton de trajet sur les membres du groupe » (Settings::partyTravelButton,
// on by default, read on every tick and click): on the party slot of every
// other accepted member (not the player, not a companion, not an invited guest;
// not the spouse either while the spouse widget shows its own teleport button
// there), the same native secondary toggle as the spouse's, with the zaap
// icon. Its tooltip gives the member's last position known to the game; a
// click travels there: the mod's route (StartChatTravel) with « Remplacer
// l’autopilote du jeu » ticked, else the game's own autopilot to a position
// (esg.zhy, what the map's « Voyager jusqu’à cette position » calls through
// fhw.betn). Nothing is sent without a click.
//
// The position: the game's party member (lg) keeps each member's world
// coordinates and subarea, written from the server's party member information
// by lg's constructors and its update (lg.eef: ddll 0x68 worldX, ddlm 0x6C
// worldY, ddln 0x70 Int16 subAreaId). The game itself shows them in its own
// join-party window: Core.UILogic.Party.JoinPartyMember's tooltip formats the
// subarea's name (DataCenterModule.subAreasDataRoot by (short)ddln, then
// SubAreaData.get_name) with ddll and ddlm. The world comes from that subarea
// (SubAreaData.get_worldmapId), as the mod's other travel entries read it.
namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* Tooltips = "Core.UILogic.Components.Tooltips";
constexpr const char* DataImage = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
constexpr int IconZaap = 336; // FigmaIcons.zaap
struct Position {
    int x = 0, y = 0, world = -1, subArea = 0;
    std::string zone;
    bool known = false;
};
struct Entry {
    Ref slot, member, root, portrait, button, tooltip;
    std::string tip, state;
    std::string reachKey; // The coordinates and world whose maps were counted.
    int maps = -1, size = 0;
    bool dead = false; // Retired, its removal to retry.
};
struct Shared { Ref hub, display, cached, compositor, party, tooltipService, character; } shared;
std::vector<Entry> entries;
bool installed = false, busy = false, reported = false;
uint64_t refresh = 0, cooldown = 0;
Klass hubClass{}, displayClass{}, slotClass{}, bindingClass{}, toggleClass{}, memberClass{}, companionClass{}, characterClass{};
Klass mapInfoClass{}, subRootClass{}, subAreaClass{}, dataCenterClass{}, tooltipClass{};
Method getRoot{}, getPortrait{}, tooltipCtor{}, addManipulator{}, removeManipulator{}, hideTooltip{};
Method memberId{}, playerId{}, autopilot{}, resolve{}, currentMap{}, mapInfo{}, mapSubArea{};
Method subAreas{}, subAreaById{}, subAreaWorld{}, subAreaName{};
bool Is(Obj object, Klass klass) { return object && api.object_get_class(object) == klass; }
void RequireField(Klass type, const char* name, size_t offset) {
    if (api.field_get_offset(api.FieldInfo(type, name)) != offset) throw std::runtime_error(std::string("Party travel field layout: ") + name);
}
void DiscoveryState(const std::string& stage) {
    static std::string previous;
    static uint64_t next = 0;
    const auto now = GetTickCount64();
    if (stage == previous || now < next) return;
    previous = stage; next = now + 5000;
    Log("PARTY_TRAVEL_WIDGET_STATE " + stage);
}
bool Available() {
    return installed && module && module->ready && module->servicesReady && !module->stopped &&
        !module->stopRequested && module->settings.partyTravelButton;
}
int Count(Obj list, int limit) {
    if (!list) return 0;
    const int n = api.Value<int>(api.Call(list, "get_Count"));
    if (n < 0 || n > limit) throw std::runtime_error("Party travel list bounds");
    return n;
}
Obj BindingElement(Obj slot, Method getter) {
    Ref binding(api.Field(slot, "binding"));
    if (!Is(binding.get(), bindingClass)) return nullptr;
    return api.Invoke(getter, api.object_unbox(binding.get()));
}
bool ContainsSlot(Obj display, Obj slot) {
    Ref slots(api.Field(display, "m_partyMemberSlots"));
    for (int i = 0, n = Count(slots.get(), 128); i < n; ++i) if (api.Call(slots.get(), "get_Item", {&i}) == slot) return true;
    return false;
}
// The game's party display and the services the buttons were made with.
bool SharedCurrent() {
    return shared.display.get() && module->cached.get() == shared.cached.get() && module->compositor.get() == shared.compositor.get() &&
        module->FindService("fqv") == shared.hub.get() && api.Field(shared.hub.get(), "dytb") == shared.display.get() &&
        module->FindService("fne") == shared.party.get() && module->FindService("fou") == shared.tooltipService.get() &&
        module->FindService("fng") == shared.character.get() &&
        !*static_cast<bool*>(api.FieldAddress(shared.display.get(), "isArenaParty"));
}
int64_t PlayerId() { return api.Value<int64_t>(api.Invoke(playerId, shared.character.get())); }
// Null for another accepted member that gets the button, else why not.
const char* Ineligible(Obj member, int64_t self) {
    if (!Is(member, memberClass)) return Is(member, companionClass) ? "companion" : "member-type";
    if (!*static_cast<bool*>(api.FieldAddress(member, "ddku"))) return "guest";
    const auto id = api.Value<int64_t>(api.Invoke(memberId, member));
    if (id <= 0) return "member-id";
    if (id == self) return "self";
    // The spouse's slot has its own teleport button (spouse-party-widget) while
    // « Téléportation au conjoint » is ticked; unticked, the spouse is a member like any other.
    std::string distance; bool online = false;
    if (ReadPartySpouse(member, distance, online)) return "spouse";
    return nullptr;
}
bool EntryCurrent(const Entry& e, int64_t self) {
    return !e.dead && ContainsSlot(shared.display.get(), e.slot.get()) &&
        api.Field(e.slot.get(), "partyMemberWrapper") == e.member.get() &&
        !*static_cast<bool*>(api.FieldAddress(e.slot.get(), "m_isWaitingMember")) &&
        BindingElement(e.slot.get(), getRoot) == e.root.get() && BindingElement(e.slot.get(), getPortrait) == e.portrait.get() &&
        api.Call(e.portrait.get(), "get_parent") == e.root.get() && api.Call(e.root.get(), "get_panel") &&
        !Ineligible(e.member.get(), self);
}
// One log line per kind of data error until a read succeeds again (every tick reads).
void DataError(const char* what, const std::exception& e) {
    static std::string previous;
    const auto line = std::string(what) + " " + e.what();
    if (line != previous) Log(line);
    previous = line;
}
// The member's last position known to the game (lg ddll, ddlm, ddln). A data
// error leaves this member's position unknown (greyed), the other buttons kept.
Position ReadPosition(Obj member) {
    Position p;
    p.x = *static_cast<int32_t*>(api.FieldAddress(member, "ddll"));
    p.y = *static_cast<int32_t*>(api.FieldAddress(member, "ddlm"));
    p.subArea = *static_cast<int16_t*>(api.FieldAddress(member, "ddln"));
    if (p.subArea <= 0 || std::abs(p.x) > 10000 || std::abs(p.y) > 10000) return p;
    try {
        Ref root(api.Invoke(subAreas, nullptr));
        if (!root.get()) return p;
        int id = p.subArea;
        Ref area(api.Invoke(subAreaById, root.get(), {&id}));
        if (!Is(area.get(), subAreaClass)) return p;
        p.world = api.Value<int>(api.Invoke(subAreaWorld, area.get()));
        p.zone = api.String(api.Invoke(subAreaName, area.get()));
    } catch (const std::exception& e) { DataError("PARTY_TRAVEL_SUBAREA_ERROR", e); p.world = -1; p.zone.clear(); }
    p.known = p.world >= 0;
    return p;
}
// The player's own position, as the spouse's distance reads it.
Position Here() {
    Position p;
    try {
        Ref location(api.Invoke(currentMap, shared.character.get()));
        if (!location.get()) return p;
        int64_t id = *static_cast<int64_t*>(api.FieldAddress(location.get(), "m_mapId"));
        if (id <= 0) return p;
        Ref info(api.Invoke(mapInfo, nullptr, {&id}));
        if (!Is(info.get(), mapInfoClass)) return p;
        Ref area(api.Invoke(mapSubArea, info.get()));
        if (!Is(area.get(), subAreaClass)) return p;
        p.x = Read<int16_t>(info.get(), 0x18); p.y = Read<int16_t>(info.get(), 0x1A);
        p.world = api.Value<int>(api.Invoke(subAreaWorld, area.get()));
        p.known = p.world >= 0;
    } catch (const std::exception& e) { DataError("PARTY_TRAVEL_HERE_ERROR", e); p = {}; }
    return p;
}
// The maps the game knows at those coordinates in that world (esg.zid, which
// both the mod's route and the game's autopilot resolve the destination with).
int Maps(Entry& e, const Position& p) {
    const auto key = std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.world);
    if (key == e.reachKey) return e.maps;
    int x = p.x, y = p.y, world = p.world;
    try {
        Ref maps(api.Invoke(resolve, shared.character.get(), {&x, &y, &world}));
        e.maps = Count(maps.get(), 4096);
    } catch (const std::exception& error) { DataError("PARTY_TRAVEL_MAPS_ERROR", error); e.maps = 0; }
    e.reachKey = key;
    return e.maps;
}
std::string Coordinates(const Position& p) { return "[" + std::to_string(p.x) + "," + std::to_string(p.y) + "]"; }
// Why the button is greyed (null: usable), and the tooltip's line for it.
const char* Blocked(Entry& e, const Position& p, const Position& here, std::string& line) {
    if (module->InCombat()) { line = "Indisponible pendant un combat."; return "combat"; }
    if (!p.known) { line = "Position inconnue : le jeu n’a pas la position de ce membre."; return "unknown-position"; }
    if (here.known && here.x == p.x && here.y == p.y && here.world == p.world) { line = "Tu es déjà à cette position."; return "same-position"; }
    if (Maps(e, p) <= 0) { line = "Le jeu ne connaît aucune carte à ces coordonnées dans ce monde."; return "no-map"; }
    return nullptr;
}
void ClearTooltip(Entry& e) {
    if (!e.tooltip.get()) return;
    api.Invoke(hideTooltip, e.tooltip.get());
    api.Invoke(removeManipulator, nullptr, {e.button.get(), e.tooltip.get()});
    e.tooltip.reset(); e.tip.clear();
}
void Pixels(Obj element, const char* property, float value) {
    int unit = 0; Ref length(api.New(api.Class("UnityEngine.UIElementsModule.dll", Ui, "Length"), {&value, &unit}));
    Ref style(api.Call(element, "get_style"));
    Ref box(api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str()));
    api.Call(box.get(), "set_value", {api.object_unbox(length.get())});
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box.get())});
}
void Absolute(Obj element) {
    int absolute = 1;
    Ref style(api.Call(element, "get_style"));
    Ref box(api.Call(style.get(), "UnityEngine.UIElements.IStyle.get_position"));
    api.Call(box.get(), "set_value", {&absolute});
    api.Call(style.get(), "UnityEngine.UIElements.IStyle.set_position", {api.object_unbox(box.get())});
}
void Refresh(Entry& e, size_t index, const Position& here) {
    const auto p = ReadPosition(e.member.get());
    std::string line;
    const char* why = Blocked(e, p, here, line);
    const bool enabled = !why && GetTickCount64() >= cooldown;
    bool on = enabled; api.Call(e.button.get(), "SetEnabled", {&on});
    bool off = false; api.Call(e.button.get(), "SetValueWithoutNotify", {&off});
    const std::string state = std::string(why ? why : "ready") + (p.known ? " x=" + std::to_string(p.x) + " y=" + std::to_string(p.y) +
        " world=" + std::to_string(p.world) : " subarea=" + std::to_string(p.subArea));
    if (state != e.state) { e.state = state; Log("PARTY_TRAVEL_BUTTON button=" + std::to_string(index) + " " + state); }
    const auto name = api.String(api.Field(e.member.get(), "ddkt"));
    std::string tip = "Aller jusqu’à " + (name.empty() ? std::string("ce membre") : name);
    if (p.known) {
        tip += " — " + Coordinates(p) + (p.zone.empty() ? "" : " " + p.zone);
        if (here.known && here.world == p.world) {
            const int distance = std::abs(here.x - p.x) + std::abs(here.y - p.y);
            tip += " (" + std::to_string(distance) + (distance > 1 ? " cases)" : " case)");
        } else if (here.known) tip += " (autre carte du monde)";
    }
    tip += module->settings.mapTravelPlanner ? "\nTrajet du mod : zaaps, directs ou Havre-Sac, puis l’autopilote du jeu." :
        "\nAutopilote du jeu jusqu’à cette position.";
    if (why) tip += "\n" + line;
    if (p.known) tip += "\nDernière position connue du jeu ; elle peut avoir changé depuis.";
    if (tip != e.tip) {
        ClearTooltip(e);
        Ref text(api.string_new(tip.c_str()));
        e.tooltip.reset(api.object_new(tooltipClass));
        int origin = 1, source = 7, behavior = 1; bool front = false;
        api.Invoke(tooltipCtor, e.tooltip.get(), {shared.tooltipService.get(), text.get(), &origin, &source, &behavior, &front});
        api.Invoke(addManipulator, nullptr, {e.button.get(), e.tooltip.get()});
        e.tip = tip;
    }
    Ref small(api.string_new("Small"));
    const bool compact = api.Value<bool>(api.Call(e.root.get(), "ClassListContains", {small.get()}));
    const int size = int(party_slot::ButtonSize(compact));
    if (size != e.size) {
        for (auto property : {"width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight"}) Pixels(e.button.get(), property, float(size));
        Pixels(e.button.get(), "left", party_slot::ButtonLeft(compact)); Pixels(e.button.get(), "top", party_slot::ButtonTop);
        e.size = size;
    }
}
void Create(Entry& e) {
    e.button.reset(api.New(toggleClass));
    Ref name(api.string_new("dofus_miniatures_party_travel")); api.Call(e.button.get(), "set_name", {name.get()});
    // The spouse's teleport button's native component and place (party-slot-layout.hpp), the zaap icon.
    int icon = IconZaap, style = 2, medium = 1;
    api.Call(e.button.get(), "set_iconValue", {&icon}); api.Call(e.button.get(), "set_mainStyle", {&style});
    api.Call(e.button.get(), "set_toggleSize", {&medium});
    Absolute(e.button.get());
    api.Call(e.portrait.get(), "Add", {e.button.get()});
}
// Takes one button out (its tooltip first); false keeps it for a retry.
bool Remove(Entry& e) {
    e.dead = true;
    try { ClearTooltip(e); }
    catch (const std::exception& error) { Log(std::string("PARTY_TRAVEL_TOOLTIP_CLEANUP_ERROR ") + error.what()); return false; }
    if (e.button.get()) {
        try { api.Call(e.button.get(), "RemoveFromHierarchy"); e.button.reset(); }
        catch (const std::exception& error) { Log(std::string("PARTY_TRAVEL_WIDGET_CLEANUP_ERROR ") + error.what()); return false; }
    }
    return true;
}
bool Visible(Obj element) {
    if (!api.Call(element, "get_panel") || !api.Value<bool>(api.Call(element, "get_enabledInHierarchy"))) return false;
    Ref current(element);
    for (int depth = 0; current.get(); ++depth) {
        if (depth >= 128) return false;
        Ref resolved(api.Call(current.get(), "get_resolvedStyle"));
        if (api.Value<int>(api.Call(resolved.get(), "get_display")) != 0 ||
            api.Value<int>(api.Call(resolved.get(), "get_visibility")) != 0) return false;
        current.reset(api.Call(current.get(), "get_parent"));
    }
    return true;
}
struct Candidate { Ref slot, member, root, portrait; };
void Discover() {
    Ref hub(module->FindService("fqv"));
    if (!Is(hub.get(), hubClass)) { DiscoveryState(hub.get() ? "hub-type" : "hub-missing"); return; }
    Ref display(api.Field(hub.get(), "dytb"));
    if (!Is(display.get(), displayClass)) { DiscoveryState(display.get() ? "display-type" : "display-missing"); return; }
    if (*static_cast<bool*>(api.FieldAddress(display.get(), "isArenaParty"))) { DiscoveryState("arena-party"); return; }
    Ref character(module->FindService("fng"));
    if (!Is(character.get(), characterClass)) { DiscoveryState("character-missing"); return; }
    Ref tooltips(module->FindService("fou"));
    if (!tooltips.get()) { DiscoveryState("tooltip-service-missing"); return; }
    shared.hub.reset(hub.get()); shared.display.reset(display.get()); shared.character.reset(character.get());
    shared.cached.reset(module->cached.get()); shared.compositor.reset(module->compositor.get());
    shared.party.reset(module->FindService("fne")); shared.tooltipService.reset(tooltips.get());
}
}
void RestorePartyTravelWidget() {
    for (auto& e : entries) Remove(e);
    std::erase_if(entries, [](const Entry& e) { return !e.button.get() && !e.tooltip.get(); });
    if (entries.empty()) shared = {};
}
bool PartyTravelWidgetCleanupPending() {
    for (const auto& e : entries) if (e.button.get() || e.tooltip.get()) return true;
    return false;
}
bool HandlePartyTravelWidgetToggle(managed::Obj owner) {
    if (!owner) return false;
    Entry* entry = nullptr;
    for (auto& e : entries) if (e.button.get() == owner) entry = &e;
    if (!entry) return false;
    if (busy) return true;
    busy = true;
    try {
        const auto now = GetTickCount64();
        const char* refused = nullptr;
        Position p;
        if (!Available()) refused = "inactive";
        else if (entry->dead || !SharedCurrent() || !EntryCurrent(*entry, PlayerId())) refused = "stale";
        else if (module->InCombat()) refused = "combat";
        else if (now < cooldown) refused = "cooldown";
        else if (!Visible(owner) || api.Call(owner, "get_parent") != entry->portrait.get()) refused = "hidden";
        else {
            p = ReadPosition(entry->member.get());
            std::string line;
            refused = Blocked(*entry, p, Here(), line);
        }
        if (refused) Log(std::string("PARTY_TRAVEL_CLICK_IGNORED reason=") + refused);
        else {
            cooldown = now + 1500; // Suppress queued double clicks and synchronous reentry.
            bool enabled = false; api.Call(owner, "SetEnabled", {&enabled});
            const auto where = "x=" + std::to_string(p.x) + " y=" + std::to_string(p.y) + " world=" + std::to_string(p.world);
            if (module->settings.mapTravelPlanner) {
                // « Remplacer l’autopilote du jeu » ticked: the mod's route, as a chat [x,y] link.
                Log("PARTY_TRAVEL_REQUEST route=mod " + where);
                StartChatTravel({p.x, p.y, p.world});
            } else {
                // Unticked: the game's own autopilot to a position, the call behind the
                // map's « Voyager jusqu’à cette position » (fhw.betn) and /travel.
                Log("PARTY_TRAVEL_REQUEST route=game native=esg.zhy " + where);
                int x = p.x, y = p.y, world = p.world; bool immediate = true;
                api.Invoke(autopilot, shared.character.get(), {&x, &y, &world, &immediate});
            }
        }
    } catch (const std::exception& e) { Log(std::string("PARTY_TRAVEL_CLICK_ERROR ") + e.what()); }
    busy = false;
    return true;
}
void TickPartyTravelWidget() {
    try {
        if (!Available()) { RestorePartyTravelWidget(); return; }
        const auto now = GetTickCount64();
        if (now < refresh) return;
        refresh = now + 300;
        // A replaced character, compositor, party display or service: start again.
        if (!entries.empty() && !SharedCurrent()) { RestorePartyTravelWidget(); if (PartyTravelWidgetCleanupPending()) return; }
        // Retired buttons whose removal failed: retried first.
        for (auto& e : entries) if (e.dead) Remove(e);
        std::erase_if(entries, [](const Entry& e) { return e.dead && !e.button.get() && !e.tooltip.get(); });
        if (PartyTravelWidgetCleanupPending() && std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return e.dead; })) return;
        if (entries.empty()) { shared = {}; Discover(); if (!shared.display.get()) return; }
        if (!SharedCurrent()) { RestorePartyTravelWidget(); return; }
        const auto self = PlayerId();
        Ref slots(api.Field(shared.display.get(), "m_partyMemberSlots"));
        if (!slots.get()) { DiscoveryState("slots-missing"); return; }
        std::vector<Candidate> wanted;
        std::string skipped;
        int filled = 0;
        for (int i = 0, n = Count(slots.get(), 128); i < n; ++i) {
            Ref slot(api.Call(slots.get(), "get_Item", {&i}));
            if (!Is(slot.get(), slotClass) || *static_cast<bool*>(api.FieldAddress(slot.get(), "m_isWaitingMember"))) continue;
            ++filled;
            Ref member(api.Field(slot.get(), "partyMemberWrapper"));
            if (auto why = Ineligible(member.get(), self)) { skipped += std::string(skipped.empty() ? "" : ",") + why; continue; }
            Ref root(BindingElement(slot.get(), getRoot)), portrait(BindingElement(slot.get(), getPortrait));
            if (!root.get() || !portrait.get() || !api.Call(root.get(), "get_panel") || api.Call(portrait.get(), "get_parent") != root.get()) {
                skipped += std::string(skipped.empty() ? "" : ",") + "binding"; continue;
            }
            wanted.push_back({Ref(slot.get()), Ref(member.get()), Ref(root.get()), Ref(portrait.get())});
        }
        // Buttons whose slot no longer shows the same member in the same elements go.
        for (auto& e : entries) {
            if (e.dead) continue;
            bool kept = false;
            for (const auto& c : wanted) kept = kept || (c.slot.get() == e.slot.get() && c.member.get() == e.member.get() &&
                c.root.get() == e.root.get() && c.portrait.get() == e.portrait.get());
            if (!kept || !EntryCurrent(e, self)) Remove(e);
        }
        std::erase_if(entries, [](const Entry& e) { return e.dead && !e.button.get() && !e.tooltip.get(); });
        for (const auto& c : wanted) {
            bool present = false;
            // A retired button still being removed from this slot: no second one until it is gone.
            for (const auto& e : entries) present = present || e.slot.get() == c.slot.get();
            if (present) continue;
            auto& e = entries.emplace_back();
            e.slot.reset(c.slot.get()); e.member.reset(c.member.get()); e.root.reset(c.root.get()); e.portrait.reset(c.portrait.get());
            Create(e);
            Log("PARTY_TRAVEL_WIDGET_READY native-toggle=secondary icon=zaap");
        }
        const auto here = Here();
        size_t index = 0;
        for (auto& e : entries) if (!e.dead) Refresh(e, index++, here);
        DiscoveryState("slots=" + std::to_string(Count(slots.get(), 128)) + " filled=" + std::to_string(filled) + " buttons=" +
            std::to_string(index) + " skipped=" + (skipped.empty() ? "none" : skipped));
        reported = false;
    } catch (const std::exception& e) {
        if (!reported) Log(std::string("PARTY_TRAVEL_WIDGET_ERROR ") + e.what());
        reported = true; RestorePartyTravelWidget(); refresh = GetTickCount64() + 2000;
    }
}
void InstallPartyTravelWidget() {
    try {
        hubClass = api.Class("Core.dll", "", "ghu");
        displayClass = api.Class("Core.dll", "Core.UILogic.Party", "PartyDisplayUi");
        slotClass = api.Class("Core.dll", "Core.UILogic.Party", "PartyMember");
        bindingClass = api.Class("Core.dll", "Core.UILogic.Bindings", "PartyMemberItemBinding");
        toggleClass = api.Class("Core.dll", Figma, "DofusToggleButtonCustom");
        memberClass = api.Class("Core.dll", "", "lg");
        companionClass = api.Class("Core.dll", "", "lf"); // lf : lg, the party's companion entities
        characterClass = api.Class("Core.dll", "", "esg");
        mapInfoClass = api.Class(DataImage, WorldSpace, "MapInformationData");
        subRootClass = api.Class(DataImage, WorldSpace, "SubAreasDataRoot");
        subAreaClass = api.Class(DataImage, WorldSpace, "SubAreaData");
        dataCenterClass = api.Class(DataImage, "Core.DataCenter", "DataCenterModule");
        tooltipClass = api.Class("Core.dll", Tooltips, "TooltipManipulator");
        // Checked offsets of the pinned 3.7.12.12 layout (contracts-new.json).
        RequireField(memberClass, "ddkt", 0x18); RequireField(memberClass, "ddku", 0x20);
        RequireField(memberClass, "ddll", 0x68); RequireField(memberClass, "ddlm", 0x6C); RequireField(memberClass, "ddln", 0x70);
        RequireField(mapInfoClass, "posX", 0x18); RequireField(mapInfoClass, "posY", 0x1A);
        getRoot = api.FindVerified(bindingClass, "get_member", 0, 0x2F8A5F0);
        getPortrait = api.FindVerified(bindingClass, "get_parentMember", 0, 0x2F8A6A0);
        tooltipCtor = api.FindVerified(tooltipClass, ".ctor", 6, 0x15DFAE0);
        hideTooltip = api.FindVerified(api.Class("Core.dll", Tooltips, "BaseTooltipManipulator"), "HideTooltip", 0, 0x15CD030);
        auto extensions = api.Class("UnityEngine.UIElementsModule.dll", Ui, "VisualElementExtensions");
        addManipulator = api.FindVerified(extensions, "AddManipulator", 2, 0x56755F0);
        removeManipulator = api.FindVerified(extensions, "RemoveManipulator", 2, 0x5676A90);
        memberId = api.FindVerified(memberClass, "oab", 0, 0x68D2B0);
        playerId = api.FindVerified(characterClass, "zcx", 0, 0x18BB140);
        autopilot = api.FindVerified(characterClass, "zhy", 4, 0x18BD470);
        resolve = api.FindVerified(characterClass, "zid", 3, 0x18BD8B0);
        currentMap = api.FindVerified(characterClass, "zeo", 0, 0x185C1E0);
        mapInfo = api.FindVerified(mapInfoClass, "GetMapInformationById", 1, 0x7C4200);
        mapSubArea = api.FindVerified(mapInfoClass, "get_subArea", 0, 0x7C44B0);
        subAreas = api.FindVerified(dataCenterClass, "get_subAreasDataRoot", 0, 0x7C1A40);
        subAreaById = api.FindVerified(subRootClass, "GetSubAreaById", 1, 0x7D1060);
        subAreaWorld = api.FindVerified(subAreaClass, "get_worldmapId", 0, 0x7D1040);
        subAreaName = api.FindVerified(subAreaClass, "get_name", 0, 0x7D0F60);
        // The single shared Toggle hook is installed by SoulWidget, which hands this button's clicks here.
        installed = true;
    } catch (const std::exception& e) { installed = false; Log(std::string("PARTY_TRAVEL_WIDGET_UNAVAILABLE ") + e.what()); }
}
