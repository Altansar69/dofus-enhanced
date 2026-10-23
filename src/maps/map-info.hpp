#pragma once
// What the maps show about a cell: on the mini-map, the name of the zone under
// the mouse (option miniMapZoneName); the dimension with the coordinates,
// [x, y, dimension], on the big map and the mini-map (coordsWorldMap,
// coordsWorldMiniMap). Each option off: the game's own display, untouched.
void InstallMapInfo();
void TickMapInfo();                         // main-thread pump, every 200 ms
void MapInfoFrame(unsigned long long now);  // every frame of the main compositor
void RestoreMapInfo();
bool MapInfoCleanupPending();
