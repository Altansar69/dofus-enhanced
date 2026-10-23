#pragma once
#include "core/runtime.hpp"
void InstallSpousePartyWidget();
void TickSpousePartyWidget();
void RestoreSpousePartyWidget();
bool SpousePartyWidgetCleanupPending();
bool HandleSpousePartyWidgetToggle(managed::Obj owner);
