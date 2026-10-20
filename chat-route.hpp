#pragma once
#include "chat-travel-policy.hpp"
namespace chat_route {
enum class Status { Pending, Found, Unreachable };
struct Result { Status status=Status::Pending; chat_travel::Destination departure; int steps=0; };
void Install();
void Begin(std::vector<chat_travel::Destination> choices, int64_t targetMap, int64_t currentMap);
Result Tick();
void Clear();
}
