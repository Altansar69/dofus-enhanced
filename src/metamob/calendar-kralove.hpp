#pragma once
// « Ouvertures Kralamoure dans le calendrier »: a mod-owned row under the day
// cards of the game's calendar with the Kralamoure openings planned on Metamob
// for the server of the user's Ocre quest. Option calendarKralamoure: inert
// when it is off or without a usable Metamob key (no row, no request).
void InstallCalendarKralove();
void TickCalendarKralove();
void RestoreCalendarKralove();
bool CalendarKraloveCleanupPending();
