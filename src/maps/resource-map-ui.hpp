#pragma once
#include "core/runtime.hpp"

namespace resourceMap {
void InstallUi();
void TickUi();
void RestoreUi();
bool UiCleanupPending();
// The open big map window (native fhi) this module follows through its own
// OnEnable/OnDisable hooks, or null: shared with the « Ocre » mode
// (archi-zones), so the window's lifecycle is hooked once.
managed::Obj CartographyWindow();
}
