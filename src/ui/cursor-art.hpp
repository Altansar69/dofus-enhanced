#pragma once
// « Curseur Dofus » (1.17.0): the mod's own cursors, drawn at run time from
// signed-distance shapes on a 32-unit grid, anti-aliased at any size. Pure
// code: no game access, tested on its own (tests/ui/cursor-art-tests.cpp).
#include <cstdint>
#include <vector>

namespace cursorArt {
// The shapes, in this order (the runtime builds one texture per shape).
enum class Shape : uint8_t {
    Arrow, Hand, Beam, Open, Closed, ResizeEW, ResizeNS, ResizeNWSE, ResizeNESW,
    ArrowForbidden, HandForbidden, ArrowBusy, HandBusy
};
inline constexpr int ShapeCount = 13;
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
// size in pixels, clamped to MinSize..MaxSize.
Image Render(Shape shape, const Palette& palette, int size);
}
