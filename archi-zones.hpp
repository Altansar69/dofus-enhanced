#pragma once
// « Ocre » on the big map and the mini-map: the zones of the monsters of
// L’éternelle moisson (archimonsters and dungeon bosses) the character still
// lacks (not validated in the quest, not in the bag, not in the Metamob quest),
// coloured by how many are missing there.
// - Big map (option archiZonesMap): an « Ocre » mode square under « Conquête »
//   in the map's mode column; selected, it opens a native side panel modelled
//   on the anomalies one (search, territories, level range, type: all,
//   archimonsters or dungeon bosses; the zones sorted by missing count; a row
//   centres the map on its zone) and colours the listed zones on the map.
// - Mini-map (option archiZonesMiniMap): the same zones for the panel's type
//   choice, without the panel.
// Each option off: no square, no zone, no reading. No hook of its own: the map
// window comes from resource-map-ui's hooks, clicks from the shared native
// click handlers.
void InstallArchiZones();
void TickArchiZones();
void RestoreArchiZones();
bool ArchiZonesCleanupPending();
