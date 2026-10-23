#pragma once
#include "travel/chat-travel-policy.hpp"
namespace chat_route {
enum class Status { Pending, Found, Unreachable };
struct Result { Status status=Status::Pending; chat_travel::Destination departure; int steps=0; };
void Install();
// targetMap: the game's own destination map. currentZone: the player's own zone
// on currentMap, or -1 when it is not known (every zone of the current map then
// starts at 0). roublardTemple: « Passer par le temple Roublard »; false excludes
// entering that temple and its statue's teleports.
void Begin(std::vector<chat_travel::Destination> choices, int64_t targetMap, int64_t currentMap, int currentZone = -1, bool roublardTemple = true);
Result Tick();
void Clear();
}
