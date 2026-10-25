#include "social/spouse-party-menu.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include "social/spouse-party-widget.cpp"

namespace fake {
using namespace managed;
struct Node { std::array<unsigned char, 32> raw{}; std::string type, text; std::unordered_map<std::string, Obj> fields; std::unordered_map<std::string, int64_t> numbers; std::unordered_map<std::string, std::array<unsigned char,16>> styles; std::vector<Obj> items; };
struct MethodData { std::string type, name; };
std::vector<std::unique_ptr<Node>> nodes;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<Obj, Node*> objects;
std::unordered_map<std::string, std::unique_ptr<std::string>> classes;
std::unordered_map<GCHandle, Obj> handles;
std::unordered_map<std::string, Obj> services;
std::unordered_map<int64_t, Obj> maps;
std::vector<std::string> logs;
GCHandle serial = 1;
Obj lastMenu{}, lastView{}, invokedMaker{};
bool combat = false, reenter = false, appendFails = false;
bool mapReadFails = false;
bool detachFails = false, constructFails = false;
bool tooltipDetachFails = false;
Obj lastButton{};
int teleports = 0, originalCalls = 0;
Obj (*createHook)(Obj, Method) = nullptr;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Obj Make(const std::string& type) { auto n = std::make_unique<Node>(); n->type = type; Obj p = n->raw.data(); objects[p] = n.get(); nodes.push_back(std::move(n)); return p; }
Node& Get(Obj o) { if (!objects.contains(o)) throw std::runtime_error("Unknown object"); return *objects.at(o); }
template<class T> Obj Box(T value) { auto o = Make("Box"); std::memcpy(o, &value, sizeof(value)); return o; }
Obj MapInfo(int64_t id, int16_t x, int16_t y, int world = 1) {
    auto info = Make("MapInformationData"), area = Make("SubAreaData");
    std::memcpy(static_cast<unsigned char*>(info) + 0x18, &x, sizeof(x));
    std::memcpy(static_cast<unsigned char*>(info) + 0x1A, &y, sizeof(y));
    Get(info).fields["area"] = area; Get(area).numbers["world"] = world;
    maps[id] = info; return info;
}
Obj List(std::initializer_list<Obj> values = {}) { auto o = Make("List"); Get(o).items = values; return o; }
Obj Item() { return Get(lastView).fields["data"]; }
Obj Callback() { return Get(Item()).fields["callback"]; }
Obj Root() { return Get(lastMenu).fields["dofusContextualMenuRoot"]; }
size_t Rows() { return Get(Get(lastMenu).fields["rows"]).items.size(); }
Obj OriginalCreate(Obj, Method) {
    ++originalCalls; lastMenu = Make("DofusContextualMenu");
    Get(lastMenu).fields["rows"] = List({Make("NativeInvite"), Make("NativeFollow")});
    auto root = Make("Root"); Get(root).fields = {{"data", lastMenu}, {"panel", root}};
    Get(lastMenu).fields["dofusContextualMenuRoot"] = root; return lastMenu;
}
Obj Invoke(Method m, Obj self, std::initializer_list<void*> args) {
    const auto& method = *static_cast<const MethodData*>(m); const auto& name = method.name;
    std::vector<void*> a(args);
    if (name == "AddManipulator" || name == "RemoveManipulator") {
        Check(!self && a.size() == 2 && Get(a[1]).type == "TooltipManipulator", "Native tooltip extension ABI");
        if (name == "RemoveManipulator" && tooltipDetachFails) throw std::runtime_error("Tooltip detach temporarily unavailable");
        Get(a[0]).fields["nativeTooltip"] = name == "AddManipulator" ? a[1] : nullptr;
        return nullptr;
    }
    if (name == "GetMapInformationById") {
        if (mapReadFails) throw std::runtime_error("Map data unavailable");
        auto id = *static_cast<int64_t*>(a[0]); return maps.contains(id) ? maps.at(id) : nullptr;
    }
    auto& n = Get(self);
    if (name == ".ctor" && method.type == "TooltipManipulator") {
        Check(a.size() == 6 && a[0] == services["fou"] && *static_cast<int*>(a[2]) == 1 && *static_cast<int*>(a[3]) == 7 && *static_cast<int*>(a[4]) == 1 && !*static_cast<bool*>(a[5]), "Native hover caption ABI and placement");
        n.text = Get(a[1]).text; return nullptr;
    }
    if (name == "HideTooltip") { n.numbers["hidden"]++; return nullptr; }
    if (name == ".ctor" && method.type == "PlayerMenuMaker") {
        Check(a.size() == 13 && a[1] == services["fqy"] && *static_cast<int64_t*>(a[2]) == 1234567890123, "Fully initialized native action owner");
        if (constructFails) throw std::runtime_error("Native constructor unavailable");
        n.fields["m_bus"] = services["fmz"]; return nullptr;
    }
    if (name == "get_member" || name == "get_parentMember") {
        Check(n.type == "BindingPayload", "Party binding must be unboxed before invocation"); return n.fields[name];
    }
    if (name == "get_parent") return n.fields["parent"];
    if (name == "get_style" || name == "get_resolvedStyle") return self;
    const std::string read = "UnityEngine.UIElements.IStyle.get_", write = "UnityEngine.UIElements.IStyle.set_";
    if (name.starts_with(read)) { auto b = Make("StyleBox"); Get(b).styles["value"] = n.styles[name.substr(read.size())]; return b; }
    if (name.starts_with(write)) { n.styles[name.substr(write.size())] = Get(a[0]).styles["value"]; return nullptr; }
    if (name == "set_value") { std::memcpy(n.styles["value"].data(), a[0], sizeof(float)); return nullptr; }
    if (name == "SetEnabled" || name == "SetValueWithoutNotify") { n.numbers[name] = *static_cast<bool*>(a[0]); if (name == "SetEnabled") n.numbers["knownEnable"] = 1; return nullptr; }
    if (name == "get_enabledInHierarchy") return Box(n.numbers["SetEnabled"] != -1 && !n.numbers["disabled"] && (n.numbers["SetEnabled"] || !n.numbers.contains("knownEnable")));
    if (name == "get_display") return Box(static_cast<int>(n.numbers["display"]));
    if (name == "get_visibility") return Box(static_cast<int>(n.numbers["visibility"]));
    if (name == "ClassListContains") return Box(bool(n.numbers["small"]));
    if (name == "set_name" || name == "set_tooltip") { n.fields[name] = a[0]; return nullptr; }
    if (name == "set_iconValue" || name == "set_mainStyle" || name == "set_toggleSize" || name == "set_icon" || name == "set_pickingMode") { n.numbers[name] = *static_cast<int*>(a[0]); return nullptr; }
    if (name == "set_color") return nullptr;
    if (name == "Add") { n.items.push_back(a[0]); Get(a[0]).fields["parent"] = self; Get(a[0]).fields["panel"] = n.fields["panel"]; return nullptr; }
    if (name == "RemoveFromHierarchy") {
        if (detachFails) throw std::runtime_error("Temporary detach failure");
        auto parent = n.fields["parent"]; if (parent) { auto& items = Get(parent).items; std::erase(items, self); }
        n.fields["parent"] = n.fields["panel"] = nullptr; return nullptr;
    }
    if (name == "zeo") { Check(n.type == "esg", "Current character location receiver"); return n.fields["location"]; }
    if (name == "get_subArea") return n.fields["area"];
    if (name == "get_worldmapId") return Box(static_cast<int>(n.numbers["world"]));
    if (name == "bfou") { Check(n.type == "fvh", "Concrete social receiver"); return n.fields["spouse"]; }
    if (name == "bfmh") { Check(n.type == "fti", "Concrete party receiver"); return n.fields["members"]; }
    if (name == "oab") return Box(n.numbers["id"]);
    if (name == "get_Count") return Box(static_cast<int>(n.items.size()));
    if (name == "get_Item") return n.items.at(*static_cast<int*>(a[0]));
    if (name == "get_panel") return n.fields["panel"];
    if (name == "get_menuData" || name == "get_itemData") return n.fields["data"];
    if (name == "get_menuItems") return n.fields["rows"];
    if (name == "get_m_callback") return n.fields["callback"];
    if (name == "get_disabled") return Box(bool(n.numbers["disabled"]));
    if (name == "RemoveItemAt") { auto& rows = Get(n.fields["rows"]).items; rows.erase(rows.begin() + *static_cast<int*>(a[0])); return nullptr; }
    if (name == "RemoveMenu") { n.fields["panel"] = nullptr; return nullptr; }
    if (name == "AppendDefaultItem") {
        Check(a.size() == 13 && !a[1] && *static_cast<bool*>(a[3]), "Native row has null fallback action and closes on selection");
        Check(Get(a[6]).text.find("hors ligne") != std::string::npos, "Disabled row explains offline state");
        Check(Get(a[4]).text.find("hors détours") != std::string::npos, "Help distinguishes coordinate distance from route length");
        Check(a[7] && a[10], "Value type arguments are unboxed");
        if (appendFails) throw std::runtime_error("Simulated append failure");
        auto item = Make("ContextMenuItem"); Get(item).text = Get(a[0]).text;
        Get(item).numbers = {{"disabled", *static_cast<bool*>(a[2])}, {"icon", *static_cast<int*>(a[9])}};
        Get(item).fields["callback"] = Make("ClickCallbackHandler"); Get(n.fields["rows"]).items.push_back(item);
        lastView = Make("View"); Get(lastView).fields = {{"data", item}, {"panel", lastView}}; return nullptr;
    }
    if (name == "OnJoinSpouse") {
        Check(method.type == "PlayerMenuMaker" && (n.type == "PartyMemberMenuMaker" || n.type == "PlayerMenuMaker") && a.empty(), "Reuse exact native parameterless spouse action");
        ++teleports; invokedMaker = self;
        if (reenter) { if (n.type == "PlayerMenuMaker") HandleSpousePartyWidgetToggle(lastButton); else HandleSpousePartyMenuCallback(Callback(), lastView); }
        return nullptr;
    }
    throw std::runtime_error("Unexpected call " + name);
}
void Initialize() {
    api.object_get_class = [](Obj o)->Klass { return api.Class("", "", Get(o).type.c_str()); };
    api.class_get_name = [](Klass c) { return static_cast<std::string*>(c)->c_str(); };
    api.object_new = [](Klass c)->Obj { return Make(*static_cast<std::string*>(c)); };
    api.object_unbox = [](Obj o)->void* { return Get(o).type == "PartyMemberItemBinding" ? Get(o).fields["payload"] : o; };
    api.string_new = [](const char* s)->Obj { auto o = Make("String"); Get(o).text = s; return o; };
    api.gchandle_new = [](Obj o, bool)->GCHandle { auto id = serial++; handles[id] = o; return id; };
    api.gchandle_get_target = [](GCHandle id)->Obj { return handles.at(id); };
    api.gchandle_free = [](GCHandle id) { handles.erase(id); };
    api.method_get_param = [](Method, uint32_t)->const void* { return api.Class("", "", "Struct"); };
    api.class_from_type = [](const void* c)->Klass { return const_cast<void*>(c); };
    api.field_get_offset = [](void* field)->size_t { if (!field) throw std::runtime_error("Unknown field"); return *static_cast<size_t*>(field); };
}
struct Fixture {
    Module owner{"offline"}; Obj maker{}, spouse{}, member{}, self{}, members{};
    Fixture() {
        module = &owner; owner.ready = owner.servicesReady = true;
        owner.cached.reset(Make("Cached")); owner.compositor.reset(Make("Compositor"));
        combat = reenter = appendFails = mapReadFails = detachFails = constructFails = tooltipDetachFails = false; teleports = originalCalls = 0; logs.clear(); maps.clear();
        state.refresh = state.cooldown = 0;
        services = {{"foi", Make("fvh")}, {"fne", Make("fti")}, {"fmz", Make("Bus")}, {"fng", Make("esg")}, {"fqy", Make("Dependencies")}};
        services["fou"] = Make("TooltipService");
        auto location = Make("tk"); Get(location).numbers["m_mapId"] = 100;
        Get(services["fng"]).fields["location"] = location;
        MapInfo(100, -5, -7); MapInfo(200, 3, -3);
        // As built by the game's spouse handler (fvg.bhft, 3.7.12.12): name (dcys),
        // online flag and map; the Int64 dcyt is never written and stays 0.
        spouse = Make("io"); Get(spouse).numbers = {{"dcyt", 0}, {"dcyy", 1}, {"dcyz", 200}};
        Get(spouse).fields["dcys"] = api.string_new("Kaelia");
        Get(services["foi"]).fields["spouse"] = spouse;
        member = Make("lg"); Get(member).numbers = {{"id", 1234567890123}, {"ddku", 1}};
        Get(member).fields["ddkt"] = api.string_new("Kaelia");
        self = Make("lg"); Get(self).numbers = {{"id", 4242}, {"ddku", 1}}; Get(self).fields["ddkt"] = api.string_new("Joueur");
        members = List({self, member}); Get(services["fne"]).fields["members"] = members;
        maker = Make("PartyMemberMenuMaker"); Get(maker).numbers["m_playerId"] = 1234567890123;
        Get(maker).fields["m_playerName"] = api.string_new("Kaelia");
        Get(maker).fields = {{"m_socialService", services["foi"]}, {"m_partyService", services["fne"]}, {"m_bus", services["fmz"]}};
    }
    void Open() { Check(createHook(maker, nullptr) == lastMenu, "Preserve native result"); }
    void Click() { Check(HandleSpousePartyMenuCallback(Callback(), lastView), "Recognize owned callback"); }
    ~Fixture() { detachFails = tooltipDetachFails = false; RestoreSpousePartyMenu(); RestoreSpousePartyWidget(); module = nullptr; }
};
struct WidgetFixture : Fixture {
    Obj hub{}, display{}, slot{}, ownSlot{}, companionSlot{}, root{}, portrait{}, binding{}, button{};
    WidgetFixture() {
        hub = services["fqv"] = Make("ghu"); display = Make("PartyDisplayUi"); Get(hub).fields["dytb"] = display;
        slot = Make("PartyMember"); Get(slot).fields["partyMemberWrapper"] = member;
        // The owner's report: 3 filled slots, the player, the spouse and a companion
        // (lf, the game's subclass of lg for party entities) shown last.
        ownSlot = Make("PartyMember"); Get(ownSlot).fields["partyMemberWrapper"] = self;
        auto companion = Make("lf"); Get(companion).numbers = {{"id", 77}, {"ddku", 1}}; Get(companion).fields["ddkt"] = api.string_new("Kaelia");
        companionSlot = Make("PartyMember"); Get(companionSlot).fields["partyMemberWrapper"] = companion;
        Get(display).fields["m_partyMemberSlots"] = List({ownSlot, slot, companionSlot});
        root = Make("Root"); Get(root).fields["panel"] = root;
        portrait = Make("Portrait"); Get(portrait).fields = {{"parent", root}, {"panel", root}};
        Get(portrait).items = {Make("NativeHealth"), Make("NativeActions"), Make("NativeCrown")};
        binding = Make("PartyMemberItemBinding"); auto value = Make("BindingPayload"); Get(binding).fields["payload"] = value;
        Get(value).fields = {{"get_member", root}, {"get_parentMember", portrait}}; Get(slot).fields["binding"] = binding;
    }
    void Tick() { state.refresh = 0; TickSpousePartyWidget(); button = state.button.get(); lastButton = button; }
    void Show() { Tick(); Check(button && Get(portrait).items.size() == 6, "Visible button, badge and border on already-open party"); }
    void ClickButton() { Check(HandleSpousePartyWidgetToggle(button), "Owned native toggle dispatch"); }
};
void WidgetCases() {
    InstallSpousePartyWidget();
    { WidgetFixture f; f.Tick();
      Check(f.button && std::find(logs.begin(), logs.end(), "SPOUSE_PARTY_WIDGET_STATE ready") != logs.end(),
            "Owner's 1.18.0 party (player, spouse, companion; game leaves dcyt at 0) shows the spouse button and outline"); }
    { WidgetFixture f; f.owner.settings.spouseTeleport = false; f.Tick();
      Check(!f.button && Get(f.portrait).items.size() == 3 && !teleports, "Disabled spouse feature creates no portrait controls");
      std::string distance; bool online = true; const char* reason = nullptr;
      Check(!ReadPartySpouse(f.member, distance, online, &reason) && !online && std::string(reason) == "disabled-in-settings", "Disabled preference gates spouse discovery");
      f.owner.settings.spouseTeleport = true; f.Show(); auto old = f.button;
      f.owner.settings.spouseTeleport = false; f.ClickButton();
      Check(!teleports && !TeleportPartySpouse(f.member), "Disable blocks existing and direct button actions immediately");
      TickSpousePartyWidget();
      Check(!SpousePartyWidgetCleanupPending() && Get(f.portrait).items.size() == 3 && !state.button.get(), "Disable removes button, heart, border and tooltip before refresh throttle");
      Check(!HandleSpousePartyWidgetToggle(old), "Detached toggle no longer owned");
      f.owner.settings.spouseTeleport = true; f.Show(); f.ClickButton();
      Check(teleports == 1, "Re-enable restores explicit native teleport"); }
    { WidgetFixture f; f.Show(); f.owner.settings.spouseTeleport = false; tooltipDetachFails = true;
      TickSpousePartyWidget(); Check(SpousePartyWidgetCleanupPending(), "Disabled preference retains failed cleanup for retry");
      f.ClickButton(); Check(!teleports, "Disable remains effective while tooltip cleanup retries");
      tooltipDetachFails = false; TickSpousePartyWidget();
      Check(!SpousePartyWidgetCleanupPending() && Get(f.portrait).items.size() == 3, "Disabled cleanup retries without touching native portrait children"); }
    { WidgetFixture f; f.Show(); auto button = f.button; f.Tick();
      Check(f.button == button && !teleports, "No duplicate UI or automatic teleport");
      Check(Get(button).numbers["set_mainStyle"] == 2 && Get(button).numbers["set_toggleSize"] == 1 && Get(button).numbers["set_iconValue"] == 304, "Match actual native party button style and teleport icon");
      auto oldTip = Get(button).fields["nativeTooltip"];
      Check(Get(oldTip).text.find("12 cases") != std::string::npos, "Native hover tooltip carries distance");
      f.Tick(); Check(Get(button).fields["nativeTooltip"] == oldTip, "Unchanged distance keeps the same hover registration");
      Check(state.size == 26 && Get(state.heart.get()).numbers["set_icon"] == 166 && Get(state.border.get()).numbers["set_pickingMode"] == 1, "Spouse decoration never steals portrait clicks");
      Get(f.root).numbers["small"] = 1; MapInfo(200, -5, -6); f.Tick();
      Check(state.size == 14 && Get(Get(button).fields["nativeTooltip"]).text.find("1 case") != std::string::npos && Get(oldTip).numbers["hidden"] == 1, "Moving spouse refresh replaces and closes only the owned tooltip");
      Check(!HandleSpousePartyWidgetToggle(Make("ForeignToggle")), "Foreign group toggles remain native");
      reenter = true; f.ClickButton(); f.ClickButton(); Check(teleports == 1, "Only explicit native teleport, duplicate and reentrant clicks blocked");
      f.owner.stopped = true; f.Tick(); Check(Get(f.portrait).items.size() == 3 && !SpousePartyWidgetCleanupPending(), "Stop preserves all native portrait elements"); }
    for (int reason = 0; reason < 13; ++reason) {
      WidgetFixture f; f.Show();
      switch (reason) {
        case 0: Get(f.spouse).numbers["dcyy"] = 0; break;
        case 1: combat = true; break;
        case 2: Get(f.members).items.clear(); break;
        case 3: Get(f.slot).fields["partyMemberWrapper"] = Make("lg"); break;
        case 4: Get(f.slot).numbers["m_isWaitingMember"] = 1; break;
        case 5: Get(f.display).numbers["isArenaParty"] = 1; break;
        case 6: Get(f.root).numbers["display"] = 1; break;
        case 7: Get(f.portrait).numbers["visibility"] = 1; break;
        case 8: f.owner.cached.reset(Make("ReplacedCharacter")); break;
        case 9: services["fqv"] = Make("ReplacedHub"); break;
        case 10: Get(f.button).fields["parent"] = Make("ForeignPortrait"); break;
        case 11: f.owner.stopRequested = true; break;
        case 12: constructFails = true; break;
      }
      f.ClickButton(); Check(!teleports, "Stale, hidden, offline, combat, invited, arena and failed-constructor clicks are inert");
    }
    { WidgetFixture f; Get(f.spouse).numbers["dcyy"] = 0; f.Show(); Check(!Get(f.button).numbers["SetEnabled"] && Get(Get(f.button).fields["nativeTooltip"]).text.find("hors ligne") != std::string::npos, "Offline spouse still identified but teleport disabled");
      Get(f.spouse).numbers["dcyy"] = 1; f.Tick(); Check(Get(f.button).numbers["SetEnabled"], "Reconnect enables button"); }
    { WidgetFixture f; combat = true; f.Show(); Check(!Get(f.button).numbers["SetEnabled"], "Combat keeps spouse badge and disables travel"); }
    { WidgetFixture f; f.Show(); Get(f.members).items.clear(); f.Tick(); Check(!f.button && Get(f.portrait).items.size() == 3, "Leaving group removes owned decoration"); }
    { WidgetFixture f; f.Show(); auto old = f.button; tooltipDetachFails = true; RestoreSpousePartyWidget();
      Check(SpousePartyWidgetCleanupPending(), "Failed tooltip detach retains all owners"); HandleSpousePartyWidgetToggle(old); Check(!teleports, "Pending tooltip cleanup disarms teleport");
      tooltipDetachFails = false; RestoreSpousePartyWidget(); Check(!SpousePartyWidgetCleanupPending(), "Tooltip cleanup retries before releasing the button"); }
    { WidgetFixture f; std::string distance; bool online; const char* reason = nullptr;
      Check(ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "ready", "Spouse diagnostic identifies a valid native member");
      Get(services["foi"]).fields["spouse"] = nullptr;
      Check(!ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "spouse-data-missing", "Missing social state is reported without player identifiers");
      Get(services["foi"]).fields["spouse"] = f.spouse; Get(f.members).items.clear();
      Check(!ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "spouse-not-in-accepted-party", "Missing party membership has a distinct diagnostic"); }
    { WidgetFixture f; std::string distance; bool online; const char* reason = nullptr;
      auto companion = Get(f.companionSlot).fields["partyMemberWrapper"];
      Check(!ReadPartySpouse(companion, distance, online, &reason) && std::string(reason) == "companion", "A companion carrying the spouse's name is never the spouse");
      Check(!ReadPartySpouse(f.self, distance, online, &reason) && std::string(reason) == "other-member", "The player's own slot is another member");
      Get(f.spouse).fields["dcys"] = api.string_new("");
      Check(!ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "spouse-name-unavailable", "Unnamed spouse data fails closed");
      Get(f.spouse).fields["dcys"] = api.string_new("Kaelia"); Get(f.member).numbers["ddku"] = 0;
      Check(!ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "spouse-not-in-accepted-party", "Invited guest with the spouse's name is not matched"); }
    { WidgetFixture f; auto twin = Make("lg"); Get(twin).numbers = {{"id", 99}, {"ddku", 1}}; Get(twin).fields["ddkt"] = api.string_new("Kaelia");
      Get(f.members).items.push_back(twin); f.Tick(); std::string distance; bool online; const char* reason = nullptr;
      Check(!f.button && !ReadPartySpouse(f.member, distance, online, &reason) && std::string(reason) == "spouse-name-ambiguous", "Two members with the spouse's name fail closed"); }
    { WidgetFixture f; Get(f.spouse).fields["dcys"] = api.string_new("kaelia"); f.Tick(); Check(!f.button, "Name match is exact, as the game's own"); }
    { WidgetFixture f; f.Show(); auto old = f.button; detachFails = true; RestoreSpousePartyWidget();
      Check(SpousePartyWidgetCleanupPending(), "Failed removal retains GC roots for retry"); HandleSpousePartyWidgetToggle(old); Check(!teleports, "Cleanup-pending button disarmed");
      detachFails = false; RestoreSpousePartyWidget(); Check(!SpousePartyWidgetCleanupPending() && Get(f.portrait).items.size() == 3, "Retried removal succeeds without deleting native children"); }
    for (int reason = 0; reason < 4; ++reason) { WidgetFixture f;
      if (reason == 0) Get(f.spouse).fields["dcys"] = api.string_new("Autre");
      if (reason == 1) Get(f.slot).numbers["m_isWaitingMember"] = 1;
      if (reason == 2) Get(f.display).numbers["isArenaParty"] = 1;
      if (reason == 3) Get(f.root).fields["panel"] = nullptr;
      f.Tick(); Check(!f.button, "No widget on other players, guests, arena or detached party"); }
}
}
namespace managed {
Api api{}; uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { auto& c = fake::classes[name]; if (!c) c = std::make_unique<std::string>(name); return c.get(); }
Method Api::Find(Klass c, const char* name, int) const { auto m = std::make_unique<fake::MethodData>(); m->type = *static_cast<std::string*>(c); m->name = name; auto p = m.get(); fake::methods.push_back(std::move(m)); return p; }
Method Api::FindVerified(Klass c, const char* name, int argc, uintptr_t) const { return Find(c, name, argc); }
Obj Api::Invoke(Method m, Obj self, std::initializer_list<void*> args) const { return fake::Invoke(m, self, args); }
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const { return Invoke(Find(object_get_class(self), name, static_cast<int>(args.size())), self, args); }
Obj Api::New(Klass c, std::initializer_list<void*> args) const { auto o = fake::Make(*static_cast<std::string*>(c)); if (*static_cast<std::string*>(c) == "Length") std::memcpy(o, *args.begin(), sizeof(float)); return o; }
Obj Api::Field(Obj self, const char* name) const { return fake::Get(self).fields[name]; }
void* Api::FieldAddress(Obj self, const char* name) const { return &fake::Get(self).numbers[name]; }
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string(); }
// Field layout of the pinned 3.7.12.12 metadata (contracts-new.json); unknown fields fail.
void* Api::FieldInfo(Klass c, const char* name) const {
    static const std::unordered_map<std::string, size_t> layout{
        {"io.dcys", 0x10}, {"io.dcyy", 0x31}, {"io.dcyz", 0x38}, {"lg.ddkt", 0x18}, {"lg.ddku", 0x20},
        {"PlayerMenuMaker.m_playerId", 0xB8}};
    auto found = layout.find(*static_cast<std::string*>(c) + "." + name);
    return found == layout.end() ? nullptr : const_cast<size_t*>(&found->second);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path p) : folder(p) {}
