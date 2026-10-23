#pragma once
// « Performances » (option perfOverlay): a discreet overlay in a corner of the
// screen (perfOverlayCorner) with the frame rate, the frame time and the memory
// the game uses. Off: nothing created, nothing measured.
void InstallPerfOverlay();
void TickPerfOverlay();                         // main-thread pump, every 200 ms
void PerfOverlayFrame(unsigned long long now);  // every frame of the main compositor
void RestorePerfOverlay();
bool PerfOverlayCleanupPending();
