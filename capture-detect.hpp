#pragma once
#include "metamob-sync.hpp"
#include <functional>
// Reads the game's own fight-end report (no hook): the local character's loot
// lists one soul item per captured boss (item type 327) or archimonster (328).
// Marketplace purchases, trades, bank and mail never produce such a report.
void InstallCaptureDetect(std::function<void(metamob::Capture)> sink);
void TickCaptureDetect();
void RestoreCaptureDetect();
