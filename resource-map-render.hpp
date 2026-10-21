#pragma once
#include <cstdint>
#include "runtime.hpp"

namespace resourceMap {
// Rendering only: queries the current native cartography views and owns a
// private group of map markers. No navigation or input hooks are installed.
void InstallRender();
void PumpRender(uint64_t now);
void RestoreRender();
bool RenderCleanupPending();
// Validated native mini-map surface, also available with no resources selected.
// The picker is a sibling of the transformed map, so native map input and zoom
// never include its controls. Dimensions are in the stable parent's UI units.
struct MiniMapSurface {
    managed::Ref root, mapRoot;
    float width=0, height=0, left=0, top=0;
};
MiniMapSurface ReadMiniMapSurface();
// Explicit UI action, valid only for this still-attached mini-map surface.
bool CenterMiniMap(const MiniMapSurface& expected);
}
