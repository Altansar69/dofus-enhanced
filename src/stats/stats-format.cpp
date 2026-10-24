#include "stats/stats-format.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iterator>

namespace stats {
namespace {
// French formats: a thin no-break space between thousands, a no-break space
// before a unit.
constexpr const char* ThinSpace = " ";
constexpr const char* NoBreak = " ";
std::string Grouped(int64_t value) {
    const bool negative = value < 0;
    const auto magnitude = negative ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    const auto digits = std::to_string(magnitude);
    std::string text = negative ? "-" : "";
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) text += ThinSpace;
        text += digits[i];
    }
    return text;
}
std::string Unit(const std::string& number, const char* unit) { return number + NoBreak + unit; }
std::string TwoDigits(int64_t value) {
    char text[8] = {};
    std::snprintf(text, sizeof(text), "%02d", static_cast<int>(value % 100));
    return text;
}
// « 12 h 05 min », « 34 min », « 45 s ».
std::string Hours(int64_t ms) {
    const auto seconds = std::max<int64_t>(0, ms) / 1000, minutes = seconds / 60, hours = minutes / 60;
    if (hours > 0) return Unit(Grouped(hours), "h") + NoBreak + Unit(TwoDigits(minutes % 60), "min");
    if (minutes > 0 || seconds == 0) return Unit(std::to_string(minutes), "min");
    return Unit(std::to_string(seconds), "s");
}
// An average: « 3 min 05 s » under an hour.
std::string Lasting(int64_t ms) {
    const auto seconds = std::max<int64_t>(0, ms) / 1000;
    if (seconds >= 3600 || seconds < 60) return Hours(ms);
    return Unit(std::to_string(seconds / 60), "min") + NoBreak + Unit(TwoDigits(seconds % 60), "s");
}
// « kamas » in full: a « k » after a number reads as thousands to Dofus players.
std::string Kamas(int64_t kamas) { return Unit(Grouped(kamas), "kamas"); }
std::string Balance(int64_t kamas) { return kamas > 0 ? "+" + Kamas(kamas) : Kamas(kamas); }
// A share of the total: one decimal under 10 %.
std::string Share(int64_t part, int64_t total) {
    if (part <= 0 || total <= 0) return Unit("0", "%");
    const double percent = 100.0 * static_cast<double>(part) / static_cast<double>(total);
    const auto tenths = std::llround(percent * 10);
    if (tenths < 100) return Unit(std::to_string(tenths / 10) + "," + std::to_string(tenths % 10), "%");
    return Unit(std::to_string(std::llround(percent)), "%");
}
std::string Counted(int64_t count, const char* one, const char* many) {
    return Grouped(count) + " " + (count > 1 || count < -1 ? many : one);
}
// « 23 octobre 2026 », « 1er mai 2026 », in local time.
std::string Day(int64_t seconds) {
    static constexpr const char* Months[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
        "septembre", "octobre", "novembre", "décembre"};
    std::tm local{};
    const __time64_t time = seconds;
    if (seconds <= 0 || _localtime64_s(&local, &time) != 0 || local.tm_mon < 0 || local.tm_mon > 11) return {};
    return (local.tm_mday == 1 ? std::string("1er") : std::to_string(local.tm_mday)) + " " + Months[local.tm_mon] + " " +
        std::to_string(local.tm_year + 1900);
}
// A table key's name from the game's data, else its id.
std::string Named(KeyNames names, Table table, int key, const char* kind) {
    auto name = names ? names(table, key) : std::string{};
    return name.empty() ? std::string(kind) + " " + std::to_string(key) : name;
}
std::string Name(const Totals& totals) {
    return !totals.name.empty() ? totals.name : "n° " + std::to_string(totals.character);
}
// « de Lou », « d’Ana »; without a name, « de ce personnage ».
std::string Of(const Totals& totals) {
    if (totals.name.empty()) return "de ce personnage";
    static constexpr const char* Vowels[] = {"A", "E", "I", "O", "U", "Y", "a", "e", "i", "o", "u", "y",
        "À", "Â", "Ä", "É", "È", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Ù", "Û", "Ü", "à", "â", "é", "è", "ê", "î", "ô", "û"};
    for (const auto vowel : Vowels) if (totals.name.rfind(vowel, 0) == 0) return "d’" + totals.name;
    return "de " + totals.name;
}
// Each section's header and number of lines.
struct PartInfo { const char* title; size_t size; };
constexpr PartInfo Parts[] = {{"Temps et déplacements", 7}, {"Progression", 2}, {"Kamas", 3}, {"Récolte et artisanat", 4},
    {"Combats", 15}, {"Sorts et éléments", 13}, {"Téléportations", 7}};
static_assert(std::size(Parts) == PartCount);
// The hover texts shared by several lines.
constexpr const char* SpellsHelp = "Les cinq sorts les plus lancés, avec leur nom dans le jeu.";
constexpr const char* ElementsHelp = "Part de chaque élément (neutre, terre, feu, eau, air) dans les dégâts infligés.";
// A table's entry of this rank (largest first), if any.
const std::pair<int, int64_t>* Top(const Totals& totals, Table table, size_t rank) {
    const auto& entries = totals.Entries(table);
    return rank < entries.size() ? &entries[rank] : nullptr;
}
std::vector<Line> TimeLines(const Totals& totals, KeyNames names) {
    const auto top = Top(totals, Table::Dimensions, 0);
    return {
        {"Temps de jeu", Hours(totals.Count(Counter::PlayMs)), "Temps passé en jeu pendant que le mod est chargé."},
        {"Plus longue session", Hours(totals.Best(Record::LongestSessionMs)), "La plus longue session de jeu sans interruption."},
        {"Cases parcourues hors combat", Grouped(totals.Count(Counter::CellsRoleplay)), "Cases parcourues à pied hors combat."},
        {"Cases parcourues en combat", Grouped(totals.Count(Counter::CellsFight)), "Cases parcourues à pied en combat."},
        {"Cartes parcourues", Grouped(totals.Count(Counter::Maps)), "Changements de carte, téléportations comprises."},
        {"Changements de dimension", Grouped(totals.Count(Counter::Dimensions)),
            "Passages d’une dimension à une autre : Monde des Douze, Incarnam, Srambad…"},
        {"Dimension la plus visitée", top ? Named(names, Table::Dimensions, top->first, "dimension") + " (" +
            Counted(top->second, "arrivée", "arrivées") + ")" : "aucune", "La dimension la plus souvent rejointe, avec son nom dans le jeu."}};
}
std::vector<Line> ProgressLines(const Totals& totals) {
    return {{"Expérience gagnée", Grouped(totals.Count(Counter::Xp)), "Points d’expérience gagnés."},
        {"Niveaux gagnés", Grouped(totals.Count(Counter::Levels)), "Niveaux gagnés."}};
}
std::vector<Line> KamasLines(const Totals& totals) {
    const auto earned = totals.Count(Counter::KamasEarned), spent = totals.Count(Counter::KamasSpent);
    return {{"Kamas gagnés", Kamas(earned),
            "Kamas gagnés ; les retraits de la banque ou d’un coffre ne comptent pas, ni les ventes à l’hôtel des ventes, créditées à la banque."},
        {"Kamas dépensés", Kamas(spent), "Kamas dépensés ; les dépôts à la banque ou dans un coffre ne comptent pas."},
        {"Solde", Balance(earned - spent), "Kamas gagnés moins kamas dépensés."}};
}
std::vector<Line> GatheringLines(const Totals& totals) {
    return {{"Ressources récoltées", Grouped(totals.Count(Counter::Harvests)),
            "Récoltes faites : une par ressource récoltée, quelle que soit la quantité obtenue."},
        {"Objets fabriqués", Grouped(totals.Count(Counter::Crafts)), "Objets fabriqués."},
        {"Quêtes terminées", Grouped(totals.Count(Counter::Quests)), "Quêtes terminées."},
        {"Succès obtenus", Grouped(totals.Count(Counter::Achievements)), "Succès obtenus."}};
}
std::vector<Line> FightLines(const Totals& totals, KeyNames names) {
    const auto count = [&](Counter counter) { return totals.Count(counter); };
    // « 123 (98 victoires, 25 défaites) »
    const auto fights = [&](Counter all, Counter won) {
        const auto played = count(all), wins = std::min(count(won), played);
        return Grouped(played) + " (" + Counted(wins, "victoire", "victoires") + ", " + Counted(played - wins, "défaite", "défaites") + ")";
    };
    const auto played = count(Counter::FightsPve) + count(Counter::FightsPvp) + count(Counter::FightsKolizeum);
    const auto top = Top(totals, Table::Dungeons, 0);
    return {
        {"Combats JcM", fights(Counter::FightsPve, Counter::WinsPve),
            "Combats contre des monstres terminés et victoires ; les autres combats sont comptés en défaites."},
        {"Combats JcJ", fights(Counter::FightsPvp, Counter::WinsPvp),
            "Combats contre d’autres joueurs terminés hors Kolizéum et victoires ; les autres combats sont comptés en défaites."},
        {"Combats Kolizéum", fights(Counter::FightsKolizeum, Counter::WinsKolizeum),
            "Combats de Kolizéum terminés et victoires ; les autres combats sont comptés en défaites."},
        {"Temps en combat", Hours(count(Counter::FightMs)), "Temps passé en combat."},
        // Without a finished fight there is no average (1.15.0 left it out too).
        {"Durée moyenne d’un combat", played > 0 ? Lasting(count(Counter::FightMs) / played) : std::string{},
            "Temps passé en combat divisé par le nombre de combats terminés.", played > 0},
        {"Tours joués", Grouped(count(Counter::FightTurns)), "Tours joués en combat."},
        {"Dégâts infligés", Grouped(count(Counter::DamageDealt)), "Dégâts infligés en combat."},
        {"Dégâts subis", Grouped(count(Counter::DamageTaken)), "Dégâts subis en combat."},
        {"Soins prodigués", Grouped(count(Counter::Heals)), "Points de vie soignés en combat."},
        {"Plus gros coup", Grouped(totals.Best(Record::BiggestHit)), "Le plus gros coup infligé en une fois."},
        {"Monstres vaincus", Grouped(count(Counter::Kills)), "Monstres vaincus dans les combats gagnés."},
        {"Morts", Grouped(count(Counter::Deaths)), "Fois où le personnage a été vaincu en combat."},
        {"Âmes capturées", Grouped(count(Counter::Captures)), "Âmes capturées dans une pierre d’âme."},
        {"Donjons terminés", Grouped(count(Counter::Dungeons)), "Donjons terminés."},
        {"Donjon le plus fait", top ? Named(names, Table::Dungeons, top->first, "donjon") + " (" + Counted(top->second, "fois", "fois") + ")" : "aucun",
            "Le donjon terminé le plus souvent, avec son nom dans le jeu."}};
}
std::vector<Line> SpellLines(const Totals& totals, KeyNames names) {
    std::vector<Line> lines{{"Sorts lancés", Grouped(totals.Count(Counter::SpellCasts)), "Sorts lancés en combat."}};
    // The five most cast: « Sort n° 1 » says « aucun » before any, the others
    // show once cast.
    for (size_t rank = 0; rank < 5; ++rank) {
        const auto entry = Top(totals, Table::Spells, rank);
        lines.push_back({"Sort n° " + std::to_string(rank + 1),
            entry ? Named(names, Table::Spells, entry->first, "sort") + " (" + Counted(entry->second, "fois", "fois") + ")" : "aucun",
            SpellsHelp, entry || rank == 0});
    }
    // Each element's share of the damage dealt, in the game's order of the
    // elements (neutre, terre, feu, eau, air), then the others; before any
    // damage one line says so.
    int64_t total = 0, others = 0;
    std::array<int64_t, 5> parts{};
    for (const auto& [key, damage] : totals.Entries(Table::Elements)) {
        total += damage;
        if (key >= 0 && key < static_cast<int>(parts.size())) parts[static_cast<size_t>(key)] += damage;
        else others += damage;
    }
    lines.push_back({"Dégâts par élément", "aucun dégât compté", ElementsHelp, total <= 0});
    for (size_t i = 0; i < parts.size(); ++i)
        lines.push_back({"Dégâts " + Named(names, Table::Elements, static_cast<int>(i), "élément"), Share(parts[i], total), ElementsHelp, total > 0});
    lines.push_back({"Autres éléments", Share(others, total), ElementsHelp, total > 0 && others > 0});
    return lines;
}
std::vector<Line> TeleportLines(const Totals& totals) {
    return {{"Zaaps", Grouped(totals.Count(Counter::TeleportZaap)), "Téléportations par zaap."},
        {"Zaapis", Grouped(totals.Count(Counter::TeleportZaapi)), "Téléportations par zaapi."},
        {"Prismes", Grouped(totals.Count(Counter::TeleportPrism)), "Téléportations par prisme."},
        {"Potions de rappel", Grouped(totals.Count(Counter::RecallPotions)), "Potions de rappel utilisées."},
        {"Entrées dans un Havre-Sac", Grouped(totals.Count(Counter::HavenBag)), "Entrées dans un Havre-Sac."},
        {"Trajets du mod", Grouped(totals.Count(Counter::ModTrips)),
            "Trajets du mod menés à bien : bouton TP du chat, voyage depuis la carte, zaaps du Havre-Sac."},
        {"Autres téléportations", Grouped(totals.Count(Counter::TeleportOther)),
            "Téléportations par une anomalie ; les potions de cité, la téléportation au conjoint et les téléportations des PNJ ne sont pas comptées."}};
}
}

