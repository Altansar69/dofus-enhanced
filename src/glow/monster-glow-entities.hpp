#pragma once
#include "core/runtime.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace monsterGlow {
// What a contour belongs to, in the order of the glow's budget: when a map
// shows more targets than the 32 outlines (or than the 128 targets a reader
// accepts), the first categories keep theirs, so the local character, its
// party, the monsters, the NPCs and the interactive objects never give way to
// the other players of a crowded map. The options and the context menu list
// the same categories in their own order (Settings' GlowCategoryToggles).
enum class GlowCategory : uint8_t { Self, Party, Monster, Npc, Interactive, Player };
struct Target {
    int64_t id = 0;
    GlowCategory category = GlowCategory::Monster;
    managed::Ref entity;
    std::vector<managed::Ref> renderers;
};
// The budget order above; a stable sort keeps each category's own order.
inline void PrioritizeTargets(std::vector<Target>& targets) {
    std::stable_sort(targets.begin(), targets.end(),
        [](const Target& a, const Target& b) { return a.category < b.category; });
}
struct Snapshot {
    managed::Ref context;
    bool combat = false;
    bool valid = false;
    std::vector<Target> targets;
    const char* reason = "unavailable";
    const char* lastRejectedPart = "none";
    float lastRejectedAlpha = -1; // -1 means the last rejection was not an alpha gate.
    int rosterCount = 0, monsterCount = 0, npcCount = 0, playerCount = 0, interactiveCount = 0, matchedRoots = 0;
};
void InstallEntities();
// Main-thread, read-only snapshot of the ticked actor categories (monsters,
// NPCs, the local character, its party, other players), in budget order. All
// managed references belong to the result; no entity or registry is retained
// internally between frames.
Snapshot ReadEntities();
}
