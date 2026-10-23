#pragma once
#include "core/settings.hpp"
#include <array>

// The « Apparence » categories as the map context menu shows them under
// « Affichage », with the titles of their options rows, in the order of
// Settings' CombatMiniatureToggles and RoleplayMiniatureToggles.
inline constexpr std::array<const char*, 6> CombatMiniatureLabels{
    "Moi", "Joueurs alliés", "Invocations alliées", "Mes invocations", "Ennemis", "Invocations ennemies"
};
inline constexpr std::array<const char*, 5> RoleplayMiniatureLabels{
    "Moi", "Membres du groupe", "Autres joueurs", "Monstres", "PNJ"
};
// Help of each context-menu checkbox (the options rows add their default).
inline constexpr std::array<const char*, 6> CombatMiniatureHelps{
    "En combat, avec Miniatures : ton personnage est réduit. Décoché, il garde sa taille normale.",
    "En combat, avec Miniatures : les autres personnages de ton équipe sont réduits. Décoché, ils gardent leur taille normale.",
    "En combat, avec Miniatures : les invocations des autres personnages de ton équipe sont réduites. Décoché, elles gardent leur taille normale.",
    "En combat, avec Miniatures : les invocations de ton personnage sont réduites. Décoché, elles gardent leur taille normale.",
    "En combat, avec Miniatures : les monstres et personnages de l’équipe adverse sont réduits. Décoché, ils gardent leur taille normale.",
    "En combat, avec Miniatures : les invocations de l’équipe adverse sont réduites. Décoché, elles gardent leur taille normale."
};
inline constexpr std::array<const char*, 5> RoleplayMiniatureHelps{
    "Hors combat, avec Miniatures : ton personnage est réduit. Décoché, il garde sa taille normale.",
    "Hors combat, avec Miniatures : les personnages de ton groupe sont réduits. Décoché, ils gardent leur taille normale.",
    "Hors combat, avec Miniatures : les personnages des autres joueurs, hors de ton groupe, sont réduits. Décoché, ils gardent leur taille normale.",
    "Hors combat, avec Miniatures : les groupes de monstres sont réduits. Décoché, ils gardent leur taille normale.",
    "Hors combat, avec Miniatures : les personnages non joueurs (PNJ) sont réduits. Décoché, ils gardent leur taille normale."
};
static_assert(CombatMiniatureLabels.size() == std::size(CombatMiniatureToggles) && CombatMiniatureHelps.size() == CombatMiniatureLabels.size() &&
    RoleplayMiniatureLabels.size() == std::size(RoleplayMiniatureToggles) && RoleplayMiniatureHelps.size() == RoleplayMiniatureLabels.size());
