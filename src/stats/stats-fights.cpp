#include "stats/stats-fights.hpp"
#include "stats/stats.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// « Statistiques » of fights, from two sources, the local character's only:
// - the game's own fight-end report, polled on the game thread as
//   capture-detect does (no hook): fxm.bhys builds a NEW report (fyz) for every
//   FightEndEvent and fzf.bioj, its only writer, stores it in fzf.dwqq. Right
//   after, fzf.wd attaches the server's statistics of that fight to it (dt, the
//   result window's « Statistiques » tab). Finished fights by kind, victories,
//   time, turns, monsters defeated, damage dealt and taken, heals, souls
//   obtained and dungeons completed;
// - one hook on gau.bixl, the game's listener of the fight actions the server
//   sends (one call per action, read before the game plays it): spells cast,
//   deaths, damage dealt by element and the biggest hit.
// A report counts only when its fight started after this client saw the
// character outside a fight while counting: never a fight that was already
// running when the mod started or counting was switched on. The report's
// experience is never read (the character's experience is counted elsewhere).
namespace stats_fights {
namespace {
using namespace managed;
using stats::Counter;
using stats::Table;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
constexpr const char* Protocol = "Ankama.Dofus.Protocol.Game.dll";
constexpr uint32_t BossSoul = 327, ArchSoul = 328; // item types "Âme de gardien de donjon", "Âme d'archimonstre"
constexpr int16_t Victory = 3;                     // fze.dwpy (emw) of a winner, as UIFightResult and fxm.bhys read it
constexpr uint8_t Spectator = 4;                   // fyy flag of a report built for a spectator (fzi.bipl)
constexpr int32_t MonsterFighter = 2;              // fze.dwqd (fzc): 1 character, 2 monster or companion, 3 tax collector
constexpr int32_t CastAction = 2, DeathAction = 3, LossAction = 37; // jsq oneof cases, as gau.bixl dispatches them
constexpr int32_t SpellCast = 2;                   // jrw oneof case of a spell (9: a weapon, never counted)
constexpr int MaxFighters = 128, MaxLoot = 512, MaxBosses = 64, MaxSoulQuantity = 100;
constexpr int64_t MaxFightMs = 86400000, MaxTurns = 100000, MaxHit = 100000000;
constexpr size_t MaxSeen = 8, MaxCharacters = 64;
constexpr uint64_t PendingMs = 120000;

// Pinned 3.7.12.12 offsets, each tied to its field name at installation.
struct Offsets {
    size_t info = 0, characterId = 0, position = 0, mapId = 0;           // esg.dpzj, z.dbrl, esg.dqab, WorldPoint.m_mapId
    size_t fight = 0, fighting = 0, history = 0, report = 0;             // exf.drih, fxc.dwan, fxc.<dwaq>, fzf.dwqq
    size_t flags = 0, type = 0, duration = 0, turns = 0, fighters = 0, statistics = 0; // fyz
    size_t id = 0, outcome = 0, loot = 0, kind = 0, items = 0;           // fze, fzg.dwqv
    size_t dealt = 0, taken = 0, healed = 0;                             // dt
    size_t dungeonId = 0, bosses = 0;                                     // DungeonData
    size_t source = 0, body = 0, which = 0;                              // jsq
    size_t castKind = 0, castSpell = 0, spellId = 0, dead = 0;           // jrw, jrq, jrz
    size_t present = 0, shield = 0, element = 0, target = 0, life = 0;   // jsd
};
struct Contract {
    Klass character{}, info{};
    Klass service{}, fight{}, history{}, report{}, fighter{}, loot{}, statistics{}, item{};
    Method itemType{}, quantity{};
    Klass point{}, dataCenter{}, dungeonRoot{}, dungeon{};
    Method dungeons{}, dungeonByMap{};
    Klass handler{}, action{}, cast{}, spell{}, death{}, loss{};
    Offsets at;
    bool base = false, reports = false, dungeonsReady = false, actions = false;
} contract;
struct State {
    Module* owner = nullptr;
    Ref compositor, history;
    // Reports already taken as the baseline or processed, newest last. Holding
    // them also keeps a later report from reusing one of their addresses.
    std::vector<Ref> seen;
    int64_t localId = 0;
    bool known = false, fighting = false;
    int64_t fightMap = 0;                // where the current fight started: a dungeon's room
    std::vector<int64_t> outside;        // characters seen outside a fight since counting started
    // The last counted report, until the server's statistics reach it.
    struct Pending { Ref report; int64_t character = 0; uint64_t since = 0; } pending;
    unsigned casts = 0, hits = 0, deaths = 0; // counted by the hook since the last report, for its log line
    std::map<std::string, uint64_t> nextLog;
} state;
// The hook's view, written by the pump: the character it counts for (0: none)
// and the game thread it runs on.
std::atomic<int64_t> acting{0};
std::atomic<DWORD> pumpThread{0};
std::atomic_bool threadLogged{false};
using ActionFn = void(*)(Obj, Obj, Method);
ActionFn originalAction = nullptr;

bool Active() {
    return module && module->ready.load() && module->servicesReady && !module->stopped.load() && !module->stopRequested.load();
}
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error("Statistics field layout");
    return offset;
}
int Count(Obj list, int maximum) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > maximum) throw std::runtime_error("Statistics collection outside bounds");
    return count;
}
// Fixed codes and numbers only: never an exception text (game strings), a name or a key.
void Error(const char* stage) {
    try {
        const auto now = GetTickCount64();
        auto& next = state.nextLog[stage];
        if (now < next) return;
        next = now + 10000;
        Log(std::string("STATS_FIGHTS_ERROR stage=") + stage);
    } catch (...) {}
}
void ResetActions() { state.casts = state.hits = state.deaths = 0; }
// Counting off: nothing kept; the next complete read is a baseline and a fight
// running then never counts.
void Forget() {
    acting.store(0);
    state.history.reset(); state.seen.clear(); state.pending = {};
    state.localId = 0; state.known = state.fighting = false; state.fightMap = 0;
    state.outside.clear(); ResetActions();
}
// Releases every reference: also safe from Module::Stop.
void Reset() {
    Forget();
    state.owner = nullptr; state.compositor.reset(); state.nextLog.clear();
    threadLogged.store(false);
}
bool Seen(Obj report) {
    return std::any_of(state.seen.begin(), state.seen.end(), [&](const Ref& known) { return known.get() == report; });
}
void Remember(Obj report) {
    if (!report || Seen(report)) return;
    if (state.seen.size() >= MaxSeen) state.seen.erase(state.seen.begin());
    state.seen.emplace_back(report);
}
bool Outside(int64_t character) {
    return std::find(state.outside.begin(), state.outside.end(), character) != state.outside.end();
}
void Roamed(int64_t character) {
    if (Outside(character)) return;
    if (state.outside.size() >= MaxCharacters) state.outside.erase(state.outside.begin());
    state.outside.push_back(character);
}
// The character's current map (esg.dqab, the position esg.zeo returns). 0: unknown.
int64_t Map(Obj character) {
    const Obj position = Read<Obj>(character, contract.at.position);
    if (!position || !api.class_is_assignable_from(contract.point, api.object_get_class(position))) return 0;
    return Read<int64_t>(position, contract.at.mapId);
}

