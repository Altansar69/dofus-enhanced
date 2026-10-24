#pragma once
// « Statistiques »: cumulative counters per character, counted from the first
// release that has them (1.15.0), kept beside the shared settings in
// DofusMiniatures-stats.json: one record per character id, merged under a file
// lock with the other game clients (one client per character). With
// settings.statsEnabled off nothing is counted and the trackers read nothing.
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace stats {
// Plain sums: durations in milliseconds, kamas in kamas.
enum class Counter : int {
    PlayMs,                                        // logged in with this character
    CellsRoleplay, CellsFight,                     // cells actually walked
    Maps, Dimensions,                              // map changes, world changes
    Xp, Levels,                                    // character experience and levels gained
    KamasEarned, KamasSpent,                       // bank and storage moves excluded
    Harvests, Crafts, Quests, Achievements,
    Captures,                                      // souls obtained
    FightsPve, FightsPvp, FightsKolizeum,          // finished fights, by kind
    WinsPve, WinsPvp, WinsKolizeum,
    FightMs, FightTurns,
    DamageDealt, DamageTaken, Heals, Kills, Deaths, Dungeons,
    SpellCasts,                                    // the character's class spells cast (KindOf), never weapons
    TeleportZaap, TeleportZaapi, TeleportPrism, TeleportOther, RecallPotions, HavenBag, ModTrips,
    Count
};
// Records: the largest value seen.
enum class Record : int { BiggestHit, LongestSessionMs, Count };
// Per-key tables; the statistics window shows their largest entries.
enum class Table : int {
    Spells,      // spell id -> casts, the character's class spells only (KindOf)
    Elements,    // element (0 neutral, 1 earth, 2 fire, 3 water, 4 air) -> damage dealt
    Dungeons,    // dungeon id -> times completed
    Dimensions,  // world id -> arrivals
    Count
};

// Trackers call these from the game's main thread (hooks and Module::Pump).
// They are cheap, never throw, and do nothing while counting is off or for an
// unknown character (0).
void Add(int64_t character, Counter counter, int64_t amount = 1);
void Raise(int64_t character, Record record, int64_t value);
void AddTo(int64_t character, Table table, int key, int64_t amount = 1);
// The character's name, shown with the totals of every character.
void Name(int64_t character, const std::string& name);
bool Counting();
// Whose spell a spell id is, in the game's data: one of a class's own spells
// (one of its class's spell pairs, SpellVariantData), or another spell the
// server casts in a character's name, such as an equipment's effect (the
// Dofus Pourpre's « Pourpre Profond », the Dofus Émeraude's « Vert
// Émeraude »). Unknown while the game's data cannot tell. Module::Pump only
// (the game is asked once per spell); never throws.
enum class SpellKind { Unknown, Class, Other };
SpellKind KindOf(int spell);
}

// The store's side for the statistics window (« Statistiques », stats-window.hpp),
// added by the store: the statistics shown, their reset and the names of the
// table keys.
namespace stats {
constexpr size_t CounterCount = static_cast<size_t>(Counter::Count);
constexpr size_t RecordCount = static_cast<size_t>(Record::Count);
constexpr size_t TableCount = static_cast<size_t>(Table::Count);
// One character's statistics, or the sums of every character's (records: the
// largest). Each table is sorted by value, largest first.
struct Totals {
    int64_t character = 0;        // the character shown; 0 for every character
    std::string name;             // its name, when known
    int characters = 0;           // characters with something counted
    int64_t first = 0, last = 0;  // first and last counted, Unix time in seconds; 0: nothing counted
    std::array<int64_t, CounterCount> counters{};
    std::array<int64_t, RecordCount> records{};
    std::array<std::vector<std::pair<int, int64_t>>, TableCount> tables{};
    int64_t Count(Counter counter) const { return counters[static_cast<size_t>(counter)]; }
    int64_t Best(Record record) const { return records[static_cast<size_t>(record)]; }
    const std::vector<std::pair<int, int64_t>>& Entries(Table table) const { return tables[static_cast<size_t>(table)]; }
};
// The character played in this client; 0 while unknown.
int64_t Current();
// What the window shows: this client's character, or every character's sums.
// Main thread; never throws.
Totals Shown(bool everyCharacter);
// Erases what the window shows, in the file under its lock; false when that is
// not possible now (no character, file in use or unwritable).
bool Clear(bool everyCharacter);
// A table key's name: the game's for spells, dungeons and worlds, the
// element's for elements; empty when unknown. Main thread; never throws.
std::string KeyName(Table table, int key);
}

// Module lifecycle: Install on the worker thread before the hooks (never
// throws); Tick from Module::Pump on the main thread (writes the file every
// 15 s at most, and at once when counting stops); Flush at Stop.
void InstallStats();
void TickStats();
void FlushStats();
