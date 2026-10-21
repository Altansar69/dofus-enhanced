#pragma once
#include <filesystem>
#include <vector>

inline constexpr int SimpleReducedPercent = 60;
inline constexpr int SimpleMinimumMonsterPercent = 50;

struct Settings {
    int mode = 2;
    int animationMode = 1; // 0: normal; 1: native shortened; 2: fighter animations; 3: instant combat visuals.
    bool autoLoad = true;
    int roleplayMode = 0;
    int preferredMode = 2;
    int preferredRoleplayMode = 1;
    bool soulReminder = true;
    bool glowNpcs = false;
    bool glowInteractives = false;
    int monsterGlowMode = 1; // 0: off; 1: UI; 2: UI and walls; 3: always.
    int soulPanelX = 24, soulPanelY = 180;
    bool fastMapTransitions = true;
    bool preloadNextMap = true;
    bool soulAutoEquip = true;
    bool spouseTeleport = true;
    bool resourceMapEnabled = true;
    bool resourceMiniMapEnabled = true;
    bool miniMapCenterButton = true;
    std::vector<int> resourceMapItems{};
    bool githubUpdates = true;
    static bool ValidResourceSelection(const std::vector<int>& items) {
        if (items.size() > 128) return false;
        int previous = 0;
        for (int item : items) {
            if (item <= previous || item > 1000000) return false;
            previous = item;
        }
        return true;
    }
    static bool ValidPanelPosition(int value) { return value >= 0 && value <= 16384; }
    static bool ValidAnimationMode(int value) { return value >= 0 && value <= 3; }
    static bool ValidMonsterGlowMode(int value) { return value >= 0 && value <= 3; }
    static Settings Read(const std::filesystem::path& path, bool* autoLoadSpecified = nullptr);
    void Save(const std::filesystem::path& path) const;
    bool operator==(const Settings&) const = default;
};
