#pragma once
// « Ocre » on the big map and the mini-map: the zones of the monsters of
// L’éternelle moisson (archimonsters and dungeon bosses), coloured by how many
// the character still lacks there (not validated in the quest, not in the bag,
// not in the Metamob quest), and in « Complet » blue where none is missing.
// - Big map (option archiZonesMap): an « Ocre » mode square under « Conquête »
//   in the map's mode column; selected, it opens a native side panel modelled
//   on the anomalies one (search, territories, level range, type: all,
//   archimonsters or dungeon bosses; the zones with missing monsters sorted by
//   missing count, then the complete ones; a row centres the map on its zone
//   and opens under it the monsters you lack and those you have, and why) and
//   colours the listed zones on the map.
// - Mini-map (option archiZonesMiniMap): every zone of the panel's type choice,
//   same colours, without the panel.
// The zones are the game's own shapes (AddAreaShape), shown the way its
// anomaly and search modes show theirs (fgh.begr). Each option off: no square,
// no zone, no reading. No hook of its own: the map window comes from
// resource-map-ui's hooks, clicks from the shared native click handlers.
void InstallArchiZones();
void TickArchiZones();
void RestoreArchiZones();
bool ArchiZonesCleanupPending();
