#pragma once
#include "metamob-sync.hpp"
#include <functional>
#include <optional>
#include <string>
// Reads the game's own fight-end report (no hook): the local character's loot
// lists one soul item per captured boss (item type 327) or archimonster (328).
// Marketplace purchases, trades, bank and mail never produce such a report.
void InstallCaptureDetect(std::function<void(metamob::Capture)> sink);
void TickCaptureDetect();
void RestoreCaptureDetect();
// « Metamob dans le chat », on the game thread: a game monster's name in the
// client's language and its Metamob type (boss, archimonster, else any monster),
// read with the verified MonsterData calls of a capture and kept per id (512 at
// most). nullopt while capture detection is off or the module is not running,
// and for a monster unknown or unreadable.
std::optional<metamob::GameMonster> CaptureDetectMonster(int monsterId);
// The local character's name as the last complete read found it (read again
// for each new character); empty when unknown.
std::string CaptureDetectCharacter();
