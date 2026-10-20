#pragma once
#include "monster-glow-entities.hpp"

namespace monsterGlow {
void InstallGlowInteractives();
// Current-map visible objects with enabled native skills; no interaction is sent.
Snapshot ReadGlowInteractives();
}
