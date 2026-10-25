#pragma once
#include <cstdint>
#include <string>

// Read-only, bounded inventory scan and cached quest progress, shared by the
// chat labels and the Ocre zones of the maps. Each client says, at each of its
// pumps, which readers its shown statuses need (the inventory for the bag
// status, the quest for Ocre); the readers run for the union of these needs,
// one read step per pump, and a reader that no client needs reads nothing.
// No client needs anything: every reader root is released. A client giving up
// a reader never resets what another client still reads.
enum class SoulClient { Chat, Map };
void WantSoulStatus(SoulClient client, bool inventory, bool quest);
// Releases every reader and forgets every client's needs.
void RestoreChatSoulStatus();
enum class ChatSoulPart { Inventory, Ocre };
std::string ChatSoulCaption(int monsterId, ChatSoulPart part, bool colorStatus = false);
std::string ChatSoulDetail(int monsterId, ChatSoulPart part);
// The states behind the chat labels, for the maps. Unknown: not read yet (or
// read for another character), never a guess.
enum class SoulState { Unknown, Missing, Done, NotStarted };
// Done: validated in L’éternelle moisson; Missing: still to validate;
// NotStarted: the quest is neither under way nor finished.
SoulState OcreProgress(int monsterId);
// Done: a soul holding this monster is in the bag (a dungeon-room soul holds
// every boss its effects 4058 list); Missing: none is.
SoulState BagProgress(int monsterId);
// Changes whenever a reader publishes another result or forgets one.
uint64_t SoulStatusRevision();
