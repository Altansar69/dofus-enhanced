#pragma once
#include "core/runtime.hpp"

namespace resourceMap {
// The resource choice on both maps. Big map: a « Ressources » mode of the map
// window, beside the game's Anomalies and Conquête (its square in the mode
// column, its window where the game shows theirs). Mini-map: the bar of the six
// jobs and its list, and the centering button.
void InstallUi();
void TickUi();
void RestoreUi();
bool UiCleanupPending();
// The open big map window (native fhi) this module follows through its own
// OnEnable/OnDisable hooks, or null: shared with the « Ocre » mode
// (archi-zones), so the window's lifecycle is hooked once.
managed::Obj CartographyWindow();
// The « Ressources » mode of the big map, for the other mode the mod adds to
// the map's mode column (« Ocre », archi-zones): one selected mode at a time,
// as the game's own. Selected from the click on its square on, before the UI
// tick that shows its window, so a mode that ticks earlier leaves in that same
// tick; CloseResourceTab() leaves it, a selection still waiting for its tick
// included (its window hides at the next UI tick).
bool ResourceTabOpen();
void CloseResourceTab();
}
