#pragma once
#include "core/settings.hpp"
#include <algorithm>
#include <cmath>

// « Afficher à la verticale » (1.18.0): the rules of a vertical gauge, without
// the game (vertical-gauges-tests covers them).
namespace verticalGauges {
// A gauge's own type (fqd+GaugesType, WidgetProgressGaugeUi.m_gaugeType):
// exactly one of the bits Settings::GaugeVerticalBits holds, the character's
// experience, the guild's, the honour points, the pods, the energy points or
// one job's experience. 4 is no gauge (the game's names keep a « GaugeMount »).
inline bool ValidType(int type) {
    return type > 0 && (type & ~Settings::GaugeVerticalBits) == 0 && (type & (type - 1)) == 0;
}
// A vertical gauge is Thickness wide (panel pixels: it follows the interface
// scale) and as long as the gauge's horizontal width, between MinLength and
// the screen's height less ScreenMargin. The widget's UXML gives it 566 x 34
// when the game wrote no size of its own.
constexpr float Thickness = 48, MinLength = 160, ScreenMargin = 16, UxmlWidth = 566, UxmlHeight = 34;
// The length of a gauge `width` wide (UxmlWidth when unknown) on a screen
// `screenHeight` high (not bounded by it while unknown: 0 or less).
inline float Length(float width, float screenHeight) {
    if (!std::isfinite(width) || width <= 0) width = UxmlWidth;
    if (std::isfinite(screenHeight) && screenHeight > 0) width = std::min(width, screenHeight - ScreenMargin);
    return std::max(MinLength, width);
}
struct Box { float x = 0, y = 0, width = 0, height = 0; };
// The box moved only as much as it needs to lie entirely on a screen
// `width` x `height` (its top-left corner on it when it is larger), as the
// game's own placement keeps its windows (goz`1.blxl): its top-left corner
// stays where it is whenever it can. Unchanged while the screen is unknown.
inline Box KeepOnScreen(Box box, float width, float height) {
    if (!(width > 0) || !(height > 0) || !std::isfinite(width) || !std::isfinite(height)) return box;
    box.x = std::max(0.f, std::min(box.x, width - box.width));
    box.y = std::max(0.f, std::min(box.y, height - box.height));
    return box;
}
// The place the game's own placements give a window `width` x `height` on a
// screen `screenWidth` x `screenHeight` (goz`1.blxe): its saved place, `x`
// and `y` per-mille of the screen (pq.paf), or flush with the screen's right
// or bottom edge (blxh), then kept on the screen (blxl).
inline Box SavedPlace(float x, float y, bool right, bool bottom, float width, float height, float screenWidth, float screenHeight) {
    const Box box{right ? screenWidth - width : x / 1000.f * screenWidth, bottom ? screenHeight - height : y / 1000.f * screenHeight,
        width, height};
    return KeepOnScreen(box, screenWidth, screenHeight);
}
}
