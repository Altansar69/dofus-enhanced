#pragma once
#include "core/runtime.hpp"
// « Bouton de trajet sur les membres du groupe » (Settings::partyTravelButton,
// 1.19.0): a travel button on the party slot of every other accepted member.
void InstallPartyTravelWidget(); // never throws: PARTY_TRAVEL_WIDGET_UNAVAILABLE disables only this
void TickPartyTravelWidget();
void RestorePartyTravelWidget();
bool PartyTravelWidgetCleanupPending();
bool HandlePartyTravelWidgetToggle(managed::Obj owner);
