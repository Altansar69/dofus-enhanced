#include "stats/stats-travel.hpp"
#include "stats/stats.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>

// « Statistiques » of the played character's travels, all from the game's own
// state, each counted once for that character and only while counting is on:
// - Module::Pump polls (every 200 ms) the played-character service (esg,
//   "fng") and the map service (epf, "fpm"): time played, maps, dimensions,
//   Havre-Sac entries, experience and levels;
// - the walk's next cell (animations/movement.cpp NextHook calls
//   StatsWalkStep), the teleport request (fbq.bcla) and the item use
//   (feh.bdsh); a request counts only once its arrival is seen;
// - the mod's own journeys (travel/chat-travel.cpp calls StatsModTrip).
// Every session (the mod's start, another character, a return after more than
// a minute away) starts from baselines: nothing gained before it is counted.
// What the game still shows when a character appears (its sheet, map and saved
// zaap) may be a previous character's: no baseline until the game replaces it.
namespace stats_travel {
namespace {
using namespace managed;
using stats::Counter;

// A session goes on while the character is back in the world within this time
// (a reconnection, a server transfer); a longer absence starts another one.
constexpr uint64_t SessionGapMs = 60000;
// One interval between two ticks in the world counts at most this much: the
// game may have been frozen, or Module::Pump may have skipped every stage
// while the account was unbound (a reconnection), which no tick can see.
constexpr uint64_t StepCapMs = 10000;
// A teleport request counts at its arrival within this time; a recall potion,
// at the next map change within a shorter one.
constexpr uint64_t TeleportMs = 30000, RecallMs = 15000;
constexpr int Anomaly = 4;              // ela 4 as the destination: an anomaly, its request naming its subarea, not a map
constexpr uint16_t GotoWaypoint = 600;  // ActionId.GotoWaypoint, the recall potions' teleport to the saved zaap
constexpr int MaxEffects = 128;
constexpr size_t MaxCached = 4096, MaxName = 128, MaxNames = 64;

struct Contract {
    // The character and its map: every statistic of this part needs them.
    Klass character{}, info{}, sheet{}, point{}, scene{}, sceneData{};
    Method sceneReady{};
    size_t infoField = 0, sheetField = 0, mapField = 0, idField = 0, nameField = 0, levelField = 0;
    size_t experienceField = 0, pointMap = 0, sceneDataField = 0, sceneId = 0;
    bool core = false;
    // Worlds and Havre-Sac maps (static game data).
    Klass mapInfo{}, subArea{}, dataCenter{}, havenbags{};
    Method mapInfoById{}, mapSubArea{}, subAreaWorld{}, havenbagRoot{}, inHavenbag{};
    size_t subAreaId = 0;
    bool places = false;
    // The walk's cells.
    Klass walkState{}, route{}, cellPoint{}, animated{};
    size_t walkEntity = 0, walkProgress = 0, walkNext = 0, walkRoute = 0, routeStart = 0, pointCell = 0, entityId = 0;
    bool walk = false;
    // Teleport requests, and what fbq.bcla checks before sending one.
    Klass teleporter{}, request{};
    size_t requestMap = 0, requestSource = 0, requestDestination = 0, requestCost = 0, teleporterCharacter = 0, kamasField = 0;
    bool teleports = false;
    // Recall potions and the saved zaap they lead to.
    Klass zaaps{}, inventory{}, item{}, itemData{}, effect{};
    Method itemId{}, itemDataOf{};
    size_t savedZaap = 0, itemEffects = 0, effectAction = 0;
    bool recall = false;
} contract;

// The time played runs on the unbiased interrupt time, which stops while the
// computer sleeps or hibernates (GetTickCount64 does not).
uint64_t UnbiasedMs() { ULONGLONG ticks = 0; QueryUnbiasedInterruptTime(&ticks); return ticks / 10000; }
uint64_t (*clockMs)() = UnbiasedMs;

// What one tick reads: the character service and its character (z), sheet
// (gb, the characteristics with the experience) and map (tk), the map service.
struct Look { Obj character = nullptr, info = nullptr, sheet = nullptr, map = nullptr, scene = nullptr; int64_t id = 0; bool present = false; };
// What the last tick that saw a character saw, in or out of the world. When
// another character appears, the sheet and map still shown are the previous
// one's until the game replaces them, unless it did since that tick (then they
// are the new one's): the map keeps it out of the world. The first character
// this tracker sees (the mod's start, counting turned on) may still show one
// played before: its sheet is no baseline and its map (place) no first map
// until the game replaces them. The saved zaap shown then (zaap) is unknown.
struct Seen { int64_t id = 0; Ref sheet, map; } seen;
struct Stale { Ref sheet, map, place; int64_t zaap = 0; } stale;
// The character continuously in the world; previous: the last tick had it there.
struct Session {
    int64_t id = 0;
    bool active = false, previous = false;
    uint64_t last = 0;     // the last tick with the character in the world
    int64_t played = 0;    // the time counted in this session
    Ref sheet;             // the sheet at that tick
} session;
// Experience and level baselines, taken from a sheet that is not stale.
struct Progress { bool known = false; int64_t experience = 0; int level = 0; } progress;
// The session's last accepted map, whether it is a Havre-Sac map, and the last world known.
enum class Bag { Unknown, No, Yes };
struct Places { int64_t map = 0; Bag bag = Bag::Unknown; int world = 0; } places;
// A teleport request or a recall potion waiting for its arrival.
struct Pending { bool waiting = false; int64_t map = 0; int source = -1, destination = -1; uint64_t at = 0; } teleport, recall;
Ref usedItem;
uint64_t usedAt = 0;
// The walking character (0: no steps count), the last cell its walk reached,
// and the steps not yet given to the store.
int64_t walker = 0;
int lastCell = -1;
int64_t roleplayCells = 0, fightCells = 0;
// Static game data already asked, and the names already given to the store.
std::unordered_map<int64_t, int> worlds;
std::unordered_map<int64_t, Bag> bags;
std::unordered_map<int, bool> recallItems;
std::unordered_map<int64_t, std::string> names;
std::map<std::string, uint64_t> nextLog;

using TeleportFn = bool(*)(Obj, Obj, Method);
using UseFn = void(*)(Obj, Obj, Obj, Method);
TeleportFn originalTeleport = nullptr;
UseFn originalUse = nullptr;

bool Active() {
    return module && module->ready.load() && module->servicesReady && !module->stopped.load() && !module->stopRequested.load();
}
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Statistics field mismatch: ") + name);
    return offset;
}
// Fixed codes only (never a game string), one line per stage every ten seconds at most.
void Error(const char* stage) noexcept {
    try {
        const auto now = GetTickCount64();
        auto& next = nextLog[stage];
        if (now < next) return;
        next = now + 10000;
        Log(std::string("STATS_TRAVEL_ERROR stage=") + stage);
    } catch (...) {}
}
bool Printable(const std::string& text) {
    if (text.empty() || text.size() > MaxName) return false;
    for (const char c : text) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    return true;
}
template<class Key, class Value> void Remember(std::unordered_map<Key, Value>& cache, Key key, Value value) {
    if (cache.size() >= MaxCached) cache.clear();
    cache[key] = value;
}

// ===================================================================== reads
Look Observe() {
    Look now;
    now.character = module->FindService("fng");
    if (!Is(now.character, contract.character)) return now;  // loading, character selection
    now.info = Read<Obj>(now.character, contract.infoField);
    now.sheet = Read<Obj>(now.character, contract.sheetField);
    now.map = Read<Obj>(now.character, contract.mapField);
    if (!Is(now.info, contract.info)) { now.info = nullptr; return now; }
    if (now.sheet && !Is(now.sheet, contract.sheet)) now.sheet = nullptr;
    if (now.map && !api.class_is_assignable_from(contract.point, api.object_get_class(now.map))) now.map = nullptr;
    now.id = Read<int64_t>(now.info, contract.idField);
    // In the world: a character, and a map the map service holds (its
    // disabling clears it; esg.Reset keeps the character and its last map).
    now.scene = module->FindService("fpm");
    if (!Is(now.scene, contract.scene)) now.scene = nullptr;
    now.present = now.id > 0 && now.scene && Read<Obj>(now.scene, contract.sceneDataField);
    return now;
}
// The map the character service shows, once the map service has finished
// loading that very map: before the first map it holds a placeholder, and
// while a map loads it still shows the previous one. None while it still
// shows the map of this tracker's first look (stale.place).
int64_t AcceptedMap(const Look& look) {
    if (!look.map || !look.scene || stale.place.get()) return 0;
    const auto map = Read<int64_t>(look.map, contract.pointMap);
    if (map <= 0 || map == places.map) return map > 0 ? map : 0;
    if (!api.Value<bool>(api.Invoke(contract.sceneReady, look.scene))) return 0;
    const auto data = Read<Obj>(look.scene, contract.sceneDataField);
    return Is(data, contract.sceneData) && Read<int32_t>(data, contract.sceneId) == map ? map : 0;
}
// The game's own test (euz.baek uses it for each map's complementary information).
Bag Havenbag(int64_t map) {
    if (!contract.places) return Bag::Unknown;
    if (const auto found = bags.find(map); found != bags.end()) return found->second;
    try {
        Ref root(api.Invoke(contract.havenbagRoot, nullptr));
        if (!Is(root.get(), contract.havenbags)) return Bag::Unknown;
        int64_t key = map;
        const auto bag = api.Value<bool>(api.Invoke(contract.inHavenbag, root.get(), {&key})) ? Bag::Yes : Bag::No;
        Remember(bags, map, bag);
        return bag;
    } catch (...) { Error("havenbag"); return Bag::Unknown; }
}
// The world of the map's subarea (MapInformationData -> SubAreaData), as the
// notebook and the chat travel read it; 0 when it has none or is unknown.
int World(int64_t map) {
    if (!contract.places) return 0;
    if (const auto found = worlds.find(map); found != worlds.end()) return found->second;
    try {
        int64_t key = map;
        Ref info(api.Invoke(contract.mapInfoById, nullptr, {&key}));
        Ref area(info.get() ? api.Invoke(contract.mapSubArea, info.get()) : nullptr);
        if (!area.get()) return 0;
        // get_worldmapId throws for a subarea without a world map.
        const int world = api.Value<int>(api.Invoke(contract.subAreaWorld, area.get()));
        if (world > 0) Remember(worlds, map, world);
        return world > 0 ? world : 0;
    } catch (...) { return 0; }
}
// The map's subarea (SubAreaData.id); 0 when unknown. Only for an anomaly's arrival.
int Subarea(int64_t map) {
    if (!contract.places) return 0;
    try {
        int64_t key = map;
        Ref info(api.Invoke(contract.mapInfoById, nullptr, {&key}));
        Ref area(info.get() ? api.Invoke(contract.mapSubArea, info.get()) : nullptr);
        return Is(area.get(), contract.subArea) ? Read<int32_t>(area.get(), contract.subAreaId) : 0;
    } catch (...) { return 0; }
}
// The saved zaap (fbq.<dspb>, the respawn map the game was told); 0 unknown.
int64_t SavedZaap() {
    try {
        const auto zaaps = module->FindService("fpf");
        return Is(zaaps, contract.zaaps) ? Read<int64_t>(zaaps, contract.savedZaap) : 0;
    } catch (...) { return 0; }
}
bool RecallItem(Obj item) {
    Ref data(api.Invoke(contract.itemDataOf, item));
    if (!Is(data.get(), contract.itemData)) return false;
    Ref effects(Read<Obj>(data.get(), contract.itemEffects));
    if (!effects.get()) return false;
    const int count = api.Value<int>(api.Call(effects.get(), "get_Count"));
    if (count < 0 || count > MaxEffects) throw std::runtime_error("Statistics item effects outside bounds");
    for (int i = 0; i < count; ++i) {
        Ref effect(api.Call(effects.get(), "get_Item", {&i}));
        if (effect.get() && api.class_is_assignable_from(contract.effect, api.object_get_class(effect.get())) &&
            Read<uint16_t>(effect.get(), contract.effectAction) == GotoWaypoint) return true;
    }
    return false;
}

// ===================================================================== counting
void Count(Counter counter, int64_t amount = 1) { stats::Add(session.id, counter, amount); }
Counter Teleporter(int source) {
    // The teleporter taken (dhc.dipj, the list's own ela): 0 zaap, 1 zaapi,
    // 2 prism, 3 the Havre-Sac's zaap, 4 anomaly, 5 a zaap chosen on the world
    // map (the mod's direct trips use it too). Whatever the destination (dipk),
    // an anomaly included: a trip from a zaap is a zaap's.
    switch (source) {
    case 0: case 3: case 5: return Counter::TeleportZaap;
    case 1: return Counter::TeleportZaapi;
    case 2: return Counter::TeleportPrism;
    default: return Counter::TeleportOther;
    }
}
void FlushCells() {
    if (walker && roleplayCells) stats::Add(walker, Counter::CellsRoleplay, roleplayCells);
    if (walker && fightCells) stats::Add(walker, Counter::CellsFight, fightCells);
    roleplayCells = fightCells = 0;
}
void Forget() {
    seen = Seen{}; stale = Stale{}; session = Session{}; progress = Progress{}; places = Places{};
    teleport = recall = Pending{}; usedItem.reset(); usedAt = 0;
    walker = 0; lastCell = -1; roleplayCells = fightCells = 0;
}
// Another character, or the same one back after more than SessionGapMs away.
void Begin(const Look& look) {
    const bool back = session.active && session.id == look.id;
    // Back after a long absence: the sheet it left is not its experience any
    // more until the game sends it again (it may have played elsewhere).
    if (back && !stale.sheet.get() && look.sheet && look.sheet == session.sheet.get()) stale.sheet.reset(look.sheet);
    session = Session{};
    session.id = look.id; session.active = true;
    progress = Progress{}; places = Places{}; teleport = recall = Pending{};
    Log(std::string("STATS_TRAVEL_SESSION reason=") + (back ? "return" : "character"));
    try {
        const auto name = api.String(Read<Obj>(look.info, contract.nameField));
        if (Printable(name) && names[look.id] != name) {
            if (names.size() > MaxNames) names.clear();
            names[look.id] = name;
            stats::Name(look.id, name);
        }
    } catch (...) { Error("name"); }
}
void Play(uint64_t step) {
    if (!step) return;
    session.played += static_cast<int64_t>(step);
    Count(Counter::PlayMs, static_cast<int64_t>(step));
    stats::Raise(session.id, stats::Record::LongestSessionMs, session.played);
}
// Experience (gb.dcjh) and level (z.dbrn): increases only; a decrease is a new baseline.
void Progression(const Look& look) {
    if (!look.sheet || stale.sheet.get()) return;  // no sheet yet, or still the previous one's
    const auto experience = Read<int64_t>(look.sheet, contract.experienceField);
    const auto level = Read<int>(look.info, contract.levelField);
    if (!progress.known) { progress = {true, experience, level}; return; }
    if (experience > progress.experience) Count(Counter::Xp, experience - progress.experience);
    if (level > progress.level) Count(Counter::Levels, level - progress.level);
    progress.experience = experience; progress.level = level;
}
// A used item recorded by the hook: a recall potion waits for its map change.
void TakeItem() {
    if (!usedItem.get()) return;
    Ref item(std::move(usedItem));
    try {
        const int gid = api.Value<int>(api.Invoke(contract.itemId, item.get()));
        bool potion = false;
        if (const auto found = recallItems.find(gid); found != recallItems.end()) potion = found->second;
        else { potion = RecallItem(item.get()); Remember(recallItems, gid, potion); }
        if (potion) recall = {true, 0, -1, -1, usedAt};
    } catch (...) { Error("item"); }
}
// A request's arrival: its map; for an anomaly, a map of the subarea its
// request names (BaseZaapSelectionUI.ValidateZaapChoice), any when unknown.
bool Reached(int64_t map) {
    if (teleport.destination != Anomaly) return map == teleport.map;
    const int area = Subarea(map);
    return !area || area == teleport.map;
}
void Arrive(int64_t map) {
    if (teleport.waiting && Reached(map)) {
        Count(Teleporter(teleport.source));
        teleport = recall = Pending{};
        return;
    }
    if (recall.waiting) {
        // The saved zaap still shown since the character appeared is unknown.
        const auto saved = SavedZaap();
        if (saved <= 0 || saved == stale.zaap || saved == map) { Count(Counter::RecallPotions); teleport = recall = Pending{}; }
    }
}
// Maps, Havre-Sac entries, dimensions and the arrivals of requests. The
// session's first map is its baseline: a start, a return or another character
// never counts as a move; a reload of the same map is no change.
void Travel(const Look& look, uint64_t now) {
    if (teleport.waiting && now - teleport.at > TeleportMs) teleport = Pending{};
    if (recall.waiting && now - recall.at > RecallMs) recall = Pending{};
    const auto map = AcceptedMap(look);
    if (!map || map == places.map) return;
    const auto bag = Havenbag(map);
    if (places.map) {
        Count(Counter::Maps);
        if (bag == Bag::Yes && places.bag == Bag::No) Count(Counter::HavenBag);
        Arrive(map);
    }
    // The Havre-Sac's maps lie in a world of their own data: never a dimension.
    if (bag == Bag::No) {
        if (const int world = World(map)) {
            if (places.world && world != places.world) {
                Count(Counter::Dimensions);
                stats::AddTo(session.id, stats::Table::Dimensions, world);
            }
            places.world = world;
        }
    }
    places.map = map; places.bag = bag;
}
void Follow(const Look& look) {
    if (look.id <= 0) return;  // no character seen: nothing learnt, nothing forgotten
    if (look.id != seen.id) {
        const bool other = seen.id != 0;
        stale.sheet.reset(look.sheet && (!other || look.sheet == seen.sheet.get()) ? look.sheet : nullptr);
        stale.map.reset(other && look.map && look.map == seen.map.get() ? look.map : nullptr);
        stale.place.reset(other ? nullptr : look.map);
        stale.zaap = contract.recall ? SavedZaap() : 0;
        seen.id = look.id;
        // For the in-game check: what the game still showed (of the previous
        // character), and whether its map service was still loaded then.
        const std::string loaded = std::string(" loaded=") + (look.present ? "1" : "0");
        if (other) Log(std::string("STATS_TRAVEL_CHARACTER sheet=") + (stale.sheet.get() ? "previous" : "new") +
            " map=" + (stale.map.get() ? "previous" : "new") + loaded);
        else Log(std::string("STATS_TRAVEL_FIRST sheet=") + (look.sheet ? "1" : "0") + " map=" + (look.map ? "1" : "0") + loaded);
    }
    if (stale.sheet.get() && look.sheet != stale.sheet.get()) stale.sheet.reset();
    if (stale.map.get() && look.map != stale.map.get()) stale.map.reset();
    if (stale.place.get() && look.map != stale.place.get()) stale.place.reset();
    if (look.sheet != seen.sheet.get()) seen.sheet.reset(look.sheet);
    if (look.map != seen.map.get()) seen.map.reset(look.map);
}
void Tick() noexcept {
    if (!contract.core) return;
    try {
        if (!Active() || !stats::Counting()) { Forget(); return; }  // counting off: nothing read, nothing kept
        const auto now = clockMs();
        FlushCells();  // the last interval's steps, to the character who walked them
        const auto look = Observe();
        Follow(look);
        // In the world, with its own map: after a character change, the map
        // still shown is the previous character's until the game replaces it.
        const bool inWorld = look.present && !stale.map.get();
        if (inWorld) {
            const bool resume = session.active && session.id == look.id && now >= session.last && now - session.last <= SessionGapMs;
            if (!resume) Begin(look);
            else if (session.previous) Play(std::min(now - session.last, StepCapMs));  // both ticks in the world: the time between them
            session.last = now; session.previous = true;
            if (look.sheet != session.sheet.get()) session.sheet.reset(look.sheet);
            Progression(look);
            TakeItem();
            Travel(look, now);
        } else {
            session.previous = false;
            TakeItem();
        }
        const auto character = inWorld ? look.id : 0;
        if (character != walker) { walker = character; lastCell = -1; }
    } catch (...) {
        Error("tick");
        // Unknown state: no time across this tick and no steps until the next one.
        session.previous = false; walker = 0; lastCell = -1;
    }
}

// ===================================================================== hooks
// xq.rjr's state at entry: the target cell (dhjz) is reached when the progress
// (dhjy) is 1 (xq.Update and the mod's instant drain call it then). A new walk
// (xq.rjq: target -1) first turns towards its route's start cell, which its
// first segment targets again: that cell is the baseline, never a step.
void WalkStep(void* state) noexcept {
    try {
        if (!walker || !state || api.object_get_class(state) != contract.walkState) return;
        const auto entity = Read<Obj>(state, contract.walkEntity);
        if (!entity || Read<int64_t>(entity, contract.entityId) != walker) return;  // pets, monsters, other players
        const int next = Read<int>(state, contract.walkNext);
        if (next < 0) {
            const auto route = Read<Obj>(state, contract.walkRoute);
            const auto start = route ? Read<Obj>(route, contract.routeStart) : nullptr;
            lastCell = start ? Read<int>(start, contract.pointCell) : -1;
            return;
        }
        if (next == lastCell || !(Read<float>(state, contract.walkProgress) >= 1.0f)) return;
        lastCell = next;
        ++(module && module->InCombat() ? fightCells : roleplayCells);
    } catch (...) {}
}
void ModTrip() noexcept {
    try { if (session.active && Active()) Count(Counter::ModTrips); } catch (...) {}
}
// fbq.bcla, the TeleportRequest action of every zaap, zaapi, prism and
// Havre-Sac zaap list, the world map's zaaps and the mod's own requests. Read
// before it runs, as it reads them: a request it refuses itself (its cost
// above the kamas of the sheet its character service dspa gives, esg+0x130:
// « not_enough_rich », nothing sent, still true) is none and leaves an earlier
// one waiting.
Pending Requested(Obj service, Obj request) noexcept {
    try {
        if (!contract.teleports || !session.active || !Active() || !Is(request, contract.request)) return {};
        const auto character = Is(service, contract.teleporter) ? Read<Obj>(service, contract.teleporterCharacter) : nullptr;
        const auto sheet = Is(character, contract.character) ? Read<Obj>(character, contract.sheetField) : nullptr;
        if (Is(sheet, contract.sheet) &&
            static_cast<int64_t>(Read<uint32_t>(request, contract.requestCost)) > Read<int64_t>(sheet, contract.kamasField)) return {};
        return {true, Read<int64_t>(request, contract.requestMap), Read<int>(request, contract.requestSource),
                Read<int>(request, contract.requestDestination), clockMs()};
    } catch (...) { return {}; }
}
bool TeleportHook(Obj service, Obj request, Method method) {
    const auto pending = Requested(service, request);
    const bool result = originalTeleport(service, request, method);
    if (pending.waiting) teleport = pending;
    return result;
}
// feh.bdsh sends every confirmed item use. Only the item is kept here; the
// next tick tells whether it is a recall potion.
void RememberUse(Obj item) noexcept {
    try {
        if (!contract.recall || !session.active || !Active() || !Is(item, contract.item)) return;
        usedItem.reset(item); usedAt = clockMs();
    } catch (...) {}
}
void UseHook(Obj inventory, Obj action, Obj item, Method method) {
    RememberUse(item);
    originalUse(inventory, action, item, method);
}

void Install() {
    constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
    constexpr const char* WorldNs = "Core.DataCenter.Metadata.World";
    contract = Contract{}; walkStep = nullptr; modTrip = nullptr;
    Contract next;
    const char* stage = "character";
    try {
        // Pinned 3.7.12.12 layouts (work/r74-analysis/contracts-new.json), each checked by name.
        next.character = api.Class("Core.dll", "", "esg");
        next.infoField = RequireField(next.character, "dpzj", 0x128);
        next.sheetField = RequireField(next.character, "<dpzm>k__BackingField", 0x130);
        next.mapField = RequireField(next.character, "dqab", 0x1A0);
        next.info = api.Class("Core.dll", "", "z");
        next.idField = RequireField(next.info, "dbrl", 0x10);
        next.nameField = RequireField(next.info, "dbrm", 0x18);
        next.levelField = RequireField(next.info, "dbrn", 0x20);
        next.sheet = api.Class("Core.dll", "", "gb");
        next.experienceField = RequireField(next.sheet, "dcjh", 0x10);
        next.point = api.Class(DataCenter, WorldNs, "WorldPoint");
        next.pointMap = RequireField(next.point, "m_mapId", 0x10);
        stage = "map";
        next.scene = api.Class("Core.dll", "", "epf");
        next.sceneDataField = RequireField(next.scene, "<doqb>k__BackingField", 0x118);
        next.sceneReady = api.FindVerified(next.scene, "xkj", 0, 0x18389C0);
        next.sceneData = api.Class("AleCore.dll", "AleCore.Data.UnityClient", "ClientMapData");
        next.sceneId = RequireField(next.sceneData, "id", 0x18);
        next.core = true;
    } catch (...) {
        try { Log(std::string("STATS_TRAVEL_DISABLED stage=") + stage); } catch (...) {}
        return;
    }
    // The optional parts: each one's failure turns off only its statistics.
    const auto part = [](const char* name, const auto& install) {
        try { install(); return true; }
        catch (...) { try { Log(std::string("STATS_TRAVEL_DISABLED stage=") + name); } catch (...) {} return false; }
    };
    next.places = part("places", [&] {
        next.mapInfo = api.Class(DataCenter, WorldNs, "MapInformationData");
        next.mapInfoById = api.FindVerified(next.mapInfo, "GetMapInformationById", 1, 0x7C4200);
        next.mapSubArea = api.FindVerified(next.mapInfo, "get_subArea", 0, 0x7C44B0);
        next.subArea = api.Class(DataCenter, WorldNs, "SubAreaData");
        next.subAreaWorld = api.FindVerified(next.subArea, "get_worldmapId", 0, 0x7D1040);
        next.subAreaId = RequireField(next.subArea, "id", 0x10);
        next.dataCenter = api.Class(DataCenter, "Core.DataCenter", "DataCenterModule");
        next.havenbagRoot = api.FindVerified(next.dataCenter, "get_havenBagThemesDataRoot", 0, 0x7BDF80);
        next.havenbags = api.Class(DataCenter, "Core.DataCenter.Metadata.House", "HavenbagThemesDataRoot");
        next.inHavenbag = api.FindVerified(next.havenbags, "IsMapIdInHavenbag", 1, 0x7E41B0);
    });
    next.walk = part("walk", [&] {
        next.walkState = api.NestedClass(api.Class("Core.dll", "", "xq"), "xm");
        next.walkEntity = RequireField(next.walkState, "dhjx", 0x20);
        next.walkProgress = RequireField(next.walkState, "dhjy", 0x28);
        next.walkNext = RequireField(next.walkState, "dhjz", 0x2C);
        next.walkRoute = RequireField(next.walkState, "dhkb", 0x38);
        next.route = api.Class("Core.dll", "", "enw");
        next.routeStart = RequireField(next.route, "<dodg>k__BackingField", 0x10);
        next.cellPoint = api.Class("Core.dll", "Core.PathFinding.WorldData", "MapPoint");
        next.pointCell = RequireField(next.cellPoint, "docz", 0x18);
        next.animated = api.Class("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated");
        next.entityId = RequireField(next.animated, "fhtc", 0x1C8);
    });
    next.teleports = part("teleports", [&] {
        next.request = api.Class("Core.dll", "", "dhc");
        next.requestMap = RequireField(next.request, "dipi", 0x10);
        next.requestSource = RequireField(next.request, "dipj", 0x18);
        next.requestDestination = RequireField(next.request, "dipk", 0x1C);
        next.requestCost = RequireField(next.request, "dipl", 0x20);
        next.kamasField = RequireField(next.sheet, "dcjl", 0x30);
        next.teleporter = api.Class("Core.dll", "", "fbq");
        next.teleporterCharacter = RequireField(next.teleporter, "dspa", 0x28);
        AddHook(api.FindVerified(next.teleporter, "bcla", 1, 0x1A4A5E0), 0x1A4A5E0,
            reinterpret_cast<void*>(&TeleportHook), reinterpret_cast<void**>(&originalTeleport));
    });
    // A recall potion counts at its arrival on the saved zaap.
    next.recall = part("recall", [&] {
        next.zaaps = api.Class("Core.dll", "", "fbq");
        next.savedZaap = RequireField(next.zaaps, "<dspb>k__BackingField", 0x30);
        next.item = api.Class("Core.dll", "", "mc");
        next.itemId = api.FindVerified(next.item, "myn", 0, 0xF20D40);
        next.itemDataOf = api.FindVerified(next.item, "myx", 0, 0x2AAC6B0);
        next.itemData = api.Class(DataCenter, "Core.DataCenter.Metadata.Item", "ItemData");
        next.itemEffects = RequireField(next.itemData, "possibleEffects", 0x68);
        next.effect = api.Class(DataCenter, "Core.DataCenter.Metadata.Effect", "EffectInstanceData");
        next.effectAction = RequireField(next.effect, "actionId", 0x1A);
        next.inventory = api.Class("Core.dll", "", "feh");
        AddHook(api.FindVerified(next.inventory, "bdsh", 2, 0x1B57D20), 0x1B57D20,
            reinterpret_cast<void*>(&UseHook), reinterpret_cast<void**>(&originalUse));
    });
    contract = next;
    walkStep = contract.walk ? &WalkStep : nullptr;
    modTrip = &ModTrip;
}
}
}

void InstallStatsTravel() {
    try { stats_travel::Install(); } catch (...) {}
}
void TickStatsTravel() { stats_travel::Tick(); }
void RestoreStatsTravel() {
    using namespace stats_travel;
    try { FlushCells(); } catch (...) {}  // before FlushStats writes the file
    Forget();
    worlds.clear(); bags.clear(); recallItems.clear(); names.clear(); nextLog.clear();
}
