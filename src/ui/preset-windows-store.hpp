#pragma once
// « Garder les fenêtres du mod dans les affichages enregistrés » (1.20.0): what
// the mod keeps with each layout the game saves (« Enregistrer l'affichage »),
// without any game access.
//
// The game keeps its layouts (PresetData) in savedPresets, a shared option of
// this PC (Shared/preset.json, every account), each known by its uid: unique
// among them, never reused while it exists (UiPresetOptionsContainer.OnLoaded
// starts new ones above the largest), kept through a rename. The mod never
// writes into them: DofusMiniatures-layouts.json, beside the shared settings,
// holds for each uid the mod's windows as they were when the layout was
// saved, under "shared" (the game's scope for these layouts). Every game
// client (one per character) reads the file again for each layout applied,
// and changes one layout's entry at a time under the file's lock
// (DofusMiniatures-layouts.json.lock), the rest of the file kept as it is. A
// file of a later revision (schema above 1) is read, never rewritten.
//
// Each part of a layout is optional: a part missing or invalid in the file
// (written by an older or later revision, or edited by hand) leaves that
// window as it is when the layout is applied.
#include "core/settings.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace presetWindows {
// A window's place: its top-left corner and its size in ten-thousandths of the
// width and height of the screen's UI, so that it follows the resolution.
inline constexpr int PlaceScale = 10000;
struct Place {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Place&) const = default;
};
bool ValidPlace(const Place& place);
// One of the mod's windows with its own header (statistics, notebook,
// treasure hunt): open or closed, its place while open, « Réduire les titres
// des modules ».
struct Panel {
    bool open = false;
    std::optional<Place> place;
    bool smallHeader = false;
    bool operator==(const Panel&) const = default;
};
// « Capture d’âme » (settings.soulReminder) and the soul panel's header; its
// place is already one of the game's own layout entries (soul-layout.cpp).
struct Soul {
    bool enabled = true, smallHeader = false;
    bool operator==(const Soul&) const = default;
};
// The performance overlay: shown (its top-bar button), its three parts, its
// place and its sizes (Settings' perfOverlay* values, as they are saved).
struct Perf {
    bool shown = true, fps = false, gpu = false, disk = false;
    int x = -1, y = -1, scaleX = 100, scaleY = 100;
    bool operator==(const Perf&) const = default;
};
// The gauges shown vertical (Settings::gaugeVertical) and each one's length
// and place kept at the vertical (Settings::gaugeVerticalBoxes).
struct Gauges {
    int vertical = 0;
    std::array<Settings::GaugeVerticalBox, 26> boxes{};
    bool operator==(const Gauges&) const = default;
};
struct Snapshot {
    std::optional<Panel> stats, notebook, treasure;
    std::optional<bool> notebookCompact; // the notebook's « Version compacte »
    std::optional<Soul> soul;
    std::optional<Perf> perf;
    std::optional<Gauges> gauges;
    bool operator==(const Snapshot&) const = default;
};
// A layout's uid as the game numbers them.
inline constexpr bool ValidUid(int uid) { return uid >= 0 && uid <= 1000000; }
// Bounds: layouts kept in the file, bytes of a name, bytes of the file.
inline constexpr size_t MaxLayouts = 512, MaxNameBytes = 256;
inline constexpr uintmax_t MaxFileBytes = 1024 * 1024;

nlohmann::json ToJson(const Snapshot& snapshot);
// The parts of `windows` that are valid; the others left out.
Snapshot FromJson(const nlohmann::json& windows);

std::filesystem::path DataPath(const std::filesystem::path& settingsPath);
// The mod's windows kept with layout `uid`; nullopt when the file has none for
// it (a layout saved before 1.20.0, or with the option unticked). Throws for an
// unreadable file.
std::optional<Snapshot> Load(const std::filesystem::path& path, int uid);
// Each write takes the file's lock, reads it again and changes `uid`'s entry only.
void Put(const std::filesystem::path& path, int uid, const std::string& name, const Snapshot& snapshot);
// False when the file had nothing for `uid`.
bool Erase(const std::filesystem::path& path, int uid);
bool Rename(const std::filesystem::path& path, int uid, const std::string& name);
// Tests: how long a writer waits for another client's lock (default 250 ms).
void UseLockWait(unsigned milliseconds);
}
