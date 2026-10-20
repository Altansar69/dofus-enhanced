#pragma once
#include "runtime.hpp"

namespace soul {
enum class CombatState { Hidden, Pending, Ready, Unsupported, Fighting };
struct CombatSnapshot {
    CombatState state = CombatState::Hidden;
    uint64_t fightIdentity = 0;
    int highestLevel = 0;
};
void InstallCombat();
void ResetCombat();
CombatSnapshot ReadCombat(managed::Obj fight, managed::Obj cached);
}
