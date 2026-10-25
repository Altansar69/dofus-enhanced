#pragma once
// « Taille du curseur » (1.17.1): how Unity's Windows player of the supported
// client makes a Windows cursor of a texture. UnityPlayer.dll of 3.7.12.12
// (sha256 bef601ad70832ed45da76262346d90bd8136d97f2d338db743f02d9b2f314712),
// read in a copy of it, never in the game: not GameAssembly, so outside
// signatures.hpp, its checked site below instead. Pure code: no game access,
// tested on its own (tests/ui/custom-cursor-tests.cpp).
//
// Cursor.SetCursor's icall (SetCursor_Injected, 0xB0D50) keeps one Windows
// cursor per texture and hotspot, made once (0xA55F10):
// - CursorMode.Auto (the game's, the mod's): the texture written at its own
//   size to a temporary .cur file (0xA54580), then loaded by the player's only
//   LoadImageW call (0xA55A15, through its import at 0x1C3EAD0):
//       LoadImageW(nullptr, file, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE)
//   A size of 0 with LR_DEFAULTSIZE makes Windows resize it to its own cursor
//   size (SM_CXCURSOR × SM_CYCURSOR: 32 × 32 at 100 %), whatever the texture's:
//   the 48 and 64 px cursors of 1.17.0 were shown at 32. Measured offline with
//   that exact call: cursor files of 48 to 128 px all load at 32 × 32, their
//   hotspot scaled down; given their size instead, they load at it, their
//   hotspot unchanged. The player never asks
//   for that size itself (no GetSystemMetrics of SM_CXCURSOR or SM_CYCURSOR,
//   no CreateIconIndirect, CreateIconFromResourceEx or CopyImage import).
//   Then ::SetCursor of that cursor at once (0xB10C1), and again at every
//   WM_SETCURSOR and WM_MOUSEMOVE while the game has the focus (0x7FB58F),
//   and when it gets it back (0x7F5D7F).
// - CursorMode.ForceSoftware: no Windows cursor (0xA55F3A): the player hides
//   Windows' (::SetCursor(nullptr) at the same places, over its title bar and
//   borders too) and draws the texture at its own size in each rendered frame,
//   at the mouse position minus the hotspot (0xA54370, after IMGUI, from the
//   end of the frame or the render pipeline's UI overlay). It would follow the
//   frame rate, stop while the game hitches or loads a map, leave no cursor
//   over the window's title bar and borders, and show two cursors or none for
//   a frame at each switch with the game's own icons (Windows cursors): the
//   mod never uses it.
// The mod keeps Windows cursors (CursorMode.Auto) and has the player load its
// own ones at their size: the only change is that load's size, while the
// mod's cursor goes on to Unity (custom-cursor.cpp, LoadImageHook).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace customCursor::player {
// Checked before the mod changes anything there: from the start of that
// load's arguments to the cursor it keeps.
//   mov r15, rdx                  the file name
//   xor r13d, r13d                0, the cursor height (cy) below
//   mov r12, rcx; mov [rcx], r13; mov [rcx+8], r13d; movsd xmm0, [r8]; movsd [rcx+0xC], xmm0
//                                 the result: no cursor yet, the hotspot
//   cmp byte [rdx+0x1F], 0x40; jb; mov rdx, [rdx]   the name's characters
//   mov dword [rsp+0x28], 0x50    LR_LOADFROMFILE | LR_DEFAULTSIZE
//   xor r9d, r9d                  0, the width (cx)
//   xor ecx, ecx                  no module
//   mov [rsp+0x20], r13d          0, the height (cy)
//   mov r8d, 2                    IMAGE_CURSOR
//   call [rip+0x11E90B5]          LoadImageW, through its import
//   mov [r12], rax                the cursor kept
inline constexpr uintptr_t LoadSite = 0xA559DA;
inline constexpr std::array<unsigned char, 69> LoadBytes{
    0x4C,0x8B,0xFA,0x45,0x33,0xED,0x4C,0x8B,0xE1,0x4C,0x89,0x29,0x44,0x89,0x69,0x08,0xF2,0x41,0x0F,0x10,0x00,0xF2,0x0F,
    0x11,0x41,0x0C,0x80,0x7A,0x1F,0x40,0x72,0x03,0x48,0x8B,0x12,0xC7,0x44,0x24,0x28,0x50,0x00,0x00,0x00,0x45,0x33,0xC9,
    0x33,0xC9,0x44,0x89,0x6C,0x24,0x20,0x41,0xB8,0x02,0x00,0x00,0x00,0xFF,0x15,0xB5,0x90,0x1E,0x01,0x49,0x89,0x04,0x24};
inline bool SiteMatches(const unsigned char* code) { return code && std::equal(LoadBytes.begin(), LoadBytes.end(), code); }
// The call's import slot (USER32.dll!LoadImageW): the end of the call, at
// LoadSite + 65, plus its displacement (LoadBytes 61 to 64).
inline constexpr uintptr_t LoadImport = 0x1C3EAD0;
inline constexpr size_t LoadCallEnd = 65;
static_assert(LoadBytes[LoadCallEnd - 6] == 0xFF && LoadBytes[LoadCallEnd - 5] == 0x15 &&
    LoadSite + LoadCallEnd + (static_cast<uintptr_t>(LoadBytes[LoadCallEnd - 4]) | static_cast<uintptr_t>(LoadBytes[LoadCallEnd - 3]) << 8 |
        static_cast<uintptr_t>(LoadBytes[LoadCallEnd - 2]) << 16 | static_cast<uintptr_t>(LoadBytes[LoadCallEnd - 1]) << 24) == LoadImport,
    "The checked call goes through LoadImageW's import");
// Win32's IMAGE_CURSOR, LR_LOADFROMFILE and LR_DEFAULTSIZE.
inline constexpr unsigned ImageCursor = 2, LoadFromFile = 0x10, DefaultSize = 0x40;
// The player's load of a new cursor file at Windows' size, exactly as it
// makes it: the only load the mod changes.
inline bool PlayerLoad(const void* instance, unsigned type, int cx, int cy, unsigned flags) {
    return !instance && type == ImageCursor && cx == 0 && cy == 0 && flags == (LoadFromFile | DefaultSize);
}
}
