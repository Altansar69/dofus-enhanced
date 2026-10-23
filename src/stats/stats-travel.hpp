#pragma once
// « Statistiques »: time played, cells walked, maps, dimensions, experience and levels, teleports, Havre-Sac
// entries and the mod's own trips (see stats/stats.hpp).
void InstallStatsTravel();  // worker thread, before the hooks; never throws
void TickStatsTravel();     // Module::Pump, main thread
void RestoreStatsTravel();  // Module::Stop