enum class Kind { Pve, Pvp, Kolizeum };
// The report's fight type (fvy), as the game's spectator list names it
// (SpectatorUi.GetArrowTooltipText): 0 challenge (« défi »), 1 aggression, 5 tax
// collector, 7 Kolizéum, 8 conquest (AvA), 9 prism. Every other type fights
// monsters: 4, and 2, 3, 6 and 10 (PvMA, MXvM, PvMU and Breach in Dofus'
// labels), which that list leaves unnamed. A challenge is a fight between
// players: it counts as JcJ, with its time, damage and spells.
Kind KindOf(int32_t type) {
    if (type == 7) return Kind::Kolizeum;
    if (type == 0 || type == 1 || type == 5 || type == 8 || type == 9) return Kind::Pvp;
    return Kind::Pve;
}
const char* KindName(Kind kind) { return kind == Kind::Kolizeum ? "kolizeum" : kind == Kind::Pvp ? "pvp" : "pve"; }

// The server's statistics of the last counted fight: added once, when they
// reach its report; dropped when another report replaces it or after two
// minutes without them (some fight kinds may have none).
void Settle(Obj current) {
    const Obj report = state.pending.report.get();
    if (!report) return;
    try {
        const Obj figures = Read<Obj>(report, contract.at.statistics);
        if (!figures) {
            if (current == report && GetTickCount64() - state.pending.since < PendingMs) return;
            state.pending = {};
            Log("STATS_FIGHT_STATISTICS missing");
            return;
        }
        const int64_t character = state.pending.character;
        state.pending = {};
        if (!Is(figures, contract.statistics)) throw std::runtime_error("Unexpected fight statistics");
        // FightResultCharacterTab.UpdateFightStatistics: dmgDone, dmgTaken, healDone.
        const int32_t dealt = Read<int32_t>(figures, contract.at.dealt), taken = Read<int32_t>(figures, contract.at.taken);
        const int32_t healed = Read<int32_t>(figures, contract.at.healed);
        if (dealt > 0) stats::Add(character, Counter::DamageDealt, dealt);
        if (taken > 0) stats::Add(character, Counter::DamageTaken, taken);
        if (healed > 0) stats::Add(character, Counter::Heals, healed);
        Log("STATS_FIGHT_STATISTICS dealt=" + std::to_string(std::max(dealt, 0)) + " taken=" + std::to_string(std::max(taken, 0)) +
            " heals=" + std::to_string(std::max(healed, 0)));
    } catch (...) {
        state.pending = {};
        Error("statistics");
    }
}
// Souls the character obtained: its own loot's boss (327) and archimonster
// (328) soul items, by quantity, as capture-detect reads them for Metamob.
int64_t Souls(Obj mine) {
    const Obj gains = Read<Obj>(mine, contract.at.loot);
    if (!gains) return 0;
    if (!Is(gains, contract.loot)) throw std::runtime_error("Unexpected fighter loot");
    const Obj items = Read<Obj>(gains, contract.at.items);
    const int count = items ? Count(items, MaxLoot) : 0;
    int64_t souls = 0;
    for (int i = 0; i < count; ++i) {
        Ref item(api.Call(items, "get_Item", {&i}));
        if (!item.get() || !api.class_is_assignable_from(contract.item, api.object_get_class(item.get()))) continue;
        const auto type = api.Value<uint32_t>(api.Invoke(contract.itemType, item.get()));
        if (type != BossSoul && type != ArchSoul) continue;
        const int quantity = api.Value<int>(api.Invoke(contract.quantity, item.get()));
        if (quantity >= 1 && quantity <= MaxSoulQuantity) souls += quantity;
    }
    return souls;
}
// A dungeon completed: a victory on one of its maps over one of its bosses
// (DungeonData.bosses holds MonsterData ids, as the report's monsters do). A
// dungeon without bosses is never completed this way. 0: none.
int Dungeon(Obj character, const std::vector<int64_t>& losers) {
    const int64_t map = state.fightMap > 0 ? state.fightMap : Map(character);
    if (map <= 0 || map > INT32_MAX || losers.empty()) return 0;
    int key = static_cast<int>(map);
    Ref root(api.Invoke(contract.dungeons, nullptr));
    Ref dungeon(root.get() ? api.Invoke(contract.dungeonByMap, root.get(), {&key}) : nullptr);
    if (!dungeon.get()) return 0;
    if (!Is(dungeon.get(), contract.dungeon)) throw std::runtime_error("Unexpected dungeon");
    const Obj bosses = Read<Obj>(dungeon.get(), contract.at.bosses);
    const int count = bosses ? Count(bosses, MaxBosses) : 0;
    for (int i = 0; i < count; ++i) {
        const int boss = api.Value<int>(api.Call(bosses, "get_Item", {&i}));
        if (boss > 0 && std::find(losers.begin(), losers.end(), boss) != losers.end()) {
            const int32_t id = Read<int32_t>(dungeon.get(), contract.at.dungeonId);
            return id > 0 ? id : 0;
        }
    }
    return 0;
}
void Process(Obj report, Obj character, int64_t localId) {
    const char* skipped = nullptr;
    int32_t type = -1;
    int16_t outcome = 0;
    int64_t ms = 0, turns = 0, souls = 0;
    std::vector<int64_t> losers;
    try {
        const auto flags = Read<uint8_t>(report, contract.at.flags);
        type = Read<int32_t>(report, contract.at.type);
        ms = Read<int32_t>(report, contract.at.duration);
        turns = Read<int32_t>(report, contract.at.turns);
        if (ms < 0 || ms > MaxFightMs) ms = 0;
        if (turns < 0 || turns > MaxTurns) turns = 0;
        const Obj fighters = Read<Obj>(report, contract.at.fighters);
        const int entries = fighters ? Count(fighters, MaxFighters) : 0;
        Ref mine;
        for (int i = 0; i < entries; ++i) {
            Ref entry(api.Call(fighters, "get_Item", {&i}));
            if (!entry.get()) continue;
            if (!Is(entry.get(), contract.fighter)) throw std::runtime_error("Unexpected fighter result");
            // Every client receives the whole fight's results: only this character counts.
            if (Read<int64_t>(entry.get(), contract.at.id) == localId) { if (!mine.get()) mine = std::move(entry); continue; }
            if (Read<int32_t>(entry.get(), contract.at.kind) == MonsterFighter && Read<int16_t>(entry.get(), contract.at.outcome) != Victory)
                losers.push_back(Read<int64_t>(entry.get(), contract.at.id));
        }
        if ((flags & Spectator) != 0) skipped = "spectator";
        else if (!mine.get()) skipped = "absent";
        else if (!Outside(localId)) skipped = "started";
        else {
            outcome = Read<int16_t>(mine.get(), contract.at.outcome);
            souls = Souls(mine.get());
        }
    } catch (...) {
        // Already remembered: a report that cannot be read is lost, never counted twice.
        Error("report");
        ResetActions();
        return;
    }
    if (skipped) {
        try { Log(std::string("STATS_FIGHT_SKIPPED reason=") + skipped + " type=" + std::to_string(type)); } catch (...) {}
        ResetActions();
        return;
    }
    const bool win = outcome == Victory;
    // « Monstres vaincus »: the monsters on the losing side of a fight the
    // character's side won, each once (dt's enemiesFinished would be the
    // character's own finishing blows only, players included).
    const auto monsters = win ? static_cast<int64_t>(losers.size()) : 0;
    int dungeon = 0;
    if (win && contract.dungeonsReady) {
        try { dungeon = Dungeon(character, losers); }
        catch (...) { dungeon = 0; Error("dungeon"); }
    }
    const auto kind = KindOf(type);
    stats::Add(localId, kind == Kind::Kolizeum ? Counter::FightsKolizeum : kind == Kind::Pvp ? Counter::FightsPvp : Counter::FightsPve);
    if (win) stats::Add(localId, kind == Kind::Kolizeum ? Counter::WinsKolizeum : kind == Kind::Pvp ? Counter::WinsPvp : Counter::WinsPve);
    if (ms > 0) stats::Add(localId, Counter::FightMs, ms);
    if (turns > 0) stats::Add(localId, Counter::FightTurns, turns);
    if (monsters > 0) stats::Add(localId, Counter::Kills, monsters);
    if (souls > 0) stats::Add(localId, Counter::Captures, souls);
    if (dungeon > 0) {
        stats::Add(localId, Counter::Dungeons);
        stats::AddTo(localId, Table::Dungeons, dungeon);
    }
    try {
        Log("STATS_FIGHT type=" + std::to_string(type) + " kind=" + KindName(kind) + " win=" + (win ? "1" : "0") + " ms=" +
            std::to_string(ms) + " turns=" + std::to_string(turns) + " monsters=" + std::to_string(monsters) + " souls=" +
            std::to_string(souls) + " dungeon=" +
            std::to_string(dungeon) + " casts=" + std::to_string(state.casts) + " hits=" + std::to_string(state.hits) +
            " deaths=" + std::to_string(state.deaths));
    } catch (...) {}
    ResetActions();
    state.pending.report.reset(report);
    state.pending.character = localId;
    state.pending.since = GetTickCount64();
    Settle(report); // often already there: the statistics arrive with the fight's end
}
void Observe() {
    Ref character(module->FindService("fng"));
    // esg.dpzj (z) is null while no character is played: nothing decided then.
    const Obj info = Is(character.get(), contract.character) ? Read<Obj>(character.get(), contract.at.info) : nullptr;
    const int64_t localId = Is(info, contract.info) ? Read<int64_t>(info, contract.at.characterId) : 0;
    acting.store(localId > 0 ? localId : 0);
    if (localId <= 0 || !contract.reports) return;
    // Resolved on every tick, as capture-detect does: module->fight is bound once.
    Ref service(module->FindService("fsl"));
    if (!Is(service.get(), contract.service)) return;
    const Obj fight = Read<Obj>(service.get(), contract.at.fight);
    if (!Is(fight, contract.fight)) return;
    const Obj history = Read<Obj>(fight, contract.at.history);
    if (!Is(history, contract.history)) return;
    const Obj report = Read<Obj>(history, contract.at.report);
    if (report && !Is(report, contract.report)) throw std::runtime_error("Unexpected fight report");
    const bool fighting = Read<bool>(fight, contract.at.fighting);
    // A fight starts: its map, a dungeon's room when it is one.
    if (fighting && !state.fighting) state.fightMap = contract.dungeonsReady ? Map(character.get()) : 0;
    state.fighting = fighting;
    Settle(report);
    // Baseline (first complete read, another report owner or character): the
    // report shown then is never processed, so neither a mod start, a
    // reconnection, a character switch nor counting switched on counts it.
    const bool baseline = !state.known || history != state.history.get() || localId != state.localId;
    const bool fresh = report && !Seen(report);
    if (history != state.history.get()) state.history.reset(history);
    state.localId = localId;
    state.known = true;
    // Remembered before processing: an error inside never reprocesses it.
    Remember(report);
    if (!baseline && fresh) {
        Process(report, character.get(), localId);
        state.fightMap = 0;
    }
    // Seen outside a fight, after this tick's report: its next fight counts.
    if (!fighting) Roamed(localId);
}

