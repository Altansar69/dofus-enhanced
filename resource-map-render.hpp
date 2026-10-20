#pragma once
#include <cstdint>

namespace resourceMap {
// Rendering only: queries the current native cartography views and owns a
// private group of map markers. No navigation or input hooks are installed.
void InstallRender();
void PumpRender(uint64_t now);
void RestoreRender();
bool RenderCleanupPending();
}
