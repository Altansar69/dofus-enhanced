#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace notebook {
enum class Kind { Position, Note, Message };
struct Entry {
    uint64_t id = 0, version = 0;
    Kind kind = Kind::Position;
    std::string title, body;
    int x = 0, y = 0, world = 1;
    int64_t map = 0;
    // Messages only: 0 keeps the chat's current channel, otherwise one of Channels.
    int channel = 0;
    bool operator==(const Entry&) const = default;
};
// Chat channels a message may target: the game's ChatChannel flags, as set by
// ChatView.ToggleChannel*Shortcut (Global 1, Team 2, Guild 4, Alliance 8,
// Party 16, Sales 32, Seek 64); 0 means the chat's current channel.
inline constexpr int Channels[] = {0, 1, 2, 4, 8, 16, 32, 64};
bool ValidChannel(int channel);
int NextChannel(int channel);          // Cycles through Channels.
const char* ChannelName(int channel);  // "actuel", "Général", "Équipe", …
struct Document {
    uint64_t nextId = 1;
    std::vector<Entry> entries;
};
std::filesystem::path DataPath(const std::filesystem::path& settingsPath);
Document Read(const std::filesystem::path& path);
// Existing entries use their version as an optimistic concurrency token.
// Every mutation locks, reloads and preserves unrelated concurrent changes.
Entry Save(const std::filesystem::path& path, Entry entry);
void Delete(const std::filesystem::path& path, uint64_t id, uint64_t version);
bool Coordinate(const std::string& text, int& value);
void Validate(const Entry& entry);
}
