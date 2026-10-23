#pragma once
#include <cstdint>

void InstallDisplayShortcut();
void TickDisplayShortcut(uint64_t nowMs = 0);
void RestoreDisplayShortcut();
