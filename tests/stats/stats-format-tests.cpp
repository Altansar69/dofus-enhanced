#include "stats/stats-format.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// The statistics window's French texts (stats-format.cpp) for the store's
// totals: every figure the options showed in 1.15.0, as label / value lines,
// with the same units and names. No game, file or window is involved.
namespace {
using namespace stats;
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr int64_t Oct23 = 1792756800, Oct1 = 1790856000; // 2026-10-23 and 2026-10-01 at noon UTC: the same day in any time zone
const std::string Nb = " ";
// « 1 234 567 » with thin no-break spaces.
std::string Thin(const std::string& digits) {
    std::string text;
    for (size_t i = 0; i < digits.size(); ++i) { if (i && (digits.size() - i) % 3 == 0) text += " "; text += digits[i]; }
    return text;
}
void Set(Totals& totals, Counter counter, int64_t value) { totals.counters[static_cast<size_t>(counter)] = value; }
std::vector<std::pair<int, int64_t>>& Entries(Totals& totals, stats::Table table) { return totals.tables[static_cast<size_t>(table)]; }
// The game's names, as stats::KeyName gives them: two spells, a dungeon, two worlds and the elements.
std::string Names(stats::Table table, int key) {
    static const std::map<int, std::string> spells{{413, "Pression"}, {12, "Mot curatif"}}, dungeons{{5, "Donjon des Bouftous"}},
        worlds{{14, "Srambad"}, {1, "Monde des Douze"}};
    static const char* const elements[] = {"neutre", "terre", "feu", "eau", "air"};
    if (table == stats::Table::Elements) return key >= 0 && key < 5 ? elements[key] : "";
    const auto& names = table == stats::Table::Spells ? spells : table == stats::Table::Dungeons ? dungeons : worlds;
    const auto found = names.find(key);
    return found != names.end() ? found->second : "";
}
// This character (Altansar), with the figures of the options' tests in 1.15.0.
Totals Mine() {
    Totals mine;
    mine.character = 101; mine.name = "Altansar"; mine.characters = 1; mine.first = Oct23; mine.last = Oct23 + 3600;
    const std::pair<Counter, int64_t> counts[] = {
        {Counter::PlayMs, 43500000}, {Counter::CellsRoleplay, 12345}, {Counter::CellsFight, 2345}, {Counter::Maps, 1234},
        {Counter::Dimensions, 12}, {Counter::Xp, 12345678}, {Counter::Levels, 3}, {Counter::KamasEarned, 1234567},
        {Counter::KamasSpent, 234567}, {Counter::Harvests, 1234}, {Counter::Crafts, 567}, {Counter::Quests, 12},
        {Counter::Achievements, 34}, {Counter::Captures, 3}, {Counter::FightsPve, 123}, {Counter::FightsPvp, 4},
        {Counter::WinsPve, 98}, {Counter::WinsPvp, 1}, {Counter::FightMs, 18720000}, {Counter::FightTurns, 1234},
        {Counter::DamageDealt, 1234567}, {Counter::DamageTaken, 234567}, {Counter::Heals, 123456}, {Counter::Kills, 2345},
        {Counter::Deaths, 12}, {Counter::Dungeons, 12}, {Counter::SpellCasts, 12345}, {Counter::TeleportZaap, 123},
        {Counter::TeleportZaapi, 45}, {Counter::TeleportPrism, 6}, {Counter::TeleportOther, 3}, {Counter::RecallPotions, 7},
        {Counter::HavenBag, 89}, {Counter::ModTrips, 12}};
    for (const auto& [counter, value] : counts) Set(mine, counter, value);
    mine.records = {3456, 10800000};
    Entries(mine, stats::Table::Spells) = {{413, 1234}, {4567, 987}, {12, 455}};
    Entries(mine, stats::Table::Elements) = {{1, 450}, {2, 300}, {4, 200}, {0, 42}, {3, 8}};
    Entries(mine, stats::Table::Dungeons) = {{5, 5}, {7, 2}};
    Entries(mine, stats::Table::Dimensions) = {{14, 8}, {1, 6}};
    return mine;
}
// Every character's sums: four characters since the first of October, 1 234 h of play.
Totals All() {
    Totals all;
    all.characters = 4; all.first = Oct1; all.last = Oct23 + 3600;
    Set(all, Counter::PlayMs, 4442400000);
    Set(all, Counter::KamasEarned, 1000); Set(all, Counter::KamasSpent, 2000);
    return all;
}
// What a section shows: each line « label | value », the hidden ones left out.
std::vector<std::string> Shown(Part part, const Totals& totals, KeyNames names = Names) {
    std::vector<std::string> shown;
    for (const auto& line : Lines(part, totals, names)) if (line.shown) shown.push_back(line.label + " | " + line.value);
    return shown;
}
void Expect(Part part, const Totals& totals, const std::vector<std::string>& expected, const std::string& what, KeyNames names = Names) {
    const auto shown = Shown(part, totals, names);
    if (shown == expected) return;
    std::string text = what + ", shown:";
    for (const auto& line : shown) text += "\n  " + line;
    throw std::runtime_error(text);
}
std::string Value(Part part, const Totals& totals, const std::string& label) {
    for (const auto& line : Lines(part, totals, Names)) if (line.label == label) return line.shown ? line.value : "(hidden)";
    throw std::runtime_error("No line " + label);
}

// Seven sections in reading order, each a fixed list of lines whatever the
// totals: short labels, never a title line, each with what it counts.
void SectionsAndLines() {
    const char* titles[] = {"Temps et déplacements", "Progression", "Kamas", "Récolte et artisanat", "Combats", "Sorts et éléments", "Téléportations"};
    const size_t sizes[] = {7, 2, 3, 4, 15, 13, 7};
    size_t total = 0;
    for (size_t index = 0; index < PartCount; ++index) {
        const auto part = static_cast<Part>(index);
        Check(std::string(PartTitle(part)) == titles[index] && PartSize(part) == sizes[index], std::string("Section ") + titles[index]);
        total += PartSize(part);
        for (const auto& totals : {Mine(), All(), Totals{}}) {
            const auto lines = Lines(part, totals, Names);
            Check(lines.size() == PartSize(part), std::string("Always as many lines in ") + titles[index]);
            for (const auto& line : lines) {
                Check(!line.label.empty() && line.label.size() <= 40 && line.label.find(" : ") == std::string::npos && line.label.find("—") == std::string::npos,
                    "A short label, no sentence nor title line: " + line.label);
                const std::string help = line.help;
                Check(!help.empty() && help.find('\'') == std::string::npos && line.label.find('\'') == std::string::npos,
                    "Each line says what it counts, with typographic apostrophes: " + line.label);
            }
        }
    }
    Check(total == 51 && PartTitle(Part::Count)[0] == '\0' && PartSize(Part::Count) == 0 && Lines(Part::Count, Mine(), Names).empty(),
        "Fifty-one lines in all; no eighth section");
}

// This character: every figure of 1.15.0's lines, in the same French formats
// (thin spaces, units after a no-break space, names from the game's data).
void EveryFigureOfACharacter() {
    const auto mine = Mine();
    Check(Who(mine, false) == "Altansar, compté depuis le 23 octobre 2026", "Who: " + Who(mine, false));
    Expect(Part::Time, mine, {"Temps de jeu | 12" + Nb + "h" + Nb + "05" + Nb + "min", "Plus longue session | 3" + Nb + "h" + Nb + "00" + Nb + "min",
        "Cases parcourues hors combat | " + Thin("12345"), "Cases parcourues en combat | " + Thin("2345"), "Cartes parcourues | " + Thin("1234"),
        "Changements de dimension | 12", "Dimension la plus visitée | Srambad (8 arrivées)"}, "Temps et déplacements");
    Expect(Part::Progress, mine, {"Expérience gagnée | " + Thin("12345678"), "Niveaux gagnés | 3"}, "Progression");
    Expect(Part::Kamas, mine, {"Kamas gagnés | " + Thin("1234567") + Nb + "kamas", "Kamas dépensés | " + Thin("234567") + Nb + "kamas",
        "Solde | +" + Thin("1000000") + Nb + "kamas"}, "Kamas");
    Expect(Part::Gathering, mine, {"Ressources récoltées | " + Thin("1234"), "Objets fabriqués | 567", "Quêtes terminées | 12", "Succès obtenus | 34"},
        "Récolte et artisanat");
    Expect(Part::Fights, mine, {"Combats JcM | 123 (98 victoires, 25 défaites)", "Combats JcJ | 4 (1 victoire, 3 défaites)",
        "Combats Kolizéum | 0 (0 victoire, 0 défaite)", "Temps en combat | 5" + Nb + "h" + Nb + "12" + Nb + "min",
        "Durée moyenne d’un combat | 2" + Nb + "min" + Nb + "27" + Nb + "s", "Tours joués | " + Thin("1234"), "Dégâts infligés | " + Thin("1234567"),
        "Dégâts subis | " + Thin("234567"), "Soins prodigués | " + Thin("123456"), "Plus gros coup | " + Thin("3456"),
        "Monstres vaincus | " + Thin("2345"), "Morts | 12", "Âmes capturées | 3", "Donjons terminés | 12",
        "Donjon le plus fait | Donjon des Bouftous (5 fois)"}, "Combats");
    Expect(Part::Spells, mine, {"Sorts lancés | " + Thin("12345"), "Sort n° 1 | Pression (" + Thin("1234") + " fois)", "Sort n° 2 | sort 4567 (987 fois)",
        "Sort n° 3 | Mot curatif (455 fois)", "Dégâts neutre | 4,2" + Nb + "%", "Dégâts terre | 45" + Nb + "%", "Dégâts feu | 30" + Nb + "%",
        "Dégâts eau | 0,8" + Nb + "%", "Dégâts air | 20" + Nb + "%"}, "Sorts et éléments");
    Expect(Part::Teleports, mine, {"Zaaps | 123", "Zaapis | 45", "Prismes | 6", "Potions de rappel | 7", "Entrées dans un Havre-Sac | 89",
        "Trajets du mod | 12", "Autres téléportations | 3"}, "Téléportations");
    // The lines not shown keep their place: the fourth and fifth spells, the line without damage, the other elements.
    const auto spells = Lines(Part::Spells, mine, Names);
    Check(spells[4].label == "Sort n° 4" && !spells[4].shown && spells[5].label == "Sort n° 5" && !spells[5].shown &&
        spells[6].label == "Dégâts par élément" && !spells[6].shown && spells[12].label == "Autres éléments" && !spells[12].shown,
        "Ranks not cast yet and the empty-damage line are hidden in place");
    // Without the game's names (data not loaded): the ids, as in 1.15.0.
    Check(Value(Part::Spells, mine, "Sort n° 2") == "sort 4567 (987 fois)" && Shown(Part::Spells, mine, nullptr)[1] == "Sort n° 1 | sort 413 (" + Thin("1234") + " fois)" &&
        Shown(Part::Fights, mine, nullptr).back() == "Donjon le plus fait | donjon 5 (5 fois)" &&
        Shown(Part::Time, mine, nullptr).back() == "Dimension la plus visitée | dimension 14 (8 arrivées)", "Unknown names show the ids");
}

// Every character's sums, and the figures before anything is counted.
void EveryCharacterAndNothingYet() {
    const auto all = All();
    Check(Who(all, true) == "4 personnages, comptés depuis le 1er octobre 2026", "Who, every character: " + Who(all, true));
    Check(Value(Part::Time, all, "Temps de jeu") == Thin("1234") + Nb + "h" + Nb + "00" + Nb + "min" && Value(Part::Time, all, "Cartes parcourues") == "0" &&
        Value(Part::Time, all, "Dimension la plus visitée") == "aucune" && Value(Part::Kamas, all, "Solde") == "-" + Thin("1000") + Nb + "kamas" &&
        Value(Part::Fights, all, "Temps en combat") == "0" + Nb + "min" && Value(Part::Fights, all, "Durée moyenne d’un combat") == "(hidden)" &&
        Value(Part::Fights, all, "Donjon le plus fait") == "aucun", "Every character's sums");
    Expect(Part::Spells, all, {"Sorts lancés | 0", "Sort n° 1 | aucun", "Dégâts par élément | aucun dégât compté"},
        "No spell nor damage yet: one line each says so");
    Totals one = Mine();
    one.characters = 1;
    Check(Who(one, true) == "1 personnage, compté depuis le 23 octobre 2026", "One character: " + Who(one, true));
    Check(Who(Totals{}, true) == "Rien de compté pour l’instant" && Who(Totals{}, false) == "Aucun personnage connecté pour l’instant",
        "Nothing counted, no character connected");
    Totals unnamed;
    unnamed.character = 77;
    Check(Who(unnamed, false) == "n° 77, rien de compté pour l’instant", "A character without a name or counts: " + Who(unnamed, false));
    unnamed.name = "Bob";
    Check(Who(unnamed, false) == "Bob, rien de compté pour l’instant", "A named character without counts");
}

// The edge formats: under a minute, an average over an hour, shares rounded,
// singulars, more victories than fights.
void EdgeFormats() {
    auto totals = Mine();
    const auto play = [&](int64_t ms) { Set(totals, Counter::PlayMs, ms); return Value(Part::Time, totals, "Temps de jeu"); };
    Check(play(45000) == "45" + Nb + "s" && play(0) == "0" + Nb + "min" && play(60000) == "1" + Nb + "min" &&
        play(3600000) == "1" + Nb + "h" + Nb + "00" + Nb + "min", "Play time under a minute, at a minute, at an hour");
    Entries(totals, stats::Table::Elements) = {{1, 996}, {0, 3}, {7, 1}};
    Expect(Part::Spells, totals, {"Sorts lancés | " + Thin("12345"), "Sort n° 1 | Pression (" + Thin("1234") + " fois)", "Sort n° 2 | sort 4567 (987 fois)",
        "Sort n° 3 | Mot curatif (455 fois)", "Dégâts neutre | 0,3" + Nb + "%", "Dégâts terre | 100" + Nb + "%", "Dégâts feu | 0" + Nb + "%",
        "Dégâts eau | 0" + Nb + "%", "Dégâts air | 0" + Nb + "%", "Autres éléments | 0,1" + Nb + "%"}, "Shares rounded, the other elements shown");
    Entries(totals, stats::Table::Elements) = {{2, 900}, {3, 100}};
    Check(Value(Part::Spells, totals, "Dégâts feu") == "90" + Nb + "%" && Value(Part::Spells, totals, "Dégâts eau") == "10" + Nb + "%" &&
        Value(Part::Spells, totals, "Dégâts neutre") == "0" + Nb + "%" && Value(Part::Spells, totals, "Autres éléments") == "(hidden)", "Two elements");
    Entries(totals, stats::Table::Spells) = {{1, 10}, {2, 9}, {3, 8}, {4, 7}, {5, 1}};
    Check(Value(Part::Spells, totals, "Sort n° 5") == "sort 5 (1 fois)" && Value(Part::Spells, totals, "Sort n° 4") == "sort 4 (7 fois)",
        "Five spells: every rank shown");
    Entries(totals, stats::Table::Dimensions) = {{1, 1}};
    Check(Value(Part::Time, totals, "Dimension la plus visitée") == "Monde des Douze (1 arrivée)", "One arrival");
    Entries(totals, stats::Table::Dungeons) = {{5, 1}};
    Check(Value(Part::Fights, totals, "Donjon le plus fait") == "Donjon des Bouftous (1 fois)", "One dungeon run");
    Set(totals, Counter::FightsPvp, 0); Set(totals, Counter::FightsPve, 1); Set(totals, Counter::WinsPve, 3);
    Set(totals, Counter::FightMs, 7200000);
    Check(Value(Part::Fights, totals, "Combats JcM") == "1 (1 victoire, 0 défaite)" &&
        Value(Part::Fights, totals, "Durée moyenne d’un combat") == "2" + Nb + "h" + Nb + "00" + Nb + "min", "More victories than fights; an average over an hour");
    Set(totals, Counter::FightMs, 30000);
    Check(Value(Part::Fights, totals, "Durée moyenne d’un combat") == "30" + Nb + "s", "An average under a minute");
    Set(totals, Counter::KamasEarned, 5); Set(totals, Counter::KamasSpent, 5);
    Check(Value(Part::Kamas, totals, "Solde") == "0" + Nb + "kamas" && Value(Part::Kamas, totals, "Kamas gagnés") == "5" + Nb + "kamas", "A zero balance");
}

// The reset at the bottom: Réinitialiser, the question naming what would be
// erased, then its outcome, as in 1.15.0.
void ResetTexts() {
    auto mine = Mine();
    Check(ResetText(Reset::Ready, false, mine) == "Réinitialiser les statistiques de ce personnage" &&
        ResetText(Reset::Ready, true, mine) == "Réinitialiser les statistiques de tous mes personnages", "Ready");
    Check(ResetText(Reset::Asking, false, mine) == "Effacer les statistiques d’Altansar ? Clique sur Confirmer dans les 10 secondes." &&
        ResetText(Reset::Asking, true, mine) == "Effacer les statistiques de tous tes personnages ? Clique sur Confirmer dans les 10 secondes.", "Asking");
    Check(ResetText(Reset::Erased, false, mine) == "Statistiques d’Altansar effacées." &&
        ResetText(Reset::Erased, true, mine) == "Statistiques de tous tes personnages effacées.", "Erased");
    Check(ResetText(Reset::Impossible, false, mine) == "Effacement impossible pour l’instant : réessaie dans un moment." &&
        ResetText(Reset::Impossible, true, mine) == ResetText(Reset::Impossible, false, mine), "Impossible");
    mine.name = "Bob";
    Check(ResetText(Reset::Asking, false, mine) == "Effacer les statistiques de Bob ? Clique sur Confirmer dans les 10 secondes.", "« de Bob »");
    mine.name = "Élodie";
    Check(ResetText(Reset::Asking, false, mine) == "Effacer les statistiques d’Élodie ? Clique sur Confirmer dans les 10 secondes.", "« d’Élodie »");
    mine.name.clear();
    Check(ResetText(Reset::Asking, false, mine) == "Effacer les statistiques de ce personnage ? Clique sur Confirmer dans les 10 secondes.", "Without a name");
    Check(std::string(ResetCaption(Reset::Ready)) == "Réinitialiser" && std::string(ResetCaption(Reset::Asking)) == "Confirmer" &&
        std::string(ResetCaption(Reset::Erased)) == "Réinitialiser" && std::string(ResetCaption(Reset::Impossible)) == "Réinitialiser",
        "Réinitialiser, then Confirmer while asking");
}
}

int main() {
    try {
        SectionsAndLines();
        EveryFigureOfACharacter();
        EveryCharacterAndNothingYet();
        EdgeFormats();
        ResetTexts();
        std::cout << "PASS: statistics window texts (seven sections in reading order, 51 short label / value lines with their hover texts, every figure of 1.15.0 for this character and every character in French formats, names from the game's data or the ids, hidden ranks and elements in place, edge formats, the reset's question and outcomes)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
