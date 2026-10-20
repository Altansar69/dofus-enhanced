#pragma once
#include "runtime.hpp"
#include <vector>

namespace monsterGlow {
struct Target {
    int64_t id = 0;
    bool interactive = false;
    managed::Ref entity;
    std::vector<managed::Ref> renderers;
};
struct Snapshot {
    managed::Ref context;
    bool combat = false;
    bool valid = false;
    std::vector<Target> targets;
    const char* reason = "unavailable";
    const char* lastRejectedPart = "none";
    float lastRejectedAlpha = -1; // -1 means the last rejection was not an alpha gate.
    int rosterCount = 0, monsterCount = 0, npcCount = 0, interactiveCount = 0, matchedRoots = 0;
};
void InstallEntities();
// Main-thread, read-only snapshot. All managed references belong to the result;
// no entity or registry is retained internally between frames.
Snapshot ReadEntities();
}
