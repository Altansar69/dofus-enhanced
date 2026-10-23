#pragma once
#include <filesystem>
#include <optional>
#include <vector>

inline constexpr int SimpleReducedPercent = 60;
inline constexpr int SimpleMinimumMonsterPercent = 50;

struct Settings {
    int mode = 2;
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
    bool travelRoublardTemple = true; // Déplacements : passage secret des Roublards (Bad Aboum) et temple Roublard dans le trajet du mod
    bool archiZonesMap = true; // Cartes : zones des archimonstres manquants sur la grande carte
    bool archiZonesMiniMap = false; // Cartes : les mêmes zones sur la mini-carte
    bool calendarKralamoure = true; // Calendrier : ouvertures Kralamoure de Metamob dans le calendrier du jeu
    bool chatMetamobStatus = true; // Chat : quantité de ta quête Metamob à côté des monstres de L’éternelle moisson partagés
    bool mapTravelPlanner = true; // Trajet du mod au double-clic sur la carte ou la mini-carte et à « Voyager jusqu'à cette position »
    // 1.13.0: one checkbox each, in the order of ExtraToggles below.
    bool resourceMiniMapBar = true; // Cartes : la barre des six métiers sur la mini-carte
    bool miniMapZoneName = true; // Cartes : le nom de la zone de la case survolée sur la mini-carte
    bool coordsWorldMap = true; // Cartes : [x, y, dimension] pour les coordonnées de la grande carte
    bool coordsWorldMiniMap = true; // Cartes : [x, y, dimension] pour les coordonnées de la mini-carte
    bool notebookPingButton = true; // Carnet : bouton de repère sur la carte à droite de chaque position
    bool mapAddPosition = true; // Carnet : « Ajouter aux positions » au clic droit d'une case des cartes
    bool coordsWorldChat = true; // Chat : [x, y, dimension] pour les positions partagées
    bool perfOverlay = false; // Performances : images par seconde et mémoire dans un coin de l'écran
    int perfOverlayCorner = 0; // 0 en haut à gauche, 1 en haut à droite, 2 en bas à gauche, 3 en bas à droite
    // Apparence : avec le mode Miniatures, chaque catégorie cochée est réduite et
    // les autres gardent leur taille normale (Créatures reste pour tous). Par
    // défaut toutes cochées, comme avant ces cases.
    bool miniatureSelf = true, miniatureAllies = true, miniatureAllySummons = true; // en combat : moi, joueurs alliés, invocations alliées
    bool miniatureOwnSummons = true, miniatureEnemies = true, miniatureEnemySummons = true; // mes invocations, ennemis, invocations ennemies
    bool roleplayMiniatureSelf = true, roleplayMiniatureParty = true; // hors combat : moi, membres du groupe
    bool roleplayMiniaturePlayers = true, roleplayMiniatureMonsters = true; // autres joueurs, monstres
    // Animations (en combat) : une case par mécanisme. Les valeurs par défaut
    // sont l'ancien mode « Raccourcies » ; animationMode (0 à 3) n'est plus
    // qu'écrit pour les révisions antérieures et lu quand ces clés manquent.
    bool noSpellEffects = true; // FightSequence.biwv : sans étapes de projectiles et d'effets des sorts
    bool shortSpellCasts = true; // dyq.uus, dzj.uxg : animation de lancer réduite du jeu (personnages joueurs)
    bool noAttackAnimations = false; // animations des combattants (gpy) terminées sans être jouées, la mort exceptée
    bool noDeathAnimations = false; // animation de mort (dwp) : seulement sans déplacements ni attentes instantanés
    bool instantMovements = false; // marche, poussée, lancer et saut finis aussitôt
    bool noVisualWaits = false; // attentes et pauses visuelles terminées aussitôt
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
    // The 0–3 animation mode of revisions before 1.12.0: the highest level whose
    // toggles are all on (1: effects and casts, 2: + attacks and deaths, 3: +
    // movements and waits). Apply sets exactly the toggles of that level.
    int LegacyAnimationMode() const {
        if (!noSpellEffects || !shortSpellCasts) return 0;
        if (!noAttackAnimations || !noDeathAnimations) return 1;
        if (!instantMovements || !noVisualWaits) return 2;
        return 3;
    }
    void ApplyLegacyAnimationMode(int level) {
        noSpellEffects = shortSpellCasts = level >= 1;
        noAttackAnimations = noDeathAnimations = level >= 2;
        instantMovements = noVisualWaits = level >= 3;
    }
    static bool ValidMonsterGlowMode(int value) { return value >= 0 && value <= 3; }
    static bool ValidPerfOverlayCorner(int value) { return value >= 0 && value <= 3; }
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

// The « Animations » toggles, in the order of their options block and of the
// map context menu.
inline constexpr bool Settings::* AnimationToggles[] = {
    &Settings::noSpellEffects, &Settings::shortSpellCasts, &Settings::noAttackAnimations,
    &Settings::noDeathAnimations, &Settings::instantMovements, &Settings::noVisualWaits};
// The miniature categories of the « Apparence » block, in their row order.
inline constexpr bool Settings::* CombatMiniatureToggles[] = {
    &Settings::miniatureSelf, &Settings::miniatureAllies, &Settings::miniatureAllySummons,
    &Settings::miniatureOwnSummons, &Settings::miniatureEnemies, &Settings::miniatureEnemySummons};
inline constexpr bool Settings::* RoleplayMiniatureToggles[] = {
    &Settings::roleplayMiniatureSelf, &Settings::roleplayMiniatureParty,
    &Settings::roleplayMiniaturePlayers, &Settings::roleplayMiniatureMonsters};
// The checkboxes added in 1.13.0, in the order of OptionsUi's extra rows.
inline constexpr bool Settings::* ExtraToggles[] = {
    &Settings::resourceMiniMapBar, &Settings::miniMapZoneName, &Settings::coordsWorldMap, &Settings::coordsWorldMiniMap,
    &Settings::notebookPingButton, &Settings::mapAddPosition, &Settings::coordsWorldChat, &Settings::perfOverlay};
