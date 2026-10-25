#pragma once
// « Curseur Dofus » (1.17.0): which of the mod's cursors stands in for the
// cursor the game asks for, and the size they are drawn at. Pure code: no game
// access, tested on its own (tests/ui/custom-cursor-tests.cpp).
#include "ui/cursor-art.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace customCursor {
using cursorArt::Shape;
// Core.UILogic.CursorType (Int64 flags, pinned snapshot): the game's cursors
// the mod draws its own way. Its icons (Gear 1 … Teleport 0x800, Sword, Bubble,
// the fight markers Ping … Three, Diving, Magnifier) keep their art.
namespace cursorType {
inline constexpr int64_t NotFound = -2, None = -1, Forbidden = 0x4000, Beam = 0x8000, Move = 0x10000,
    ResizeEW = 0x20000, ResizeNESW = 0x40000, ResizeNS = 0x80000, ResizeNWSE = 0x100000, Link = 0x200000;
}
// Core.UILogic.CursorStateEnum bits: the game's cross (Disabled) or hourglass
// (Wait) over its cursor.
inline constexpr int32_t StateDisabled = 1, StateWait = 2;
// Who asks: DofusCursor (ApplyCursor, the request it stored in
// m_currentCursor), or another caller of UnityEngine.Cursor.SetCursor (UI
// Toolkit's CursorManager).
enum class Origin : uint8_t { Other, ApplyCursor };
struct Request {
    Origin origin = Origin::Other;
    bool textureNull = true; // no texture: the Windows arrow
    int32_t layer = 0;       // CursorLayerEnum: 0 Map, 1 Interactive, 2 Frustum, 3 UI
    int64_t type = cursorType::None;
    int32_t state = 0;
};
// What DofusCursor does not tell: an item dragged (DragAndDropService), the
// primary button held (read for Move only: a window moved) and an enabled text
// field under the pointer (read for the arrow only: the game marks two kinds
// of inputs, not the chat nor the searches).
struct Hover {
    bool dragging = false, button = false, overText = false;
    bool operator==(const Hover&) const = default;
};
// The request asks for the plain arrow: the game's None, or no texture from
// another caller (UI Toolkit's reset).
inline bool AsksArrow(const Request& r) {
    return r.origin == Origin::ApplyCursor ? r.type == cursorType::None : r.textureNull;
}
inline bool AsksMove(const Request& r) { return r.origin == Origin::ApplyCursor && r.type == cursorType::Move; }

// The mod's cursor for a request, or none: the call goes on unchanged.
// - an item dragged: the closed hand, whatever is asked;
// - DofusCursor: the arrow (the text cursor over a text field), the hand of
//   Link on every layer (buttons, tabs, links, map borders, the cells the game
//   marks), the text cursor, the open hand of Move (closed while the button is
//   held), the four resize arrows, Forbidden; its Disabled and Wait states as a
//   badge on the arrow or the hand;
// - the game's icons (sword, bubble, tools, fight markers, NotFound…) and
//   unknown types keep the game's texture; without one (the game could not load
//   it), the Windows arrow would show: the mod's arrow stands in;
// - another caller: its texture stays, its reset to no texture is the arrow.
inline std::optional<Shape> Choose(const Request& r, const Hover& h) {
    if (h.dragging) return Shape::Closed;
    if (r.origin != Origin::ApplyCursor) {
        if (!r.textureNull) return std::nullopt;
        return h.overText ? Shape::Beam : Shape::Arrow;
    }
    Shape base = Shape::Arrow;
    switch (r.type) {
    case cursorType::None: base = h.overText ? Shape::Beam : Shape::Arrow; break;
    case cursorType::Link: base = Shape::Hand; break;
    case cursorType::Beam: base = Shape::Beam; break;
    case cursorType::Move: base = h.button ? Shape::Closed : Shape::Open; break;
    case cursorType::ResizeEW: return Shape::ResizeEW;
    case cursorType::ResizeNS: return Shape::ResizeNS;
    case cursorType::ResizeNWSE: return Shape::ResizeNWSE;
    case cursorType::ResizeNESW: return Shape::ResizeNESW;
    case cursorType::Forbidden: return Shape::ArrowForbidden;
    default:
        if (!r.textureNull) return std::nullopt;
        base = Shape::Arrow;
    }
    if (r.state & StateDisabled) return base == Shape::Hand ? Shape::HandForbidden : Shape::ArrowForbidden;
    if (r.state & StateWait) return base == Shape::Hand ? Shape::HandBusy : Shape::ArrowBusy;
    return base;
}

// « Taille du curseur »: 0 the Windows pointer's, 1 half again, 2 twice.
inline constexpr int SizeChoices = 3;
// The cursors' size in pixels: the Windows pointer's base size
// (CursorBaseSize, 32 unless enlarged in Windows' settings; anything outside
// 32..256 counts as 32) at the game window's scale (dpi / 96, 96 when
// unknown), times the size choice, within cursorArt's 32..128.
inline int CursorPixels(unsigned dpi, unsigned windowsBase, int sizeChoice) {
    if (dpi == 0) dpi = 96;
    if (windowsBase < 32 || windowsBase > 256) windowsBase = 32;
    constexpr double Factors[SizeChoices] = {1.0, 1.5, 2.0};
    const double factor = sizeChoice >= 0 && sizeChoice < SizeChoices ? Factors[sizeChoice] : 1.0;
    const double pixels = std::round(static_cast<double>(windowsBase) * static_cast<double>(dpi) / 96.0 * factor);
    return static_cast<int>(std::clamp(pixels, static_cast<double>(cursorArt::MinSize), static_cast<double>(cursorArt::MaxSize)));
}

// A theme's colours until they are read, or when they cannot be: the Standard
// theme's accent (--highlight_primary100) and dark background
// (--background_dark97), 0xRRGGBB.
inline constexpr uint32_t StandardAccent = 0xF0FF4F, StandardDark = 0x1B1D32;
// A USS colour (channels from 0 to 1) as 0xRRGGBB, its alpha left out.
inline uint32_t RgbOf(float red, float green, float blue) {
    const auto channel = [](float value) {
        return static_cast<uint32_t>(std::lround(std::clamp(static_cast<double>(value), 0.0, 1.0) * 255.0));
    };
    return channel(red) << 16 | channel(green) << 8 | channel(blue);
}
}
