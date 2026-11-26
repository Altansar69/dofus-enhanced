#pragma once
// « Échanges Metamob » (1.22.0): the trade proposals other Metamob users sent
// to the account (metamob-sync.hpp, GET /v1/trade-proposals), in their own
// native window built like the statistics' (MagneticDragWindow with its header,
// ⋯ menu, close button and resizers): a line telling what the last reading
// found, then each open proposal, newest first (who proposes it and since
// when, where it stands, what the user would receive and give). Its buttons:
// « Actualiser » (a reading as soon as the pauses allow), « Notifications »
// (settings.metamobTradesNotify: a chat line for each new proposal, on by
// default) and « Ouvrir Metamob » (the site's messages, in the browser, where
// a proposal is answered: the mod itself never accepts, refuses nor sends one).
// Opened or closed by its top-bar button, there while a usable Metamob key is
// stored, its close button and its ⋯ menu. The proposals are read every minute
// while the notifications are on or the window is open. Main thread.
//
// Never throws: a contract of the pinned client that does not hold leaves the
// window out (METAMOB_TRADES_WINDOW_DISABLED), the chat notices still told.
void InstallTradesWindow();
void TickTradesWindow();
// Closes the window and lets go of every native part it holds (the window's
// close, a logout, Stop); retried while CleanupPending.
void RestoreTradesWindow();
bool TradesWindowCleanupPending();
