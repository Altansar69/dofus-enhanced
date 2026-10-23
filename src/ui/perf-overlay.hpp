#pragma once
// « Performances » (option perfOverlay): a discreet overlay in a corner of the
// screen (perfOverlayCorner), or where the mouse dragged it (perfOverlayDrag,
// perfOverlayX/Y), with the frame rate and frame times of the last second,
// the memory the game uses (working set, private bytes, managed heap), its CPU
// share and, read about once a second by a worker thread, its share of the
// graphics card (perfOverlayGpu) and the activity of its disk
// (perfOverlayDisk). Off: nothing created, nothing measured.
void InstallPerfOverlay();                      // worker thread, before the hooks; never throws
void TickPerfOverlay();                         // main-thread pump, every 200 ms
void PerfOverlayFrame(unsigned long long now);  // every frame of the main compositor: one counter read when on, the left button too with the mouse placement
void RestorePerfOverlay();                      // main thread: at Stop, then while the cleanup is pending
bool PerfOverlayCleanupPending();
