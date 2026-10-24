#pragma once
// « Statistiques » (1.16.0): what the statistics window shows, as French text,
// from the store's totals (stats.hpp). No game access: every figure is tested
// on its own (tests/stats/stats-format-tests.cpp). Each section is a fixed
// list of short label / value lines holding every figure the options showed
// in 1.15.0, one sentence per line then, with the same units and names.
#include "stats/stats.hpp"
#include <string>
#include <vector>

namespace stats {
// The window's sections, in reading order.
enum class Part : int { Time, Progress, Kamas, Gathering, Fights, Spells, Teleports, Count };
constexpr size_t PartCount = static_cast<size_t>(Part::Count);
// « Temps et déplacements », « Progression »… (the section headers).
const char* PartTitle(Part part);
// A section's number of lines, whatever the totals.
size_t PartSize(Part part);
// One figure: its label on the left, its value on the right and what it
// counts (the hover text). A line not shown has nothing to show now (a fifth
// spell not cast yet, the elements before any damage) and keeps its place.
struct Line {
    std::string label, value;
    const char* help = "";
    bool shown = true;
};
// A table key's name from the game's data (stats::KeyName in the module),
// empty when unknown: the line then names it « sort 4567 ».
using KeyNames = std::string (*)(Table table, int key);
// A section's lines (PartSize of them) for these totals.
std::vector<Line> Lines(Part part, const Totals& totals, KeyNames names);
// Who is shown above the sections: the character and the day its counting
// began, or how many characters are added up (every) and since when.
std::string Who(const Totals& totals, bool every);
// The reset at the bottom of the window: ready, asking for Confirmer within
// 10 s, then its outcome. every: what is erased (asked for), every
// character's statistics or the one named in totals.
enum class Reset { Ready, Asking, Erased, Impossible };
std::string ResetText(Reset step, bool every, const Totals& totals);
const char* ResetCaption(Reset step);  // « Réinitialiser », then « Confirmer »
}
