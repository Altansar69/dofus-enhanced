#include "social/party-travel-widget.hpp"
#include "social/party-slot-layout.hpp"
#include "travel/chat-travel.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <unordered_map>
#include <algorithm>

// Offline: the travel button on the other party members' slots, with a fake
// native runtime (no game process). The party: the player, Alpha, the spouse
// Kaelia (her slot keeps the spouse widget's teleport button), a companion, an
// invited guest and Bravo.
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
std::unordered_map<int, Obj> subAreas;
std::set<std::string> knownCells; // "x,y,world" where the game knows a map (esg.zid)
std::vector<std::string> logs;
std::vector<ChatTpTarget> routes;              // StartChatTravel
std::vector<std::array<int, 4>> autopilots;    // esg.zhy (x, y, world, immediate)
GCHandle serial = 1;
bool combat = false, detachFails = false, tooltipDetachFails = false, reenter = false, subAreaFails = false;
int zidCalls = 0;
Obj spouseMember{};  // the member ReadPartySpouse claims (the spouse widget's slot)
Obj lastButton{};
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Obj Make(const std::string& type) { auto n = std::make_unique<Node>(); n->type = type; Obj p = n->raw.data(); objects[p] = n.get(); nodes.push_back(std::move(n)); return p; }
Node& Get(Obj o) { if (!objects.contains(o)) throw std::runtime_error("Unknown object"); return *objects.at(o); }
template<class T> Obj Box(T value) { auto o = Make("Box"); std::memcpy(o, &value, sizeof(value)); return o; }
Obj List(std::initializer_list<Obj> values = {}) { auto o = Make("List"); Get(o).items = values; return o; }
Obj SubArea(int id, int world, const char* name) {
    auto area = Make("SubAreaData"); Get(area).numbers["world"] = world; Get(area).text = name; subAreas[id] = area; return area;
}
Obj MapInfo(int64_t id, int16_t x, int16_t y, Obj area) {
    auto info = Make("MapInformationData");
    std::memcpy(static_cast<unsigned char*>(info) + 0x18, &x, sizeof(x));
    std::memcpy(static_cast<unsigned char*>(info) + 0x1A, &y, sizeof(y));
    Get(info).fields["area"] = area; maps[id] = info; return info;
}
float StyleValue(Obj o, const char* property) { float v = 0; std::memcpy(&v, Get(o).styles[property].data(), sizeof(v)); return v; }
std::string Key(int x, int y, int world) { return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(world); }
Obj Invoke(Method m, Obj self, std::initializer_list<void*> args) {
    const auto& method = *static_cast<const MethodData*>(m); const auto& name = method.name;
    std::vector<void*> a(args);
    if (name == "AddManipulator" || name == "RemoveManipulator") {
        Check(!self && a.size() == 2 && Get(a[1]).type == "TooltipManipulator", "Native tooltip extension ABI");
        if (name == "RemoveManipulator" && tooltipDetachFails) throw std::runtime_error("Tooltip detach temporarily unavailable");
        Get(a[0]).fields["nativeTooltip"] = name == "AddManipulator" ? a[1] : nullptr;
        return nullptr;
    }
    if (name == "GetMapInformationById") { auto id = *static_cast<int64_t*>(a[0]); return maps.contains(id) ? maps.at(id) : nullptr; }
    if (name == "get_subAreasDataRoot") { Check(!self && method.type == "DataCenterModule", "Static subarea root"); return services["subRoot"]; }
    auto& n = Get(self);
    if (name == "GetSubAreaById") {
        Check(n.type == "SubAreasDataRoot" && a.size() == 1, "Subarea lookup on the game's root, by Int32 id");
        if (subAreaFails) throw std::runtime_error("Subarea data unavailable");
        auto id = *static_cast<int*>(a[0]); return subAreas.contains(id) ? subAreas.at(id) : nullptr;
    }
    if (name == ".ctor" && method.type == "TooltipManipulator") {
        Check(a.size() == 6 && a[0] == services["fou"] && *static_cast<int*>(a[2]) == 1 && *static_cast<int*>(a[3]) == 7 && *static_cast<int*>(a[4]) == 1 && !*static_cast<bool*>(a[5]), "Native hover caption ABI and placement");
        n.text = Get(a[1]).text; return nullptr;
    }
    if (name == "HideTooltip") { n.numbers["hidden"]++; return nullptr; }
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
    if (name == "set_name") { n.text = Get(a[0]).text; return nullptr; }
    if (name == "set_iconValue" || name == "set_mainStyle" || name == "set_toggleSize") { n.numbers[name] = *static_cast<int*>(a[0]); return nullptr; }
    if (name == "Add") { n.items.push_back(a[0]); Get(a[0]).fields["parent"] = self; Get(a[0]).fields["panel"] = n.fields["panel"]; return nullptr; }
    if (name == "RemoveFromHierarchy") {
        if (detachFails) throw std::runtime_error("Temporary detach failure");
        auto parent = n.fields["parent"]; if (parent) { auto& items = Get(parent).items; std::erase(items, self); }
        n.fields["parent"] = n.fields["panel"] = nullptr; return nullptr;
    }
    if (name == "zcx") { Check(n.type == "esg", "Player id from the character service"); return Box(int64_t(4242)); }
    if (name == "zeo") { Check(n.type == "esg", "Current character location receiver"); return n.fields["location"]; }
    if (name == "zid") {
        Check(n.type == "esg" && a.size() == 3, "Maps at coordinates from the character service");
        ++zidCalls;
        const auto key = Key(*static_cast<int*>(a[0]), *static_cast<int*>(a[1]), *static_cast<int*>(a[2]));
        return knownCells.contains(key) ? List({Box(int64_t(5000))}) : List();
    }
    if (name == "zhy") {
        Check(n.type == "esg" && a.size() == 4, "The game's autopilot to a position: esg.zhy(x, y, world, immediate)");
        autopilots.push_back({*static_cast<int*>(a[0]), *static_cast<int*>(a[1]), *static_cast<int*>(a[2]), int(*static_cast<bool*>(a[3]))});
        if (reenter) HandlePartyTravelWidgetToggle(lastButton);
        return nullptr;
    }
    if (name == "get_subArea") return n.fields["area"];
    if (name == "get_worldmapId") return Box(static_cast<int>(n.numbers["world"]));
    if (name == "get_name") { auto s = Make("String"); Get(s).text = n.text; return s; }
    if (name == "oab") return Box(n.numbers["id"]);
    if (name == "get_Count") return Box(static_cast<int>(n.items.size()));
    if (name == "get_Item") return n.items.at(*static_cast<int*>(a[0]));
    if (name == "get_panel") return n.fields["panel"];
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
    api.field_get_offset = [](void* field)->size_t { if (!field) throw std::runtime_error("Unknown field"); return *static_cast<size_t*>(field); };
}
Obj Member(int64_t id, const char* name, int x, int y, int subArea, const char* type = "lg") {
    auto m = Make(type); Get(m).numbers = {{"id", id}, {"ddku", 1}, {"ddll", x}, {"ddlm", y}, {"ddln", subArea}};
    Get(m).fields["ddkt"] = api.string_new(name); return m;
}
struct SlotParts { Obj slot, root, portrait; };
struct Fixture {
    Module owner{"offline"}; Obj hub{}, display{}, self{}, alpha{}, spouse{}, companion{}, guest{}, bravo{};
    std::vector<SlotParts> slots;
    Fixture() {
        module = &owner; owner.ready = owner.servicesReady = true;
        owner.cached.reset(Make("Cached")); owner.compositor.reset(Make("Compositor"));
        combat = detachFails = tooltipDetachFails = reenter = subAreaFails = false; zidCalls = 0;
        logs.clear(); maps.clear(); subAreas.clear(); knownCells.clear(); routes.clear(); autopilots.clear();
        services = {{"fne", Make("fti")}, {"fng", Make("esg")}, {"fou", Make("TooltipService")}, {"subRoot", Make("SubAreasDataRoot")}};
        hub = services["fqv"] = Make("ghu"); display = Make("PartyDisplayUi"); Get(hub).fields["dytb"] = display;
        auto here = SubArea(95, 1, "Astrub");
        auto location = Make("tk"); Get(location).numbers["m_mapId"] = 100; Get(services["fng"]).fields["location"] = location;
        MapInfo(100, -5, -7, here);
        SubArea(440, 1, "Plaine des Scarafeuilles"); SubArea(12, 1, "Village d’Amakna");
        knownCells = {Key(3, -3, 1), Key(-2, 0, 1), Key(4, 5, 1)};
        self = Member(4242, "Joueur", -5, -7, 95);
        alpha = Member(1001, "Alpha", 3, -3, 440);
        spouse = Member(1002, "Kaelia", 4, 5, 12);
        companion = Member(77, "Compagnon", 3, -3, 440, "lf");
        guest = Member(1003, "Invite", 3, -3, 440); Get(guest).numbers["ddku"] = 0;
        bravo = Member(1004, "Bravo", -2, 0, 12);
        spouseMember = spouse;
        std::vector<Obj> list;
        for (auto m : {self, alpha, spouse, companion, guest, bravo}) { slots.push_back(Slot(m)); list.push_back(slots.back().slot); }
        auto all = Make("List"); Get(all).items = list; Get(display).fields["m_partyMemberSlots"] = all;
    }
    SlotParts Slot(Obj member) {
        SlotParts s{Make("PartyMember"), Make("Root"), Make("Portrait")};
        Get(s.slot).fields["partyMemberWrapper"] = member;
        Get(s.root).fields["panel"] = s.root;
        Get(s.portrait).fields = {{"parent", s.root}, {"panel", s.root}};
        Get(s.portrait).items = {Make("NativeHealth"), Make("NativeActions"), Make("NativeCrown")};
        auto binding = Make("PartyMemberItemBinding"), value = Make("BindingPayload");
        Get(binding).fields["payload"] = value; Get(value).fields = {{"get_member", s.root}, {"get_parentMember", s.portrait}};
        Get(s.slot).fields["binding"] = binding; return s;
    }
    void Tick() { TickPartyTravelWidget(); }
    void Refresh() { ForceRefresh(); Tick(); }
    static void ForceRefresh();
    // The mod's button on a slot's portrait, if any.
    Obj Button(size_t slot) {
        for (auto item : Get(slots[slot].portrait).items) if (Get(item).type == "DofusToggleButtonCustom") return item;
        return nullptr;
    }
    size_t Buttons() { size_t n = 0; for (size_t i = 0; i < slots.size(); ++i) n += Button(i) != nullptr; return n; }
    std::string Tip(size_t slot) { auto tip = Get(Button(slot)).fields["nativeTooltip"]; return tip ? Get(tip).text : std::string(); }
    bool Enabled(size_t slot) { return Get(Button(slot)).numbers["SetEnabled"] != 0; }
    bool Click(size_t slot) { lastButton = Button(slot); return HandlePartyTravelWidgetToggle(lastButton); }
    ~Fixture() { detachFails = tooltipDetachFails = false; RestorePartyTravelWidget(); module = nullptr; }
};
bool Logged(const std::string& text) { return std::any_of(logs.begin(), logs.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; }); }
}
// The widget's refresh throttle and click cooldown, reached through the source.
#include "social/party-travel-widget.cpp"
void fake::Fixture::ForceRefresh() { refresh = 0; cooldown = 0; }
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
        {"lg.ddkt", 0x18}, {"lg.ddku", 0x20}, {"lg.ddll", 0x68}, {"lg.ddlm", 0x6C}, {"lg.ddln", 0x70},
        {"MapInformationData.posX", 0x18}, {"MapInformationData.posY", 0x1A}};
    auto found = layout.find(*static_cast<std::string*>(c) + "." + name);
    return found == layout.end() ? nullptr : const_cast<size_t*>(&found->second);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path p) : folder(p) {}
