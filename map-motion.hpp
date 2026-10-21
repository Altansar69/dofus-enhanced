#pragma once
#include "runtime.hpp"
#include <cstdint>

// Smooth cartography motion for both views (full map and mini-map):
// - mouse-wheel zoom steps are spread over a few frames through the game's own
//   zoom routine, so the map glides instead of jumping;
// - centering on the player glides for two seconds and, when the map is more
//   zoomed out than the world's start scale, zooms in to that scale meanwhile.
// Only the view's native zoom, clamp and move functions are called; no
// managed delegate is created and nothing touches an open client at install.
namespace mapMotion {
void Install();
void Tick(uint64_t now);
void Restore();
bool CleanupPending();
// Start the animated centering on a cartography view. Returns false when the
// view, the player position or the world cannot be read; callers then fall
// back to the game's own centering.
bool CenterOnPlayer(managed::Obj view);
}
