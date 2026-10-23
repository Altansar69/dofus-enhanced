#pragma once
// « Ouvertures Kralamoure dans le calendrier »: in the game's calendar, a
// « Kralamoure » entry of « Mes calendriers » (its checkbox is the option
// calendarKralamoure) and, while it is checked, native event lines with the
// Kralamoure openings planned on Metamob for the server of the user's Ocre
// quest. Inert without a usable Metamob key (no entry, no line, no request).
void InstallCalendarKralove();
void TickCalendarKralove();
void RestoreCalendarKralove();
bool CalendarKraloveCleanupPending();
