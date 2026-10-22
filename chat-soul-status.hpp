#pragma once
#include <string>

// Read-only, bounded inventory scan and cached quest progress for chat labels.
// Each reader runs only while a chat label shows its status: the inventory for
// the bag status, the quest for Ocre. Neither: every reader root is released.
void TickChatSoulStatus(bool inventory, bool quest);
void RestoreChatSoulStatus();
enum class ChatSoulPart { Inventory, Ocre };
std::string ChatSoulCaption(int monsterId, ChatSoulPart part, bool colorStatus = false);
std::string ChatSoulDetail(int monsterId, ChatSoulPart part);