bool Module::InCombat() const { return fake::combat; }
managed::Obj Module::FindService(const char* name) const { return fake::services[name]; }
void Log(const std::string& s) { fake::logs.push_back(s); }
void AddHook(managed::Method, uintptr_t rva, void* replacement, void** original) {
    fake::Check(rva == 0x16ECE90, "Only PlayerMenuMaker.CreateMenu is hooked");
    fake::createHook = reinterpret_cast<decltype(fake::createHook)>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalCreate);
}
int main() { using namespace fake; try {
    Initialize(); InstallSpousePartyMenu();
    { Fixture f; f.owner.settings.spouseTeleport = false; f.Open();
      Check(originalCalls == 1 && Rows() == 2 && !teleports, "Disabled preference preserves native menu without spouse entry");
      f.owner.settings.spouseTeleport = true; f.Open(); Check(Rows() == 3, "Enabling creates spouse entry");
      f.owner.settings.spouseTeleport = false; f.Click(); Check(!teleports, "Disable disarms already-open spouse menu immediately");
      TickSpousePartyMenu(); Check(Rows() == 2, "Disable removes only the owned spouse row");
      f.owner.settings.spouseTeleport = true; f.Open(); f.Click(); Check(teleports == 1, "Re-enable restores explicit menu action"); }
    { Fixture f; f.Open(); Check(originalCalls == 1 && Rows() == 3 && teleports == 0, "Opening menu must only append one item without teleporting");
      Check(Get(Item()).text == "Se téléporter au conjoint — 12 cases" && Get(Item()).numbers["icon"] == 304, "French distance label and native teleport icon");
      TickSpousePartyMenu(); Check(teleports == 0, "Tick never triggers travel"); reenter = true; f.Click(); f.Click();
      Check(teleports == 1 && invokedMaker == f.maker, "Explicit click dispatches once, even on reentry"); }
    { Fixture f; MapInfo(200, -5, -7); f.Open(); Check(Get(Item()).text.ends_with("0 case"), "Same coordinates display zero"); }
    { Fixture f; MapInfo(200, -5, -6); f.Open(); Check(Get(Item()).text.ends_with("1 case"), "One map step has singular label"); }
    { Fixture f; MapInfo(200, -5, -7, 14); f.Open(); Check(Get(Item()).text.ends_with("autre carte du monde"), "Matching coordinates in another world never display zero distance"); }
    { Fixture f; MapInfo(200, 3, -3, -1); f.Open(); Check(Get(Item()).text.ends_with("distance inconnue"), "Unknown world cannot produce a numeric distance"); }
    { Fixture f; maps.erase(200); f.Open(); Check(Get(Item()).text.ends_with("distance inconnue"), "Missing spouse map is not map zero"); f.Click(); Check(teleports == 1, "Unavailable display data must not change native teleport behavior"); }
    { Fixture f; Get(f.spouse).numbers["dcyz"] = 0; f.Open(); Check(Get(Item()).text.ends_with("distance inconnue"), "Unset spouse location is not a distance"); }
    { Fixture f; Get(maps.at(200)).fields["area"] = nullptr; f.Open(); Check(Get(Item()).text.ends_with("distance inconnue"), "Missing area does not guess a world"); }
    { Fixture f; services.erase("fng"); f.Open(); Check(Get(Item()).text.ends_with("distance inconnue"), "Missing local character position is explicit"); }
    { Fixture f; mapReadFails = true; f.Open(); Check(Get(Item()).text.ends_with("distance inconnue") && Rows() == 3, "Optional distance errors keep native action available"); }
    { Fixture f; MapInfo(100, -32768, -32768); MapInfo(200, 32767, 32767); f.Open(); Check(Get(Item()).text.ends_with("131070 cases"), "Signed coordinates widen before subtraction"); }
    { Fixture f; f.Open(); MapInfo(200, -4, -7); f.Open(); Check(Get(Item()).text.ends_with("1 case"), "Reopening uses latest spouse map position"); MapInfo(100, -6, -9); f.Open(); Check(Get(Item()).text.ends_with("4 cases"), "Reopening uses latest local position"); }
    { Fixture f; Get(f.spouse).numbers["dcyy"] = 0; f.Open(); Check(Get(Item()).text.ends_with("hors ligne"), "Offline status must not present an old numeric distance"); }
    { Fixture f; Get(f.maker).type = "PlayerMenuMaker"; f.Open(); Check(Rows() == 2, "Other player/chat menus untouched"); }
    { Fixture f; Get(f.maker).numbers["m_playerId"]++; f.Open(); Check(Rows() == 2, "Another group member cannot get spouse action"); }
    { Fixture f; Get(f.maker).numbers["m_playerId"] = 4242; f.Open(); Check(Rows() == 2, "Right-clicking the player's own party slot gets no spouse action"); }
    { Fixture f; Get(f.spouse).fields["dcys"] = api.string_new(""); f.Open(); Check(Rows() == 2, "Unnamed spouse data gets no action"); }
    { Fixture f; auto twin = Make("lg"); Get(twin).numbers = {{"id", 99}, {"ddku", 1}}; Get(twin).fields["ddkt"] = api.string_new("Kaelia");
      Get(f.members).items.push_back(twin); f.Open(); Check(Rows() == 2, "Ambiguous spouse name fails closed"); }
    { Fixture f; f.Open(); Get(f.member).fields["ddkt"] = api.string_new("Renomme"); f.Click(); Check(!teleports, "Renamed member after opening rejected"); }
    { Fixture f; Get(services["foi"]).fields["spouse"] = nullptr; f.Open(); Check(Rows() == 2, "Unmarried player gets no action"); }
    { Fixture f; Get(f.member).numbers["ddku"] = 0; f.Open(); Check(Rows() == 2, "Invited guest is not an actual group member"); }
    { Fixture f; Get(f.members).items.clear(); f.Open(); Check(Rows() == 2, "Out-of-group spouse gets no party action"); }
    { Fixture f; combat = true; f.Open(); Check(Rows() == 2, "No teleport entry during combat"); }
    { Fixture f; Get(f.spouse).numbers["dcyy"] = 0; f.Open(); Check(Rows() == 3 && Get(Item()).numbers["disabled"], "Offline spouse has disabled entry"); f.Click(); Check(!teleports, "Offline row never sends action"); }
    { Fixture f; Get(f.spouse).numbers["dcyt"] = 999; f.Open(); f.Click(); Check(teleports == 1, "Spouse identification never depends on the unwritten Int64 dcyt"); }
    { Fixture f; f.Open(); Get(f.spouse).numbers["dcyy"] = 0; f.Click(); Check(!teleports, "Disconnect after opening rejected"); TickSpousePartyMenu(); Check(Rows() == 2, "Stale row removed"); }
    { Fixture f; f.Open(); Get(f.members).items.clear(); f.Click(); Check(!teleports, "Leaving party after opening rejected"); }
    { Fixture f; f.Open(); Get(f.member).numbers["ddku"] = 0; f.Click(); Check(!teleports, "Member becoming a guest rejected"); }
    { Fixture f; f.Open(); auto replacement = Make("lg"); Get(replacement).numbers = Get(f.member).numbers; Get(replacement).fields = Get(f.member).fields; Get(f.members).items = {replacement}; f.Click(); Check(!teleports, "Leave/rejoin same ID invalidates old row"); }
    { Fixture f; f.Open(); Get(f.spouse).fields["dcys"] = api.string_new("Autre"); f.Click(); Check(!teleports, "Changed marriage invalidates action"); }
    { Fixture f; f.Open(); Get(f.maker).numbers["m_playerId"]++; f.Click(); Check(!teleports, "Rebound menu target invalidates action"); }
    { Fixture f; f.Open(); combat = true; f.Click(); Check(!teleports, "Fight beginning before click invalidates action"); }
    for (auto name : {"foi", "fne", "fmz"}) { Fixture f; f.Open(); services[name] = Make("Replacement"); f.Click(); Check(!teleports, "Replaced service invalidates action"); }
    { Fixture f; f.Open(); f.owner.cached.reset(Make("OtherCharacter")); f.Click(); Check(!teleports, "Character change invalidates action"); }
    { Fixture f; f.Open(); f.owner.stopRequested = true; f.Click(); Check(!teleports, "Stopped mod cannot send action"); }
    { Fixture f; f.Open(); Get(lastView).fields["panel"] = nullptr; f.Click(); Check(!teleports, "Detached view cannot send action"); }
    { Fixture f; f.Open(); Get(Root()).fields["panel"] = nullptr; f.Click(); Check(!teleports, "Detached menu root cannot send action"); }
    { Fixture f; f.Open(); Get(Root()).fields["data"] = Make("NewMenu"); f.Click(); Check(!teleports, "Recycled menu root cannot send action"); }
    { Fixture f; f.Open(); Get(Item()).numbers["disabled"] = 1; f.Click(); Check(!teleports, "Disabled native row cannot send action"); }
    { Fixture f; f.Open(); auto callback = Callback(); Get(lastView).fields["data"] = Make("OtherRow"); Check(HandleSpousePartyMenuCallback(callback, lastView) && !teleports, "Mismatched row cannot send action"); }
    { Fixture f; f.Open(); auto old = Callback(), oldView = lastView; f.Open(); Check(Rows() == 3 && !HandleSpousePartyMenuCallback(old, oldView), "Reopening creates one row and releases old null callback"); }
    { Fixture f; f.Open(); TickSpousePartyMenu(); auto old = Callback(); Get(Root()).fields["panel"] = nullptr; TickSpousePartyMenu(); Check(Rows() == 2 && !HandleSpousePartyMenuCallback(old, lastView), "Closing menu releases stale callback"); }
    { Fixture f; Get(f.members).items.resize(129, f.member); f.Open(); Check(Rows() == 2 && !teleports, "Malformed collection fails closed"); }
    { Fixture f; appendFails = true; f.Open(); Check(Rows() == 2 && !teleports, "Failed native append cannot dispatch"); }
    WidgetCases();
    Check(!HandleSpousePartyMenuCallback(nullptr, nullptr), "Unrelated callback passes through");
    Check(handles.empty(), "All managed roots released");
    std::cout << "PASS: party spouse identification, native explicit action, stale menu guards and cleanup\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
