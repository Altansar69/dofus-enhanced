#pragma once

void InstallMonsterGlow();
void TickMonsterGlow();
void RestoreMonsterGlow();
bool MonsterGlowCleanupPending();
// End-of-frame glow tick (the engine's UI Toolkit repaint step): only for the
// on-screen repaint, never re-entered.
inline bool RunGlowFrame(bool onlyOffscreen, bool reentered) { return !onlyOffscreen && !reentered; }
