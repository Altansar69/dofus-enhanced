#pragma once
// « Curseur Dofus » (1.17.0, customCursor): the mod's own cursors, drawn by
// cursorArt in Dofus' style, stand in for the Windows arrow and for the game's
// Windows-like cursors (the hand, the text cursor, the move and resize
// cursors), in the colour of customCursorColor (gold, the game theme's accent,
// emerald or silver) and at the size of customCursorSize (the Windows
// pointer's at the game window's scale, half again, or twice). One detour on
// UnityEngine.Cursor.SetCursor swaps them in within the game's own calls; the
// game's icons (sword, bubble, tools, fight markers…) stay. Off by default;
// unticked, or at Stop, the game's cursor is back at once and the textures are
// destroyed.
void InstallCustomCursor();                      // worker thread, before the hooks are enabled; never throws
void CustomCursorFrame(unsigned long long now);  // every compositor frame, the login screens included (at most every 8 ms); never throws
void RestoreCustomCursor();                      // main thread: first at Stop, then while the cleanup is pending; never throws
bool CustomCursorCleanupPending();               // a texture of the mod not destroyed yet
