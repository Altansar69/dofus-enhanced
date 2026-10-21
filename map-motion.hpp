#pragma once
#include "runtime.hpp"
#include <cstdint>

// Smooth cartography motion for both views (full map and mini-map). The game
// keeps full control of its own state: every wheel notch and every centering
// is applied at once through the native zoom/move routines, exactly as before.
// Only the *display* of the content element is interpolated afterwards: its
// transform scale and position glide from the previous picture to the new one
// (about 200 ms for a wheel notch, two seconds for centering), so no native
// resize or marker relayout happens per frame. A drag, another native move or
// a module stop snaps the picture back to the game's state. No managed
// delegate is created and nothing touches an open client at install.
namespace mapMotion {
void Install();
void Tick(uint64_t now);
void Restore();
bool CleanupPending();
// Centre a cartography view on the player through the native move, then glide
// the picture there. Returns false when the view, the player position or the
// world cannot be read; callers then fall back to the game's own centering.
bool CenterOnPlayer(managed::Obj view);
}
