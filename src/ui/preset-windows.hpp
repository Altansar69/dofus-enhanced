#pragma once
// « Garder les fenêtres du mod dans les affichages enregistrés » (1.20.0,
// settings.presetModWindows, read at each tick): the game's saved layouts
// (« Enregistrer l'affichage ») keep the mod's windows too.
//
// When the player saves a layout, new (UiPresetOptionsContainer.AddPreset) or
// over an existing one (SavePreset), the mod writes beside it, in its own file
// (preset-windows-store.hpp, keyed by the layout's uid), its windows as they
// are: the statistics window, the notebook (and its compact version) and the
// treasure hunt panel, each open or closed with its place and size; their
// header sizes and the soul panel's, « Capture d’âme »; the performance
// overlay (shown, its parts, place and sizes); the vertical gauges and each
// one's length and place. When the player applies a layout (ApplyPreset), the
// mod puts them back: the settings through Module::Change, then each window
// opened or closed, and placed once it is built. A layout saved before 1.20.0
// (nothing kept for it) leaves the mod's windows as they are. A deleted layout
// (RemovePreset) loses its entry; a renamed one (EditPreset) keeps it.
//
// The fight layout (« Affichage de combat », the saved layout assigned with
// AssignFightPreset): the game switches to it by itself when a fight starts
// and back when it ends (get_isUsingFightPreset), without ApplyPreset. The mod
// follows it: at the start, its windows as they are kept in memory and those
// kept with the fight layout put in place; at the end, those of before the
// fight put back, when the fight changed them (the fight layout's, or a
// layout applied during the fight). As the game, which goes back to the
// layout of before the fight.
void InstallPresetWindows(); // worker thread, before the hooks are enabled; never throws
void TickPresetWindows();    // Module::Pump, after the panels' ticks
void RestorePresetWindows(); // logout and Module::Stop: nothing pending any more
