#pragma once
// « Curseur Dofus » (1.17.0): the mod's own cursors, drawn at run time from
// signed-distance shapes on a 32-unit grid, anti-aliased at any size. Pure
// code: no game access, tested on its own (tests/ui/cursor-art-tests.cpp).
#include <cstdint>
#include <vector>

namespace cursorArt {
// The shapes, in this order (the runtime builds one texture per shape): the
// thirteen of 1.17.0, then (1.19.0, « Curseurs du jeu aussi ») the game's own
// icons in Icon order, the same with the forbidden badge, then with the
// hourglass (IconShape).
enum class Shape : uint8_t {
    Arrow, Hand, Beam, Open, Closed, ResizeEW, ResizeNS, ResizeNWSE, ResizeNESW,
    ArrowForbidden, HandForbidden, ArrowBusy, HandBusy
};
inline constexpr int CoreShapeCount = 13;
// The game's icons (Core.UILogic.CursorType) drawn the mod's way: the
// interactive elements' skills (1 << SkillData.cursor), the world's entities,
// the fight markers and NotFound's question mark. PingSword is the Sword.
enum class Icon : uint8_t {
    Gear, Bubble, Lock, Sword, Scythe, Axe, Hook, Book, House, Pickaxe, Shears, Teleport, Diving, Magnifier, Question,
    Ping, Shield, Heart, Target, Skull, One, Two, Three
};
inline constexpr int IconCount = 23;
// The game's states over an icon: Disabled (its cross), Wait (its hourglass).
enum class Badge : uint8_t { None, Forbidden, Busy };
inline constexpr int ShapeCount = CoreShapeCount + 3 * IconCount;
constexpr Shape IconShape(Icon icon, Badge badge = Badge::None) {
    return static_cast<Shape>(CoreShapeCount + static_cast<int>(badge) * IconCount + static_cast<int>(icon));
}
constexpr bool IsIcon(Shape shape) { return static_cast<int>(shape) >= CoreShapeCount && static_cast<int>(shape) < ShapeCount; }
// For an icon's shape only.
constexpr Icon IconOf(Shape shape) { return static_cast<Icon>((static_cast<int>(shape) - CoreShapeCount) % IconCount); }
constexpr Badge BadgeOf(Shape shape) { return static_cast<Badge>((static_cast<int>(shape) - CoreShapeCount) / IconCount); }
// The palettes: Or, a game theme's (« Défaut » and each theme of « Couleur du
// curseur », custom-cursor.cpp's ArtColour), Émeraude, Argent.
enum class Colour : uint8_t { Gold = 0, Theme = 1, Emerald = 2, Silver = 3 };
// The colours a cursor is drawn with, 0xRRGGBB.
struct Palette {
    uint32_t light = 0, mid = 0, deep = 0, outline = 0;
    uint32_t gemLight = 0, gemMid = 0, gemDeep = 0;
};
// Theme: from the game theme's accent and dark colours (0xRRGGBB, as the game
// gives them); a too dark accent is lightened. The other colours ignore both.
Palette PaletteOf(Colour colour, uint32_t themeAccent = 0, uint32_t themeDark = 0);
// RGBA32, straight alpha, rows bottom-up (the order LoadRawTextureData
// expects); the hotspot in pixels from the top-left.
struct Image {
    int size = 0, hotX = 0, hotY = 0;
    std::vector<uint8_t> rgba;
};
inline constexpr int MinSize = 32, MaxSize = 128;
// size in pixels, clamped to MinSize..MaxSize; a value past ShapeCount is the arrow.
Image Render(Shape shape, const Palette& palette, int size);
}
