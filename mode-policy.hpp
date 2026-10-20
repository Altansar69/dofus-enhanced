#pragma once
#include "settings.hpp"

// A native checkbox expresses on/off; retain which simplified appearance it enables.
inline int ModeAfterNativeToggle(int mode, int preferred, bool enabled) {
    if (!enabled) return 0;
    return mode > 0 ? mode : preferred;
}
inline int ActiveDisplayMode(const Settings& settings, bool combat) {
    return combat ? settings.mode : settings.roleplayMode;
}
inline bool NotifyCreaturePreference(bool targetCombat, bool currentlyCombat) {
    return targetCombat == currentlyCombat;
}
enum class MiniatureGroup : int { AlliedPlayer, EnemyPlayer, EnemyMonster, EnemySummon, AlliedSummon, Unknown };
inline MiniatureGroup ClassifyMiniature(bool player, bool monster, bool summon, int team, int localTeam) {
    if (team < 0 || team > 3 || localTeam < 0 || localTeam > 3) return MiniatureGroup::Unknown;
    const bool ally = team == localTeam;
    if (summon) return ally ? MiniatureGroup::AlliedSummon : MiniatureGroup::EnemySummon;
    if (player) return ally ? MiniatureGroup::AlliedPlayer : MiniatureGroup::EnemyPlayer;
    if (monster && !ally) return MiniatureGroup::EnemyMonster;
    return MiniatureGroup::Unknown;
}
