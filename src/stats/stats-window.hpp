#pragma once
// « Statistiques » (1.16.0): the statistics of stats.hpp in their own native
// window, built like the notebook's (MagneticDragWindow with its header, ⋯
// menu, close button and resizers): what is shown (« Ce personnage » or « Tous
// mes personnages », the game's tabs), the sections in two or three columns of
// label / value lines that follow the store once a second while the window is
// open, and the reset at the bottom. Opened, brought to the front or closed by
// its top-bar button (settings.statsButton), its close button, its ⋯ menu and
// the options' « Voir mes statistiques ». Main thread.
void InstallStatsWindow();
// The options' button: opens the window, or brings it to the front.
void OpenStatsWindow();
void TickStatsWindow();
// Closes the window and lets go of every native part it holds (the window's
// close, a logout, Stop); retried while CleanupPending.
void RestoreStatsWindow();
bool StatsWindowCleanupPending();
// Tests: the clock of the refresh and of the reset's delays, in milliseconds
// (GetTickCount64 for nullptr).
void UseStatsWindowClock(unsigned long long (*now)());
