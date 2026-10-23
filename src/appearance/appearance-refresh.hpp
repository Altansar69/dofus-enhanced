#pragma once

// Main-thread native appearance refresh, independent of the creature bool's
// event deduplication. Call after the display cache epoch and native mode update.
void InstallCombatAppearanceRefresh();
void RefreshCombatAppearance();
