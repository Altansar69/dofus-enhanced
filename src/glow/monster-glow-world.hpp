#pragma once
#include "glow/monster-glow-ui.hpp"
#include <vector>

namespace monsterGlow {
struct WorldDecor {
    managed::Ref element, renderer;
    int layer = 0, order = 0;
    // Cache only inside this frame-owned snapshot, shared by its target queries.
    mutable ScreenRect bounds{};
    mutable uint64_t generation = 0, serial = 0;
    mutable int projection = 0;
};
struct WorldFrame {
    managed::Ref compositor, service, map, loader, resources, sprites;
    std::vector<WorldDecor> decor;
    int64_t mapId = 0;
    int count = 0, version = 0;
    bool valid = false;
    const char* reason = "unavailable";
};
struct WorldSelection {
    std::vector<managed::Ref> renderers;
    bool valid = false;
    const char* reason = "unavailable";
};
void InstallGlowWorld();
// Read-only, main-thread snapshots. References are owned only by these results;
// no client objects or render settings are retained or changed by the provider.
WorldFrame ReadGlowWorld();
// targetBounds and projected decor bounds are physical, bottom-left screen pixels.
// A failed/limited selection is empty and invalid: callers may retain HUD glow.
WorldSelection ForegroundGlowDecor(const WorldFrame& world, const UiFrame& ui,
    const std::vector<managed::Obj>& monsters, ScreenRect targetBounds);
}
