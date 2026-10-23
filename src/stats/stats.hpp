#pragma once
// « Statistiques »: cumulative counters per character, counted from the first
// release that has them (1.15.0), kept beside the shared settings in
// DofusMiniatures-stats.json: one record per character id, merged under a file
// lock with the other game clients (one client per character). With
// settings.statsEnabled off nothing is counted and the trackers read nothing.
#include <cstdint>
#include <string>

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
    SpellCasts,
    TeleportZaap, TeleportZaapi, TeleportPrism, TeleportOther, RecallPotions, HavenBag, ModTrips,
    Count
};
// Records: the largest value seen.
enum class Record : int { BiggestHit, LongestSessionMs, Count };
// Per-key tables; the options show their largest entries.
enum class Table : int {
    Spells,      // spell id -> casts
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
}

// Module lifecycle: Install on the worker thread before the hooks (never
// throws); Tick from Module::Pump on the main thread (writes the file at most
// once a minute); Flush at Stop.
void InstallStats();
void TickStats();
void FlushStats();