bool Module::InCombat() const { return fake::combat; }
managed::Obj Module::FindService(const char* name) const { return fake::services[name]; }
void Log(const std::string& s) { fake::logs.push_back(s); }
// The spouse widget's own claim: its slot keeps the teleport button.
bool ReadPartySpouse(managed::Obj member, std::string& distance, bool& online, const char** reason) {
    distance = "12 cases"; online = true;
    const bool spouse = module && module->settings.spouseTeleport && member == fake::spouseMember;
    if (reason) *reason = spouse ? "ready" : "other-member";
    return spouse;
}
void StartChatTravel(ChatTpTarget target) {
    fake::routes.push_back(target);
    if (fake::reenter) HandlePartyTravelWidgetToggle(fake::lastButton);
}
int main() { using namespace fake; try {
    Initialize();
    // Installed natives and checked layout; without them the feature stays off.
    {
        Fixture f; InstallPartyTravelWidget(); Check(installed && !Logged("PARTY_TRAVEL_WIDGET_UNAVAILABLE"), "Install verifies natives and field offsets");
    }
    // One button per other accepted member: Alpha and Bravo; not the player, the
    // spouse (her own teleport button), the companion or the invited guest.
    {
        Fixture f; f.Refresh();
        Check(f.Buttons() == 2 && f.Button(1) && f.Button(5), "Buttons on Alpha and Bravo only");
        Check(!f.Button(0) && !f.Button(2) && !f.Button(3) && !f.Button(4), "No button on the player, the spouse, the companion or the guest");
        Check(Logged("PARTY_TRAVEL_WIDGET_STATE slots=6 filled=6 buttons=2 skipped=self,spouse,companion,guest"), "Discovery explains each skipped slot");
        auto b = f.Button(1);
        Check(Get(b).numbers["set_iconValue"] == 336 && Get(b).numbers["set_mainStyle"] == 2 && Get(b).numbers["set_toggleSize"] == 1 &&
            Get(b).text == "dofus_miniatures_party_travel", "Spouse button's native style, zaap icon");
        Check(Get(f.slots[1].portrait).items.size() == 4, "Native slot children kept, one owned button added");
        Check(f.Enabled(1) && f.Enabled(5), "Known, reachable positions enable the button");
        Check(f.Tip(1).starts_with("Aller jusqu’à Alpha — [3,-3] Plaine des Scarafeuilles (12 cases)") &&
            f.Tip(1).find("Trajet du mod") != std::string::npos && f.Tip(1).find("Dernière position connue") != std::string::npos,
            "Tooltip: member, coordinates, zone, distance and the mod's route");
        Check(f.Tip(5).starts_with("Aller jusqu’à Bravo — [-2,0] Village d’Amakna (10 cases)"), "Each member's own position");
        auto tip = Get(b).fields["nativeTooltip"]; f.Refresh();
        Check(Get(b).fields["nativeTooltip"] == tip && f.Buttons() == 2, "Unchanged state keeps the same button and tooltip");
        // The member moves: the tooltip follows, the old one closed.
        Get(f.alpha).numbers["ddll"] = 4; Get(f.alpha).numbers["ddlm"] = 5; Get(f.alpha).numbers["ddln"] = 12; f.Refresh();
        Check(f.Tip(1).starts_with("Aller jusqu’à Alpha — [4,5] Village d’Amakna (21 cases)") && Get(tip).numbers["hidden"] == 1,
            "Moving member refreshes the tooltip and closes the old one");
        Check(Logged("PARTY_TRAVEL_BUTTON button=0 ready x=4 y=5 world=1"), "State change logged without names");
    }
    // Layout: the button sits in the top band, clear of the game's crown, sword,
    // life bar, follow arrow and icon column, on big and small slots.
    {
        Fixture f; f.Refresh();
        for (int compact = 0; compact < 2; ++compact) {
            Get(f.slots[1].root).numbers["small"] = compact; f.Refresh();
            auto b = f.Button(1);
            const party_slot::Box box{StyleValue(b, "left"), StyleValue(b, "top"), StyleValue(b, "width"), StyleValue(b, "height")};
            Check(box.width == (compact ? 14.0f : 26.0f) && box.height == box.width && StyleValue(b, "maxWidth") == box.width, "Native toggle size per slot size");
            Check(party_slot::Clear(box, compact == 1), "The travel button covers none of the game's slot elements");
            for (const auto& native : party_slot::NativeElements) Check(!party_slot::Overlap(box, compact ? native.small : native.big), native.name);
        }
    }
    // Click with « Remplacer l’autopilote du jeu » ticked: the mod's route.
    {
        Fixture f; f.Refresh();
        Check(f.Click(1) && routes.size() == 1 && routes[0] == ChatTpTarget{3, -3, 1} && autopilots.empty(), "Planner on: StartChatTravel to the member's coordinates and world");
        Check(Logged("PARTY_TRAVEL_REQUEST route=mod x=3 y=-3 world=1"), "Mod route logged");
        Check(!Get(f.Button(1)).numbers["SetEnabled"], "Button disabled at once after a click");
        Check(f.Click(1) && routes.size() == 1 && Logged("PARTY_TRAVEL_CLICK_IGNORED reason=cooldown"), "Double click ignored");
        f.Refresh(); reenter = true; f.Click(5); Check(routes.size() == 2 && routes[1] == ChatTpTarget{-2, 0, 1}, "Reentrant click dispatches once");
    }
    // Unticked: the game's own autopilot to that position (esg.zhy).
    {
        Fixture f; f.owner.settings.mapTravelPlanner = false; f.Refresh();
        Check(f.Tip(1).find("Autopilote du jeu jusqu’à cette position.") != std::string::npos, "Tooltip names the game's autopilot");
        Check(f.Click(1) && routes.empty() && autopilots.size() == 1 && autopilots[0] == std::array<int, 4>{3, -3, 1, 1},
            "Planner off: esg.zhy(x, y, world, immediate) on the character service");
        Check(Logged("PARTY_TRAVEL_REQUEST route=game native=esg.zhy x=3 y=-3 world=1"), "Game route logged");
        f.owner.settings.mapTravelPlanner = true; f.Refresh(); f.Click(5);
        Check(routes.size() == 1 && autopilots.size() == 1, "The setting is read at each click");
    }
    // Combat: kept, greyed, inert.
    {
        Fixture f; combat = true; f.Refresh();
        Check(f.Buttons() == 2 && !f.Enabled(1) && f.Tip(1).find("Indisponible pendant un combat.") != std::string::npos, "Combat greys the button");
        Check(f.Click(1) && routes.empty() && autopilots.empty() && Logged("PARTY_TRAVEL_CLICK_IGNORED reason=combat"), "Combat click inert");
        Check(zidCalls == 0, "No map lookup in combat");
        combat = false; f.Refresh(); Check(f.Enabled(1), "After combat the button comes back");
        f.Refresh(); combat = true; Check(f.Click(1) && routes.empty(), "A fight starting before the click blocks it");
    }
    // Unknown or unreachable positions.
    {
        Fixture f; Get(f.alpha).numbers["ddln"] = 0; f.Refresh();
        Check(!f.Enabled(1) && f.Tip(1).find("Position inconnue") != std::string::npos && f.Tip(1).starts_with("Aller jusqu’à Alpha\n") &&
            f.Tip(1).find("Dernière position connue") == std::string::npos, "No subarea: unknown position, no claim of a last known one");
        Check(Logged("PARTY_TRAVEL_BUTTON button=0 unknown-position subarea=0"), "Unknown position logged");
        Check(f.Click(1) && routes.empty() && Logged("PARTY_TRAVEL_CLICK_IGNORED reason="), "Unknown position: the greyed button is inert");
        Get(f.alpha).numbers["ddln"] = 9999; f.Refresh(); Check(!f.Enabled(1), "Subarea missing from the game's data: unknown");
        Get(f.alpha).numbers["ddln"] = 440; f.Refresh(); Check(f.Enabled(1), "Known again");
        Get(f.bravo).numbers["ddlm"] = 1; f.Refresh(); // a cell without any map the game knows
        Check(!f.Enabled(5) && f.Tip(5).find("aucune carte") != std::string::npos && Logged("no-map x=-2 y=1 world=1"), "No game map at the coordinates: greyed and logged");
        Check(f.Click(5) && routes.empty(), "Unreachable click inert");
        Get(f.alpha).numbers["ddll"] = -5; Get(f.alpha).numbers["ddlm"] = -7; Get(f.alpha).numbers["ddln"] = 95; knownCells.insert(Key(-5, -7, 1)); f.Refresh();
        Check(!f.Enabled(1) && f.Tip(1).find("Tu es déjà à cette position.") != std::string::npos, "Same position as the player: nothing to do");
        SubArea(800, 3, "Srambad"); knownCells.insert(Key(3, -3, 3)); Get(f.alpha).numbers["ddll"] = 3; Get(f.alpha).numbers["ddlm"] = -3;
        Get(f.alpha).numbers["ddln"] = 800; f.Refresh();
        Check(f.Enabled(1) && f.Tip(1).find("(autre carte du monde)") != std::string::npos, "Another world: no distance, the route decides");
        f.Click(1); Check(routes.size() == 1 && routes[0] == ChatTpTarget{3, -3, 3}, "Another world's coordinates travel with their world");
        subAreaFails = true; f.Refresh(); f.Refresh();
        Check(f.Buttons() == 2 && !f.Enabled(1) && !f.Enabled(5) && f.Tip(1).find("Position inconnue") != std::string::npos,
            "A subarea data error greys the buttons, position unknown");
        Check(std::count_if(logs.begin(), logs.end(), [](const std::string& l) { return l.starts_with("PARTY_TRAVEL_SUBAREA_ERROR"); }) == 1,
            "Logged once, not at every tick");
        subAreaFails = false; f.Refresh(); Check(f.Enabled(1) && f.Tip(1).find("Srambad") != std::string::npos, "Recovered after the error");
    }
    // The option, read on the fly.
    {
        Fixture f; f.owner.settings.partyTravelButton = false; f.Refresh();
        Check(f.Buttons() == 0, "Unticked: no button");
        f.owner.settings.partyTravelButton = true; f.Refresh(); auto old = f.Button(1);
        f.owner.settings.partyTravelButton = false; Check(HandlePartyTravelWidgetToggle(old) && routes.empty(), "Unticked before the tick: click inert");
        f.Tick(); Check(f.Buttons() == 0 && !PartyTravelWidgetCleanupPending() && !HandlePartyTravelWidgetToggle(old), "Unticked: buttons and tooltips removed");
        Check(Get(f.slots[1].portrait).items.size() == 3, "Native slot children untouched");
        f.owner.settings.partyTravelButton = true; f.Refresh(); Check(f.Buttons() == 2, "Ticked again: back");
    }
    // Without « Téléportation au conjoint », the spouse is a member like any other.
    {
        Fixture f; f.owner.settings.spouseTeleport = false; f.Refresh();
        Check(f.Buttons() == 3 && f.Button(2), "Spouse widget off: the spouse gets the travel button");
        f.owner.settings.spouseTeleport = true; f.Refresh(); Check(f.Buttons() == 2 && !f.Button(2), "Spouse widget on: her slot keeps only its own button");
    }
    // Party changes.
    {
        Fixture f; f.Refresh(); auto old = f.Button(1);
        auto charlie = Member(1005, "Charlie", 4, 5, 12); Get(f.slots[1].slot).fields["partyMemberWrapper"] = charlie; f.Refresh();
        Check(f.Button(1) && f.Button(1) != old && f.Tip(1).starts_with("Aller jusqu’à Charlie"), "Slot reassigned: a new button for the new member");
        Check(!HandlePartyTravelWidgetToggle(old), "Old member's button released");
        Get(f.slots[1].slot).numbers["m_isWaitingMember"] = 1; f.Refresh(); Check(!f.Button(1), "Waiting slot: no button");
        Get(f.slots[1].slot).numbers["m_isWaitingMember"] = 0; Get(charlie).numbers["ddku"] = 0; f.Refresh(); Check(!f.Button(1), "Guest: no button");
        Get(Get(f.display).fields["m_partyMemberSlots"]).items.clear(); f.Refresh(); Check(f.Buttons() == 0, "Party left: all buttons removed");
    }
    {
        Fixture f; f.Refresh(); Get(f.display).numbers["isArenaParty"] = 1; f.Refresh(); Check(f.Buttons() == 0, "Arena party: none");
        Get(f.display).numbers["isArenaParty"] = 0; f.Refresh(); Check(f.Buttons() == 2, "Back from the arena");
        f.owner.cached.reset(Make("OtherCharacter")); auto old = f.Button(1); Check(f.Click(1) && routes.empty() && Logged("reason=stale"), "Character change: click inert");
        f.Refresh(); Check(f.Buttons() == 2 && f.Button(1) != old, "Character change: buttons rebuilt");
        Get(f.slots[1].root).fields["panel"] = nullptr; f.Refresh(); Check(!f.Button(1) && f.Button(5), "Detached slot: its button goes");
    }
    // Hidden or foreign: inert.
    for (int reason = 0; reason < 5; ++reason) {
        Fixture f; f.Refresh();
        switch (reason) {
            case 0: Get(f.slots[1].root).numbers["display"] = 1; break;
            case 1: Get(f.slots[1].portrait).numbers["visibility"] = 1; break;
            case 2: Get(f.Button(1)).fields["parent"] = Make("ForeignPortrait"); break;
            case 3: f.owner.stopRequested = true; break;
            case 4: services["fqv"] = Make("ReplacedHub"); break;
        }
        f.Click(1); Check(routes.empty() && autopilots.empty(), "Hidden, foreign, stopped or stale clicks are inert");
    }
    { Fixture f; f.Refresh(); Check(!HandlePartyTravelWidgetToggle(Make("ForeignToggle")) && !HandlePartyTravelWidgetToggle(nullptr), "Foreign toggles stay the game's"); }
    // Cleanup failures keep the owned elements for a retry.
    {
        Fixture f; f.Refresh(); auto old = f.Button(1); tooltipDetachFails = true; RestorePartyTravelWidget();
        Check(PartyTravelWidgetCleanupPending(), "Failed tooltip detach retains the buttons");
        HandlePartyTravelWidgetToggle(old); Check(routes.empty(), "Cleanup-pending button disarmed");
        tooltipDetachFails = false; RestorePartyTravelWidget(); Check(!PartyTravelWidgetCleanupPending() && f.Buttons() == 0, "Retried cleanup");
    }
    {
        Fixture f; f.Refresh(); detachFails = true; f.owner.settings.partyTravelButton = false; f.Tick();
        Check(PartyTravelWidgetCleanupPending(), "Failed removal retained");
        f.owner.settings.partyTravelButton = true; f.Refresh();
        Check(f.Buttons() == 2 && PartyTravelWidgetCleanupPending(), "No second button on a slot while its old one is still being removed");
        f.owner.settings.partyTravelButton = false; f.Refresh();
        detachFails = false; f.Tick(); Check(!PartyTravelWidgetCleanupPending() && Get(f.slots[1].portrait).items.size() == 3, "Retried removal keeps native children");
    }
    {
        Fixture f; f.Refresh(); f.owner.stopped = true; f.Tick(); Check(f.Buttons() == 0 && !PartyTravelWidgetCleanupPending(), "Stop removes every button");
    }
    Check(handles.empty(), "All managed roots released");
    std::cout << "PASS: party travel button per member, positions, mod route and game autopilot, combat, companions, layout and cleanup\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
