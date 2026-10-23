#pragma once
// « Statistiques »: kamas earned and spent (bank and storage moves excluded), harvests, crafts, quests and
// achievements (see stats/stats.hpp).
void InstallStatsEconomy();  // worker thread, before the hooks; never throws
void TickStatsEconomy();     // Module::Pump, main thread
void RestoreStatsEconomy();  // Module::Stop
