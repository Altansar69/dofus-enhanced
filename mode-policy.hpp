#pragma once
#include "settings.hpp"
#include <cstdint>

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

// Whose appearance Miniatures converts, one group per « Apparence » checkbox.
// In combat: the local character's fighter, the other players of its team,
// their summons, its own summons, the opposing fighters and their summons.
// Out of combat: the local character, the members of its party, the other
// players and the monster groups. Unknown (a fighter of an unknown or unsupported
// team, an allied non-summoned monster) keeps its native appearance, as before
// these checkboxes; RoleplayOther (NPCs, merchants…) always follows the mode.
enum class MiniatureGroup : int {
    Self, AlliedPlayer, AlliedSummon, OwnSummon, Enemy, EnemySummon,
    RoleplaySelf, PartyMember, OtherPlayer, Monster, RoleplayOther,
    Unknown
};
// self: the fighter is the local character's own (same ID); ownSummon: its
// summoner is the local character. Both only count within the local team.
inline MiniatureGroup ClassifyMiniature(bool player, bool monster, bool summon, int team, int localTeam,
                                        bool self = false, bool ownSummon = false) {
    if (team < 0 || team > 3 || localTeam < 0 || localTeam > 3) return MiniatureGroup::Unknown;
    const bool ally = team == localTeam;
    if (summon) return !ally ? MiniatureGroup::EnemySummon : ownSummon ? MiniatureGroup::OwnSummon : MiniatureGroup::AlliedSummon;
    if (player) return !ally ? MiniatureGroup::Enemy : self ? MiniatureGroup::Self : MiniatureGroup::AlliedPlayer;
    if (monster && !ally) return MiniatureGroup::Enemy;
    return MiniatureGroup::Unknown;
}
inline MiniatureGroup ClassifyRoleplayMiniature(bool player, bool monster, bool self, bool partyMember) {
    if (self) return MiniatureGroup::RoleplaySelf;
    if (player) return partyMember ? MiniatureGroup::PartyMember : MiniatureGroup::OtherPlayer;
    if (monster) return MiniatureGroup::Monster;
    return MiniatureGroup::RoleplayOther;
}
// Whether Miniatures reduces this group: its own checkbox. Créatures and
// Normal never read these choices.
inline bool MiniatureGroupEnabled(const Settings& settings, MiniatureGroup group) {
    switch (group) {
    case MiniatureGroup::Self: return settings.miniatureSelf;
    case MiniatureGroup::AlliedPlayer: return settings.miniatureAllies;
    case MiniatureGroup::AlliedSummon: return settings.miniatureAllySummons;
    case MiniatureGroup::OwnSummon: return settings.miniatureOwnSummons;
    case MiniatureGroup::Enemy: return settings.miniatureEnemies;
    case MiniatureGroup::EnemySummon: return settings.miniatureEnemySummons;
    case MiniatureGroup::RoleplaySelf: return settings.roleplayMiniatureSelf;
    case MiniatureGroup::PartyMember: return settings.roleplayMiniatureParty;
    case MiniatureGroup::OtherPlayer: return settings.roleplayMiniaturePlayers;
    case MiniatureGroup::Monster: return settings.roleplayMiniatureMonsters;
    case MiniatureGroup::RoleplayOther: return true;
    case MiniatureGroup::Unknown: return false;
    }
    return false;
}
// The local identity is only read when it can change an appearance, so that
// the default choices (every box ticked) add no native read: in combat, the
// local fighter's own ID when « Moi » and « Joueurs alliés » differ, a summon's
// summoner when « Mes invocations » and « Invocations alliées » differ; out of
// combat, the local character and its party once « Moi », « Membres du groupe »
// or « Autres joueurs » is unticked (every other entity is then still reduced).
inline bool CombatSelfMatters(const Settings& settings) {
    return settings.miniatureSelf != settings.miniatureAllies;
}
inline bool CombatOwnSummonMatters(const Settings& settings) {
    return settings.miniatureOwnSummons != settings.miniatureAllySummons;
}
inline bool RoleplayIdentityMatters(const Settings& settings) {
    return !(settings.roleplayMiniatureSelf && settings.roleplayMiniatureParty && settings.roleplayMiniaturePlayers);
}
// Every category checkbox, one bit each: a change invalidates cached looks.
inline unsigned MiniatureCategoryMask(const Settings& settings) {
    unsigned mask = 0, bit = 1;
    for (auto field : CombatMiniatureToggles) { if (settings.*field) mask |= bit; bit <<= 1; }
    for (auto field : RoleplayMiniatureToggles) { if (settings.*field) mask |= bit; bit <<= 1; }
    return mask;
}

// « Animations » (combat only). The native reads of the account option
// DofusOptionsContainer.allowSpellEffects the mod may answer false for, by
// their return address in the pinned build: FightSequence.biwv then leaves the
// spell projectile/effect steps (gqa, gpz, gpw, gqb) out of the sequence;
// dyq.uus and dzj.uxg pick the game's reduced cast animation for a caster with
// a positive (player) ID. Every other read keeps the account value.
enum class SpellEffectsRead { Other, EffectSteps, CasterAnimation };
inline SpellEffectsRead ClassifySpellEffectsRead(uintptr_t callerRva) {
    switch (callerRva) {
    case 0x1ECE82C: return SpellEffectsRead::EffectSteps;     // FightSequence.biwv
    case 0x272E250:                                            // dyq.uus
    case 0x2744303: return SpellEffectsRead::CasterAnimation; // dzj.uxg
    default: return SpellEffectsRead::Other;
    }
}
inline bool OverrideSpellEffectsRead(const Settings& settings, uintptr_t callerRva) {
    switch (ClassifySpellEffectsRead(callerRva)) {
    case SpellEffectsRead::EffectSteps: return settings.noSpellEffects;
    case SpellEffectsRead::CasterAnimation: return settings.shortSpellCasts;
    case SpellEffectsRead::Other: return false;
    }
    return false;
}
// Provenance of a fighter animation step (gpy), recorded by its constructor
// hook: the death step built by dwp.uia, any other step, or a step built before
// the hooks were installed (Unknown, possibly a death).
enum class AnimationStepKind { Ordinary, Death, Unknown };
// Instant movements or waits keep the death step native: its own end event
// (Animated+0x238) makes dwp.uko dispose of the dead fighter's rendered entity,
// which base completion does not emit, and those instant paths were verified
// with the native death only. Without them a death is skipped like before 1.12.0.
inline bool InstantCombatVisuals(const Settings& settings) {
    return settings.instantMovements || settings.noVisualWaits;
}
inline bool SkipFighterAnimation(const Settings& settings, AnimationStepKind kind) {
    switch (kind) {
    case AnimationStepKind::Ordinary: return settings.noAttackAnimations;
    case AnimationStepKind::Death: return settings.noDeathAnimations && !InstantCombatVisuals(settings);
    case AnimationStepKind::Unknown:
        return settings.noAttackAnimations && settings.noDeathAnimations && !InstantCombatVisuals(settings);
    }
    return false;
}
// Whether any fighter step may be skipped at all (the hook's cheap first gate).
inline bool AnyFighterAnimationSkipped(const Settings& settings) {
    return settings.noAttackAnimations || (settings.noDeathAnimations && !InstantCombatVisuals(settings));
}
