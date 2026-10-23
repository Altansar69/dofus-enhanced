#pragma once
#include <filesystem>

// Called only by the native option's explicit click callback.
void OpenTreasureOverlay(const std::filesystem::path& moduleFolder);
void InstallTreasurePanel();
void TickTreasurePanel();
void RestoreTreasurePanel();
bool TreasurePanelCleanupPending();