const char* PartTitle(Part part) {
    const auto index = static_cast<size_t>(part);
    return index < PartCount ? Parts[index].title : "";
}
size_t PartSize(Part part) {
    const auto index = static_cast<size_t>(part);
    return index < PartCount ? Parts[index].size : 0;
}
std::vector<Line> Lines(Part part, const Totals& totals, KeyNames names) {
    switch (part) {
    case Part::Time: return TimeLines(totals, names);
    case Part::Progress: return ProgressLines(totals);
    case Part::Kamas: return KamasLines(totals);
    case Part::Gathering: return GatheringLines(totals);
    case Part::Fights: return FightLines(totals, names);
    case Part::Spells: return SpellLines(totals, names);
    case Part::Teleports: return TeleportLines(totals);
    default: return {};
    }
}
std::string Who(const Totals& totals, bool every) {
    if (every) {
        if (totals.characters <= 0 || totals.first <= 0) return "Rien de compté pour l’instant";
        return Counted(totals.characters, "personnage, compté", "personnages, comptés") + " depuis le " + Day(totals.first);
    }
    if (totals.character <= 0) return "Aucun personnage connecté pour l’instant";
    if (totals.first <= 0) return Name(totals) + ", rien de compté pour l’instant";
    return Name(totals) + ", compté depuis le " + Day(totals.first);
}
std::string ResetText(Reset step, bool every, const Totals& totals) {
    const auto whose = every ? std::string("de tous tes personnages") : Of(totals);
    switch (step) {
    case Reset::Asking: return "Effacer les statistiques " + whose + " ? Clique sur Confirmer dans les 10 secondes.";
    case Reset::Erased: return "Statistiques " + whose + " effacées.";
    case Reset::Impossible: return "Effacement impossible pour l’instant : réessaie dans un moment.";
    default: return every ? "Réinitialiser les statistiques de tous mes personnages" : "Réinitialiser les statistiques de ce personnage";
    }
}
const char* ResetCaption(Reset step) { return step == Reset::Asking ? "Confirmer" : "Réinitialiser"; }
}
