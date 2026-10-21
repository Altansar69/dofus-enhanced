#pragma once
#include <string>

namespace treasure {
// Current step of the player's treasure hunt, read from the game's own hunt
// state (classic first, then legendary, then portal). Direction follows the
// game's Helpers.Direction and DofusDB: 0 east, 2 south, 4 west, 6 north.
// Only read access; no hunt message, validation or dig is ever sent.
struct Step {
    bool active = false;
    int index = 0, type = 0, direction = -1;
    unsigned poiId = 0, npcId = 0, mapCount = 0;
    std::string hint;
    bool operator==(const Step&) const = default;
};
void InstallHunt();
Step CurrentStep();
}
