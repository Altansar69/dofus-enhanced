#pragma once

struct ChatTpTarget {
    int x = 0, y = 0, world = 0;
    bool operator==(const ChatTpTarget&) const = default;
};

void InstallChatTpLinks(void (*clicked)(ChatTpTarget));
void TickChatTpLinks();
void RestoreChatTpLinks();
bool ChatTpLinksCleanupPending();
