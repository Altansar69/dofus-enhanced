// The game left the world (ACCOUNT_OPTIONS_UNBOUND): every window and overlay
// of the mod leaves the screen (src/core/world-exit.cpp). Each step's own
// closing is covered by its feature's test (notebook, soul helper, statistics,
// treasure, presets, overlay); this one proves none is left out.
#include "core/world-exit.hpp"
#include "notebook/notebook-panel.hpp"
#include "souls/soul-helper.hpp"
#include "stats/stats-window.hpp"
#include "treasure/treasure-launch.hpp"
#include "ui/perf-overlay.hpp"
#include "ui/preset-windows.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

std::vector<std::string> calls;
void RestorePerfOverlay() { calls.push_back("perf-overlay"); }
void RestoreTreasurePanel() { calls.push_back("treasure"); }
void RestoreStatsWindow() { calls.push_back("stats"); }
void RestorePresetWindows() { calls.push_back("presets"); }
void CloseNotebookPanelOffWorld() { calls.push_back("notebook"); }
void HideSoulHelper() { calls.push_back("soul"); }

int main() {
    try {
        CloseWindowsOffWorld();
        const std::vector<std::string> expected{"perf-overlay", "treasure", "stats", "presets", "notebook", "soul"};
        if (calls != expected) throw std::runtime_error("Every window of the mod must close off the world");
        CloseWindowsOffWorld();
        if (calls.size() != 2 * expected.size()) throw std::runtime_error("Each pump off the world closes them again (a failed cleanup retries)");
        std::cout << "PASS: off the world, the overlay, the treasure, statistics and preset windows, the notebook and the soul panel all close\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
