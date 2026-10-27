#include "core/world-exit.hpp"
#include "notebook/notebook-panel.hpp"
#include "souls/soul-helper.hpp"
#include "stats/stats-window.hpp"
#include "treasure/treasure-launch.hpp"
#include "ui/perf-overlay.hpp"
#include "ui/preset-windows.hpp"

void CloseWindowsOffWorld() {
    // The overlay comes back with the binding (it follows its options); the
    // panels with their button, or for the soul panel with the next fight.
    RestorePerfOverlay();
    RestoreTreasurePanel();
    RestoreStatsWindow();
    RestorePresetWindows();
    CloseNotebookPanelOffWorld();
    HideSoulHelper();
}
