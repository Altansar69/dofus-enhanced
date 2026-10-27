#pragma once
// The game left the world: the account options came unbound (Module::Pump,
// ACCOUNT_OPTIONS_UNBOUND), on an inactivity disconnection, a logout, a
// character selection or an automatic reconnection. Every window and overlay
// of the mod leaves the screen as the game's own windows do; what each keeps
// (settings, positions, notebook entries and an unsaved draft) stays. Called at
// every pump while unbound: each step costs nothing once closed, and retries a
// cleanup that failed.
void CloseWindowsOffWorld();
