#pragma once
// « Statistiques »: time played, cells walked, maps, dimensions, experience and levels, teleports, Havre-Sac
// entries and the mod's own trips (see stats/stats.hpp).
void InstallStatsTravel();  // worker thread, before the hooks; never throws
void TickStatsTravel();     // Module::Pump, main thread
void RestoreStatsTravel();  // Module::Stop

// The one-line calls of other features, on the main thread. Empty until
// InstallStatsTravel has verified their contracts, so those features' own
// offline tests link without the statistics. They never throw.
namespace stats_travel {
inline void (*walkStep)(void* state) noexcept = nullptr;
inline void (*modTrip)() noexcept = nullptr;
}
// animations/movement.cpp NextHook, before the game's xq.rjr: a walk state
// (xq+xm) about to take its next segment, its target cell just reached or not.
inline void StatsWalkStep(void* state) noexcept { if (const auto step = stats_travel::walkStep) step(state); }
// travel/chat-travel.cpp: a journey of the mod has reached its end (one call per journey).
inline void StatsModTrip() noexcept { if (const auto trip = stats_travel::modTrip) trip(); }
