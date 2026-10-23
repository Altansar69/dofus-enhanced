#pragma once
#include "glow/monster-glow-entities.hpp"

namespace monsterGlow {
void InstallGlowInteractives();
// Current-map visible objects with enabled native skills; no interaction is sent.
Snapshot ReadGlowInteractives();
}
