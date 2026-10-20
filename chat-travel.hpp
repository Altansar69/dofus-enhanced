#pragma once
#include "chat-tp-links.hpp"
#include <cstdint>
void InstallChatTravel();
void StartChatTravel(ChatTpTarget target);
// Explicit map-menu selection: use this exact zaap, with no onward route.
void StartHavenbagZaap(int64_t map);
void TickChatTravel();
void TickChatTravelPlanning();
void RestoreChatTravel();
