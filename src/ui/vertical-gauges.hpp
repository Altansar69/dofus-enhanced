#pragma once
#include "core/runtime.hpp"
#include <cstdint>

// « Afficher à la verticale » (1.18.0): a native checkbox row in the « … » menu
// of each of the game's gauges (the character's experience, the guild's, the
// honour points, the pods, the energy points, each job's experience) turns
// that gauge vertical; settings.gaugeVertical keeps the gauges ticked,
// settings.gaugeVerticalMenu (« Jauges » in the options) the row itself, and
// (1.18.1) settings.gaugeVerticalBoxes each vertical gauge's own length and
// place, chosen with the mouse and never saved in the game's layout.
void InstallVerticalGauges();            // worker thread, before the hooks are enabled; never throws
void TickVerticalGauges();               // Module::Pump, every 200 ms
void VerticalGaugesFrame(uint64_t now);  // UpdateHook, each compositor frame while the services are ready
void RestoreVerticalGauges();            // Module::Stop, then each stopping frame while cleanup is pending
bool VerticalGaugesCleanupPending();
// ClickCallbackHandler.InvokeCallback (context-display's detour): true for the
// row's own handler, acted on or not; false for any other.
bool HandleVerticalGaugeMenuCallback(managed::Obj callback, managed::Obj view);
