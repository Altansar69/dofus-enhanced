#pragma once
#include <array>

// The « Animations » options block and the map context menu show the same
// toggles, in the order of Settings' AnimationToggles. Combat only.
inline constexpr std::array<const char*, 6> AnimationToggleLabels{
    "Sans effets des sorts",
    "Lancers de sorts raccourcis",
    "Sans animations d’attaque et de sort",
    "Sans animations de mort",
    "Déplacements instantanés",
    "Sans attentes visuelles"
};
// Short help of each context-menu checkbox (the options rows say more).
inline constexpr std::array<const char*, 6> AnimationToggleHelps{
    "En combat : sans projectiles ni effets visuels des sorts, comme l’option du jeu, sans la modifier.",
    "En combat : les personnages joueurs prennent l’animation de lancer réduite du jeu.",
    "En combat : les combattants ne jouent plus leurs animations d’attaque, de sort ni de coup reçu. Expérimental.",
    "En combat : un combattant vaincu disparaît sans son animation de mort, sauf avec Déplacements instantanés ou Sans attentes visuelles. Expérimental.",
    "En combat : marche, poussée, lancer et saut arrivent aussitôt à destination. Expérimental.",
    "En combat : le combat n’attend plus certaines fins d’animation ni pauses visuelles. Les délais du serveur restent. Expérimental."
};
