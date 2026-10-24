#pragma once
#include "core/settings.hpp"
#include <array>

inline constexpr std::array<const char*, 4> GlowModeLabels{
    "Désactivée", "Derrière l’interface", "Derrière l’interface et les murs", "Tout le temps"
};
// The « Surbrillance » categories as the options block and the map context
// menu show them, in the order of Settings' GlowCategoryToggles.
inline constexpr std::array<const char*, 6> GlowCategoryLabels{
    "Monstres", "PNJ", "Éléments interactifs", "Moi", "Membres du groupe", "Autres joueurs"
};
// Help of each context-menu checkbox (the options rows add their default).
inline constexpr std::array<const char*, 6> GlowCategoryHelps{
    "Hors combat : les groupes de monstres visibles ont un contour, au niveau de surbrillance choisi. Décoché, ils n’en ont plus.",
    "Hors combat : les personnages non joueurs (PNJ) visibles ont un contour, au niveau de surbrillance choisi.",
    "Hors combat : les objets visibles avec une action disponible (ressources, portes, zaaps…) ont un contour, au niveau de surbrillance choisi.",
    "Hors combat : ton personnage a un contour, au niveau de surbrillance choisi.",
    "Hors combat : les personnages de ton groupe ont un contour, au niveau de surbrillance choisi.",
    "Hors combat : les personnages des autres joueurs, hors de ton groupe, ont un contour, au niveau de surbrillance choisi."
};
static_assert(GlowCategoryLabels.size() == std::size(GlowCategoryToggles) && GlowCategoryHelps.size() == GlowCategoryLabels.size());
// The ticked categories, one bit per GlowCategoryToggles entry (bit 0: Monstres).
inline unsigned GlowCategoryMask(const Settings& settings) {
    unsigned mask = 0;
    for (size_t index = 0; index < std::size(GlowCategoryToggles); ++index)
        if (settings.*GlowCategoryToggles[index]) mask |= 1u << index;
    return mask;
}
