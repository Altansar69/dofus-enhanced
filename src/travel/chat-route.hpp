#pragma once
#include "travel/chat-travel-policy.hpp"
namespace chat_route {
enum class Status { Pending, Found, Unreachable };
// A way between two maps outside the game's graph (the Roublards' secret
// passage), taken by the mod itself and never by the game's autopilot: from
// every zone of map from to every zone of map to, cost map changes.
struct Shortcut { int id=0; int64_t from=0, to=0; int cost=1; };
// shortcut: id of the first shortcut on the best route, -1 when it takes none.
struct Result { Status status=Status::Pending; chat_travel::Destination departure; int steps=0; int shortcut=-1; };
// The shortcuts the following searches may take (none by default; Clear keeps them).
void SetShortcuts(std::vector<Shortcut> shortcuts);
void Install();
// targetMap: the game's own destination map. currentZone: the player's own zone
// on currentMap, or -1 when it is not known (every zone of the current map then
// starts at 0). roublardTemple: « Passer par le temple Roublard »; false excludes
// entering that temple and its statue's teleports.
void Begin(std::vector<chat_travel::Destination> choices, int64_t targetMap, int64_t currentMap, int currentZone = -1, bool roublardTemple = true);
Result Tick();
void Clear();
}
