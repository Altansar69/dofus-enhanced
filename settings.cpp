#include "settings.hpp"
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
}

Settings Settings::Read(const std::filesystem::path& path, bool* autoLoadSpecified) {
    Settings settings;
    if (autoLoadSpecified) *autoLoadSpecified = false;
    std::ifstream file(path);
    std::string line;
    bool preferredRead = false, preferredRoleplayRead = false, animationModeRead = false, monsterGlowModeRead = false;
    bool resourceMiniMapRead = false;
    while (std::getline(file, line)) {
        if (line.size() > 256) throw std::runtime_error("Settings line too long");
        if (line == "mode=0") settings.mode = 0;
        else if (line == "mode=1") settings.mode = 1;
        else if (line == "mode=2") settings.mode = 2;
        else if (line == "noAnimations=0" && !animationModeRead) settings.animationMode = 0;
        else if (line == "noAnimations=1" && !animationModeRead) settings.animationMode = 1;
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
            if (key == "githubUpdates" && (value == 0 || value == 1)) settings.githubUpdates = value != 0;
            else if (key == "roleplayMode" && value >= 0 && value <= 2) settings.roleplayMode = value;
            else if (key == "animationMode" && ValidAnimationMode(value)) { settings.animationMode = value; animationModeRead = true; }
            else if (key == "soulAutoEquip" && (value == 0 || value == 1)) settings.soulAutoEquip = value != 0;
            else if (key == "soulReminder" && (value == 0 || value == 1)) settings.soulReminder = value != 0;
            else if (key == "glowNpcs" && (value == 0 || value == 1)) settings.glowNpcs = value != 0;
            else if (key == "glowInteractives" && (value == 0 || value == 1)) settings.glowInteractives = value != 0;
            else if (key == "monsterGlowMode" && ValidMonsterGlowMode(value)) { settings.monsterGlowMode = value; monsterGlowModeRead = true; }
            else if (key == "monsterUiGlow" && !monsterGlowModeRead && (value == 0 || value == 1)) settings.monsterGlowMode = value;
            else if (key == "fastMapTransitions" && (value == 0 || value == 1)) settings.fastMapTransitions = value != 0;
            else if (key == "preloadNextMap" && (value == 0 || value == 1)) settings.preloadNextMap = value != 0;
            else if (key == "spouseTeleport" && (value == 0 || value == 1)) settings.spouseTeleport = value != 0;
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
    if (!preferredRead && settings.mode > 0) settings.preferredMode = settings.mode;
    if (!preferredRoleplayRead && settings.roleplayMode > 0) settings.preferredRoleplayMode = settings.roleplayMode;
    // Earlier revisions used one switch for both maps. Preserve that choice
    // until the new mini-map preference has been explicitly saved.
    if (!resourceMiniMapRead) settings.resourceMiniMapEnabled = settings.resourceMapEnabled;
    return settings;
}
void Settings::Save(const std::filesystem::path& path) const {
    if (mode < 0 || mode > 2) throw std::runtime_error("Invalid display mode");
    if (!ValidAnimationMode(animationMode)) throw std::runtime_error("Invalid animation mode");
    if (!ValidMonsterGlowMode(monsterGlowMode)) throw std::runtime_error("Invalid monster glow mode");
    if (!ValidResourceSelection(resourceMapItems)) throw std::runtime_error("Invalid resource map selection");
    if (roleplayMode < -1 || roleplayMode > 2 || preferredMode < 1 || preferredMode > 2 ||
        preferredRoleplayMode < 1 || preferredRoleplayMode > 2)
        throw std::runtime_error("Invalid context display preferences");
    if (!ValidPanelPosition(soulPanelX) || !ValidPanelPosition(soulPanelY))
        throw std::runtime_error("Invalid soul panel position");
    static std::atomic_uint64_t nextSave{0};
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++nextSave);
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << "# Dofus Miniatures - 3.7.11.11\nmode=" << mode
             << "\nnoAnimations=" << (animationMode != 0 ? 1 : 0)
             << "\nanimationMode=" << animationMode
             << "\nautoLoad=" << (autoLoad ? 1 : 0)
             << "\ngithubUpdates=" << (githubUpdates ? 1 : 0)
             << "\nroleplayMode=" << roleplayMode
             << "\npreferredMode=" << preferredMode
             << "\npreferredRoleplayMode=" << preferredRoleplayMode
             << "\nsoulAutoEquip=" << (soulAutoEquip ? 1 : 0)
             << "\nsoulReminder=" << (soulReminder ? 1 : 0)
             << "\nmonsterUiGlow=" << (monsterGlowMode != 0 ? 1 : 0)
             << "\nmonsterGlowMode=" << monsterGlowMode
             << "\nglowNpcs=" << (glowNpcs ? 1 : 0)
             << "\nglowInteractives=" << (glowInteractives ? 1 : 0)
             << "\nfastMapTransitions=" << (fastMapTransitions ? 1 : 0)
             << "\npreloadNextMap=" << (preloadNextMap ? 1 : 0)
             << "\nspouseTeleport=" << (spouseTeleport ? 1 : 0)
             << "\nresourceMapEnabled=" << (resourceMapEnabled ? 1 : 0)
             << "\nresourceMiniMapEnabled=" << (resourceMiniMapEnabled ? 1 : 0)
             << "\nsoulPanelX=" << soulPanelX << "\nsoulPanelY=" << soulPanelY << '\n';
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
