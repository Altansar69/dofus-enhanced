#pragma once
#include <string>
// What the maps show about a cell: on the mini-map, the name of the zone under
// the mouse (option miniMapZoneName); the dimension with the coordinates,
// [x, y, dimension], on the big map and the mini-map (coordsWorldMap,
// coordsWorldMiniMap). Each option off: the game's own display, untouched.
// Both maps show the cell under the mouse in the game's own cartography
// tooltip (CartographyTooltipBuilder, « [x,y] »): this module only rewrites
// that tooltip's coordinates text, so it never takes the maps' clicks, drag or
// zoom.
void InstallMapInfo();
void TickMapInfo();                         // main-thread pump, every 200 ms
void MapInfoFrame(unsigned long long now);  // every frame of the main compositor
void RestoreMapInfo();
bool MapInfoCleanupPending();

namespace mapInfo {
// The game's coordinates, « [x,y] », with the dimension (WorldMapData name):
// « [4,-19, Monde des Douze] ». Without a dimension name, the game's own text.
// Shared by the maps' tooltip and the chat's position links.
inline std::string PositionText(int x, int y, const std::string& world) {
    std::string text = "[" + std::to_string(x) + "," + std::to_string(y);
    if (!world.empty()) text += ", " + world;
    return text + "]";
}
}
