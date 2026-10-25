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
    bool soulButton = true; // Pierre d’âme : bouton Capture d’âme dans la barre du haut, qui coche ou décoche soulReminder (1.16.1)
    // Surbrillance : une case par catégorie entourée hors combat, dans l'ordre de
    // GlowCategoryToggles (r116). Les monstres, seuls entourés avant ces cases,
    // restent cochés par défaut ; sans case cochée, rien n'est entouré.
    bool glowMonsters = true;
    bool glowNpcs = false;
    bool glowInteractives = false;
    bool glowSelf = false, glowParty = false, glowPlayers = false; // moi, membres du groupe, autres joueurs
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
    bool smallHeaderStats = false; // "Réduire les titres des modules" : la fenêtre des statistiques (1.16.0)
    bool notebookCompact = false; // Carnet : version compacte (liste et actions seulement), choisie dans le menu ⋯ du panneau
    // Les boutons de la chasse au trésor et du carnet dans la barre du haut
    // (1.16.1), leur seul raccourci : décoché, le bouton disparaît et un panneau
    // ouvert le reste.
    bool treasureButton = true; // Chasse au trésor : bouton du panneau de recherche DofusDB
    bool notebookButton = true; // Carnet : bouton du carnet personnel
    // Curseur (1.17.0) : les curseurs du mod à la place de la souris de Windows,
    // désactivés par défaut, leur couleur et leur taille.
    bool customCursor = false; // Curseur : curseurs Dofus du mod à la place de la souris de Windows
    int customCursorColor = 0; // 0 Or, 1 couleur du thème du jeu, 2 Émeraude, 3 Argent
    int customCursorSize = 0; // 0 comme la souris de Windows, 1 grande (×1,5), 2 très grande (×2)
    // Jauges (1.18.0) : les jauges affichées à la verticale (case « Afficher à
    // la verticale » du menu … de chaque jauge), un bit fqd+GaugesType chacune
    // (GaugeVerticalBits) ; la case elle-même dans ces menus, une case
    // d'ExtraToggles : décochée, toutes les jauges à l'horizontale, leurs choix gardés.
    int gaugeVertical = 0;
    bool gaugeVerticalMenu = true; // Jauges : la case « Afficher à la verticale » dans le menu … des jauges
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
    bool mapMenuSettings = true; // Cartes : les réglages de chaque carte au clic droit de la grande carte et de la mini-carte (1.16.0)
    // 1.13.0: one checkbox each, in the order of ExtraToggles below.
    bool resourceMiniMapBar = true; // Cartes : la barre des six métiers sur la mini-carte
    bool miniMapZoneName = true; // Cartes : le nom de la zone de la case survolée sur la mini-carte
    bool notebookPingButton = true; // Carnet : bouton de repère sur la carte à droite de chaque position
    bool notebookCopyButton = true; // Carnet : bouton de copie à droite de chaque entrée : coordonnées d'une position (1.16.0), texte d'une note ou d'un message (1.17.1), à la place de « Copier »
    bool notebookTravelButton = true; // Carnet : bouton de voyage à droite de chaque position (1.16.0, à la place de « Voyager »)
    bool notebookSendButtons = true; // Carnet : boutons d'envoi et de préparation dans le chat à droite de chaque message (1.17.1, à la place de « Envoyer » et « Préparer dans le chat »)
    bool mapAddPosition = true; // Carnet : « Ajouter aux positions » au clic droit d'une case des cartes
    // La dimension avec les coordonnées, [x, y, dimension], une liste chacune :
    // 0 non, 1 oui sauf dans le Monde des Douze, 2 oui ; « oui sauf » par défaut
    // depuis la 1.16.0. Chat : les positions partagées (une liste depuis la
    // 1.14.0, une case avant) ; grande carte et mini-carte : la case survolée
    // (des cases avant la 1.16.0).
    int coordsWorldChatMode = 1;
    int coordsWorldMapMode = 1, coordsWorldMiniMapMode = 1;
    bool statsEnabled = true; // Statistiques : compter les statistiques de mes personnages (1.15.0)
    bool statsButton = true; // Statistiques : bouton de la fenêtre des statistiques dans la barre du haut (1.16.0)
    // Performances : chaque case ajoute sa partie à l'affichage, qui apparaît dès
    // qu'une des trois est cochée (1.16.0 ; avant, le GPU et le disque ne
    // faisaient que compléter perfOverlay).
    bool perfOverlay = false; // Performances : images par seconde, mémoire, tas géré et processeur dans l'affichage
    // Coin de l'affichage sans place choisie à la souris : 0 en haut à gauche, 1 en
    // haut à droite, 2 en bas à gauche, 3 en bas à droite. Plus proposé dans les
    // options depuis la 1.16.0 : seul le coin choisi par une révision antérieure
    // reste, toujours lu et écrit pour elles.
    int perfOverlayCorner = 0;
    // 1.14.0: the overlay's extra figures and its mouse placement (ExtraToggles).
    // The GPU and the disk are off by default since 1.16.0, where each shows the
    // overlay by itself (Settings::Read migrates older files).
    bool perfOverlayGpu = false; // Performances : charge du GPU (part du jeu) dans l'affichage
    bool perfOverlayDisk = false; // Performances : le disque du jeu (temps d'activité, débit de tous les programmes)
    bool perfOverlayDrag = true; // Performances : l'affichage se déplace en le faisant glisser au clic gauche et se redimensionne comme une fenêtre (bord droit : largeur, bord bas : hauteur, coin en bas à droite : les deux) ; décochée, il reste verrouillé à sa place et à sa taille
    // 1.16.0 : son bouton dans la barre du haut (une case d'ExtraToggles), qui le
    // masque et le réaffiche sans décocher ses parties : perfOverlayShown, faux
    // une fois masqué avec ce bouton, gardé d'un démarrage à l'autre (pas une case).
    bool perfOverlayButton = true; // Performances : bouton de l'affichage en haut à droite, à côté de Capture d’âme
    bool perfOverlayShown = true; // Performances : l'affichage apparaît tant qu'une de ses parties est cochée
    // Place choisie à la souris : le coin haut gauche du cadre, en dix-millièmes
    // de la place libre (largeur ou hauteur de l'écran moins celle du cadre),
    // donc indépendante de la définition et de la taille du cadre, gardée que
    // perfOverlayDrag soit cochée ou non. -1 et -1 : le coin perfOverlayCorner.
    // Jamais une seule des deux.
    int perfOverlayX = -1, perfOverlayY = -1;
    // Taille choisie à la souris (1.16.0), en pour cent de la taille normale,
    // de 50 à 300 : depuis la 1.17.1, sa largeur (X) et sa hauteur (Y) chacune,
    // son texte, ses marges et ses coins à la plus petite des deux. Un fichier
    // de la 1.16.x ou de la 1.17.0 n'a que perfOverlayScale, la taille des deux,
    // que Read leur donne et que Save écrit toujours pour elles (la plus petite).
    int perfOverlayScaleX = 100, perfOverlayScaleY = 100;
    // Apparence : avec le mode Miniatures, chaque catégorie cochée est réduite et
    // les autres gardent leur taille normale (Créatures reste pour tous). Par
    // défaut cochées comme avant ces cases, sauf les PNJ, que le jeu ne laissait
    // jamais réduire (1.14.0).
    bool miniatureSelf = true, miniatureAllies = true, miniatureAllySummons = true; // en combat : moi, joueurs alliés, invocations alliées
    bool miniatureOwnSummons = true, miniatureEnemies = true, miniatureEnemySummons = true; // mes invocations, ennemis, invocations ennemies
    bool roleplayMiniatureSelf = true, roleplayMiniatureParty = true; // hors combat : moi, membres du groupe
    bool roleplayMiniaturePlayers = true, roleplayMiniatureMonsters = true; // autres joueurs, monstres
    bool roleplayMiniatureNpcs = false; // PNJ
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
    static bool ValidCoordsWorldMode(int value) { return value >= 0 && value <= 2; } // a dimension choice, chat or maps
    static bool ValidPerfOverlayPlace(int value) { return value >= -1 && value <= 10000; }
    static bool ValidPerfOverlayPlace(int x, int y) {
        return ValidPerfOverlayPlace(x) && ValidPerfOverlayPlace(y) && (x < 0) == (y < 0);
    }
    static bool ValidPerfOverlayScale(int value) { return value >= 50 && value <= 300; }
    static bool ValidCustomCursorColor(int value) { return value >= 0 && value <= 3; }
    static bool ValidCustomCursorSize(int value) { return value >= 0 && value <= 2; }
    // fqd+GaugesType's gauges: the character's experience (1), the guild's (2),
    // the honour points (8), the pods (0x10), the energy points (0x20) and the
    // twenty jobs' experience (0x40 to 0x2000000); 4 is none.
    static constexpr int GaugeVerticalBits = 0x3FFFFFB;
    static bool ValidGaugeVertical(int value) { return value >= 0 && (value & ~GaugeVerticalBits) == 0; }
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
    &Settings::roleplayMiniaturePlayers, &Settings::roleplayMiniatureMonsters, &Settings::roleplayMiniatureNpcs};
