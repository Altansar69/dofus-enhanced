#pragma once
// « Statistiques »: finished fights by kind with victories, time and turns, damage, heals, kills, deaths,
// dungeons, captures, spells cast and elements (see stats/stats.hpp).
void InstallStatsFights();  // worker thread, before the hooks; never throws
void TickStatsFights();     // Module::Pump, main thread
void RestoreStatsFights();  // Module::Stop
