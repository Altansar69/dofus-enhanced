#pragma once
#include "notebook/notebook-data.hpp"
#include "core/runtime.hpp"
namespace notebook {
void InstallNative();
Entry CurrentPosition();
// The game's world maps (id and localized name), ascending id; never empty.
struct World { int id = 0; std::string name; };
std::vector<World> Worlds();
void Travel(const Entry& entry);
void PrepareMessage(const std::string& text);
// Sends through the game's own chat entry point; channel 0 keeps the chat's
// current channel, otherwise a notebook::Channels value.
void SendChatMessage(const std::string& text, int channel);
void Copy(const std::string& text);

// Map pins: the game's own map flag (the one a chat position link places),
// shown on the big map and the mini-map. Each position has its own flag id,
// so only the mod's flag is ever moved or removed, never the player's flags.
bool PinsAvailable();
bool PinShown(const Entry& entry);
void ShowPin(const Entry& entry); // Places the flag, or moves it to the entry.
void HidePin(uint64_t id);        // Removes that position's flag, on every world.
// The flags this module placed that are still on the map, each with the
// position as placed (one the player removed from the map is forgotten).
std::vector<Entry> PlacedPins();

// A cell of the big map or the mini-map added to the positions, titled with its
// zone and saved like the panel's entries. A position already in the notebook
// (same coordinates and world) is returned instead, with created false.
struct Added { Entry entry; bool created = false; };
Added AddMapPosition(int x, int y, int world, int64_t map);

// Right-click menu of the panel's entry rows (the game's contextual menu):
// « Voyager » and « Afficher sur la carte » or « Retirer de la carte ».
struct RowMenu {
    bool (*entry)(managed::Obj row, Entry& entry) = nullptr; // The position on that row, if any.
    void (*done)(const Entry& entry, const std::string& status) = nullptr; // After a chosen action.
};
void SetRowMenu(RowMenu menu);
bool AttachRowMenu(managed::Obj row); // False when the native menus are unavailable.
void ReleaseRowMenus();
}