// One fight action the server sent (jsq), read before the game plays it: raw
// reads of pinned fields only, no managed call.
void Act(Obj message) {
    const int64_t local = acting.load(std::memory_order_relaxed);
    if (!local || !message || !contract.actions) return;
    // The game dispatches its messages on its main thread, the pump's; any
    // other thread is never counted (the store is the main thread's).
    if (GetCurrentThreadId() != pumpThread.load(std::memory_order_relaxed)) {
        if (!threadLogged.exchange(true)) Log("STATS_FIGHTS_THREAD");
        return;
    }
    if (!Active() || !stats::Counting() || !Is(message, contract.action)) return;
    const auto which = Read<int32_t>(message, contract.at.which);
    if (which != CastAction && which != DeathAction && which != LossAction) return;
    const Obj body = Read<Obj>(message, contract.at.body);
    if (!body) return;
    // The action's author (gau.biyu passes it as a death's killer).
    const auto source = Read<int64_t>(message, contract.at.source);
    if (which == CastAction) {
        // A spell the character casts (gau.bixd); a weapon hit is not a spell.
        if (source != local || !Is(body, contract.cast) || Read<int32_t>(body, contract.at.castKind) != SpellCast) return;
        const Obj spell = Read<Obj>(body, contract.at.castSpell);
        if (!Is(spell, contract.spell)) return;
        const auto id = Read<int32_t>(spell, contract.at.spellId);
        if (id <= 0) return;
        stats::Add(local, Counter::SpellCasts);
        stats::AddTo(local, Table::Spells, id);
        ++state.casts;
    } else if (which == DeathAction) {
        // The character dies (gau.biyu), resurrected later or not.
        if (!Is(body, contract.death) || Read<int64_t>(body, contract.at.dead) != local) return;
        stats::Add(local, Counter::Deaths);
        ++state.deaths;
    } else {
        // Life and shield the character's own action takes from another fighter
        // (gau.bixr builds its life and shield steps from them).
        if (source != local || !Is(body, contract.loss) || Read<int64_t>(body, contract.at.target) == local) return;
        const int64_t life = Read<int32_t>(body, contract.at.life);
        const int64_t shield = (Read<int32_t>(body, contract.at.present) & 1) != 0 ? Read<int32_t>(body, contract.at.shield) : 0;
        const int64_t hit = std::max<int64_t>(life, 0) + std::max<int64_t>(shield, 0);
        if (hit <= 0 || hit > MaxHit) return;
        // dzv: 0 neutral, 1 earth, 2 fire, 3 water, 4 air (ebw.vql maps them to
        // their resistances); the others (best or worst element, unknown) are none.
        const auto element = Read<int32_t>(body, contract.at.element);
        if (element >= 0 && element <= 4) stats::AddTo(local, Table::Elements, element, hit);
        stats::Raise(local, stats::Record::BiggestHit, hit);
        ++state.hits;
    }
}
void ActionHook(Obj handler, Obj message, Method method) {
    try { Act(message); } catch (...) { Error("action"); }
    originalAction(handler, message, method);
}
}
}

