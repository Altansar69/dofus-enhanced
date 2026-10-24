#include "core/settings.hpp"
#include <fstream>
#include <string>
#include <stdexcept>
#include <windows.h>
#include <charconv>
#include <atomic>
#include <algorithm>

namespace {
bool ParseInt(const std::string& text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
struct Flag { const char* key; bool Settings::* field; };
// The « Apparence » miniature categories and the « Animations » toggles, each
// its own 0/1 line. An animation key the file does not hold takes the value of
// the legacy animationMode (or noAnimations) the same file holds.
constexpr Flag AppearanceFlags[] = {
    {"miniatureSelf", &Settings::miniatureSelf}, {"miniatureAllies", &Settings::miniatureAllies},
    {"miniatureAllySummons", &Settings::miniatureAllySummons}, {"miniatureOwnSummons", &Settings::miniatureOwnSummons},
    {"miniatureEnemies", &Settings::miniatureEnemies}, {"miniatureEnemySummons", &Settings::miniatureEnemySummons},
    {"roleplayMiniatureSelf", &Settings::roleplayMiniatureSelf}, {"roleplayMiniatureParty", &Settings::roleplayMiniatureParty},
    {"roleplayMiniaturePlayers", &Settings::roleplayMiniaturePlayers}, {"roleplayMiniatureMonsters", &Settings::roleplayMiniatureMonsters},
    {"roleplayMiniatureNpcs", &Settings::roleplayMiniatureNpcs}};
constexpr Flag AnimationFlags[] = {
    {"noSpellEffects", &Settings::noSpellEffects}, {"shortSpellCasts", &Settings::shortSpellCasts},
    {"noAttackAnimations", &Settings::noAttackAnimations}, {"noDeathAnimations", &Settings::noDeathAnimations},
    {"instantMovements", &Settings::instantMovements}, {"noVisualWaits", &Settings::noVisualWaits}};
static_assert(std::size(AnimationFlags) == std::size(AnimationToggles));
// The « Surbrillance » categories (GlowCategoryToggles), each its own 0/1 line.
// A file without one keeps its default: the monsters, alone outlined before
// these boxes (r116), stay ticked, the other categories stay unticked.
constexpr Flag GlowFlags[] = {
    {"glowMonsters", &Settings::glowMonsters}, {"glowNpcs", &Settings::glowNpcs},
    {"glowInteractives", &Settings::glowInteractives}, {"glowSelf", &Settings::glowSelf},
    {"glowParty", &Settings::glowParty}, {"glowPlayers", &Settings::glowPlayers}};
static_assert(std::size(GlowFlags) == std::size(GlowCategoryToggles));
// The checkboxes added in 1.13.0, 1.14.0 and 1.16.0 (ExtraToggles), each its own
// 0/1 line. A file without the notebook's copy and travel buttons or the
// performance overlay's top-bar button (before 1.16.0) keeps their default:
// all shown.
constexpr Flag ExtraFlags[] = {
    {"resourceMiniMapBar", &Settings::resourceMiniMapBar}, {"miniMapZoneName", &Settings::miniMapZoneName},
    {"notebookPingButton", &Settings::notebookPingButton}, {"notebookCopyButton", &Settings::notebookCopyButton},
    {"notebookTravelButton", &Settings::notebookTravelButton}, {"mapAddPosition", &Settings::mapAddPosition},
    {"perfOverlay", &Settings::perfOverlay},
    {"perfOverlayGpu", &Settings::perfOverlayGpu}, {"perfOverlayDisk", &Settings::perfOverlayDisk},
    {"perfOverlayDrag", &Settings::perfOverlayDrag}, {"perfOverlayButton", &Settings::perfOverlayButton}};
static_assert(std::size(ExtraFlags) == std::size(ExtraToggles));
}

Settings Settings::Read(const std::filesystem::path& path, bool* autoLoadSpecified) {
    Settings settings;
    if (autoLoadSpecified) *autoLoadSpecified = false;
    std::ifstream file(path);
    std::string line;
    bool preferredRead = false, preferredRoleplayRead = false, animationModeRead = false, monsterGlowModeRead = false;
    bool resourceMiniMapRead = false, chatModeRead = false;
    bool perfOverlayPartsRead = false; // written by 1.16.0 or later: the overlay's boxes are independent
    int legacyChatDimension = -1; // 1.13.0's checkbox, before the chat's three choices
    // The maps' dimension choices (1.16.0), each read or not, and their
    // checkboxes before (-1: absent); dimensionChoices=1 marks a file written since.
    bool mapModeRead = false, miniMapModeRead = false, dimensionChoices = false;
    int legacyMapDimension = -1, legacyMiniMapDimension = -1;
    int legacyAnimationMode = 1; // the default of every revision that had it
    unsigned animationFlagsRead = 0;
    while (std::getline(file, line)) {
        if (line.size() > 256) throw std::runtime_error("Settings line too long");
        if (line == "mode=0") settings.mode = 0;
        else if (line == "mode=1") settings.mode = 1;
        else if (line == "mode=2") settings.mode = 2;
        else if (line == "noAnimations=0" && !animationModeRead) legacyAnimationMode = 0;
        else if (line == "noAnimations=1" && !animationModeRead) legacyAnimationMode = 1;
        else if (line == "autoLoad=0" || line == "autoLoad=1") {
            settings.autoLoad = line == "autoLoad=1";
            if (autoLoadSpecified) *autoLoadSpecified = true;
        }
        else {
            const auto split = line.find('=');
            if (split == std::string::npos) continue;
            const auto key = line.substr(0, split);
            int value = 0;
            if (!ParseInt(line.substr(split + 1), value)) continue;
            bool flag = false;
            for (const auto& [name, field] : GlowFlags)
                if (key == name) { if (value == 0 || value == 1) settings.*field = value != 0; flag = true; }
            for (const auto& [name, field] : AppearanceFlags)
                if (key == name) { if (value == 0 || value == 1) settings.*field = value != 0; flag = true; }
            for (const auto& [name, field] : ExtraFlags)
                if (key == name) { if (value == 0 || value == 1) settings.*field = value != 0; flag = true; }
            for (size_t index = 0; index != std::size(AnimationFlags); ++index)
                if (key == AnimationFlags[index].key) {
                    if (value == 0 || value == 1) { settings.*AnimationFlags[index].field = value != 0; animationFlagsRead |= 1u << index; }
                    flag = true;
                }
            if (flag) continue;
            if (key == "githubUpdates" && (value == 0 || value == 1)) settings.githubUpdates = value != 0;
            else if (key == "roleplayMode" && value >= 0 && value <= 2) settings.roleplayMode = value;
            else if (key == "animationMode" && ValidAnimationMode(value)) { legacyAnimationMode = value; animationModeRead = true; }
            else if (key == "soulAutoEquip" && (value == 0 || value == 1)) settings.soulAutoEquip = value != 0;
            else if (key == "soulReminder" && (value == 0 || value == 1)) settings.soulReminder = value != 0;
            else if (key == "miniMapCenterButton" && (value == 0 || value == 1)) settings.miniMapCenterButton = value != 0;
            else if (key == "smallHeaderSoul" && (value == 0 || value == 1)) settings.smallHeaderSoul = value != 0;
            else if (key == "smallHeaderTreasure" && (value == 0 || value == 1)) settings.smallHeaderTreasure = value != 0;
            else if (key == "smallHeaderNotebook" && (value == 0 || value == 1)) settings.smallHeaderNotebook = value != 0;
            else if (key == "smallHeaderStats" && (value == 0 || value == 1)) settings.smallHeaderStats = value != 0;
            else if (key == "notebookCompact" && (value == 0 || value == 1)) settings.notebookCompact = value != 0;
            else if (key == "metamobSync" && (value == 0 || value == 1)) settings.metamobSync = value != 0;
            else if (key == "monsterGlowMode" && ValidMonsterGlowMode(value)) { settings.monsterGlowMode = value; monsterGlowModeRead = true; }
            else if (key == "monsterUiGlow" && !monsterGlowModeRead && (value == 0 || value == 1)) settings.monsterGlowMode = value;
            else if (key == "fastMapTransitions" && (value == 0 || value == 1)) settings.fastMapTransitions = value != 0;
            else if (key == "preloadNextMap" && (value == 0 || value == 1)) settings.preloadNextMap = value != 0;
            else if (key == "spouseTeleport" && (value == 0 || value == 1)) settings.spouseTeleport = value != 0;
            else if (key == "chatTravelLinks" && (value == 0 || value == 1)) settings.chatTravelLinks = value != 0;
            else if (key == "chatSoulInventory" && (value == 0 || value == 1)) settings.chatSoulInventory = value != 0;
            else if (key == "chatOcreStatus" && (value == 0 || value == 1)) settings.chatOcreStatus = value != 0;
            else if (key == "chatMetamobStatus" && (value == 0 || value == 1)) settings.chatMetamobStatus = value != 0;
            else if (key == "archiZonesMap" && (value == 0 || value == 1)) settings.archiZonesMap = value != 0;
            else if (key == "travelRoublardTemple" && (value == 0 || value == 1)) settings.travelRoublardTemple = value != 0;
            else if (key == "archiZonesMiniMap" && (value == 0 || value == 1)) settings.archiZonesMiniMap = value != 0;
            else if (key == "calendarKralamoure" && (value == 0 || value == 1)) settings.calendarKralamoure = value != 0;
            else if (key == "mapTravelPlanner" && (value == 0 || value == 1)) settings.mapTravelPlanner = value != 0;
            else if (key == "mapMenuSettings" && (value == 0 || value == 1)) settings.mapMenuSettings = value != 0;
            else if (key == "perfOverlayCorner" && ValidPerfOverlayCorner(value)) settings.perfOverlayCorner = value;
            else if (key == "coordsWorldChatMode" && ValidCoordsWorldMode(value)) { settings.coordsWorldChatMode = value; chatModeRead = true; }
            else if (key == "coordsWorldChat" && (value == 0 || value == 1)) legacyChatDimension = value;
            else if (key == "coordsWorldMapMode" && ValidCoordsWorldMode(value)) { settings.coordsWorldMapMode = value; mapModeRead = true; }
            else if (key == "coordsWorldMap" && (value == 0 || value == 1)) legacyMapDimension = value;
            else if (key == "coordsWorldMiniMapMode" && ValidCoordsWorldMode(value)) { settings.coordsWorldMiniMapMode = value; miniMapModeRead = true; }
            else if (key == "coordsWorldMiniMap" && (value == 0 || value == 1)) legacyMiniMapDimension = value;
            else if (key == "dimensionChoices" && value == 1) dimensionChoices = true;
            else if (key == "statsEnabled" && (value == 0 || value == 1)) settings.statsEnabled = value != 0;
            else if (key == "statsButton" && (value == 0 || value == 1)) settings.statsButton = value != 0;
            else if (key == "perfOverlayX" && ValidPerfOverlayPlace(value)) settings.perfOverlayX = value;
            else if (key == "perfOverlayY" && ValidPerfOverlayPlace(value)) settings.perfOverlayY = value;
            else if (key == "perfOverlayScale" && ValidPerfOverlayScale(value)) settings.perfOverlayScale = value;
            else if (key == "perfOverlayShown" && (value == 0 || value == 1)) settings.perfOverlayShown = value != 0;
            else if (key == "perfOverlayParts" && value >= 1) perfOverlayPartsRead = true;
            else if (key == "resourceMapEnabled" && (value == 0 || value == 1)) settings.resourceMapEnabled = value != 0;
            else if (key == "resourceMiniMapEnabled" && (value == 0 || value == 1)) { settings.resourceMiniMapEnabled = value != 0; resourceMiniMapRead = true; }
            else if (key == "resourceMapItem" && value > 0 && value <= 1000000) {
                auto& items = settings.resourceMapItems;
                const auto where = std::lower_bound(items.begin(), items.end(), value);
                if (items.size() < 128 && (where == items.end() || *where != value)) items.insert(where, value);
            }
            else if (key == "soulPanelX" && ValidPanelPosition(value)) settings.soulPanelX = value;
            else if (key == "soulPanelY" && ValidPanelPosition(value)) settings.soulPanelY = value;
            else if (key == "preferredMode" && value >= 1 && value <= 2) { settings.preferredMode = value; preferredRead = true; }
            else if (key == "preferredRoleplayMode" && value >= 1 && value <= 2) { settings.preferredRoleplayMode = value; preferredRoleplayRead = true; }
            // Legacy sizeMethod and per-category sizes are ignored. Miniatures
            // now always reduce, preserving the selected display modes.
        }
    }
    // Revisions before 1.12.0 had one 0–3 animation mode: its toggles stand in
    // for every animation key this file does not hold.
    Settings legacy;
    legacy.ApplyLegacyAnimationMode(legacyAnimationMode);
    for (size_t index = 0; index != std::size(AnimationFlags); ++index)
        if (!(animationFlagsRead & (1u << index))) settings.*AnimationFlags[index].field = legacy.*AnimationFlags[index].field;
    if (!preferredRead && settings.mode > 0) settings.preferredMode = settings.mode;
    if (!preferredRoleplayRead && settings.roleplayMode > 0) settings.preferredRoleplayMode = settings.roleplayMode;
    // Earlier revisions used one switch for both maps. Preserve that choice
    // until the new mini-map preference has been explicitly saved.
    if (!resourceMiniMapRead) settings.resourceMiniMapEnabled = settings.resourceMapEnabled;
    // A file of 1.13.x: its checkbox becomes « Oui » or « Non ».
    if (!chatModeRead && legacyChatDimension >= 0) settings.coordsWorldChatMode = legacyChatDimension ? 2 : 0;
    // The maps' checkboxes before 1.16.0: checked becomes « Oui, sauf dans le
    // Monde des Douze », the default since, unchecked « Non ».
    if (!mapModeRead && legacyMapDimension >= 0) settings.coordsWorldMapMode = legacyMapDimension ? 1 : 0;
    if (!miniMapModeRead && legacyMiniMapDimension >= 0) settings.coordsWorldMiniMapMode = legacyMiniMapDimension ? 1 : 0;
    // A file written before 1.16.0: the chat's « Oui », the default then,
    // becomes the default since, « Oui, sauf dans le Monde des Douze ». A file
    // written since keeps every choice.
    if (!dimensionChoices && settings.coordsWorldChatMode == 2) settings.coordsWorldChatMode = 1;
    // A place needs both coordinates; one alone means the chosen corner.
    if (!ValidPerfOverlayPlace(settings.perfOverlayX, settings.perfOverlayY)) settings.perfOverlayX = settings.perfOverlayY = -1;
    // Before 1.16.0, « Charge du GPU » and « Activité du disque » only added to
    // the overlay of « Afficher les FPS et la mémoire »: a file of those
    // revisions keeps them only with that box ticked, so that no overlay
    // appears by itself after the update (both were ticked by default).
    if (!perfOverlayPartsRead && !settings.perfOverlay) settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    return settings;
}
std::optional<bool> Settings::ReadMetamobSync(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::optional<bool> value;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "metamobSync=0") value = false;
        else if (line == "metamobSync=1") value = true;
    }
    return value;
}
void Settings::Save(const std::filesystem::path& path, bool withMetamobSync) const {
    if (mode < 0 || mode > 2) throw std::runtime_error("Invalid display mode");
    if (!ValidMonsterGlowMode(monsterGlowMode)) throw std::runtime_error("Invalid monster glow mode");
    if (!ValidResourceSelection(resourceMapItems)) throw std::runtime_error("Invalid resource map selection");
    if (roleplayMode < -1 || roleplayMode > 2 || preferredMode < 1 || preferredMode > 2 ||
        preferredRoleplayMode < 1 || preferredRoleplayMode > 2)
        throw std::runtime_error("Invalid context display preferences");
    if (!ValidPanelPosition(soulPanelX) || !ValidPanelPosition(soulPanelY))
        throw std::runtime_error("Invalid soul panel position");
    if (!ValidPerfOverlayCorner(perfOverlayCorner)) throw std::runtime_error("Invalid performance overlay corner");
    if (!ValidCoordsWorldMode(coordsWorldChatMode)) throw std::runtime_error("Invalid chat dimension choice");
    if (!ValidCoordsWorldMode(coordsWorldMapMode) || !ValidCoordsWorldMode(coordsWorldMiniMapMode))
        throw std::runtime_error("Invalid map dimension choice");
    if (!ValidPerfOverlayPlace(perfOverlayX, perfOverlayY)) throw std::runtime_error("Invalid performance overlay place");
    if (!ValidPerfOverlayScale(perfOverlayScale)) throw std::runtime_error("Invalid performance overlay scale");
    static std::atomic_uint64_t nextSave{0};
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++nextSave);
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        std::ofstream file(temporary, std::ios::trunc);
        // noAnimations and animationMode: for revisions before 1.12.0 only.
        const int legacyAnimationMode = LegacyAnimationMode();
        file << "# Dofus Miniatures - 3.7.11.11\nmode=" << mode
             << "\nnoAnimations=" << (legacyAnimationMode != 0 ? 1 : 0)
             << "\nanimationMode=" << legacyAnimationMode
             << "\nautoLoad=" << (autoLoad ? 1 : 0)
             << "\ngithubUpdates=" << (githubUpdates ? 1 : 0)
             << "\nroleplayMode=" << roleplayMode
             << "\npreferredMode=" << preferredMode
             << "\npreferredRoleplayMode=" << preferredRoleplayMode
             << "\nsoulAutoEquip=" << (soulAutoEquip ? 1 : 0)
             << "\nsoulReminder=" << (soulReminder ? 1 : 0)
             << "\nmonsterUiGlow=" << (monsterGlowMode != 0 ? 1 : 0)
             << "\nmonsterGlowMode=" << monsterGlowMode
             << "\nfastMapTransitions=" << (fastMapTransitions ? 1 : 0)
             << "\npreloadNextMap=" << (preloadNextMap ? 1 : 0)
             << "\nspouseTeleport=" << (spouseTeleport ? 1 : 0)
             << "\nresourceMapEnabled=" << (resourceMapEnabled ? 1 : 0)
             << "\nresourceMiniMapEnabled=" << (resourceMiniMapEnabled ? 1 : 0)
             << "\nminiMapCenterButton=" << (miniMapCenterButton ? 1 : 0)
             << "\nsmallHeaderSoul=" << (smallHeaderSoul ? 1 : 0) << "\nsmallHeaderTreasure=" << (smallHeaderTreasure ? 1 : 0) << "\nsmallHeaderNotebook=" << (smallHeaderNotebook ? 1 : 0)
             << "\nsmallHeaderStats=" << (smallHeaderStats ? 1 : 0)
             << "\nnotebookCompact=" << (notebookCompact ? 1 : 0)
             << "\nchatTravelLinks=" << (chatTravelLinks ? 1 : 0)
             << "\nchatSoulInventory=" << (chatSoulInventory ? 1 : 0)
             << "\nchatOcreStatus=" << (chatOcreStatus ? 1 : 0)
             << "\nchatMetamobStatus=" << (chatMetamobStatus ? 1 : 0)
             << "\narchiZonesMap=" << (archiZonesMap ? 1 : 0)
             << "\ntravelRoublardTemple=" << (travelRoublardTemple ? 1 : 0)
             << "\narchiZonesMiniMap=" << (archiZonesMiniMap ? 1 : 0)
             << "\ncalendarKralamoure=" << (calendarKralamoure ? 1 : 0)
             << "\nmapTravelPlanner=" << (mapTravelPlanner ? 1 : 0);
        file << "\nmapMenuSettings=" << (mapMenuSettings ? 1 : 0); // 1.16.0: each map's settings in its right-click menu
        for (const auto& [name, field] : GlowFlags) file << '\n' << name << '=' << (this->*field ? 1 : 0);
        for (const auto& [name, field] : AppearanceFlags) file << '\n' << name << '=' << (this->*field ? 1 : 0);
        for (const auto& [name, field] : AnimationFlags) file << '\n' << name << '=' << (this->*field ? 1 : 0);
        for (const auto& [name, field] : ExtraFlags) file << '\n' << name << '=' << (this->*field ? 1 : 0);
        file << "\nperfOverlayCorner=" << perfOverlayCorner << "\nperfOverlayX=" << perfOverlayX << "\nperfOverlayY=" << perfOverlayY;
        file << "\nperfOverlayScale=" << perfOverlayScale << "\nperfOverlayShown=" << (perfOverlayShown ? 1 : 0);
        // The overlay's boxes are independent (1.16.0): Read keeps them as they are.
        file << "\nperfOverlayParts=1";
        // Each dimension choice with the checkbox of the revisions before it (the
        // chat's of 1.13.x, the maps' before 1.16.0), checked for any choice but
        // « Non », for a client of those sharing this file; then the mark of a
        // file written since 1.16.0, whose choices Read keeps as they are.
        file << "\ncoordsWorldChatMode=" << coordsWorldChatMode << "\ncoordsWorldChat=" << (coordsWorldChatMode != 0 ? 1 : 0)
             << "\ncoordsWorldMapMode=" << coordsWorldMapMode << "\ncoordsWorldMap=" << (coordsWorldMapMode != 0 ? 1 : 0)
             << "\ncoordsWorldMiniMapMode=" << coordsWorldMiniMapMode << "\ncoordsWorldMiniMap=" << (coordsWorldMiniMapMode != 0 ? 1 : 0)
             << "\ndimensionChoices=1";
        file << "\nstatsEnabled=" << (statsEnabled ? 1 : 0) << "\nstatsButton=" << (statsButton ? 1 : 0);
        if (withMetamobSync) file << "\nmetamobSync=" << (metamobSync ? 1 : 0);
        file << "\nsoulPanelX=" << soulPanelX << "\nsoulPanelY=" << soulPanelY << '\n';
        for (int item : resourceMapItems) file << "resourceMapItem=" << item << '\n';
        file.flush();
        if (!file) throw std::runtime_error("Cannot save settings");
    }
    // Windows can briefly deny a rename while another client is replacing or
    // reading the same destination. Each writer owns its complete temporary.
    for (unsigned attempt = 0; ; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const auto error = GetLastError();
        if (attempt == 15 || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED))
            throw std::runtime_error("Cannot replace settings: " + std::to_string(error));
        Sleep(5);
    }
}
