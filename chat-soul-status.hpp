#pragma once
#include <string>

// Read-only, bounded inventory scan and cached quest progress for chat labels.
void TickChatSoulStatus(bool needed);
void RestoreChatSoulStatus();
enum class ChatSoulPart { Inventory, Ocre };
std::string ChatSoulCaption(int monsterId, ChatSoulPart part, bool colorStatus = false);
std::string ChatSoulDetail(int monsterId, ChatSoulPart part);
