#pragma once
#include "travel/chat-travel-policy.hpp"
namespace chat_route {
enum class Status { Pending, Found, Unreachable };
// A way between two maps outside the game's graph (the Roublards' secret
// passage), taken by the mod itself and never by the game's autopilot: from
// every zone of map from to every zone of map to, cost map changes (1 to 64;
// others are ignored). The temple option applies to it as to the game's edges.
struct Shortcut { int id=0; int64_t from=0, to=0; int cost=1; };
// shortcut: id of the shortcut on the best route, -1 when it takes none. A route
// takes at most one (the trip is planned again after it) and wins only when
// strictly shorter; between equal routes with one, the better-ranked departure,
// then the earlier shortcut in the list.
struct Result { Status status=Status::Pending; chat_travel::Destination departure; int steps=0; int shortcut=-1; };
// The shortcuts the following searches may take (none by default; Clear keeps them).
void SetShortcuts(std::vector<Shortcut> shortcuts);
void Install();
// targetMap: the game's own destination map. currentZone: the player's own zone
// on currentMap, or -1 when it is not known (every zone of the current map then
// starts at 0). roublardTemple: « Passer par le temple Roublard »; false excludes
// entering that temple and its statue's teleports.
void Begin(std::vector<chat_travel::Destination> choices, int64_t targetMap, int64_t currentMap, int currentZone = -1, bool roublardTemple = true);
// Advances the search within the Tick budget. The first search copies the
// game's graph once (kept while the game keeps the same graph object, released
// when the module stops); later searches start from that copy.
Result Tick();
void Clear();
// The main-thread time one Tick may spend: 6000 µs. Tests lower it to slice the
// work as finely as possible; the result never depends on it.
void SetTickBudget(int microseconds);
}
