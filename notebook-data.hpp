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
    bool operator==(const Entry&) const = default;
};
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
