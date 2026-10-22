#pragma once
#include "chat-tp-links.hpp"
#include <cstdint>
#include <string>
void InstallChatTravel();
void StartChatTravel(ChatTpTarget target);
// Explicit map-menu selection: use this exact zaap, with no onward route.
void StartHavenbagZaap(int64_t map);
void TickChatTravel();
void TickChatTravelPlanning();
void RestoreChatTravel();
// A line in the native local chat history (no send, no network action), with
// its own prefix ("TP : ", "Metamob : "); same text within 2 s is shown once.
void PostChatNotice(const std::string& text);
