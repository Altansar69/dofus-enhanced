#pragma once
// « Statistiques »: kamas earned and spent (bank and storage moves excluded), harvests, crafts, quests and
// achievements (see stats/stats.hpp), for the character played in this client only, as the game applies
// each server message:
// - KamasEarned / KamasSpent: every change of the character's kamas, except the kamas a character has when
//   it is loaded or when counting starts (login, character switch, reconnection, server transfer, the mod
//   started in game, statistics turned on), moves to and from the bank, a chest or the Havre-Sac chest, and
//   a trade with another character played with the mod on this PC at the same time. Marketplace sales are
//   credited to the bank by the game, never to the character: they are not counted.
// - Harvests: one per harvest (one resource gathered, whatever the quantity it gave).
// - Crafts: the objects the character crafted (each craft's quantity), also for a customer of its
//   multicraft workshop; never an object another player crafted for it; smithmagic is not a craft.
// - Quests: one per quest completed (a repeatable quest each time).
// - Achievements: one per achievement unlocked by this character.
#include <cstdint>

void InstallStatsEconomy();  // worker thread, before the hooks; never throws
void TickStatsEconomy();     // Module::Pump, main thread
void RestoreStatsEconomy();  // Module::Stop

// Offline tests: a scripted clock, and a private name for the list of this PC's characters that the game
// clients share (nullptr: GetTickCount64 and the clients' own list).
void UseStatsEconomyClock(uint64_t (*now)());
void UseStatsEconomyCharacterList(const wchar_t* name);