// The « Surbrillance » categories, in the order of their options block and of
// the map context menu (r116). The glow's budget has its own priority order
// (monsterGlow::GlowCategory).
inline constexpr bool Settings::* GlowCategoryToggles[] = {
    &Settings::glowMonsters, &Settings::glowNpcs, &Settings::glowInteractives,
    &Settings::glowSelf, &Settings::glowParty, &Settings::glowPlayers};
// The checkboxes added in 1.13.0, 1.14.0, 1.16.0 (the notebook's copy and
// travel buttons, the performance overlay's top-bar button), 1.17.1 (the
// send buttons of the notebook's messages) and 1.18.0 (the gauges' « Afficher
// à la verticale » row), in the order of OptionsUi's extra rows.
inline constexpr bool Settings::* ExtraToggles[] = {
    &Settings::resourceMiniMapBar, &Settings::miniMapZoneName,
    &Settings::notebookPingButton, &Settings::notebookCopyButton, &Settings::notebookTravelButton, &Settings::mapAddPosition, &Settings::perfOverlay,
    &Settings::perfOverlayGpu, &Settings::perfOverlayDisk, &Settings::perfOverlayDrag, &Settings::perfOverlayButton,
    &Settings::notebookSendButtons, &Settings::gaugeVerticalMenu};