void InstallStatsFights() {
    using namespace stats_fights;
    // Fields are read at their pinned offsets, checked here by name: a port that
    // reshuffles them disables only that part (STATS_FIGHTS_DISABLED). Each part
    // installs once; the hook is created at most once.
    if (!contract.base) {
        try {
            Contract next;
            next.character = api.Class("Core.dll", "", "esg");
            next.at.info = RequireField(next.character, "dpzj", 0x128);
            next.info = api.Class("Core.dll", "", "z");
            next.at.characterId = RequireField(next.info, "dbrl", 0x10);
            next.base = true;
            contract = next;
        } catch (...) {
            try { Log("STATS_FIGHTS_DISABLED stage=character"); } catch (...) {}
            return;
        }
    }
    if (!contract.reports) {
        try {
            Contract next = contract;
            next.service = api.Class("Core.dll", "", "exf");
            next.at.fight = RequireField(next.service, "drih", 0x18);
            next.fight = api.Class("Core.dll", "", "fxc");
            next.at.fighting = RequireField(next.fight, "dwan", 0xB0);
            next.at.history = RequireField(next.fight, "<dwaq>k__BackingField", 0xC8);
            next.history = api.Class("Core.dll", "", "fzf");
            next.at.report = RequireField(next.history, "dwqq", 0x18);
            next.report = api.Class("Core.dll", "", "fyz");
            next.at.flags = RequireField(next.report, "<dwon>k__BackingField", 0x10);
            next.at.type = RequireField(next.report, "dwoo", 0x14);
            next.at.duration = RequireField(next.report, "dwop", 0x18);
            next.at.turns = RequireField(next.report, "dwoq", 0x1C);
            next.at.fighters = RequireField(next.report, "dwou", 0x30);
            next.at.statistics = RequireField(next.report, "dwpa", 0x60);
            next.fighter = api.Class("Core.dll", "", "fze");
            next.at.id = RequireField(next.fighter, "dwpw", 0x10);
            next.at.outcome = RequireField(next.fighter, "dwpy", 0x20);
            next.at.loot = RequireField(next.fighter, "dwqb", 0x28);
            next.at.kind = RequireField(next.fighter, "dwqd", 0x34);
            next.loot = api.Class("Core.dll", "", "fzg");
            next.at.items = RequireField(next.loot, "dwqv", 0x10);
            next.statistics = api.Class("Core.dll", "", "dt");
            next.at.dealt = RequireField(next.statistics, "dcbc", 0x10);
            next.at.taken = RequireField(next.statistics, "dcbd", 0x14);
            next.at.healed = RequireField(next.statistics, "dcbg", 0x20);
            // The item accessors capture-detect reads souls with (soul-inventory.cpp's RVAs).
            next.item = api.Class("Core.dll", "", "mc");
            next.itemType = api.FindVerified(next.item, "mzb", 0, 0x2AAC720);
            next.quantity = api.FindVerified(next.item, "myo", 0, 0x861E70);
            next.reports = true;
            contract = next;
        } catch (...) {
            try { Log("STATS_FIGHTS_DISABLED stage=report"); } catch (...) {}
        }
    }
    if (contract.reports && !contract.dungeonsReady) {
        try {
            // The dungeons as archi-zones.cpp reads them (its verified data root and map lookup).
            Contract next = contract;
            next.at.position = RequireField(next.character, "dqab", 0x1A0);
            next.point = api.Class(DataCenter, WorldSpace, "WorldPoint");
            next.at.mapId = RequireField(next.point, "m_mapId", 0x10);
            next.dataCenter = api.Class(DataCenter, "Core.DataCenter", "DataCenterModule");
            next.dungeons = api.FindVerified(next.dataCenter, "get_dungeonsDataRoot", 0, 0x7BC7C0);
            next.dungeonRoot = api.Class(DataCenter, WorldSpace, "DungeonsDataRoot");
            next.dungeonByMap = api.FindVerified(next.dungeonRoot, "GetDungeonByMapId", 1, 0x7C26B0);
            next.dungeon = api.Class(DataCenter, WorldSpace, "DungeonData");
            next.at.dungeonId = RequireField(next.dungeon, "id", 0x10);
            next.at.bosses = RequireField(next.dungeon, "bosses", 0x50);
            next.dungeonsReady = true;
            contract = next;
        } catch (...) {
            try { Log("STATS_FIGHTS_DISABLED stage=dungeons"); } catch (...) {}
        }
    }
    if (!contract.actions) {
        try {
            // The fight actions: gau.bixl, the game's registered listener of jsq
            // messages (FightSequenceService.cs), and the messages it dispatches.
            Contract next = contract;
            next.action = api.Class(Protocol, "", "jsq");
            next.at.source = RequireField(next.action, "ercs", 0x20);
            next.at.body = RequireField(next.action, "ered", 0x28);
            next.at.which = RequireField(next.action, "eree", 0x30);
            const auto types = api.NestedClass(next.action, "jsp");
            next.cast = api.NestedClass(types, "jrw");
            next.at.castSpell = RequireField(next.cast, "eqxm", 0x40);
            next.at.castKind = RequireField(next.cast, "eqxn", 0x48);
            next.spell = api.NestedClass(api.NestedClass(next.cast, "jrv"), "jrq");
            next.at.spellId = RequireField(next.spell, "eqvk", 0x1C);
            next.death = api.NestedClass(types, "jrz");
            next.at.dead = RequireField(next.death, "eqyd", 0x20);
            next.loss = api.NestedClass(types, "jsd");
            next.at.present = RequireField(next.loss, "eqyu", 0x18);
            next.at.shield = RequireField(next.loss, "eqyz", 0x20);
            next.at.element = RequireField(next.loss, "eqzd", 0x30);
            next.at.target = RequireField(next.loss, "eqzf", 0x38);
            next.at.life = RequireField(next.loss, "eqzh", 0x40);
            next.handler = api.Class("Core.dll", "", "gau");
            const auto listener = api.FindVerified(next.handler, "bixl", 1, 0x1EA0910);
            AddHook(listener, 0x1EA0910, reinterpret_cast<void*>(&ActionHook), reinterpret_cast<void**>(&originalAction));
            next.actions = true;
            contract = next;
        } catch (...) {
            try { Log("STATS_FIGHTS_DISABLED stage=actions"); } catch (...) {}
        }
    }
}
void TickStatsFights() {
    using namespace stats_fights;
    if (!contract.base || !Active()) return;
    pumpThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    try {
        if (state.owner != module || state.compositor.get() != module->compositor.get()) {
            Reset();
            state.owner = module;
            state.compositor.reset(module->compositor.get());
        }
        // Counting off: nothing read.
        if (!stats::Counting()) { Forget(); return; }
        Observe();
    } catch (...) {
        // Nothing reaches Module::Pump. The state is kept: a report shown
        // meanwhile is processed once reads work again.
        acting.store(0);
        Error("read");
    }
}
void RestoreStatsFights() {
    try { stats_fights::Reset(); } catch (...) {}
}
