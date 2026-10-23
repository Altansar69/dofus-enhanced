#pragma once
#include <span>
#include <cstdint>

namespace resourceMap {
struct Resource { int itemId, jobId, level, iconId; const char* name; const char* mapLabel=nullptr; };
// Recorded harvest-node counts, never units received from harvesting.
// mapId is a deduplication key: a native map ID or a source-scoped synthetic
// key for a published coordinate record. Never use it for game/network calls.
// No counts are distributed from subarea totals across guessed map positions.
struct Placement {
    int64_t mapId;
    int world, x, y, itemId, count;
    bool interior;
};
struct Job { int id; const char* name; };
inline constexpr Job Jobs[]{{1,"Base"},{2,"Bûcheron"},{24,"Mineur"},
    {26,"Alchimiste"},{28,"Paysan"},{36,"Pêcheur"}};
std::span<const Resource> Catalog();
std::span<const Placement> Placements();
}
