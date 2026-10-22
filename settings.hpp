#pragma once
#include <filesystem>
#include <optional>
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
    bool smallHeaderSoul = false, smallHeaderTreasure = false, smallHeaderNotebook = false; // "Réduire les titres des modules"
    bool notebookCompact = false; // Carnet : version compacte (liste et actions seulement), choisie dans le menu ⋯ du panneau
    bool metamobSync = false; // Metamob : +1 dans la quête Metamob après chaque capture d'archimonstre ou de boss (la clé n'est jamais ici)
    std::vector<int> resourceMapItems{};
    bool githubUpdates = true;
    bool chatTravelLinks = true; // Chat : TP à côté des positions et des groupes de monstres partagés
    bool chatSoulInventory = true; // Chat : « Âme en sac » / « Pas en sac » à côté des archimonstres et boss de L’éternelle moisson partagés
    bool chatOcreStatus = true; // Chat : avancement Ocre à côté des archimonstres et boss de L’éternelle moisson partagés
    bool chatMetamobStatus = true; // Chat : quantité de ta quête Metamob à côté des monstres de L’éternelle moisson partagés
    bool mapTravelPlanner = true; // Trajet du mod au double-clic sur la carte ou la mini-carte et à « Voyager jusqu'à cette position »
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
    // metamobSync as the shared file holds it now; nullopt when the file is
    // unreadable or was last written by a revision that does not know it.
    static std::optional<bool> ReadMetamobSync(const std::filesystem::path& path);
    // withMetamobSync false leaves the metamobSync line out, as an older
    // revision does: a client that has never seen the shared value there does
    // not write its default over the other clients' choice.
    void Save(const std::filesystem::path& path, bool withMetamobSync = true) const;
    bool operator==(const Settings&) const = default;
};
