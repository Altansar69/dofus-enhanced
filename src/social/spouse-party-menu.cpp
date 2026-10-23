#include "social/spouse-party-menu.hpp"
#include "core/module.hpp"
#include <cstdlib>
#include <memory>

namespace {
using namespace managed;
constexpr const char* MakerSpace = "Core.UILogic.Components.ContextMenu.ContextMenuFactory";
struct Menu {
    Ref menu, maker, social, party, bus, cached, compositor, spouse, member, item, callback;
    int64_t playerId = 0;
    bool online = false, armed = false, attached = false;
    uint64_t created = 0;
};
std::unique_ptr<Menu> owned;
Klass partyMaker{}, socialClass{}, partyClass{}, spouseClass{}, memberClass{};
Method append{}, getSpouse{}, getMembers{}, memberId{}, joinSpouse{};
Method playerCtor{};
Klass characterClass{}, mapClass{}, mapInfoClass{}, subareaClass{};
Method currentMap{}, mapInfo{}, subarea{}, world{};
bool installed = false;
using CreateFn = Obj(*)(Obj, Method);
CreateFn originalCreate = nullptr;
bool Is(Obj obj, Klass klass) { return obj && api.object_get_class(obj) == klass; }
void MenuState(const char* stage, Obj maker) {
    static std::string previous;
    static uint64_t next = 0;
    const std::string value = std::string(stage) + " maker=" + (maker ? api.class_get_name(api.object_get_class(maker)) : "null");
    const auto now = GetTickCount64();
    if (value == previous || now < next) return;
    previous = value; next = now + 5000;
    Log("SPOUSE_PARTY_MENU_STATE " + value);
}
bool Available() {
    return installed && module && module->ready && module->servicesReady &&
        !module->stopped && !module->stopRequested && module->settings.spouseTeleport && !module->InCombat();
}
int Count(Obj list) {
    if (!list) return 0;
    int n = api.Value<int>(api.Call(list, "get_Count"));
    if (n < 0 || n > 128) throw std::runtime_error("Spouse party menu list bounds");
    return n;
}
Obj Member(Obj party, int64_t id) {
    Ref members(api.Invoke(getMembers, party));
    for (int i = 0, n = Count(members.get()); i < n; ++i) {
        Ref member(api.Call(members.get(), "get_Item", {&i}));
        if (Is(member.get(), memberClass) && api.Value<int64_t>(api.Invoke(memberId, member.get())) == id &&
            *static_cast<bool*>(api.FieldAddress(member.get(), "ddku"))) return member.get();
    }
    return nullptr;
}
bool Fresh(const Menu& m) {
    if (!Available() || module->cached.get() != m.cached.get() || module->compositor.get() != m.compositor.get() ||
        module->FindService("foi") != m.social.get() || module->FindService("fne") != m.party.get() ||
        module->FindService("fmz") != m.bus.get() ||
        *static_cast<int64_t*>(api.FieldAddress(m.maker.get(), "m_playerId")) != m.playerId) return false;
    Ref spouse(api.Invoke(getSpouse, m.social.get()));
    return spouse.get() == m.spouse.get() && Is(spouse.get(), spouseClass) &&
        *static_cast<int64_t*>(api.FieldAddress(spouse.get(), "dcyt")) == m.playerId &&
        *static_cast<bool*>(api.FieldAddress(spouse.get(), "dcyy")) == m.online &&
        Member(m.party.get(), m.playerId) == m.member.get();
}
int Index(const Menu& m) {
    Ref rows(api.Call(m.menu.get(), "get_menuItems"));
    for (int i = 0, n = Count(rows.get()); i < n; ++i)
        if (api.Call(rows.get(), "get_Item", {&i}) == m.item.get()) return i;
    return -1;
}
Obj Root(const Menu& m) {
    Obj root = api.Field(m.menu.get(), "dofusContextualMenuRoot");
    return root && api.Call(root, "get_menuData") == m.menu.get() ? root : nullptr;
}
std::string DistanceCaption(const Menu& m) {
    if (!m.online) return "hors ligne";
    try {
        Ref character(module->FindService("fng"));
        if (!Is(character.get(), characterClass)) return "distance inconnue";
        Ref location(api.Invoke(currentMap, character.get()));
        if (!Is(location.get(), mapClass)) return "distance inconnue";
        int64_t fromId = *static_cast<int64_t*>(api.FieldAddress(location.get(), "m_mapId"));
        int64_t toId = *static_cast<int64_t*>(api.FieldAddress(m.spouse.get(), "dcyz"));
        if (fromId <= 0 || toId <= 0) return "distance inconnue";
        Ref from(api.Invoke(mapInfo, nullptr, {&fromId}));
        Ref to(api.Invoke(mapInfo, nullptr, {&toId}));
        if (!Is(from.get(), mapInfoClass) || !Is(to.get(), mapInfoClass)) return "distance inconnue";
        Ref fromArea(api.Invoke(subarea, from.get())), toArea(api.Invoke(subarea, to.get()));
        if (!Is(fromArea.get(), subareaClass) || !Is(toArea.get(), subareaClass)) return "distance inconnue";
        const int fromWorld = api.Value<int>(api.Invoke(world, fromArea.get()));
        const int toWorld = api.Value<int>(api.Invoke(world, toArea.get()));
        if (fromWorld < 0 || toWorld < 0) return "distance inconnue";
        if (fromWorld != toWorld) return "autre carte du monde";
        // Distance on the world-map grid, not route length or teleport eligibility.
        const int dx = int(Read<int16_t>(from.get(), 0x18)) - int(Read<int16_t>(to.get(), 0x18));
        const int dy = int(Read<int16_t>(from.get(), 0x1A)) - int(Read<int16_t>(to.get(), 0x1A));
        const int distance = std::abs(dx) + std::abs(dy);
        return std::to_string(distance) + (distance > 1 ? " cases" : " case");
    } catch (const std::exception& e) {
        Log(std::string("SPOUSE_PARTY_DISTANCE_ERROR ") + e.what());
        return "distance inconnue";
    }
}
Obj CreateHook(Obj maker, Method method) {
    RestoreSpousePartyMenu();
    Obj result = originalCreate(maker, method);
    if (!Available()) { MenuState("unavailable-or-combat", maker); return result; }
    if (!result || !Is(maker, partyMaker)) { MenuState("other-menu", maker); return result; }
    try {
        auto next = std::make_unique<Menu>();
        next->menu.reset(result); next->maker.reset(maker);
        next->cached.reset(module->cached.get()); next->compositor.reset(module->compositor.get());
        next->social.reset(api.Field(maker, "m_socialService"));
        next->party.reset(api.Field(maker, "m_partyService")); next->bus.reset(api.Field(maker, "m_bus"));
        if (!Is(next->social.get(), socialClass) || !Is(next->party.get(), partyClass) || !next->bus.get()) { MenuState("services-unavailable", maker); return result; }
        next->playerId = *static_cast<int64_t*>(api.FieldAddress(maker, "m_playerId"));
        next->spouse.reset(api.Invoke(getSpouse, next->social.get()));
        if (!Is(next->spouse.get(), spouseClass) || next->playerId <= 0 ||
            *static_cast<int64_t*>(api.FieldAddress(next->spouse.get(), "dcyt")) != next->playerId) { MenuState(next->spouse.get() ? "spouse-id-or-type-mismatch" : "spouse-data-missing", maker); return result; }
        next->online = *static_cast<bool*>(api.FieldAddress(next->spouse.get(), "dcyy"));
        next->member.reset(Member(next->party.get(), next->playerId));
        if (!next->member.get() || !Fresh(*next)) { MenuState("party-member-or-session-mismatch", maker); return result; }
        const auto distance = DistanceCaption(*next);
        Ref text(api.string_new(("Se téléporter au conjoint — " + distance).c_str())), empty(api.string_new(""));
        Ref help(api.string_new("Distance en cases de carte d'après la dernière position connue, hors détours, actualisée à l'ouverture du menu. La portée et les restrictions de téléportation restent vérifiées par Dofus."));
        Ref unavailable(api.string_new("Votre conjoint est hors ligne."));
        Ref asset(api.object_new(api.class_from_type(api.method_get_param(append, 7))));
        Ref color(api.object_new(api.class_from_type(api.method_get_param(append, 10))));
        bool disabled = !next->online, close = true;
        unsigned delay = 500; int style = 1, icon = 304; // FigmaIcons.teleport
        Ref rows(api.Call(result, "get_menuItems")); int index = Count(rows.get());
        owned = std::move(next); owned->created = GetTickCount64();
        // Null native action: detached/stale rows cannot dispatch after our owner is released.
        api.Invoke(append, result, {text.get(), nullptr, &disabled, &close, help.get(), &delay,
            unavailable.get(), api.object_unbox(asset.get()), &style, &icon,
            api.object_unbox(color.get()), empty.get(), empty.get()});
        owned->item.reset(api.Call(rows.get(), "get_Item", {&index}));
        owned->callback.reset(api.Call(owned->item.get(), "get_m_callback"));
        owned->armed = !disabled;
        Log("SPOUSE_PARTY_MENU_READY online=" + std::to_string(owned->online) + " distance=" + distance);
    } catch (const std::exception& e) {
        Log(std::string("SPOUSE_PARTY_MENU_ERROR ") + e.what()); RestoreSpousePartyMenu();
    }
    return result;
}
}
bool ReadPartySpouse(managed::Obj member, std::string& distance, bool& online, const char** reason) {
    const auto reject = [reason](const char* why) { if (reason) *reason = why; return false; };
    online = false;
    if (!installed || !module || !module->ready || !module->servicesReady || module->stopped || module->stopRequested)
        return reject("inactive");
    if (!module->settings.spouseTeleport) return reject("disabled-in-settings");
    if (!Is(member, memberClass)) return reject(member ? "member-type" : "member-missing");
    Menu current;
    current.social.reset(module->FindService("foi")); current.party.reset(module->FindService("fne"));
    if (!Is(current.social.get(), socialClass)) return reject("social-service");
    if (!Is(current.party.get(), partyClass)) return reject("party-service");
    current.spouse.reset(api.Invoke(getSpouse, current.social.get()));
    if (!Is(current.spouse.get(), spouseClass)) return reject(current.spouse.get() ? "spouse-type" : "spouse-data-missing");
    current.playerId = *static_cast<int64_t*>(api.FieldAddress(current.spouse.get(), "dcyt"));
    if (current.playerId <= 0) return reject("spouse-id-unavailable");
    Ref currentMember(Member(current.party.get(), current.playerId));
    if (!currentMember.get()) return reject("spouse-not-in-accepted-party");
    if (currentMember.get() != member) return reject(api.Value<int64_t>(api.Invoke(memberId, member)) == current.playerId ? "spouse-wrapper-mismatch" : "other-member");
    online = current.online = *static_cast<bool*>(api.FieldAddress(current.spouse.get(), "dcyy"));
    distance = DistanceCaption(current);
    if (reason) *reason = "ready";
    return true;
}
bool TeleportPartySpouse(managed::Obj member) {
    std::string distance; bool online = false;
    if (!Available() || !ReadPartySpouse(member, distance, online) || !online) return false;
    Ref dependencies(module->FindService("fqy")), bus(module->FindService("fmz"));
    Ref cached(module->cached.get()), compositor(module->compositor.get());
    if (!dependencies.get() || !bus.get()) return false;
    // Construct the normal native action owner with its real dependency service.
    // No menu is opened and no synthetic network message is constructed.
    Ref name(api.string_new("")), maker(api.object_new(api.Class("Core.dll", MakerSpace, "PlayerMenuMaker")));
    int64_t id = api.Value<int64_t>(api.Invoke(memberId, member));
    bool no = false; int account = 0;
    api.Invoke(playerCtor, maker.get(), {name.get(), dependencies.get(), &id, &no, nullptr, nullptr,
        nullptr, nullptr, &account, &no, &no, &no, &no});
    if (!Available() || module->cached.get() != cached.get() || module->compositor.get() != compositor.get() ||
        module->FindService("fmz") != bus.get() || module->FindService("fqy") != dependencies.get() || api.Field(maker.get(), "m_bus") != bus.get() ||
        !ReadPartySpouse(member, distance, online) || !online) return false;
    Log("SPOUSE_PARTY_TELEPORT_REQUEST source=visible-button native=OnJoinSpouse");
    api.Invoke(joinSpouse, maker.get());
    return true;
}
bool HandleSpousePartyMenuCallback(managed::Obj callback, managed::Obj view) {
    if (!callback || !owned || owned->callback.get() != callback) return false;
    try {
        if (!owned->armed || !view || !api.Call(view, "get_panel") ||
            api.Call(view, "get_itemData") != owned->item.get() || Index(*owned) < 0 ||
            api.Value<bool>(api.Call(owned->item.get(), "get_disabled")) || !Fresh(*owned)) return true;
        Ref root(Root(*owned));
        if (!root.get() || !api.Call(root.get(), "get_panel")) return true;
        Ref maker(owned->maker.get());
        owned->armed = false; // Disarm before native dispatch, including synchronous reentry.
        Log("SPOUSE_PARTY_TELEPORT_REQUEST native=OnJoinSpouse");
        api.Invoke(joinSpouse, maker.get());
        // Native action/response handles distance, restricted maps and other game rules.
    } catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_TELEPORT_ERROR ") + e.what()); }
    return true;
}
void RestoreSpousePartyMenu() {
    auto old = std::move(owned); if (!old) return;
    old->armed = false;
    try {
        if (old->item.get()) { int index = Index(*old); if (index >= 0) api.Call(old->menu.get(), "RemoveItemAt", {&index}); }
        Ref root(Root(*old));
        if (root.get() && api.Call(root.get(), "get_panel")) { bool delayed = false; api.Call(root.get(), "RemoveMenu", {&delayed}); }
    } catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_RESTORE_ERROR ") + e.what()); }
}
void TickSpousePartyMenu() {
    if (!owned) return;
    try {
        if (!Fresh(*owned)) { RestoreSpousePartyMenu(); return; }
        Ref root(Root(*owned)); bool attached = root.get() && api.Call(root.get(), "get_panel");
        if (attached) owned->attached = true;
        if (!attached && (owned->attached || GetTickCount64() - owned->created > 5000)) RestoreSpousePartyMenu();
    } catch (const std::exception& e) { Log(std::string("SPOUSE_PARTY_TICK_ERROR ") + e.what()); RestoreSpousePartyMenu(); }
}
void InstallSpousePartyMenu() {
    partyMaker = api.Class("Core.dll", MakerSpace, "PartyMemberMenuMaker");
    socialClass = api.Class("Core.dll", "", "fvh"); partyClass = api.Class("Core.dll", "", "fti");
    spouseClass = api.Class("Core.dll", "", "io"); memberClass = api.Class("Core.dll", "", "lg");
    auto player = api.Class("Core.dll", MakerSpace, "PlayerMenuMaker");
    getSpouse = api.FindVerified(socialClass, "bfou", 0, 0x752E20);
    getMembers = api.FindVerified(partyClass, "bfmh", 0, 0x752E20);
    memberId = api.FindVerified(memberClass, "oab", 0, 0x68D2B0);
    characterClass = api.Class("Core.dll", "", "esg");
    mapClass = api.Class("Core.dll", "", "tk");
    currentMap = api.FindVerified(characterClass, "zeo", 0, 0x185C1E0);
    constexpr const char* dataImage = "Ankama.Dofus.Core.DataCenter.dll", *dataSpace = "Core.DataCenter.Metadata.World";
    mapInfoClass = api.Class(dataImage, dataSpace, "MapInformationData");
    subareaClass = api.Class(dataImage, dataSpace, "SubAreaData");
    mapInfo = api.FindVerified(mapInfoClass, "GetMapInformationById", 1, 0x7C4200);
    subarea = api.FindVerified(mapInfoClass, "get_subArea", 0, 0x7C44B0);
    world = api.FindVerified(subareaClass, "get_worldmapId", 0, 0x7D1040);
    joinSpouse = api.FindVerified(player, "OnJoinSpouse", 0, 0x16F0DF0);
    playerCtor = api.FindVerified(player, ".ctor", 13, 0x16F3950);
    append = api.FindVerified(api.Class("Core.dll", "Core.UILogic.Components.ContextMenu", "DofusContextualMenu"),
        "AppendDefaultItem", 13, 0x16D6C60);
    auto create = api.FindVerified(player, "CreateMenu", 0, 0x16ECE90);
    AddHook(create, 0x16ECE90, reinterpret_cast<void*>(&CreateHook), reinterpret_cast<void**>(&originalCreate));
    installed = true;
}
