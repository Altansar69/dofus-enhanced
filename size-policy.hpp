#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

// Heights use the same authored idle-animation units. The factor is uniform on
// both axes, preserving aspect ratio and the signs of the original look scales.
inline std::optional<float> SizeFactor(int percent, int minimumPercent,
                                      bool monster, float naturalHeight, float playerHeight) {
    if (!monster || minimumPercent == 0) return percent / 100.0f;
    if (!std::isfinite(naturalHeight) || !std::isfinite(playerHeight) || naturalHeight <= 0 || playerHeight <= 0) {
        // A missing measurement postpones the player-relative floor, not the
        // proportional reduction.
        return percent / 100.0f;
    }
    float height = naturalHeight * percent / 100.0f;
    if (monster && minimumPercent > 0) height = std::max(height, playerHeight * minimumPercent / 100.0f);
    const float factor = height / naturalHeight;
    if (!std::isfinite(factor) || factor <= 0) return std::nullopt;
    return factor;
}
