#pragma once
// « Performances »: a discreet overlay, shown as soon as one of its three
// parts is ticked and with those parts only: the frame rate and frame times of
// the last second, the memory the game uses (working set, private bytes,
// managed heap) and its CPU share (perfOverlay); its share of the graphics
// card (perfOverlayGpu) and the activity of its disk (perfOverlayDisk), read
// about once a second by a worker thread. Its top-bar button (perfOverlayButton)
// hides it and shows it again without unticking its parts (perfOverlayShown).
// In its corner (perfOverlayCorner, which only an earlier revision chose: the
// top left otherwise) until the mouse drags it (perfOverlayDrag) to a place
// kept (perfOverlayX/Y), or resizes it as a window to sizes kept
// (perfOverlayScaleX/Y): its right edge its width, its bottom edge its height,
// its bottom-right corner both, where a grip shows while the cursor is over
// it; its text at the largest size its box holds. Over it, the game's cursor
// manager shows the double arrow of the handle under the cursor, or the move
// cursor. With perfOverlayDrag off it stays locked there.
// Only what is shown is measured; nothing shown: nothing created, nothing
// measured.
void InstallPerfOverlay();                      // worker thread, before the hooks, with its top-bar button; never throws
void TickPerfOverlay();                         // main-thread pump, every 200 ms
void PerfOverlayFrame(unsigned long long now);  // every frame of the main compositor: one counter read with the frame rate shown, the left button too with the mouse placement and resize (the cursor every 50 ms: its grip and the game's cursor)
void RestorePerfOverlay();                      // main thread: at Stop, then while the cleanup is pending
bool PerfOverlayCleanupPending();
