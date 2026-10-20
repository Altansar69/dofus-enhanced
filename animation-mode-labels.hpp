#pragma once
#include <array>

// Settings and the map context menu expose the same stored animation modes.
inline constexpr std::array<const char*, 4> AnimationModeLabels{
    "Normal",
    "Raccourcies",
    "Sans animations des combattants (exp\xC3\xA9" "rimental)",
    "Instantan\xC3\xA9 (exp\xC3\xA9" "rimental)"
};
