#include "ui/options-ui.hpp"
#include "core/module.hpp"
#include "core/version.hpp"
#include "animations/animation-mode-labels.hpp"
#include "glow/glow-mode-labels.hpp"
#include "ui/native-click.hpp"
#include "stats/stats.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>
#include <optional>
#include <string>

using namespace managed;

namespace {
constexpr int ModCategory = 0x4D4F44;
// Native sections of the mod's category (see Attach), in display order: ids
// ModCategory + 1 to ModCategory + ModSections.
constexpr int ModSections = 13;
constexpr int GameCategory = 0;
constexpr const char* Config = "Core.UILogic.Config";
constexpr const char* Elements = "Core.UILogic.Config.OptionElement";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr const char* UiModule = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Modes[] = {"Normal", "Créatures", "Miniatures"};
// « Apparence » categories and « Animations » toggles: row title and
// description, in the order of Settings' toggle tables (see options-ui.hpp).
struct CheckRow { const char* title; const char* description; };
constexpr CheckRow CombatMiniatureRows[] = {
    {"Moi", "En combat, avec Miniatures : ton personnage est réduit. Décoché, il garde sa taille normale. Activé par défaut."},
    {"Joueurs alliés", "En combat, avec Miniatures : les autres personnages de ton équipe sont réduits. Décoché, ils gardent leur taille normale. Activé par défaut."},
    {"Invocations alliées", "En combat, avec Miniatures : les invocations des autres personnages de ton équipe sont réduites. Décoché, elles gardent leur taille normale. Activé par défaut."},
    {"Mes invocations", "En combat, avec Miniatures : les invocations de ton personnage sont réduites. Décoché, elles gardent leur taille normale. Activé par défaut."},
    {"Ennemis", "En combat, avec Miniatures : les monstres et personnages de l’équipe adverse sont réduits. Décoché, ils gardent leur taille normale. Activé par défaut."},
    {"Invocations ennemies", "En combat, avec Miniatures : les invocations de l’équipe adverse sont réduites. Décoché, elles gardent leur taille normale. Activé par défaut."}};
constexpr CheckRow RoleplayMiniatureRows[] = {
    {"Moi", "Hors combat, avec Miniatures : ton personnage est réduit. Décoché, il garde sa taille normale. Activé par défaut."},
    {"Membres du groupe", "Hors combat, avec Miniatures : les personnages de ton groupe sont réduits. Décoché, ils gardent leur taille normale. Activé par défaut."},
    {"Autres joueurs", "Hors combat, avec Miniatures : les personnages des autres joueurs, hors de ton groupe, sont réduits. Décoché, ils gardent leur taille normale. Activé par défaut."},
    {"Monstres", "Hors combat, avec Miniatures : les groupes de monstres sont réduits. Décoché, ils gardent leur taille normale. Activé par défaut."},
    {"PNJ", "Hors combat, avec Miniatures : les personnages non joueurs (PNJ) sont réduits. Décoché, ils gardent leur taille normale. Désactivé par défaut."}};
constexpr const char* AnimationDescriptions[] = {
    "En combat, le jeu fait comme si son option des effets de sorts était désactivée, sans la modifier : les projectiles et effets visuels des sorts ne sont plus ajoutés aux séquences de combat. Certains effets restent visibles. Activé par défaut.",
    "En combat, les personnages joueurs lancent leurs sorts avec l’animation réduite que le jeu utilise quand ses effets de sorts sont désactivés. Sans effet sur les monstres. Activé par défaut.",
    "En combat, les combattants ne jouent plus les animations des séquences de combat : attaques, lancers de sorts, coups reçus… Leurs déplacements restent animés. Expérimental : certaines animations restent jouées. Désactivé par défaut.",
    "En combat, un combattant vaincu disparaît sans jouer son animation de mort. Sans effet quand Déplacements instantanés ou Sans attentes visuelles est coché : sa mort reste alors celle du jeu, qui retire ensuite le combattant. Expérimental. Désactivé par défaut.",
    "En combat, la marche, les poussées, les lancers de personnage porté et les sauts arrivent aussitôt à destination, cases intermédiaires comprises. Un trajet inhabituel garde son animation. Expérimental. Désactivé par défaut.",
    "En combat, le jeu n’attend plus certaines fins d’animation : décalage entre les cibles d’un sort, pauses des scripts de sorts, animation en cours avant un déplacement ou un changement d’apparence, animations évitées par les cases ci-dessus. Les délais du serveur et certaines attentes restent inchangés. Expérimental. Désactivé par défaut."};
// The checkboxes added in 1.13.0 and 1.14.0, in the order of Settings' ExtraToggles.
enum ExtraRow : size_t { MiniMapBarRow, MiniMapZoneNameRow, MapCoordsRow, MiniMapCoordsRow, NotebookPingRow, MapAddPositionRow,
    PerfOverlayRow, PerfGpuRow, PerfDiskRow, PerfDragRow };
constexpr CheckRow ExtraRows[] = {
    {"Barre des ressources sur la mini-carte", "Affiche sur la mini-carte la barre des six métiers qui ouvre le choix des ressources. Décochée, la barre et sa liste disparaissent, mais les repères des ressources choisies restent affichés si « Ressources sur la mini-carte » est cochée ; le choix se fait alors depuis la grande carte. Activé par défaut."},
    {"Nom de la zone au survol de la mini-carte", "Sur la mini-carte, l’infobulle de la case survolée affiche le nom de sa zone au-dessus de ses coordonnées. Activé par défaut."},
    {"Dimension avec les coordonnées sur la grande carte", "Sur la grande carte, l’infobulle de la case survolée affiche ses coordonnées avec la dimension de la carte affichée : [x,y, dimension], par exemple [4,-19, Monde des Douze]. Activé par défaut."},
    {"Dimension avec les coordonnées sur la mini-carte", "Sur la mini-carte, l’infobulle de la case survolée affiche ses coordonnées avec leur dimension : [x,y, dimension]. Activé par défaut."},
    {"Bouton de repère à côté des positions", "Dans les positions du carnet, un petit bouton drapeau à droite de chaque nom place un repère sur cette position : le drapeau du jeu, visible sur la grande carte et la mini-carte. Le bouton reste enfoncé tant que le repère est affiché ; un second clic le retire. Décochée, le bouton disparaît ; le clic droit sur une position permet toujours d’afficher ou de retirer son repère. Activé par défaut."},
    {"« Ajouter aux positions » au clic droit sur les cartes", "Au clic droit sur une case de la grande carte ou de la mini-carte, ajoute l’entrée « Ajouter aux positions » : elle enregistre cette case (coordonnées, dimension et nom de la zone) dans les positions du carnet, même fermé, et le chat le confirme. Une case déjà enregistrée n’est pas ajoutée deux fois. Activé par défaut."},
    {"Afficher les FPS et la mémoire", "Affiche discrètement, dans un coin de l’écran ou à la place choisie à la souris, les images par seconde (FPS), la durée moyenne et la plus longue d’une image sur la dernière seconde, la mémoire utilisée par le jeu et sa part du processeur, mises à jour deux fois par seconde, puis la charge du GPU et l’activité du disque si leurs cases sont cochées. Le cadre ne capte jamais le clavier ; les menus, fenêtres de confirmation et infobulles du jeu passent au-dessus. Désactivé par défaut."},
    {"Charge du GPU", "Ajoute à l’affichage des performances la part de la carte graphique utilisée par le jeu, comme la colonne GPU du Gestionnaire des tâches : celle de son moteur le plus chargé (la 3D le plus souvent), mesurée une fois par seconde, par exemple « GPU 23 % ». Si Windows ne fournit pas cette mesure, elle n’apparaît pas. Activé par défaut."},
    {"Activité du disque", "Ajoute à l’affichage des performances l’activité du disque où le jeu est installé, comme l’onglet Performances du Gestionnaire des tâches : son temps d’activité et son débit (lectures et écritures de tous les programmes sur ce disque), mesurés une fois par seconde, par exemple « Disque C: 4 % · 1,2 Mo/s ». Pour un jeu installé sur le réseau, elle n’apparaît pas. Activé par défaut."},
    {"Déplacer l’affichage à la souris", "Fais glisser l’affichage des performances avec le clic gauche pour le placer où tu veux ; il reste entièrement à l’écran et sa place est gardée, même après un redémarrage ou un changement de résolution. Les clics sur l’affichage ne vont pas au jeu. Décochée, l’affichage reprend son coin et laisse passer les clics vers le jeu. Activé par défaut."}};
constexpr const char* PerfCorners[] = {"En haut à gauche", "En haut à droite", "En bas à gauche", "En bas à droite"};
constexpr const char* ChatDimensionChoices[] = {"Non", "Oui, sauf dans le Monde des Douze", "Oui"};
constexpr const char* TempleTitle = "Passage secret et temple des Roublards";
constexpr const char* TempleDescription = "Le trajet du mod peut emprunter le passage secret des Roublards : Bad Aboum, dans la crypte sous la vieille chapelle de la Cité d’Astrub [5,-15], conduit au temple Roublard [10,16] et au village d’Amakna [3,3], et en revient. Quand c’est plus court, l’autopilote du jeu le rejoint, puis le mod lui parle et choisit la destination comme un clic, sans jamais rien payer ; si Bad Aboum ne propose pas ce trajet à ton personnage, le trajet continue sans lui et ce refus est retenu. Le trajet peut aussi traverser le temple ; sa statue reste réservée aux Roublards par le jeu. Décoché, le mod n’emprunte ni le passage ni le temple. Activé par défaut.";
static_assert(std::size(ExtraRows) == std::size(ExtraToggles) &&
    std::tuple_size_v<decltype(OptionsUi::extraSources)> == std::size(ExtraToggles) && std::size(PerfCorners) == 4 &&
    std::size(ChatDimensionChoices) == 3);
static_assert(std::size(CombatMiniatureRows) == std::size(CombatMiniatureToggles) &&
    std::size(RoleplayMiniatureRows) == std::size(RoleplayMiniatureToggles) &&
    std::size(AnimationDescriptions) == std::size(AnimationToggles) && AnimationToggleLabels.size() == std::size(AnimationToggles));
static_assert(std::tuple_size_v<decltype(OptionsUi::combatMiniatureSources)> == std::size(CombatMiniatureToggles) &&
    std::tuple_size_v<decltype(OptionsUi::roleplayMiniatureSources)> == std::size(RoleplayMiniatureToggles) &&
    std::tuple_size_v<decltype(OptionsUi::animationSources)> == std::size(AnimationToggles));
Klass generalClass = nullptr;
using EnableFn = void(*)(Obj, Method);
using InitFn = void(*)(Obj, bool, bool, int, Method);
EnableFn originalEnable = nullptr;
InitFn originalInit = nullptr;
// TextButtonOptionElement.<Init>b__3_0(VisualElement), the click, and Init(TextButtonOption).
using TextButtonFn = void(*)(Obj, Obj, Method);
TextButtonFn originalTextButton = nullptr, originalTextButtonInit = nullptr;
Method textButtonModelGetter = nullptr, openUrl = nullptr, setPassword = nullptr, setEnabled = nullptr;
constexpr const char* ProjectUrl = "https://github.com/Altansar69/dofus-enhanced";
// Metamob rows. The key never goes to Settings, a log, a notice or an exception.
constexpr int IconSave = 148, IconTrash = 316; // FigmaIcons.floppy, trash
constexpr int KeyMaxLength = 256;
// DisplayStyle.Flex/None, FlexDirection.Row, Align.Center, WhiteSpace.Normal.
constexpr int DisplayFlex = 0, DisplayNone = 1, RowDirection = 2, AlignCenter = 2, WrapText = 0;
// DofusButtonCustom: SizeEnum.small, ComponentStyleEnum primary/secondary/tertiary, ButtonStatusEnum normal/negative.
constexpr int ButtonSmall = 0, StylePrimary = 0, StyleSecondary = 1, StyleTertiary = 2, StatusNormal = 0, StatusNegative = 1;
// The trash's question waits 10 s for an answer.
constexpr ULONGLONG ConfirmMs = 10000, KeyFieldRefreshMs = 1000, KeyFieldLogMs = 10000;
constexpr size_t MaxKeyFields = 8; // pooled views showing the key row at once (normally one)
constexpr const char* KeyHint = "Colle ici ta clé API Metamob";
// A saved key shows a fixed run of stars, never derived from the key (content or length).
constexpr const char* KeyStars = "************";
constexpr const char* ConfirmQuestion = "Voulez-vous supprimer la clé Metamob ?";
constexpr const char* PasteKeyFirst = "Metamob : colle d’abord ta clé dans le champ.";
// The indicator after the stars shows the capture sync's status of the saved
// key (caption, FigmaIcons icon, button status) and its click explains it in
// the chat: never « valide » unless Metamob accepted that key file.
struct Indicator { metamob::KeyStatus status; const char* caption; int icon, look; const char* notice; };
constexpr Indicator Indicators[] = {
    {metamob::KeyStatus::Saved, "Vérification…", 172, StatusNormal, // hourglass: not checked yet in this session
        "Metamob : vérification de la clé enregistrée en cours."},
    {metamob::KeyStatus::Verified, "Clé valide", 91, StatusNormal, // circleTick
        "Metamob : une clé valide est enregistrée sur ce PC. Pour en changer, supprime-la avec la corbeille puis colle la nouvelle."},
    {metamob::KeyStatus::Unverified, "Clé non vérifiée", 236, StatusNormal, // questionMark: its click asks for a new check
        "Metamob : la clé enregistrée n’a pas pu être vérifiée auprès de Metamob ; nouvelle vérification en cours."},
    {metamob::KeyStatus::Refused, "Clé refusée", 87, StatusNegative, // circleCross
        "Metamob : clé refusée par Metamob (révoquée ou régénérée ?). Supprime-la avec la corbeille, puis enregistre une nouvelle clé."},
    {metamob::KeyStatus::Unreadable, "Clé illisible", 94, StatusNegative, // circleWarning
        "Metamob : clé illisible sur ce compte Windows. Supprime-la avec la corbeille, puis enregistre-la à nouveau."}};
// What the key row shows, the same in every view of it: the masked field (no
// key saved), the saved key, or the question after the trash.
enum KeyState : int { KeyEditable, KeySaved, KeyConfirm };

// « Statistiques » (1.15.0): the counting checkbox, what is shown, then one
// text-button row per line below (label « name : value »), grouped under title
// lines, and the reset of what is shown. The labels follow the store while the
// page is open (once a second).
constexpr const char* StatsTitle = "Compter mes statistiques";
constexpr const char* StatsDescription = "Compte pour chacun de tes personnages ce que le jeu ne garde pas : temps de jeu, déplacements, expérience, kamas, récoltes et fabrications, quêtes et succès, combats, sorts et éléments, téléportations. Le comptage commence avec la version 1.15.0 du mod et ne retient que ce qui se passe pendant que le mod est chargé. Tout reste sur ce PC, dans un fichier à côté des réglages du mod ; rien n’est envoyé. Décoché, rien n’est compté et les statistiques déjà comptées restent affichées. Activé par défaut.";
constexpr const char* StatsScopeTitle = "Statistiques affichées";
constexpr const char* StatsScopeDescription = "« Ce personnage » : les statistiques du personnage connecté dans ce jeu. « Tous mes personnages » : celles de tous tes personnages comptés sur ce PC, additionnées ; les records gardent la plus grande valeur. Les valeurs se mettent à jour pendant que la page est ouverte. Revient à « Ce personnage » à chaque lancement du jeu.";
constexpr const char* StatsScopes[] = {"Ce personnage", "Tous mes personnages"};
enum StatsLine : size_t {
    StatsWho,
    TimeTitle, PlayTime, LongestSession, CellsWalked, MapsChanged, DimensionsChanged, TopDimension,
    ProgressTitle, Experience, LevelsGained,
    KamasTitle, KamasIn, KamasOut, KamasBalance,
    GatherTitle, Harvested, Crafted, QuestsDone, AchievementsDone,
    FightTitle, FightsMonsters, FightsPlayers, FightsKolizeum, FightTime, TurnsPlayed, DamageLine, HealsDone,
    BiggestHitLine, KillsLine, DeathsLine, CapturesLine, DungeonsDone, TopDungeon,
    SpellTitle, SpellsCast, TopSpell1, TopSpell2, TopSpell3, TopSpell4, TopSpell5, ElementShares,
    TeleportTitle, ZaapsLine, ZaapisLine, PrismsLine, RecallLine, HavenBagLine, ModTripsLine, OtherTeleportsLine,
    StatsReset, StatsLineCount
};
// Each line's description (its hover text and its unique identity).
constexpr const char* StatsDescriptions[] = {
    "Le personnage dont les statistiques sont affichées, ou le nombre de tes personnages additionnés, et le jour où leur comptage a commencé.",
    "Temps de jeu, plus longue session, cases parcourues, cartes et dimensions.",
    "Temps passé en jeu pendant que le mod est chargé.",
    "La plus longue session de jeu sans interruption.",
    "Cases parcourues à pied, hors combat et en combat.",
    "Changements de carte, téléportations comprises.",
    "Passages d’une dimension à une autre : Monde des Douze, Incarnam, Srambad…",
    "La dimension la plus souvent rejointe, avec son nom dans le jeu.",
    "Expérience et niveaux gagnés.",
    "Points d’expérience gagnés.",
    "Niveaux gagnés.",
    "Kamas gagnés et dépensés.",
    "Kamas gagnés ; les retraits de la banque ou d’un coffre ne comptent pas, ni les ventes à l’hôtel des ventes, créditées à la banque.",
    "Kamas dépensés ; les dépôts à la banque ou dans un coffre ne comptent pas.",
    "Kamas gagnés moins kamas dépensés.",
    "Ressources récoltées, objets fabriqués, quêtes terminées et succès obtenus.",
    "Récoltes faites : une par ressource récoltée, quelle que soit la quantité obtenue.",
    "Objets fabriqués.",
    "Quêtes terminées.",
    "Succès obtenus.",
    "Combats terminés, temps, dégâts, soins, morts, âmes capturées et donjons.",
    "Combats contre des monstres terminés et victoires ; les autres combats sont comptés en défaites.",
    "Combats contre d’autres joueurs terminés hors Kolizéum et victoires ; les autres combats sont comptés en défaites.",
    "Combats de Kolizéum terminés et victoires ; les autres combats sont comptés en défaites.",
    "Temps passé en combat et durée moyenne d’un combat.",
    "Tours joués en combat.",
    "Dégâts infligés et subis en combat.",
    "Points de vie soignés en combat.",
    "Le plus gros coup infligé en une fois.",
    "Monstres vaincus dans les combats gagnés.",
    "Fois où le personnage a été vaincu en combat.",
    "Âmes capturées dans une pierre d’âme.",
    "Donjons terminés.",
    "Le donjon terminé le plus souvent, avec son nom dans le jeu.",
    "Sorts lancés et part de chaque élément dans les dégâts.",
    "Sorts lancés en combat.",
    "Les cinq sorts les plus lancés, avec leur nom dans le jeu.",
    "Les cinq sorts les plus lancés, avec leur nom dans le jeu.",
    "Les cinq sorts les plus lancés, avec leur nom dans le jeu.",
    "Les cinq sorts les plus lancés, avec leur nom dans le jeu.",
    "Les cinq sorts les plus lancés, avec leur nom dans le jeu.",
    "Part de chaque élément (neutre, terre, feu, eau, air) dans les dégâts infligés.",
    "Zaaps, zaapis, prismes, potions de rappel, Havre-Sac et trajets du mod.",
    "Téléportations par zaap.",
    "Téléportations par zaapi.",
    "Téléportations par prisme.",
    "Potions de rappel utilisées.",
    "Entrées dans un Havre-Sac.",
    "Trajets du mod menés à bien : bouton TP du chat, voyage depuis la carte, zaaps du Havre-Sac.",
    "Téléportations par une anomalie ; les potions de cité, la téléportation au conjoint et les téléportations des PNJ ne sont pas comptées.",
    "Efface les statistiques affichées : celles de ce personnage, ou celles de tous tes personnages avec « Tous mes personnages ». Clique sur Réinitialiser, puis sur Confirmer dans les 10 secondes ; sinon rien n’est effacé. Des statistiques effacées ne peuvent pas être récupérées."};
static_assert(std::size(StatsDescriptions) == StatsLineCount && std::size(StatsScopes) == 2);
constexpr int IconReset = 249; // FigmaIcons.reset, the game's own reset buttons
constexpr ULONGLONG StatsRefreshMs = 1000, StatsConfirmMs = 10000, StatsResultMs = 6000, StatsLogMs = 10000;
// Confirmer is the same button as Réinitialiser: a click within this time of the
// question (a double click, Enter twice) is the same gesture, never a confirmation.
constexpr ULONGLONG StatsConfirmWaitMs = 1000;
constexpr size_t MaxStatsViews = 4 * StatsLineCount; // pooled views and search clones showing a line
// <displayedText>k__BackingField of OptionElement`1: the label UpdateText shows.
constexpr const char* DisplayedText = "<displayedText>k__BackingField";
Method statsUpdateText = nullptr; // TextButtonOptionElement.UpdateText; null: labels follow at the next Init
// French formats: a thin no-break space between thousands, a no-break space
// before a unit.
constexpr const char* ThinSpace = "\u202F";
constexpr const char* NoBreak = "\u00A0";
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
std::string Named(stats::Table table, int key, const char* kind) {
    auto name = stats::KeyName(table, key);
    return name.empty() ? std::string(kind) + " " + std::to_string(key) : name;
}
std::string Who(const stats::Totals& totals) {
    return !totals.name.empty() ? totals.name : "n° " + std::to_string(totals.character);
}
// « de Lou », « d’Ana »; without a name, « de ce personnage ».
std::string Of(const stats::Totals& totals) {
    if (totals.name.empty()) return "de ce personnage";
    static constexpr const char* Vowels[] = {"A", "E", "I", "O", "U", "Y", "a", "e", "i", "o", "u", "y",
        "À", "Â", "Ä", "É", "È", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Ù", "Û", "Ü", "à", "â", "é", "è", "ê", "î", "ô", "û"};
    for (const auto vowel : Vowels) if (totals.name.rfind(vowel, 0) == 0) return "d’" + totals.name;
    return "de " + totals.name;
}
std::string StatsText(size_t line, const stats::Totals& totals, bool every) {
    using stats::Counter;
    using stats::Record;
    using stats::Table;
    const auto count = [&](Counter counter) { return totals.Count(counter); };
    const auto fights = [&](const char* kind, Counter all, Counter won) {
        const auto played = count(all), wins = std::min(count(won), played);
        return std::string("Combats ") + kind + " : " + Grouped(played) + " (" + Counted(wins, "victoire", "victoires") + ", " +
            Counted(played - wins, "défaite", "défaites") + ")";
    };
    const auto top = [&](Table table, size_t rank) -> const std::pair<int, int64_t>* {
        const auto& entries = totals.Entries(table);
        return rank < entries.size() ? &entries[rank] : nullptr;
    };
    const auto spell = [&](size_t rank) {
        const auto entry = top(Table::Spells, rank);
        const auto label = "Sort n° " + std::to_string(rank + 1) + " : ";
        if (!entry) return label + "aucun";
        return label + Named(Table::Spells, entry->first, "sort") + ", lancé " + Counted(entry->second, "fois", "fois");
    };
    switch (line) {
    case StatsWho:
        if (every) {
            if (totals.characters <= 0 || totals.first <= 0) return "Tous mes personnages : rien de compté pour l’instant";
            return "Tous mes personnages : " + Counted(totals.characters, "personnage, compté", "personnages, comptés") + " depuis le " + Day(totals.first);
        }
        if (totals.character <= 0) return "Ce personnage : aucun personnage connecté pour l’instant";
        if (totals.first <= 0) return "Ce personnage : " + Who(totals) + ", rien de compté pour l’instant";
        return "Ce personnage : " + Who(totals) + ", compté depuis le " + Day(totals.first);
    case TimeTitle: return "— Temps et déplacements —";
    case PlayTime: return "Temps de jeu : " + Hours(count(Counter::PlayMs));
    case LongestSession: return "Plus longue session : " + Hours(totals.Best(Record::LongestSessionMs));
    case CellsWalked:
        return "Cases parcourues : " + Grouped(count(Counter::CellsRoleplay)) + " hors combat, " + Grouped(count(Counter::CellsFight)) + " en combat";
    case MapsChanged: return "Cartes parcourues : " + Grouped(count(Counter::Maps));
    case DimensionsChanged: return "Changements de dimension : " + Grouped(count(Counter::Dimensions));
    case TopDimension: {
        const auto entry = top(Table::Dimensions, 0);
        if (!entry) return "Dimension la plus visitée : aucune";
        return "Dimension la plus visitée : " + Named(Table::Dimensions, entry->first, "dimension") + " (" + Counted(entry->second, "arrivée", "arrivées") + ")";
    }
    case ProgressTitle: return "— Progression —";
    case Experience: return "Expérience gagnée : " + Grouped(count(Counter::Xp));
    case LevelsGained: return "Niveaux gagnés : " + Grouped(count(Counter::Levels));
    case KamasTitle: return "— Kamas —";
    case KamasIn: return "Kamas gagnés : " + Kamas(count(Counter::KamasEarned));
    case KamasOut: return "Kamas dépensés : " + Kamas(count(Counter::KamasSpent));
    case KamasBalance: return "Solde : " + Balance(count(Counter::KamasEarned) - count(Counter::KamasSpent));
    case GatherTitle: return "— Récolte et artisanat —";
    case Harvested: return "Ressources récoltées : " + Grouped(count(Counter::Harvests));
    case Crafted: return "Objets fabriqués : " + Grouped(count(Counter::Crafts));
    case QuestsDone: return "Quêtes terminées : " + Grouped(count(Counter::Quests));
    case AchievementsDone: return "Succès obtenus : " + Grouped(count(Counter::Achievements));
    case FightTitle: return "— Combats —";
    case FightsMonsters: return fights("JcM", Counter::FightsPve, Counter::WinsPve);
    case FightsPlayers: return fights("JcJ", Counter::FightsPvp, Counter::WinsPvp);
    case FightsKolizeum: return fights("Kolizéum", Counter::FightsKolizeum, Counter::WinsKolizeum);
    case FightTime: {
        const auto played = count(Counter::FightsPve) + count(Counter::FightsPvp) + count(Counter::FightsKolizeum);
        const auto time = "Temps en combat : " + Hours(count(Counter::FightMs));
        return played > 0 ? time + " (" + Lasting(count(Counter::FightMs) / played) + " en moyenne)" : time;
    }
    case TurnsPlayed: return "Tours joués : " + Grouped(count(Counter::FightTurns));
    case DamageLine: return "Dégâts : " + Grouped(count(Counter::DamageDealt)) + " infligés, " + Grouped(count(Counter::DamageTaken)) + " subis";
    case HealsDone: return "Soins prodigués : " + Grouped(count(Counter::Heals));
    case BiggestHitLine: return "Plus gros coup : " + Grouped(totals.Best(Record::BiggestHit));
    case KillsLine: return "Monstres vaincus : " + Grouped(count(Counter::Kills));
    case DeathsLine: return "Morts : " + Grouped(count(Counter::Deaths));
    case CapturesLine: return "Âmes capturées : " + Grouped(count(Counter::Captures));
    case DungeonsDone: return "Donjons terminés : " + Grouped(count(Counter::Dungeons));
    case TopDungeon: {
        const auto entry = top(Table::Dungeons, 0);
        if (!entry) return "Donjon le plus fait : aucun";
        return "Donjon le plus fait : " + Named(Table::Dungeons, entry->first, "donjon") + " (" + Counted(entry->second, "fois", "fois") + ")";
    }
    case SpellTitle: return "— Sorts et éléments —";
    case SpellsCast: return "Sorts lancés : " + Grouped(count(Counter::SpellCasts));
    case TopSpell1: case TopSpell2: case TopSpell3: case TopSpell4: case TopSpell5: return spell(line - TopSpell1);
    case ElementShares: {
        // In the game's order of the elements: neutre, terre, feu, eau, air, then the others.
        int64_t total = 0, others = 0;
        std::array<int64_t, 5> parts{};
        for (const auto& [key, damage] : totals.Entries(Table::Elements)) {
            total += damage;
            if (key >= 0 && key < static_cast<int>(parts.size())) parts[static_cast<size_t>(key)] += damage;
            else others += damage;
        }
        if (total <= 0) return "Dégâts par élément : aucun dégât compté";
        std::string text = "Dégâts par élément : ";
        for (size_t i = 0; i < parts.size(); ++i)
            text += (i ? ", " : "") + Named(Table::Elements, static_cast<int>(i), "élément") + " " + Share(parts[i], total);
        if (others > 0) text += ", autres " + Share(others, total);
        return text;
    }
    case TeleportTitle: return "— Téléportations —";
    case ZaapsLine: return "Zaaps : " + Grouped(count(Counter::TeleportZaap));
    case ZaapisLine: return "Zaapis : " + Grouped(count(Counter::TeleportZaapi));
    case PrismsLine: return "Prismes : " + Grouped(count(Counter::TeleportPrism));
    case RecallLine: return "Potions de rappel : " + Grouped(count(Counter::RecallPotions));
    case HavenBagLine: return "Entrées dans un Havre-Sac : " + Grouped(count(Counter::HavenBag));
    case ModTripsLine: return "Trajets du mod : " + Grouped(count(Counter::ModTrips));
    case OtherTeleportsLine: return "Autres téléportations : " + Grouped(count(Counter::TeleportOther));
    default: return {};
    }
}
// The reset line: the question while it waits for Confirmer, then its outcome.
std::string StatsResetText(const OptionsUi& ui, const stats::Totals& totals) {
    const bool every = ui.statsConfirm ? ui.statsConfirmAll : ui.statsScope == 1;
    const auto whose = every ? std::string("de tous tes personnages") : Of(totals);
    if (ui.statsConfirm) return "Effacer les statistiques " + whose + " ? Clique sur Confirmer dans les 10 secondes.";
    if (ui.statsResult == 1) return "Statistiques " + whose + " effacées.";
    if (ui.statsResult == 2) return "Effacement impossible pour l’instant : réessaie dans un moment.";
    return every ? "Réinitialiser les statistiques de tous mes personnages" : "Réinitialiser les statistiques de ce personnage";
}
const char* StatsResetCaption(const OptionsUi& ui) { return ui.statsConfirm ? "Confirmer" : "Réinitialiser"; }
int StatsResetIcon(const OptionsUi& ui) { return ui.statsConfirm ? IconTrash : IconReset; }

template<class T> T& FieldValue(Obj owner, const char* name) {
    return *static_cast<T*>(api.FieldAddress(owner, name));
}
void SetTextField(Obj owner, const char* field, const char* text) {
    Ref value(api.string_new(text));
    api.gc_wbarrier_set_field(owner, static_cast<void**>(api.FieldAddress(owner, field)), value.get());
}
// A field of the class or of a parent (a generic base) at its pinned offset.
void RequireField(Klass type, const char* name, size_t offset) {
    for (auto current = type; current; current = api.class_get_parent(current)) {
        const auto field = api.class_get_field_from_name(current, name);
        if (!field) continue;
        if (api.field_get_offset(field) == offset) return;
        break;
    }
    throw std::runtime_error(std::string("Options field mismatch: ") + name);
}
int Count(Obj list) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > 4096) throw std::runtime_error("Invalid options list size");
    return count;
}
Obj Item(Obj list, int index) { return api.Call(list, "get_Item", {&index}); }
bool Contains(Obj list, Obj item) { return api.Value<bool>(api.Call(list, "Contains", {item})); }
bool IsGeneral(Obj page) {
    return page && generalClass && api.class_is_assignable_from(generalClass, api.object_get_class(page));
}
Obj BindingGet(Obj page, const char* getter) {
    auto binding = api.Class("Core.dll", "Core.UILogic.Bindings", "OptionTabBinding");
    return api.Invoke(api.Find(binding, getter, 0), api.FieldAddress(page, "dzyi"));
}
void Describe(Obj option, const char* title, const char* description) {
    Ref text(api.string_new(title));
    api.Call(option, "set_text", {text.get()});
    Ref data(api.New(api.Class("Core.dll", Config, "DescriptionData")));
    SetTextField(data.get(), "text", description);
    api.Call(option, "set_description", {data.get()});
}
Obj MakeCategory(const char* name, int id, bool subcategory) {
    Ref category(api.New(api.Class("Core.dll", Config, "CategoryData")));
    SetTextField(category.get(), "name", name);
    FieldValue<int>(category.get(), "categoryId") = id;
    FieldValue<bool>(category.get(), "isSubCategory") = subcategory;
    if (!api.Field(category.get(), "subcategories") || !api.Field(category.get(), "options"))
        throw std::runtime_error("Native option category lists are unavailable");
    return category.get();
}

bool ModelsLoaded(const OptionsUi& ui) {
    auto loaded = [](const Ref& source) {
        return source.get() && api.Value<bool>(api.Call(source.get(), "IsLoaded"));
    };
    const auto allLoaded = [&](const auto& sources) {
        for (const auto& source : sources) if (!loaded(source)) return false;
        return true;
    };
    return loaded(ui.modeSource) && loaded(ui.roleplayModeSource) && allLoaded(ui.combatMiniatureSources) &&
        allLoaded(ui.roleplayMiniatureSources) && allLoaded(ui.animationSources) && loaded(ui.autoLoadSource) && loaded(ui.soulSource) && loaded(ui.soulAutoSource) && loaded(ui.glowSource) && loaded(ui.glowNpcsSource) && loaded(ui.glowInteractivesSource) && loaded(ui.transitionsSource) && loaded(ui.preloadSource) && loaded(ui.spouseSource) && loaded(ui.resourcesSource) && loaded(ui.resourcesMiniSource) && loaded(ui.miniCenterSource) && loaded(ui.githubSource) && loaded(ui.metamobSyncSource) && loaded(ui.travelPlannerSource) && loaded(ui.chatTravelSource) && loaded(ui.chatSoulSource) && loaded(ui.chatOcreSource) && loaded(ui.chatMetamobSource) && loaded(ui.archiZonesSource) && loaded(ui.archiZonesMiniSource) && loaded(ui.calendarKraloveSource) && loaded(ui.templeSource) &&
        allLoaded(ui.extraSources) && loaded(ui.perfCornerSource) && loaded(ui.chatCoordsSource) &&
        loaded(ui.statsSource) && loaded(ui.statsScopeSource);
}
template<class T> void SetSource(Obj source, T value, bool notify) {
    if (api.Value<T>(api.Call(source, "get_property")) != value)
        api.Call(source, notify ? "set_property" : "SetValueWithoutNotify", {&value});
}
void ApplySettings(OptionsUi& ui, const Settings& settings, bool notify) {
    SetSource(ui.modeSource.get(), settings.mode, notify);
    SetSource(ui.roleplayModeSource.get(), settings.roleplayMode, notify);
    for (size_t i = 0; i < ui.combatMiniatureSources.size(); ++i)
        SetSource(ui.combatMiniatureSources[i].get(), settings.*CombatMiniatureToggles[i], notify);
    for (size_t i = 0; i < ui.roleplayMiniatureSources.size(); ++i)
        SetSource(ui.roleplayMiniatureSources[i].get(), settings.*RoleplayMiniatureToggles[i], notify);
    for (size_t i = 0; i < ui.animationSources.size(); ++i)
        SetSource(ui.animationSources[i].get(), settings.*AnimationToggles[i], notify);
    SetSource(ui.autoLoadSource.get(), settings.autoLoad, notify);
    SetSource(ui.githubSource.get(), settings.githubUpdates, notify);
    SetSource(ui.soulSource.get(), settings.soulReminder, notify);
    SetSource(ui.soulAutoSource.get(), settings.soulAutoEquip, notify);
    SetSource(ui.glowSource.get(), settings.monsterGlowMode, notify);
    SetSource(ui.glowNpcsSource.get(), settings.glowNpcs, notify);
    SetSource(ui.glowInteractivesSource.get(), settings.glowInteractives, notify);
    SetSource(ui.transitionsSource.get(), settings.fastMapTransitions, notify);
    SetSource(ui.preloadSource.get(), settings.preloadNextMap, notify);
    SetSource(ui.spouseSource.get(), settings.spouseTeleport, notify);
    SetSource(ui.resourcesSource.get(), settings.resourceMapEnabled, notify);
    SetSource(ui.resourcesMiniSource.get(), settings.resourceMiniMapEnabled, notify);
    SetSource(ui.miniCenterSource.get(), settings.miniMapCenterButton, notify);
    SetSource(ui.metamobSyncSource.get(), settings.metamobSync, notify);
    SetSource(ui.travelPlannerSource.get(), settings.mapTravelPlanner, notify);
    SetSource(ui.chatTravelSource.get(), settings.chatTravelLinks, notify);
    SetSource(ui.chatSoulSource.get(), settings.chatSoulInventory, notify);
    SetSource(ui.chatOcreSource.get(), settings.chatOcreStatus, notify);
    SetSource(ui.chatMetamobSource.get(), settings.chatMetamobStatus, notify);
    SetSource(ui.archiZonesSource.get(), settings.archiZonesMap, notify);
    SetSource(ui.archiZonesMiniSource.get(), settings.archiZonesMiniMap, notify);
    SetSource(ui.calendarKraloveSource.get(), settings.calendarKralamoure, notify);
    SetSource(ui.templeSource.get(), settings.travelRoublardTemple, notify);
    for (size_t i = 0; i < ui.extraSources.size(); ++i)
        SetSource(ui.extraSources[i].get(), settings.*ExtraToggles[i], notify);
    SetSource(ui.perfCornerSource.get(), settings.perfOverlayCorner, notify);
    SetSource(ui.chatCoordsSource.get(), settings.coordsWorldChatMode, notify);
    SetSource(ui.statsSource.get(), settings.statsEnabled, notify);
}
using Choices = std::vector<std::pair<int, std::string>>;
Obj MakeChoiceOption(Obj source, Obj container, const char* title, const char* description, const Choices& entries) {
    int size = -1, icon = 0; // FigmaIcons.none
    bool wheel = false;
    Ref option(api.New(api.Class("Core.dll", Elements, "MultipleChoiceOption"),
        {source, container, &size, &wheel, &icon}));
    Describe(option.get(), title, description);
    auto choicesSetter = api.Find(api.object_get_class(option.get()), "set_choices", 1);
    Ref choices(api.New(api.class_from_type(api.method_get_param(choicesSetter, 0))));
    auto addChoice = api.Find(api.object_get_class(choices.get()), "Add", 1);
    auto tupleClass = api.class_from_type(api.method_get_param(addChoice, 0));
    if (!tupleClass || !api.class_is_valuetype(tupleClass)) throw std::runtime_error("Native choice tuple type unavailable");
    for (const auto& [value, label] : entries) {
        int id = value;
        Ref text(api.string_new(label.c_str()));
        Ref tuple(api.New(tupleClass, {&id, text.get()}));
        api.Invoke(addChoice, choices.get(), {api.object_unbox(tuple.get())});
    }
    api.Invoke(choicesSetter, option.get(), {choices.get()});
    return option.get();
}
Obj MakeBoolOption(Obj source, Obj container, Obj tooltip, const char* title, const char* description) {
    Ref option(api.New(api.Class("Core.dll", Elements, "BoolOption"), {tooltip, source, container}));
    Describe(option.get(), title, description);
    return option.get();
}
// Owned text-button rows have no delegate (null callback): TextButtonHook knows
// them by their unique description reference, kept as marker.
Obj MakeTextButtonOption(Obj tooltip, const char* title, const char* description, const char* button, int icon, Ref& marker) {
    Ref option(api.New(api.Class("Core.dll", Config, "TextButtonOption"), {nullptr, tooltip}));
    Describe(option.get(), title, description);
    SetTextField(option.get(), "buttonText", button);
    if (icon) FieldValue<int>(option.get(), "icon") = icon; // ButtonOption.icon, shown by TextButtonOptionElement.Init
    marker.reset(api.Call(option.get(), "get_description"));
    if (!marker.get()) throw std::runtime_error("Owned option identity unavailable");
    return option.get();
}

bool EnsureModels(OptionsUi& ui, Obj page) {
    Ref current(api.Call(module->cached.get(), "get_dofusOptionsContainer"));
    if (!current.get() || api.Field(current.get(), "creatureModeFight") != module->creatureOption.get())
        return false;
    if (ui.container.get()) {
        if (ui.container.get() != current.get()) return false;
        return ModelsLoaded(ui);
    }
    Ref existingInt(api.Field(current.get(), "creaturesMode"));
    if (!existingInt.get()) throw std::runtime_error("Integer option type unavailable");
    Settings defaults;
    bool readOnly = false;
    const auto integerType = api.object_get_class(existingInt.get());
    const auto booleanType = api.object_get_class(module->creatureOption.get());
    OptionsUi prepared;
    prepared.modeSource.reset(api.New(integerType, {&defaults.mode, &readOnly}));
    prepared.roleplayModeSource.reset(api.New(integerType, {&defaults.roleplayMode, &readOnly}));
    // One private source per checkbox, its reset default the Settings default.
    const auto toggleSources = [&](auto& sources, const auto& fields) {
        for (size_t i = 0; i < sources.size(); ++i) {
            bool value = defaults.*fields[i];
            sources[i].reset(api.New(booleanType, {&value, &readOnly}));
        }
    };
    toggleSources(prepared.combatMiniatureSources, CombatMiniatureToggles);
    toggleSources(prepared.roleplayMiniatureSources, RoleplayMiniatureToggles);
    toggleSources(prepared.animationSources, AnimationToggles);
    toggleSources(prepared.extraSources, ExtraToggles);
    prepared.perfCornerSource.reset(api.New(integerType, {&defaults.perfOverlayCorner, &readOnly}));
    prepared.chatCoordsSource.reset(api.New(integerType, {&defaults.coordsWorldChatMode, &readOnly}));
    prepared.autoLoadSource.reset(api.New(booleanType, {&defaults.autoLoad, &readOnly}));
    prepared.githubSource.reset(api.New(booleanType, {&defaults.githubUpdates, &readOnly}));
    prepared.soulSource.reset(api.New(booleanType, {&defaults.soulReminder, &readOnly}));
    prepared.soulAutoSource.reset(api.New(booleanType, {&defaults.soulAutoEquip, &readOnly}));
    prepared.glowSource.reset(api.New(integerType, {&defaults.monsterGlowMode, &readOnly}));
    prepared.glowNpcsSource.reset(api.New(booleanType, {&defaults.glowNpcs, &readOnly}));
    prepared.glowInteractivesSource.reset(api.New(booleanType, {&defaults.glowInteractives, &readOnly}));
    prepared.transitionsSource.reset(api.New(booleanType, {&defaults.fastMapTransitions, &readOnly}));
    prepared.preloadSource.reset(api.New(booleanType, {&defaults.preloadNextMap, &readOnly}));
    prepared.spouseSource.reset(api.New(booleanType, {&defaults.spouseTeleport, &readOnly}));
    prepared.resourcesSource.reset(api.New(booleanType, {&defaults.resourceMapEnabled, &readOnly}));
    prepared.resourcesMiniSource.reset(api.New(booleanType, {&defaults.resourceMiniMapEnabled, &readOnly}));
    prepared.miniCenterSource.reset(api.New(booleanType, {&defaults.miniMapCenterButton, &readOnly}));
    prepared.metamobSyncSource.reset(api.New(booleanType, {&defaults.metamobSync, &readOnly}));
    prepared.travelPlannerSource.reset(api.New(booleanType, {&defaults.mapTravelPlanner, &readOnly}));
    prepared.chatTravelSource.reset(api.New(booleanType, {&defaults.chatTravelLinks, &readOnly}));
    prepared.chatSoulSource.reset(api.New(booleanType, {&defaults.chatSoulInventory, &readOnly}));
    prepared.chatOcreSource.reset(api.New(booleanType, {&defaults.chatOcreStatus, &readOnly}));
    prepared.chatMetamobSource.reset(api.New(booleanType, {&defaults.chatMetamobStatus, &readOnly}));
    prepared.archiZonesSource.reset(api.New(booleanType, {&defaults.archiZonesMap, &readOnly}));
    prepared.archiZonesMiniSource.reset(api.New(booleanType, {&defaults.archiZonesMiniMap, &readOnly}));
    prepared.calendarKraloveSource.reset(api.New(booleanType, {&defaults.calendarKralamoure, &readOnly}));
    prepared.templeSource.reset(api.New(booleanType, {&defaults.travelRoublardTemple, &readOnly}));
    // « Statistiques »: the counting checkbox, and what is shown (this character by default).
    prepared.statsSource.reset(api.New(booleanType, {&defaults.statsEnabled, &readOnly}));
    int thisCharacter = 0;
    prepared.statsScopeSource.reset(api.New(integerType, {&thisCharacter, &readOnly}));
    if (!ModelsLoaded(prepared)) return false;
    // Native reset uses constructor defaults, independently of persisted values.
    ApplySettings(prepared, module->settings, false);
    Ref tooltip(api.Call(page, "blrl"));
    if (!tooltip.get()) throw std::runtime_error("Native options tooltip service unavailable");
    Choices modes;
    Choices glowModes;
    for (int i = 0; i < static_cast<int>(GlowModeLabels.size()); ++i)
        glowModes.emplace_back(i, GlowModeLabels[i]);
    for (int i = 0; i != 3; ++i) modes.emplace_back(i, Modes[i]);
    prepared.modeOption.reset(MakeChoiceOption(prepared.modeSource.get(), current.get(), "En combat",
        "En combat uniquement : apparence normale, créatures ou miniatures. Les raccourcis utilisent ce même réglage. Miniatures réduit à 60 % de la taille d’origine, avec un minimum automatique de 50 % de la hauteur de référence d’un joueur pour les monstres et invocations, lorsque les mesures sont disponibles. Avec Miniatures, seules les catégories cochées ci-dessous sont réduites, les autres gardent leur taille normale ; Créatures s’applique à tous les combattants.", modes));
    for (size_t i = 0; i < prepared.combatMiniatureOptions.size(); ++i)
        prepared.combatMiniatureOptions[i].reset(MakeBoolOption(prepared.combatMiniatureSources[i].get(), current.get(), tooltip.get(),
            CombatMiniatureRows[i].title, CombatMiniatureRows[i].description));
    prepared.roleplayModeOption.reset(MakeChoiceOption(prepared.roleplayModeSource.get(), current.get(), "Hors combat",
        "Affichage des personnages et monstres hors combat, indépendant du réglage en combat. Les raccourcis utilisent ce même réglage. Miniatures réduit à 60 % de la taille d’origine, avec un minimum automatique de 50 % de la hauteur de référence d’un joueur pour les monstres et invocations, lorsque les mesures sont disponibles. Avec Miniatures, seules les catégories cochées ci-dessous sont réduites ; les autres éléments le sont toujours. Créatures s’applique à tous, sauf aux PNJ, que le jeu laisse tels quels.", modes));
    for (size_t i = 0; i < prepared.roleplayMiniatureOptions.size(); ++i)
        prepared.roleplayMiniatureOptions[i].reset(MakeBoolOption(prepared.roleplayMiniatureSources[i].get(), current.get(), tooltip.get(),
            RoleplayMiniatureRows[i].title, RoleplayMiniatureRows[i].description));
    // Section « Animations »: each toggle on its own row, all in combat only.
    for (size_t i = 0; i < prepared.animationOptions.size(); ++i)
        prepared.animationOptions[i].reset(MakeBoolOption(prepared.animationSources[i].get(), current.get(), tooltip.get(),
            AnimationToggleLabels[i], AnimationDescriptions[i]));
    prepared.autoLoadOption.reset(MakeBoolOption(prepared.autoLoadSource.get(), current.get(), tooltip.get(), "Chargement automatique",
        "Charge la dernière version compatible lors des prochains lancements de Dofus, sans saisie du PID. Si les mises à jour GitHub sont activées, vérifie les nouvelles versions avant le chargement. Les clients déjà ouverts conservent leur version chargée."));
    prepared.githubOption.reset(MakeBoolOption(prepared.githubSource.get(), current.get(), tooltip.get(), "Mises à jour GitHub",
        "À chaque nouveau lancement, recherche une version publiée sur Altansar69/dofus-enhanced, vérifie son intégrité et sa compatibilité avec Dofus, puis la télécharge. Sans réseau, conserve la version locale compatible. Nécessite le chargement automatique. Un correctif du mod peut rester nécessaire après une mise à jour du jeu."));
    prepared.soulOption.reset(MakeBoolOption(prepared.soulSource.get(), current.get(), tooltip.get(), "Alerte pierre d’âme",
        "Pour un combat contenant un boss ou un archimonstre avec une âme capturable définie. Masqué pour les groupes ordinaires et les cartes interdisant la capture. Pendant le placement, affiche la tranche minimale requise, le stock et la pierre équipée. Équiper choisit uniquement cette tranche. En combat : Capture d’âme en rouge si l’état Chercheur d’âmes manque, en vert s’il est actif. L’effet ne garantit pas à lui seul la capture."));
    prepared.soulAutoOption.reset(MakeBoolOption(prepared.soulAutoSource.get(), current.get(), tooltip.get(), "Équiper automatiquement la pierre d’âme",
        "Avec l’alerte pierre d’âme activée, tente d’équiper la pierre de la tranche requise pendant le placement d’un combat capturable. Une seule tentative par combat et tranche, uniquement si une pierre utilisable est disponible. En cas de refus, le bouton Équiper reste disponible. Activé par défaut."));
    ui.modeSource = std::move(prepared.modeSource);
    prepared.glowOption.reset(MakeChoiceOption(prepared.glowSource.get(), current.get(), "Surbrillance",
        "Affiche le contour des monstres derrière les modules d’affichage, derrière ces modules et les murs, ou tout le temps. La grande carte, l’inventaire, les sorts et les autres fenêtres restent devant le contour dans tous les modes. Les clics restent ceux du jeu. Par défaut : derrière l’interface.", glowModes));
    prepared.glowNpcsOption.reset(MakeBoolOption(prepared.glowNpcsSource.get(), current.get(), tooltip.get(), "Inclure les PNJ",
        "Ajoute les PNJ visibles hors combat au niveau de surbrillance choisi ci-dessus. Indépendant des éléments interactifs. Désactivé par défaut."));
    prepared.glowInteractivesOption.reset(MakeBoolOption(prepared.glowInteractivesSource.get(), current.get(), tooltip.get(), "Inclure les éléments interactifs",
        "Ajoute les objets visibles avec une action disponible hors combat : ressources récoltables, portes, zaaps… Utilise le niveau de surbrillance choisi ci-dessus. Désactivé par défaut."));
    prepared.transitionsOption.reset(MakeBoolOption(prepared.transitionsSource.get(), current.get(), tooltip.get(), "Transitions rapides entre cartes",
        "Raccourcit les fondus de sortie et d’arrivée hors combat. Le chargement des ressources et l’attente du serveur restent nécessaires : cette option ne garantit pas des changements de carte sans chargement. Activé par défaut."));
    prepared.preloadOption.reset(MakeBoolOption(prepared.preloadSource.get(), current.get(), tooltip.get(), "Précharger la prochaine carte (expérimental)",
        "Pendant le déplacement vers une sortie connue, prépare les données et une partie des décors de la prochaine carte dans le cache du jeu. Une seule carte conservée brièvement, ressources limitées. Peut réduire le chargement local, avec un surcoût de mémoire ; le serveur et la construction de la scène restent nécessaires. Activé par défaut."));
    prepared.spouseOption.reset(MakeBoolOption(prepared.spouseSource.get(), current.get(), tooltip.get(), "Téléportation au conjoint",
        "Ajoute un bouton de téléportation, un cœur et la distance sur la vignette de votre conjoint dans le groupe, ainsi qu’une entrée au clic droit. Le bouton est indisponible si votre conjoint est hors ligne ou pendant un combat. La téléportation respecte les conditions du jeu et se lance uniquement au clic. Activé par défaut."));
    prepared.resourcesOption.reset(MakeBoolOption(prepared.resourcesSource.get(), current.get(), tooltip.get(), "Ressources sur la grande carte",
        "Ajoute sur la grande carte (M) un carré « Ressources » sous « Anomalies ». Comme les modes du jeu, il ouvre une fenêtre : ouvre un métier puis coche les ressources souhaitées. La carte et la mini-carte affichent leurs points de récolte recensés par case. Cela ne renseigne pas leur disponibilité en temps réel. Désactiver retire le carré et conserve ta sélection. Activé par défaut."));
    prepared.resourcesMiniOption.reset(MakeBoolOption(prepared.resourcesMiniSource.get(), current.get(), tooltip.get(), "Ressources sur la mini-carte",
        "Affiche les repères et la sélection des ressources sur la mini-carte. Indépendant de la grande carte ; les ressources sélectionnées restent partagées."));
    prepared.miniCenterOption.reset(MakeBoolOption(prepared.miniCenterSource.get(), current.get(), tooltip.get(), "Bouton de recentrage sur la mini-carte",
        "Affiche en bas à gauche de la mini-carte le bouton qui la ramène sur ton personnage, avec le même aspect que le bouton joueur de la grande carte. Décocher le masque."));
    prepared.archiZonesOption.reset(MakeBoolOption(prepared.archiZonesSource.get(), current.get(), tooltip.get(), "Archimonstres manquants sur la grande carte",
        "Ajoute sur la grande carte un carré « Ocre » sous « Conquête ». Comme le mode Anomalies, il ouvre un panneau (recherche, territoires, niveaux, Tous / Archimonstres / Boss de donjon) et colore chaque zone des archimonstres et boss de donjon de L’éternelle moisson selon le nombre qu’il te manque : ni validés dans la quête, ni dans ton sac, ni sur ta quête Metamob (si une clé est enregistrée). Du jaune au pourpre, plus il en manque ; en bleu, « Complet », quand il ne t’en manque aucun. Un clic sur une zone de la liste centre la carte et montre, sous la ligne, ceux qu’il te manque et ceux que tu as. Activé par défaut."));
    prepared.archiZonesMiniOption.reset(MakeBoolOption(prepared.archiZonesMiniSource.get(), current.get(), tooltip.get(), "Archimonstres manquants sur la mini-carte",
        "Colore aussi les zones sur la mini-carte, avec les mêmes couleurs (bleu « Complet » compris), selon le choix Tous / Archimonstres / Boss de donjon du panneau Ocre de la grande carte (sans sa recherche ni ses filtres de territoire et de niveau), même quand ce mode est fermé. Désactivé par défaut."));
    prepared.travelPlannerOption.reset(MakeBoolOption(prepared.travelPlannerSource.get(), current.get(), tooltip.get(), "Remplacer l’autopilote du jeu",
        "Le double-clic sur la grande carte ou la mini-carte et l’entrée « Voyager jusqu’à cette position » de leur clic droit lancent le trajet du mod : zaaps, directs ou par le Havre-Sac, puis l’autopilote du jeu. Ctrl + double-clic, le menu d’un point de repère et la commande /travel gardent l’autopilote du jeu. Décoché, le double-clic et cette entrée lancent directement l’autopilote du jeu. Activé par défaut."));
    prepared.templeOption.reset(MakeBoolOption(prepared.templeSource.get(), current.get(), tooltip.get(), TempleTitle, TempleDescription));
    // Section « Chat »: each addition next to positions and monster groups shared in chat.
    prepared.chatTravelOption.reset(MakeBoolOption(prepared.chatTravelSource.get(), current.get(), tooltip.get(), "Bouton TP dans le chat",
        "Ajoute TP à côté des positions et des groupes de monstres partagés dans le chat, et fait des coordonnées écrites [x,y] des liens de position. Un clic lance le trajet du mod : zaaps, directs ou par le Havre-Sac, puis l’autopilote du jeu (indisponible en combat). Activé par défaut."));
    prepared.chatSoulOption.reset(MakeBoolOption(prepared.chatSoulSource.get(), current.get(), tooltip.get(), "Âme en sac dans le chat",
        "À côté d’un archimonstre ou d’un boss de L’éternelle moisson partagé dans le chat, indique si une de ses âmes est dans le sac de ce personnage : Âme en sac ou Pas en sac (Sac : ? tant que le sac n’est pas lu). La banque et le coffre ne sont pas consultés ; décoché, le sac n’est plus lu. Activé par défaut."));
    prepared.chatOcreOption.reset(MakeBoolOption(prepared.chatOcreSource.get(), current.get(), tooltip.get(), "Avancement Ocre dans le chat",
        "À côté d’un archimonstre ou d’un boss de L’éternelle moisson partagé dans le chat, indique son objectif : Ocre validé, Ocre à faire ou Ocre non commencée (Ocre : ? sans données). Un clic ouvre la quête dans le journal. Activé par défaut."));
    prepared.chatMetamobOption.reset(MakeBoolOption(prepared.chatMetamobSource.get(), current.get(), tooltip.get(), "Metamob dans le chat",
        "Après l’avancement Ocre, indique combien d’exemplaires de ce monstre ta quête Metamob compte : Metamob : 2, ou Metamob : 0 en rouge (Metamob : ? tant que la quête n’est pas lue). Nécessite une clé Metamob enregistrée ; la quête est relue au plus toutes les 10 minutes et une capture synchronisée la met à jour aussitôt. Activé par défaut."));
    // Section « Calendrier »: Metamob's Kralamoure openings in the game's calendar.
    prepared.calendarKraloveOption.reset(MakeBoolOption(prepared.calendarKraloveSource.get(), current.get(), tooltip.get(), "Ouvertures Kralamoure dans le calendrier",
        "Dans le calendrier du jeu, ajoute « Kralamoure » à « Mes calendriers », avec son icône et sa couleur ; sa case est cette option. Coché, les ouvertures planifiées sur Metamob pour le serveur de ta quête s’affichent comme les autres événements : bannière, puis une carte par ouverture sur son jour, avec l’heure locale et les personnages inscrits ; survole une carte pour l’organisateur et la description, clique pour ouvrir l’événement sur metamob.fr. Nécessite une clé Metamob enregistrée ; la liste n’est lue que calendrier ouvert et Kralamoure coché, au plus toutes les 10 minutes. Activé par défaut."));
    for (size_t i = 0; i < prepared.extraOptions.size(); ++i)
        prepared.extraOptions[i].reset(MakeBoolOption(prepared.extraSources[i].get(), current.get(), tooltip.get(),
            ExtraRows[i].title, ExtraRows[i].description));
    Choices corners;
    for (int i = 0; i != static_cast<int>(std::size(PerfCorners)); ++i) corners.emplace_back(i, PerfCorners[i]);
    prepared.perfCornerOption.reset(MakeChoiceOption(prepared.perfCornerSource.get(), current.get(), "Position de l’affichage des performances",
        "Coin de l’écran où s’affichent les FPS et la mémoire quand « Afficher les FPS et la mémoire » est coché. En haut à droite, le cadre se place sous les boutons du menu du jeu. Choisir un autre coin y replace l’affichage déplacé à la souris. En haut à gauche par défaut.", corners));
    Choices chatDimensions;
    for (int i = 0; i != static_cast<int>(std::size(ChatDimensionChoices)); ++i) chatDimensions.emplace_back(i, ChatDimensionChoices[i]);
    prepared.chatCoordsOption.reset(MakeChoiceOption(prepared.chatCoordsSource.get(), current.get(), "Dimension avec les coordonnées dans le chat",
        "Les positions et les groupes de monstres partagés dans le chat s’affichent avec leur dimension : [x,y, dimension]. « Oui, sauf dans le Monde des Douze » ne l’ajoute qu’aux autres dimensions (Incarnam, Srambad…). Le lien du jeu, son clic et le bouton TP ne changent pas ; des coordonnées simplement écrites [x,y] restent comme le joueur les a tapées. « Oui » par défaut.", chatDimensions));
    // Section « Statistiques »: the checkbox, what is shown, then one owned
    // text-button row per line (see BindStatsView), the last one the reset.
    prepared.statsOption.reset(MakeBoolOption(prepared.statsSource.get(), current.get(), tooltip.get(), StatsTitle, StatsDescription));
    Choices scopes;
    for (int i = 0; i != static_cast<int>(std::size(StatsScopes)); ++i) scopes.emplace_back(i, StatsScopes[i]);
    prepared.statsScopeOption.reset(MakeChoiceOption(prepared.statsScopeSource.get(), current.get(), StatsScopeTitle, StatsScopeDescription, scopes));
    {
        const auto totals = stats::Shown(false);
        for (size_t line = 0; line < StatsLineCount; ++line) {
            const bool reset = line == StatsReset;
            prepared.statsTexts.push_back(reset ? StatsResetText(prepared, totals) : StatsText(line, totals, false));
            Ref marker;
            prepared.statsRows.emplace_back(MakeTextButtonOption(tooltip.get(), prepared.statsTexts.back().c_str(), StatsDescriptions[line],
                reset ? StatsResetCaption(prepared) : "", reset ? StatsResetIcon(prepared) : 0, marker));
            prepared.statsMarkers.push_back(std::move(marker));
        }
    }
    // Section « À propos », alone: credits, an owned text button (see TextButtonHook).
    const std::string creditsTitle = std::string(ModDisplayName) + " " + ModSemanticVersion + " — par Altansar";
    const std::string creditsText = std::string("Mod créé par Altansar. Code source, versions et signalements : ") + ProjectUrl +
        " (Dofus 3.7.12.12). Le bouton ouvre la page du projet dans le navigateur.";
    prepared.creditsOption.reset(MakeTextButtonOption(tooltip.get(), creditsTitle.c_str(), creditsText.c_str(), "Ouvrir GitHub", 0, prepared.creditsMarker));
    // « Pierre d’âme », after the soul rows: the Metamob sync preference, then the key row, an owned
    // text button (see BindKeyField). The key itself stays in the capture sync, reached through ui.metamob only.
    prepared.metamobSyncOption.reset(MakeBoolOption(prepared.metamobSyncSource.get(), current.get(), tooltip.get(), "Synchroniser les captures avec Metamob",
        "Après chaque capture d’un archimonstre ou d’un boss (pierre d’âme remplie en combat), ajoute 1 à ce monstre dans ta quête Ocre sur Metamob, sans rien modifier d’autre. Nécessite ta clé API personnelle (metamob.fr › Paramètres › Clé API). Désactivé par défaut."));
    prepared.metamobKeyOption.reset(MakeTextButtonOption(tooltip.get(), "Clé API Metamob",
        "Colle ta clé dans le champ masqué puis clique sur Enregistrer. Elle est vérifiée auprès de Metamob, chiffrée pour ton compte Windows et gardée uniquement sur ce PC ; elle n’est jamais affichée, journalisée ni incluse dans les mises à jour. Une fois enregistrée, elle reste masquée et la corbeille la supprime de ce PC, après confirmation. Elle reste valable sur Metamob : régénère-la sur le site pour la révoquer.",
        "Enregistrer", IconSave, prepared.metamobKeyMarker));
    ui.creditsOption = std::move(prepared.creditsOption);
    ui.creditsMarker = std::move(prepared.creditsMarker);
    ui.metamobSyncSource = std::move(prepared.metamobSyncSource);
    ui.metamobSyncOption = std::move(prepared.metamobSyncOption);
    ui.metamobKeyOption = std::move(prepared.metamobKeyOption);
    ui.metamobKeyMarker = std::move(prepared.metamobKeyMarker);
    ui.resourcesMiniSource = std::move(prepared.resourcesMiniSource);
    ui.resourcesMiniOption = std::move(prepared.resourcesMiniOption);
    ui.miniCenterSource = std::move(prepared.miniCenterSource);
    ui.miniCenterOption = std::move(prepared.miniCenterOption);
    ui.travelPlannerSource = std::move(prepared.travelPlannerSource);
    ui.travelPlannerOption = std::move(prepared.travelPlannerOption);
    ui.chatTravelSource = std::move(prepared.chatTravelSource);
    ui.chatTravelOption = std::move(prepared.chatTravelOption);
    ui.chatSoulSource = std::move(prepared.chatSoulSource);
    ui.chatSoulOption = std::move(prepared.chatSoulOption);
    ui.chatOcreSource = std::move(prepared.chatOcreSource);
    ui.chatOcreOption = std::move(prepared.chatOcreOption);
    ui.chatMetamobSource = std::move(prepared.chatMetamobSource);
    ui.chatMetamobOption = std::move(prepared.chatMetamobOption);
    ui.archiZonesSource = std::move(prepared.archiZonesSource);
    ui.archiZonesOption = std::move(prepared.archiZonesOption);
    ui.archiZonesMiniSource = std::move(prepared.archiZonesMiniSource);
    ui.archiZonesMiniOption = std::move(prepared.archiZonesMiniOption);
    ui.calendarKraloveSource = std::move(prepared.calendarKraloveSource);
    ui.calendarKraloveOption = std::move(prepared.calendarKraloveOption);
    ui.templeSource = std::move(prepared.templeSource);
    ui.templeOption = std::move(prepared.templeOption);
    ui.extraSources = std::move(prepared.extraSources);
    ui.extraOptions = std::move(prepared.extraOptions);
    ui.perfCornerSource = std::move(prepared.perfCornerSource);
    ui.perfCornerOption = std::move(prepared.perfCornerOption);
    ui.chatCoordsSource = std::move(prepared.chatCoordsSource);
    ui.chatCoordsOption = std::move(prepared.chatCoordsOption);
    ui.statsSource = std::move(prepared.statsSource);
    ui.statsOption = std::move(prepared.statsOption);
    ui.statsScopeSource = std::move(prepared.statsScopeSource);
    ui.statsScopeOption = std::move(prepared.statsScopeOption);
    ui.statsRows = std::move(prepared.statsRows);
    ui.statsMarkers = std::move(prepared.statsMarkers);
    ui.statsTexts = std::move(prepared.statsTexts);
    ui.statsScope = 0;
    ui.statsConfirm = ui.statsConfirmAll = false;
    ui.statsResult = 0;
    ui.resourcesSource = std::move(prepared.resourcesSource);
    ui.resourcesOption = std::move(prepared.resourcesOption);
    ui.spouseSource = std::move(prepared.spouseSource);
    ui.spouseOption = std::move(prepared.spouseOption);
    ui.preloadSource = std::move(prepared.preloadSource);
    ui.preloadOption = std::move(prepared.preloadOption);
    ui.transitionsSource = std::move(prepared.transitionsSource);
    ui.transitionsOption = std::move(prepared.transitionsOption);
    ui.glowSource = std::move(prepared.glowSource);
    ui.glowOption = std::move(prepared.glowOption);
    ui.glowNpcsSource = std::move(prepared.glowNpcsSource);
    ui.glowNpcsOption = std::move(prepared.glowNpcsOption);
    ui.glowInteractivesSource = std::move(prepared.glowInteractivesSource);
    ui.glowInteractivesOption = std::move(prepared.glowInteractivesOption);
    ui.combatMiniatureSources = std::move(prepared.combatMiniatureSources);
    ui.combatMiniatureOptions = std::move(prepared.combatMiniatureOptions);
    ui.roleplayMiniatureSources = std::move(prepared.roleplayMiniatureSources);
    ui.roleplayMiniatureOptions = std::move(prepared.roleplayMiniatureOptions);
    ui.animationSources = std::move(prepared.animationSources);
    ui.animationOptions = std::move(prepared.animationOptions);
    ui.modeOption = std::move(prepared.modeOption);
    ui.roleplayModeSource = std::move(prepared.roleplayModeSource);
    ui.roleplayModeOption = std::move(prepared.roleplayModeOption);
    ui.autoLoadSource = std::move(prepared.autoLoadSource);
    ui.githubSource = std::move(prepared.githubSource);
    ui.githubOption = std::move(prepared.githubOption);
    ui.autoLoadOption = std::move(prepared.autoLoadOption);
    ui.soulSource = std::move(prepared.soulSource);
    ui.soulOption = std::move(prepared.soulOption);
    ui.soulAutoSource = std::move(prepared.soulAutoSource);
    ui.soulAutoOption = std::move(prepared.soulAutoOption);
    ui.container = std::move(current);
    return true;
}

int SelectedCategory(Obj page) {
    const auto uid = api.String(api.Field(page, "m_currentId"));
    auto categories = api.Field(page, "m_options");
    for (int i = 0, count = Count(categories); i < count; ++i) {
        Ref category(Item(categories, i));
        if (!uid.empty() && api.String(api.Field(category.get(), "uid")) == uid)
            return FieldValue<int>(category.get(), "categoryId");
        auto children = api.Field(category.get(), "subcategories");
        for (int j = 0, childCount = Count(children); j < childCount; ++j) {
            Ref child(Item(children, j));
            if (!uid.empty() && api.String(api.Field(child.get(), "uid")) == uid)
                return FieldValue<int>(child.get(), "categoryId");
        }
    }
    return GameCategory;
}
void Rebuild(Obj page, int selected) {
    bool rebuild = true;
    bool connected = FieldValue<bool>(page, "isPlayerConnected");
    api.Call(page, "Init", {&rebuild, &connected, &selected});
}
bool SelectionReady(Obj page, const std::string& uid) {
    Ref displayed(api.Field(page, "m_displayedOption"));
    if (!displayed.get() || uid.empty()) return false;
    if (api.String(api.Field(displayed.get(), "uid")) == uid) return true;
    auto children = api.Field(displayed.get(), "subcategories");
    for (int i = 0, count = Count(children); i < count; ++i)
        if (api.String(api.Field(Item(children, i), "uid")) == uid) return true;
    return false;
}
void UiError(const std::exception& error) {
    Log(std::string("UI_ERROR ") + error.what());
    if (module) module->stopRequested.store(true);
}
bool Active() {
    return module && module->ready.load() && !module->stopped && !module->stopRequested.load();
}
// Metamob key row. Fixed codes only: a managed exception text can carry the
// typed key, so what() is never logged here and a failure never stops the mod.
void KeyFieldError(OptionsUi& ui, const char* step) noexcept {
    try {
        const auto now = GetTickCount64();
        if (now < ui.nextKeyFieldLog) return;
        ui.nextKeyFieldLog = now + KeyFieldLogMs;
        Log(std::string("METAMOB_KEY_FIELD_ERROR step=") + step);
    } catch (...) {}
}
// Plaintext copies of the key held here are overwritten before release (a
// resize within the capacity never reallocates and zeroes the stale bytes).
void Wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    SecureZeroMemory(text.data(), text.size());
    text.clear();
}
// The capture sync's status of the saved key (None: no key file, or one being
// forgotten), from the file's size and dates: the key is never read here.
metamob::KeyStatus ReadKeyStatus(OptionsUi& ui) noexcept {
    try { return ui.metamob.keyStatus ? ui.metamob.keyStatus() : metamob::KeyStatus::None; }
    catch (...) { KeyFieldError(ui, "status"); return metamob::KeyStatus::None; }
}
// A key not checked yet is shown (again: « Clé non vérifiée » clicked): the
// capture sync checks it with Metamob, once, silently.
void CheckKey(OptionsUi& ui, bool again) noexcept {
    try { if (ui.metamob.checkKey) ui.metamob.checkKey(again); }
    catch (...) { KeyFieldError(ui, "check"); }
}
const Indicator& Indicate(metamob::KeyStatus status) {
    for (const auto& indicator : Indicators) if (indicator.status == status) return indicator;
    return Indicators[0]; // never shown without a key, and never « valide » by default
}
std::vector<OptionsUi::KeyField>::iterator FindKeyField(OptionsUi& ui, Obj element) {
    return std::find_if(ui.keyFields.begin(), ui.keyFields.end(),
        [element](const OptionsUi::KeyField& field) { return field.element.get() == element; });
}
// Inline style, as the notebook's inputs: the row's controls take the free
// width between the row label and the row button.
Obj StyleBox(Obj element, const char* property) {
    Ref style(api.Call(element, "get_style"));
    Obj box = api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.get_") + property).c_str());
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Key field style unavailable");
    return box;
}
void PutStyle(Obj element, const char* property, Obj box) {
    Ref style(api.Call(element, "get_style"));
    api.Call(style.get(), (std::string("UnityEngine.UIElements.IStyle.set_") + property).c_str(), {api.object_unbox(box)});
}
void WriteStyle(Obj element, const char* property, void* value) {
    Ref box(StyleBox(element, property));
    api.Call(box.get(), "set_value", {value});
    PutStyle(element, property, box.get());
}
void EnumStyle(Obj element, const char* property, int value) { WriteStyle(element, property, &value); }
void Shown(Obj element, bool shown) { EnumStyle(element, "display", shown ? DisplayFlex : DisplayNone); }
void Pixels(Obj element, const char* property, float value) {
    int pixel = 0; // LengthUnit.Pixel
    Ref length(api.New(api.Class(UiModule, UiSpace, "Length"), {&value, &pixel}));
    WriteStyle(element, property, api.object_unbox(length.get()));
}
void Grow(Obj element) {
    float grow = 1.f;
    WriteStyle(element, "flexGrow", &grow);
    Pixels(element, "minWidth", 0.f);
}
Obj Added(Obj parent, Obj child) {
    api.Call(parent, "Add", {child});
    return child;
}
void EmptyField(Obj input) {
    Ref empty(api.string_new(""));
    api.Call(input, "set_text", {empty.get()});
}
// No key saved: the game's own TextInput in password mode.
Obj NewKeyInput() {
    Ref input(api.New(api.Class("Core.dll", Figma, "TextInput")));
    bool masked = true;
    api.Invoke(setPassword, input.get(), {&masked});
    int maxLength = KeyMaxLength;
    api.Call(input.get(), "set_maxLength", {&maxLength});
    Ref hint(api.string_new(KeyHint));
    api.Call(input.get(), "set_placeholderText", {hint.get()});
    Grow(input.get()); Pixels(input.get(), "minHeight", 32.f);
    return input.get();
}
// A key saved: the same masked field, disabled, holding a fixed run of stars
// and never the key: nothing to edit, copy or reveal.
Obj NewStarsField() {
    Ref stars(api.New(api.Class("Core.dll", Figma, "TextInput")));
    bool masked = true, enabled = false;
    api.Invoke(setPassword, stars.get(), {&masked});
    Ref text(api.string_new(KeyStars));
    api.Call(stars.get(), "set_text", {text.get()});
    api.Invoke(setEnabled, stars.get(), {&enabled});
    Grow(stars.get()); Pixels(stars.get(), "minHeight", 32.f);
    return stars.get();
}
// The game's own button, as the notebook's: small, with its icon and caption.
Obj NewKeyButton(Obj parent, const char* text, int icon, int style, int status) {
    Ref button(api.New(api.Class("Core.dll", Figma, "DofusButtonCustom")));
    int size = ButtonSmall;
    api.Call(button.get(), "set_size", {&size});
    api.Call(button.get(), "set_mainStyle", {&style});
    if (status) api.Call(button.get(), "set_status", {&status});
    if (icon) api.Call(button.get(), "set_icon", {&icon});
    if (text) { Ref caption(api.string_new(text)); api.Call(button.get(), "set_text", {caption.get()}); }
    Pixels(button.get(), "marginLeft", 8.f);
    return Added(parent, button.get());
}
// The question after the trash, in the game's own label; it may wrap.
Obj NewQuestion(Obj parent) {
    Ref label(api.New(api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel")));
    Ref text(api.string_new(ConfirmQuestion));
    api.Call(label.get(), "set_text", {text.get()});
    Grow(label.get()); EnumStyle(label.get(), "whiteSpace", WrapText);
    return Added(parent, label.get());
}
// Every control of the key row in its own container, complete before it is
// placed: a failure never leaves a half-built row. ShowKeyState then shows
// those of the current state.
OptionsUi::KeyField NewKeyField(Obj element, Obj button) {
    OptionsUi::KeyField field;
    field.element.reset(element);
    field.button.reset(button);
    field.buttonDisplay.reset(StyleBox(button, "display"));
    field.slot.reset(api.New(api.Class(UiModule, UiSpace, "VisualElement")));
    const auto slot = field.slot.get();
    EnumStyle(slot, "flexDirection", RowDirection); EnumStyle(slot, "alignItems", AlignCenter);
    Grow(slot); Pixels(slot, "marginLeft", 12.f); Pixels(slot, "marginRight", 8.f);
    field.input.reset(Added(slot, NewKeyInput()));
    field.stars.reset(Added(slot, NewStarsField()));
    field.indicator.reset(NewKeyButton(slot, nullptr, 0, StyleSecondary, 0)); // caption, icon and status: ShowIndicator
    field.trash.reset(NewKeyButton(slot, nullptr, IconTrash, StyleTertiary, StatusNegative));
    field.question.reset(NewQuestion(slot));
    field.yes.reset(NewKeyButton(slot, "Oui", 0, StylePrimary, StatusNegative));
    field.no.reset(NewKeyButton(slot, "Non", 0, StyleSecondary, 0));
    return field;
}
// Emptied, its native button back to its own display, then detached; its
// references go even when the view is already gone.
std::vector<OptionsUi::KeyField>::iterator DropKeyField(OptionsUi& ui, std::vector<OptionsUi::KeyField>::iterator field) noexcept {
    try { EmptyField(field->input.get()); }
    catch (...) { KeyFieldError(ui, "remove"); }
    try { if (!field->saveShown) PutStyle(field->button.get(), "display", field->buttonDisplay.get()); }
    catch (...) { KeyFieldError(ui, "remove"); }
    try { api.Call(field->slot.get(), "RemoveFromHierarchy"); }
    catch (...) { KeyFieldError(ui, "remove"); }
    return ui.keyFields.erase(field);
}
void DropKeyFields(OptionsUi& ui) noexcept {
    while (!ui.keyFields.empty()) DropKeyField(ui, ui.keyFields.begin());
}
// The field's length only: polling never copies a character of the key.
int TypedLength(Obj input) {
    const Obj text = api.Call(input, "get_text");
    return text ? api.string_length(text) : 0;
}
// The row's own « Enregistrer »: hidden, or shown with its own display;
// written again when the whole state is (force), otherwise on a change only.
void ShowSaveButton(OptionsUi::KeyField& field, bool shown, bool force = false) {
    if (field.saveShown == shown && !force) return;
    if (shown) PutStyle(field.button.get(), "display", field.buttonDisplay.get());
    else EnumStyle(field.button.get(), "display", DisplayNone);
    field.saveShown = shown;
}
// One state for every view of the key row. Out of the masked field's state the
// field is emptied (a key typed in one view while another view or client saved one).
void ShowKeyState(OptionsUi::KeyField& field, KeyState state) {
    const bool editable = state == KeyEditable, saved = state == KeySaved, confirm = state == KeyConfirm;
    if (!editable) EmptyField(field.input.get());
    Shown(field.input.get(), editable);
    Shown(field.stars.get(), saved); Shown(field.indicator.get(), saved); Shown(field.trash.get(), saved);
    Shown(field.question.get(), confirm); Shown(field.yes.get(), confirm); Shown(field.no.get(), confirm);
    ShowSaveButton(field, editable && TypedLength(field.input.get()) > 0, true);
    field.shown = state;
}
// The indicator of a saved key: the caption, icon and button status of its
// status, written again whenever that status changes.
void ShowIndicator(OptionsUi::KeyField& field, metamob::KeyStatus status) {
    const auto& shown = Indicate(status);
    Ref caption(api.string_new(shown.caption));
    api.Call(field.indicator.get(), "set_text", {caption.get()});
    int icon = shown.icon, look = shown.look;
    api.Call(field.indicator.get(), "set_icon", {&icon});
    api.Call(field.indicator.get(), "set_status", {&look});
    field.status = static_cast<int>(status);
}
// keyStatus() reads the key file's size and dates, never the key: at each Init
// of a key view, otherwise at most once a second. While the row is shown, a
// key not checked yet in this session is checked once by the capture sync.
void RefreshKeyStatus(OptionsUi& ui, ULONGLONG now, bool force) noexcept {
    if (!force && now < ui.nextKeyFieldRefresh) return;
    ui.nextKeyFieldRefresh = now + KeyFieldRefreshMs;
    ui.keyStatus = ReadKeyStatus(ui);
    if (ui.keyStatus == metamob::KeyStatus::None) ui.confirmForget = false; // gone: forgotten here or by another client
    else if (ui.keyStatus == metamob::KeyStatus::Saved) CheckKey(ui, false);
}
// No key, also while « Oui » is being done (the capture sync says None until
// the file is deleted, then follows it: a key saved right after shows at once).
KeyState CurrentKeyState(OptionsUi& ui, ULONGLONG now) noexcept {
    if (ui.confirmForget && now >= ui.confirmDeadline) ui.confirmForget = false; // unanswered: the key stays
    if (ui.keyStatus == metamob::KeyStatus::None) return KeyEditable;
    return ui.confirmForget ? KeyConfirm : KeySaved;
}
// Every view of the key row in the current state, a saved key's indicator in
// its current status; Enregistrer follows the typed length. A view that fails
// loses its controls (its next Init places them again).
void ShowKeyFields(OptionsUi& ui, ULONGLONG now) noexcept {
    const auto state = CurrentKeyState(ui, now);
    for (auto field = ui.keyFields.begin(); field != ui.keyFields.end();) {
        try {
            if (state == KeySaved && field->status != static_cast<int>(ui.keyStatus)) ShowIndicator(*field, ui.keyStatus);
            if (field->shown != state) ShowKeyState(*field, state);
            else if (state == KeyEditable) ShowSaveButton(*field, TypedLength(field->input.get()) > 0);
            ++field;
        } catch (...) { KeyFieldError(ui, "refresh"); field = DropKeyField(ui, field); }
    }
}
// Each Tick: a key saved, checked or forgotten (here, by the capture sync or by
// another client) shows within a second, Enregistrer as soon as a character is typed.
void RefreshKeyFields(OptionsUi& ui) noexcept {
    if (ui.keyFields.empty()) return;
    const auto now = GetTickCount64();
    RefreshKeyStatus(ui, now, false);
    ShowKeyFields(ui, now);
}
Obj RowButton(Obj element) {
    auto rowBinding = api.Class("Core.dll", "Core.UI.Bindings", "TextButtonOptionElementBinding");
    return api.Invoke(api.Find(rowBinding, "get_btn_button", 0), api.FieldAddress(element, "m_binding"));
}
// After TextButtonOptionElement.Init: exactly one owned container, just before
// the button, in a view showing the key row (or a search clone of it); none in
// any other view, since the game pools these views for every text-button row.
void BindKeyField(OptionsUi& ui, Obj element, Obj option) noexcept {
    try {
        auto field = FindKeyField(ui, element);
        const bool key = Active() && option && ui.metamobKeyMarker.get() &&
            api.Call(option, "get_description") == ui.metamobKeyMarker.get();
        if (!key) { if (field != ui.keyFields.end()) DropKeyField(ui, field); return; }
        Ref button(RowButton(element));
        Ref row(button.get() ? api.Call(button.get(), "get_parent") : nullptr);
        if (!row.get()) throw std::runtime_error("Key row layout unavailable");
        const auto now = GetTickCount64();
        RefreshKeyStatus(ui, now, true);
        if (field != ui.keyFields.end()) {
            if (field->button.get() == button.get() && api.Call(field->slot.get(), "get_parent") == row.get()) {
                field->shown = field->status = -1; // re-Init of the same row: shown again as it is now
                ShowKeyFields(ui, now);
                return;
            }
            DropKeyField(ui, field); // taken out of the row by the game: placed again below
        }
        if (ui.keyFields.size() >= MaxKeyFields) DropKeyField(ui, ui.keyFields.begin());
        ui.keyFields.reserve(ui.keyFields.size() + 1); // the push_back below cannot fail once inserted
        auto owned = NewKeyField(element, button.get());
        int index = api.Value<int>(api.Call(row.get(), "IndexOf", {button.get()}));
        if (index < 0) throw std::runtime_error("Key row button detached");
        api.Call(row.get(), "Insert", {&index, owned.slot.get()});
        ui.keyFields.push_back(std::move(owned));
        ShowKeyFields(ui, now);
    } catch (...) { KeyFieldError(ui, "bind"); }
}
// « Enregistrer »: the field is read once and emptied at once; the capture
// sync then verifies, stores and wipes the text.
void SaveKeyClick(OptionsUi& ui, Obj element) noexcept {
    std::string key;
    try {
        auto field = FindKeyField(ui, element);
        // Out of the masked field's state the button is hidden: a click arriving anyway does nothing.
        if (field != ui.keyFields.end() && field->shown != KeyEditable) return;
        if (field != ui.keyFields.end()) {
            key = api.String(api.Call(field->input.get(), "get_text"));
            EmptyField(field->input.get());
        }
        if (key.empty()) { if (ui.metamob.notice) ui.metamob.notice(PasteKeyFirst); }
        else if (ui.metamob.submitKey) ui.metamob.submitKey(std::move(key));
        field = FindKeyField(ui, element);
        if (field != ui.keyFields.end()) ShowSaveButton(*field, false); // the field is empty now
    } catch (...) { KeyFieldError(ui, "save"); }
    Wipe(key);
}
// The key row's own buttons, through the game's click (nativeClick): the
// indicator explains the key's status in the chat (a key not verified is
// checked again), the trash asks, « Non » keeps the key and « Oui » forgets it
// (the capture sync deletes the file and says so in the chat).
bool KeyButtonClick(Obj button) noexcept {
    const auto ui = module ? module->ui.get() : nullptr;
    if (!ui || !button) return false;
    enum class Kind { None, Status, Trash, Yes, No } kind = Kind::None;
    try {
        for (const auto& field : ui->keyFields) {
            kind = button == field.indicator.get() ? Kind::Status : button == field.trash.get() ? Kind::Trash :
                button == field.yes.get() ? Kind::Yes : button == field.no.get() ? Kind::No : Kind::None;
            if (kind != Kind::None) break;
        }
    } catch (...) { KeyFieldError(*ui, "click"); return false; } // unknown: the button keeps the game's click
    if (kind == Kind::None) return false;
    if (!Active()) return true;
    try {
        const auto now = GetTickCount64();
        if (kind == Kind::Status) {
            RefreshKeyStatus(*ui, now, true); // the click tells the status as it is now
            const auto status = ui->keyStatus;
            if (status == metamob::KeyStatus::Unverified) { CheckKey(*ui, true); RefreshKeyStatus(*ui, now, true); } // « Vérification… » meanwhile
            ShowKeyFields(*ui, now);
            if (status != metamob::KeyStatus::None && ui->metamob.notice) ui->metamob.notice(Indicate(status).notice);
            return true;
        }
        if (kind == Kind::Trash) { ui->confirmForget = true; ui->confirmDeadline = now + ConfirmMs; }
        else if (kind == Kind::No) ui->confirmForget = false;
        else if (ui->confirmForget) { // « Oui », while its question is shown
            ui->confirmForget = false;
            if (ui->metamob.forgetKey) ui->metamob.forgetKey();
            RefreshKeyStatus(*ui, now, true); // no key shown while the capture sync deletes the file
        }
        ShowKeyFields(*ui, now);
    } catch (...) { KeyFieldError(*ui, "click"); }
    return true;
}
// « Statistiques »: a failure stays in the section (logged at most every ten
// seconds) and never stops the mod.
void StatsError(OptionsUi& ui, const char* step, const char* detail = nullptr) noexcept {
    try {
        const auto now = GetTickCount64();
        if (now < ui.nextStatsLog) return;
        ui.nextStatsLog = now + StatsLogMs;
        Log(std::string("STATS_UI_ERROR step=") + step + (detail ? std::string(" ") + detail : std::string{}));
    } catch (...) {}
}
// The statistics line a row model shows: native search clones keep its description reference.
std::optional<size_t> StatsLineOf(const OptionsUi& ui, Obj option) {
    if (!option || ui.statsMarkers.size() != StatsLineCount) return std::nullopt;
    const Obj description = api.Call(option, "get_description");
    if (!description) return std::nullopt;
    for (size_t line = 0; line < StatsLineCount; ++line)
        if (ui.statsMarkers[line].get() == description) return line;
    return std::nullopt;
}
std::vector<OptionsUi::StatsView>::iterator FindStatsView(OptionsUi& ui, Obj element) {
    return std::find_if(ui.statsViews.begin(), ui.statsViews.end(),
        [element](const OptionsUi::StatsView& view) { return view.element.get() == element; });
}
// The view's native button back to its own display, then the view forgotten,
// even when it is already gone.
std::vector<OptionsUi::StatsView>::iterator DropStatsView(OptionsUi& ui, std::vector<OptionsUi::StatsView>::iterator view) noexcept {
    try { if (view->hidden) PutStyle(view->button.get(), "display", view->buttonDisplay.get()); }
    catch (...) { StatsError(ui, "remove"); }
    return ui.statsViews.erase(view);
}
void DropStatsViews(OptionsUi& ui) noexcept {
    while (!ui.statsViews.empty()) DropStatsView(ui, ui.statsViews.begin());
}
// Each line's label from the store, for what is shown; a changed label goes
// into its row model too (its next Init, the native search), the reset's
// caption and icon with it.
void UpdateStatsTexts(OptionsUi& ui) {
    if (ui.statsRows.size() != StatsLineCount) return;
    const bool every = ui.statsScope == 1;
    const auto totals = stats::Shown(every);
    ui.statsTexts.resize(StatsLineCount);
    for (size_t line = 0; line < StatsLineCount; ++line) {
        const bool reset = line == StatsReset;
        auto text = reset ? StatsResetText(ui, totals) : StatsText(line, totals, every);
        if (text == ui.statsTexts[line]) continue;
        const auto model = ui.statsRows[line].get();
        Ref label(api.string_new(text.c_str()));
        api.Call(model, "set_text", {label.get()});
        if (reset) {
            SetTextField(model, "buttonText", StatsResetCaption(ui));
            FieldValue<int>(model, "icon") = StatsResetIcon(ui);
        }
        ui.statsTexts[line] = std::move(text);
    }
}
// A view of a line: its label, and its native button hidden, except the
// reset's, shown with the reset's caption and icon.
void ShowStatsView(OptionsUi& ui, OptionsUi::StatsView& view) {
    const bool reset = view.line == StatsReset;
    if (view.hidden == reset) {
        if (reset) PutStyle(view.button.get(), "display", view.buttonDisplay.get());
        else EnumStyle(view.button.get(), "display", DisplayNone);
        view.hidden = !reset;
    }
    if (reset && view.caption != StatsResetCaption(ui)) {
        Ref caption(api.string_new(StatsResetCaption(ui)));
        api.Call(view.button.get(), "set_text", {caption.get()});
        int icon = StatsResetIcon(ui);
        api.Call(view.button.get(), "set_icon", {&icon});
        view.caption = StatsResetCaption(ui);
    }
    const auto& text = ui.statsTexts.at(view.line);
    if (statsUpdateText && view.shown != text) {
        SetTextField(view.element.get(), DisplayedText, text.c_str());
        api.Invoke(statsUpdateText, view.element.get());
        view.shown = text;
    }
}
// Every view as its line is now; a view that fails is let go (its next Init follows it again).
void ShowStatsViews(OptionsUi& ui, const char* step) noexcept {
    for (auto view = ui.statsViews.begin(); view != ui.statsViews.end();) {
        try { ShowStatsView(ui, *view); ++view; }
        catch (const std::exception& error) { StatsError(ui, step, error.what()); view = DropStatsView(ui, view); }
        catch (...) { StatsError(ui, step); view = DropStatsView(ui, view); }
    }
}
// Once a second while a view of a line is in the open page: the labels follow
// the store (the play time, another client's writes, a reset). An unanswered
// question goes after 10 s, an outcome after 6 s.
void RefreshStats(OptionsUi& ui) noexcept {
    try {
        const auto now = GetTickCount64();
        bool changed = false;
        if (ui.statsConfirm && now >= ui.statsConfirmDeadline) { ui.statsConfirm = false; changed = true; }
        if (ui.statsResult && now >= ui.statsResultUntil) { ui.statsResult = 0; changed = true; }
        if (ui.statsViews.empty() || (!changed && now < ui.nextStatsRefresh)) return;
        ui.nextStatsRefresh = now + StatsRefreshMs;
        // A closed page reads nothing: its views follow at their next Init.
        const bool open = std::any_of(ui.statsViews.begin(), ui.statsViews.end(),
            [](const OptionsUi::StatsView& view) { return api.Call(view.element.get(), "get_panel") != nullptr; });
        if (!open) return;
        UpdateStatsTexts(ui);
        ShowStatsViews(ui, "refresh");
    } catch (const std::exception& error) { StatsError(ui, "refresh", error.what()); }
    catch (...) { StatsError(ui, "refresh"); }
}
// Before the key row binds a recycled view: a view that leaves a statistics
// line gets its native button back as it was.
void ReleaseStatsView(OptionsUi& ui, Obj element, Obj option) noexcept {
    try {
        const auto view = FindStatsView(ui, element);
        if (view == ui.statsViews.end() || (Active() && StatsLineOf(ui, option))) return;
        DropStatsView(ui, view);
    } catch (...) { StatsError(ui, "release"); }
}
// After TextButtonOptionElement.Init: a view showing a line (or a search clone
// of it) is followed, its label current (read again when a second old).
void BindStatsView(OptionsUi& ui, Obj element, Obj option) noexcept {
    try {
        if (!Active()) return;
        const auto line = StatsLineOf(ui, option);
        if (!line) return;
        Ref button(RowButton(element));
        if (!button.get()) throw std::runtime_error("Statistics row button unavailable");
        auto view = FindStatsView(ui, element);
        if (view != ui.statsViews.end() && view->button.get() != button.get()) { DropStatsView(ui, view); view = ui.statsViews.end(); }
        if (view == ui.statsViews.end()) {
            if (ui.statsViews.size() >= MaxStatsViews) DropStatsView(ui, ui.statsViews.begin());
            OptionsUi::StatsView fresh;
            fresh.element.reset(element);
            fresh.button.reset(button.get());
            fresh.buttonDisplay.reset(StyleBox(button.get(), "display"));
            ui.statsViews.push_back(std::move(fresh));
            view = std::prev(ui.statsViews.end());
        }
        view->line = *line;
        view->shown.clear();  // the native Init wrote the model's label: written again below when stale
        view->caption.clear();
        const auto now = GetTickCount64();
        if (now >= ui.nextStatsRefresh) { ui.nextStatsRefresh = now + StatsRefreshMs; UpdateStatsTexts(ui); }
        ShowStatsView(ui, *view);
    } catch (const std::exception& error) { StatsError(ui, "bind", error.what()); }
    catch (...) { StatsError(ui, "bind"); }
}
// The reset line's button: the first click asks (the line names what would be
// erased, the button becomes Confirmer); Confirmer within 10 s erases what was
// shown when asked, for the same character. A click in the first second after
// the question is ignored: the question stays.
void StatsResetClick(OptionsUi& ui) noexcept {
    try {
        const auto now = GetTickCount64();
        if (!ui.statsConfirm || now >= ui.statsConfirmDeadline) {
            ui.statsConfirm = true;
            ui.statsConfirmAll = ui.statsScope == 1;
            ui.statsConfirmCharacter = stats::Current();
            ui.statsConfirmDeadline = now + StatsConfirmMs;
            ui.statsConfirmFrom = now + StatsConfirmWaitMs;
            ui.statsResult = 0;
        } else if (now < ui.statsConfirmFrom) {
            return;
        } else {
            ui.statsConfirm = false;
            const bool same = ui.statsConfirmAll || (ui.statsConfirmCharacter > 0 && stats::Current() == ui.statsConfirmCharacter);
            ui.statsResult = same && stats::Clear(ui.statsConfirmAll) ? 1 : 2;
            ui.statsResultUntil = now + StatsResultMs;
        }
        UpdateStatsTexts(ui);
        ShowStatsViews(ui, "reset");
        ui.nextStatsRefresh = now + StatsRefreshMs;
    } catch (const std::exception& error) { StatsError(ui, "reset", error.what()); }
    catch (...) { StatsError(ui, "reset"); }
}
void TextButtonHook(Obj self, Obj element, Method method) {
    const auto ui = module ? module->ui.get() : nullptr;
    Obj description = nullptr;
    try {
        Ref option(api.Invoke(textButtonModelGetter, self));
        // Native search/display uses MemberwiseClone: the unique description
        // reference identifies our rows even when the model is cloned. The
        // credits and Metamob markers are set and released together.
        if (ui && ui->creditsMarker.get() && option.get()) description = api.Call(option.get(), "get_description");
        if (description && description == ui->creditsMarker.get()) {
            if (Active()) { Ref url(api.string_new(ProjectUrl)); api.Invoke(openUrl, nullptr, {url.get()}); }
            return;
        }
    } catch (const std::exception& error) {
        // Never fall through to a possibly owned action with a null delegate.
        Log(std::string("CREDITS_LINK_ERROR ") + error.what());
        return;
    }
    // The Metamob key row: its own handling, which never throws, never the native action.
    if (description && description == ui->metamobKeyMarker.get()) { if (Active()) SaveKeyClick(*ui, self); return; }
    // The statistics lines: no native action (null delegate); the reset line asks, then erases.
    if (description && ui->statsMarkers.size() == StatsLineCount) {
        if (description == ui->statsMarkers[StatsReset].get()) { if (Active()) StatsResetClick(*ui); return; }
        for (const auto& marker : ui->statsMarkers) if (description == marker.get()) return;
    }
    originalTextButton(self, element, method);
}
// The game pools row views and re-Inits them with any model: after the native
// Init, the view gets or loses the key row's controls, and follows a
// statistics line or stops following it (its button given back first).
void TextButtonInitHook(Obj self, Obj option, Method method) {
    originalTextButtonInit(self, option, method);
    if (!module || !module->ui) return;
    ReleaseStatsView(*module->ui, self, option);
    BindKeyField(*module->ui, self, option);
    BindStatsView(*module->ui, self, option);
}
void EnableHook(Obj self, Method method) {
    originalEnable(self, method);
    if (!Active()) return;
    try { module->ui->CapturePage(self); }
    catch (const std::exception& error) { UiError(error); }
}
void InitHook(Obj self, bool justCreated, bool connected, int category, Method method) {
    if (Active() && module->servicesReady && IsGeneral(self)) {
        try { if (module->ui->Attach(self)) justCreated = true; }
        catch (const std::exception& error) { UiError(error); }
    }
    originalInit(self, justCreated, connected, category, method);
}
}

void OptionsUi::Install() {
    generalClass = api.Class("Core.dll", Config, "OptionGeneralUi");
    auto tab = api.Class("Core.dll", Config, "OptionTabUi");
    AddHook(api.Find(generalClass, "OnEnable", 0), 0x2D80470,
            reinterpret_cast<void*>(EnableHook), reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(tab, "Init", 3), 0x2D84EC0,
            reinterpret_cast<void*>(InitHook), reinterpret_cast<void**>(&originalInit));
    auto textButton = api.Class("Core.dll", Config, "TextButtonOptionElement");
    textButtonModelGetter = api.FindVerified(textButton, "get_m_textButtonOption", 0, 0x2D88930);
    openUrl = api.FindVerified(api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"), "OpenURL", 1, 0x53ACA50);
    AddHook(api.Find(textButton, "<Init>b__3_0", 1), 0x2D88800,
            reinterpret_cast<void*>(TextButtonHook), reinterpret_cast<void**>(&originalTextButton));
    // Metamob key row: the game's masked TextInput, buttons and label, kept in the row view.
    AddHook(api.FindVerified(textButton, "Init", 1, 0x2D885A0), 0x2D885A0,
            reinterpret_cast<void*>(TextButtonInitHook), reinterpret_cast<void**>(&originalTextButtonInit));
    // « Statistiques »: a line's label refreshed in its view, as the game's own
    // UpdateText shows OptionElement`1.displayedText. Without them, a view
    // shows its line as it was at its Init.
    try {
        RequireField(textButton, "<displayedText>k__BackingField", 0x2F0);
        statsUpdateText = api.FindVerified(textButton, "UpdateText", 0, 0x2D88830);
    } catch (const std::exception& error) {
        statsUpdateText = nullptr;
        Log(std::string("STATS_LABELS_AT_INIT ") + error.what());
    }
    auto inputClass = api.Class("Core.dll", Figma, "TextInput");
    api.FindVerified(inputClass, ".ctor", 0, 0x178DC90);
    api.FindVerified(inputClass, "get_text", 0, 0x178DDA0);
    api.FindVerified(inputClass, "set_text", 1, 0x178EC50);
    api.FindVerified(inputClass, "set_placeholderText", 1, 0x178EAA0);
    api.FindVerified(inputClass, "set_maxLength", 1, 0x178E9B0);
    setPassword = api.FindVerified(inputClass, "set_isPassword", 1, 0x178E8F0);
    // The saved key's stars: a disabled field (DofusVisualElement hides VisualElement.SetEnabled).
    auto dofusElementClass = api.Class("Core.dll", Figma, "DofusVisualElement");
    setEnabled = api.FindVerified(dofusElementClass, "SetEnabled", 1, 0x1721860);
    auto elementClass = api.Class(UiModule, UiSpace, "VisualElement");
    api.FindVerified(elementClass, ".ctor", 0, 0x55F50B0);
    api.FindVerified(elementClass, "Add", 1, 0x55DFAD0);
    api.FindVerified(elementClass, "get_parent", 0, 0x55F7180);
    api.FindVerified(elementClass, "IndexOf", 1, 0x55E55B0);
    api.FindVerified(elementClass, "Insert", 2, 0x55E5710);
    api.FindVerified(elementClass, "RemoveFromHierarchy", 0, 0x55E7BD0);
    api.FindVerified(elementClass, "get_style", 0, 0x55F74A0);
    // Inline styles of the row's controls, and the native Enregistrer button's display, kept and put back.
    auto styleClass = api.Class(UiModule, UiSpace, "InlineStyleAccess");
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.get_display", 0, 0x55D1270);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.set_display", 1, 0x55D37F0);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.get_flexDirection", 0, 0x55D13C0);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.set_flexDirection", 1, 0x55D3A80);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.get_alignItems", 0, 0x55D0C60);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.set_alignItems", 1, 0x55D2870);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.get_whiteSpace", 0, 0x55D26B0);
    api.FindVerified(styleClass, "UnityEngine.UIElements.IStyle.set_whiteSpace", 1, 0x55D6370);
    auto buttonClass = api.Class("Core.dll", Figma, "DofusButtonCustom");
    api.FindVerified(buttonClass, ".ctor", 0, 0x170D6F0);
    api.FindVerified(buttonClass, "set_size", 1, 0x170E330);
    api.FindVerified(buttonClass, "set_mainStyle", 1, 0x170E210);
    api.FindVerified(buttonClass, "set_status", 1, 0x170E400);
    api.FindVerified(buttonClass, "set_icon", 1, 0x170DFE0);
    api.FindVerified(buttonClass, "set_text", 1, 0x170E5F0);
    auto labelClass = api.Class("Core.dll", "Core.UILogic.Components", "DofusLabel");
    api.FindVerified(labelClass, ".ctor", 0, 0x1593940);
    api.FindVerified(labelClass, "set_text", 1, 0x1594570);
    // Clicks on the row's own buttons (the key status indicator, trash, « Oui », « Non »).
    nativeClick::AddHandler(KeyButtonClick);
}
void OptionsUi::CapturePage(Obj page) {
    pendingPage.reset(page);
    stableList.reset();
    stableSelection.clear();
    stableTicks = 0;
}
bool OptionsUi::Attach(Obj page) {
    if (!module || !module->servicesReady || module->stopped || !IsGeneral(page)) return false;
    Ref categories(api.Field(page, "m_options"));
    for (const auto& owned : pages) {
        if (owned.owner.get() == page && Contains(categories.get(), owned.category.get())) return false;
    }
    if (!EnsureModels(*this, page)) return false;
    for (int i = 0, count = Count(categories.get()); i < count; ++i) {
        auto existing = Item(categories.get(), i);
        if (FieldValue<int>(existing, "categoryId") == ModCategory)
            throw std::runtime_error("Mod category identifier already belongs to another entry");
    }
    Ref category(MakeCategory(ModCategoryTitle, ModCategory, false));
    // Parent/section shape of the native menu: the page shows each section as
    // its own block, in this order, with the ids that follow ModCategory.
    struct Section { const char* title; std::vector<Obj> rows; };
    // « Apparence »: each context's choice, then its category checkboxes.
    std::vector<Obj> appearance{modeOption.get()}, animations;
    for (const auto& option : combatMiniatureOptions) appearance.push_back(option.get());
    appearance.push_back(roleplayModeOption.get());
    for (const auto& option : roleplayMiniatureOptions) appearance.push_back(option.get());
    for (const auto& option : animationOptions) animations.push_back(option.get());
    const auto extra = [&](ExtraRow row) { return extraOptions[row].get(); };
    // « Statistiques »: the checkbox, what is shown, then its lines.
    std::vector<Obj> statistics{statsOption.get(), statsScopeOption.get()};
    for (const auto& row : statsRows) statistics.push_back(row.get());
    const Section layout[] = {
        {"Apparence", appearance},
        {"Animations", animations},
        {"Surbrillance", {glowOption.get(), glowNpcsOption.get(), glowInteractivesOption.get()}},
        {"Cartes", {resourcesOption.get(), resourcesMiniOption.get(), extra(MiniMapBarRow), miniCenterOption.get(), extra(MiniMapZoneNameRow),
            extra(MapCoordsRow), extra(MiniMapCoordsRow), archiZonesOption.get(), archiZonesMiniOption.get()}},
        {"Carnet", {extra(NotebookPingRow), extra(MapAddPositionRow)}},
        {"Déplacements", {transitionsOption.get(), preloadOption.get(), spouseOption.get(), travelPlannerOption.get(), templeOption.get()}},
        {"Pierre d’âme", {soulOption.get(), soulAutoOption.get(), metamobSyncOption.get(), metamobKeyOption.get()}},
        {"Chat", {chatTravelOption.get(), chatSoulOption.get(), chatOcreOption.get(), chatMetamobOption.get(), chatCoordsOption.get()}},
        {"Calendrier", {calendarKraloveOption.get()}},
        {"Performances", {extra(PerfOverlayRow), extra(PerfGpuRow), extra(PerfDiskRow), extra(PerfDragRow), perfCornerOption.get()}},
        {"Statistiques", statistics},
        {"Chargement et mises à jour", {autoLoadOption.get(), githubOption.get()}},
        {"À propos", {creditsOption.get()}}};
    static_assert(std::size(layout) == ModSections, "Restore maps every section id back to the game category");
    auto sections = api.Field(category.get(), "subcategories");
    int id = ModCategory;
    for (const auto& [title, rows] : layout) {
        Ref section(MakeCategory(title, ++id, true));
        auto options = api.Field(section.get(), "options");
        for (const auto row : rows) api.Call(options, "Add", {row});
        api.Call(sections, "Add", {section.get()});
    }
    Page owned;
    owned.owner.reset(page);
    owned.category = std::move(category);
    pages.push_back(std::move(owned));
    try { api.Call(categories.get(), "Add", {pages.back().category.get()}); }
    catch (...) { pages.pop_back(); throw; }
    Log(std::string("UI_MOD_REGISTERED category=") + ModCategoryTitle + " controls=native loaded=" + ModVersion + " version=" + ModSemanticVersion);
    return true;
}
void OptionsUi::Tick() {
    if (!module || !module->servicesReady || module->stopped) return;
    RefreshKeyFields(*this);
    RefreshStats(*this);
    const auto page = pendingPage.get();
    if (page) {
        const bool attached = std::any_of(pages.begin(), pages.end(), [page](const Page& item) {
            return item.owner.get() == page && Contains(api.Field(page, "m_options"), item.category.get());
        });
        if (!attached) {
            Ref root(api.Call(page, "blqu"));
            if (root.get() && api.Call(root.get(), "get_panel")) {
                Ref list(BindingGet(page, "get_gd_category"));
                Ref displayed(api.Field(page, "m_optionsDisplayed"));
                auto selection = api.String(api.Field(page, "m_currentId"));
                // Summary/selection builders yield before changing the UI; wait
                // for a settled list on two distinct main-thread pumps.
                if (displayed.get() && api.Call(list.get(), "get_itemsSource") == displayed.get() &&
                    SelectionReady(page, selection)) {
                    if (stableList.get() == displayed.get() && stableSelection == selection) ++stableTicks;
                    else { stableList.reset(displayed.get()); stableSelection = selection; stableTicks = 1; }
                    if (stableTicks >= 2) {
                        int selected = SelectedCategory(page);
                        if (Attach(page)) Rebuild(page, selected);
                    }
                } else { stableList.reset(); stableTicks = 0; }
            }
        }
    }
    if (!ModelsLoaded(*this)) return;
    auto next = module->settings;
    next.mode = api.Value<int>(api.Call(modeSource.get(), "get_property"));
    next.roleplayMode = api.Value<int>(api.Call(roleplayModeSource.get(), "get_property"));
    for (size_t i = 0; i < combatMiniatureSources.size(); ++i)
        next.*CombatMiniatureToggles[i] = api.Value<bool>(api.Call(combatMiniatureSources[i].get(), "get_property"));
    for (size_t i = 0; i < roleplayMiniatureSources.size(); ++i)
        next.*RoleplayMiniatureToggles[i] = api.Value<bool>(api.Call(roleplayMiniatureSources[i].get(), "get_property"));
    for (size_t i = 0; i < animationSources.size(); ++i)
        next.*AnimationToggles[i] = api.Value<bool>(api.Call(animationSources[i].get(), "get_property"));
    next.autoLoad = api.Value<bool>(api.Call(autoLoadSource.get(), "get_property"));
    next.githubUpdates = api.Value<bool>(api.Call(githubSource.get(), "get_property"));
    next.soulReminder = api.Value<bool>(api.Call(soulSource.get(), "get_property"));
    next.soulAutoEquip = api.Value<bool>(api.Call(soulAutoSource.get(), "get_property"));
    next.monsterGlowMode = api.Value<int>(api.Call(glowSource.get(), "get_property"));
    next.glowNpcs = api.Value<bool>(api.Call(glowNpcsSource.get(), "get_property"));
    next.glowInteractives = api.Value<bool>(api.Call(glowInteractivesSource.get(), "get_property"));
    next.fastMapTransitions = api.Value<bool>(api.Call(transitionsSource.get(), "get_property"));
    next.preloadNextMap = api.Value<bool>(api.Call(preloadSource.get(), "get_property"));
    next.spouseTeleport = api.Value<bool>(api.Call(spouseSource.get(), "get_property"));
    next.resourceMapEnabled = api.Value<bool>(api.Call(resourcesSource.get(), "get_property"));
    next.resourceMiniMapEnabled = api.Value<bool>(api.Call(resourcesMiniSource.get(), "get_property"));
    next.miniMapCenterButton = api.Value<bool>(api.Call(miniCenterSource.get(), "get_property"));
    next.metamobSync = api.Value<bool>(api.Call(metamobSyncSource.get(), "get_property"));
    next.mapTravelPlanner = api.Value<bool>(api.Call(travelPlannerSource.get(), "get_property"));
    next.chatTravelLinks = api.Value<bool>(api.Call(chatTravelSource.get(), "get_property"));
    next.chatSoulInventory = api.Value<bool>(api.Call(chatSoulSource.get(), "get_property"));
    next.chatOcreStatus = api.Value<bool>(api.Call(chatOcreSource.get(), "get_property"));
    next.chatMetamobStatus = api.Value<bool>(api.Call(chatMetamobSource.get(), "get_property"));
    next.archiZonesMap = api.Value<bool>(api.Call(archiZonesSource.get(), "get_property"));
    next.archiZonesMiniMap = api.Value<bool>(api.Call(archiZonesMiniSource.get(), "get_property"));
    next.calendarKralamoure = api.Value<bool>(api.Call(calendarKraloveSource.get(), "get_property"));
    next.travelRoublardTemple = api.Value<bool>(api.Call(templeSource.get(), "get_property"));
    for (size_t i = 0; i < extraSources.size(); ++i)
        next.*ExtraToggles[i] = api.Value<bool>(api.Call(extraSources[i].get(), "get_property"));
    next.perfOverlayCorner = api.Value<int>(api.Call(perfCornerSource.get(), "get_property"));
    next.coordsWorldChatMode = api.Value<int>(api.Call(chatCoordsSource.get(), "get_property"));
    next.statsEnabled = api.Value<bool>(api.Call(statsSource.get(), "get_property"));
    // What the statistics lines show: kept here for the session, never in
    // Settings; another choice drops a pending question and shows at once.
    const int scope = api.Value<int>(api.Call(statsScopeSource.get(), "get_property"));
    if (scope != 0 && scope != 1) SetSource(statsScopeSource.get(), statsScope, true);
    else if (scope != statsScope) {
        statsScope = scope;
        statsConfirm = false;
        statsResult = 0;
        nextStatsRefresh = 0;
        RefreshStats(*this);
    }
    // Another corner, or the mouse placement turned off: back to the corner.
    if (!next.perfOverlayDrag || next.perfOverlayCorner != module->settings.perfOverlayCorner) next.perfOverlayX = next.perfOverlayY = -1;
    if (next.mode < 0 || next.mode > 2 || next.roleplayMode < 0 || next.roleplayMode > 2 ||
        !Settings::ValidMonsterGlowMode(next.monsterGlowMode) || !Settings::ValidPerfOverlayCorner(next.perfOverlayCorner) ||
        !Settings::ValidCoordsWorldChatMode(next.coordsWorldChatMode)) {
        Sync(); return;
    }
    if (!(next == module->settings)) {
        const bool autoLoadChanged = next.autoLoad != module->settings.autoLoad;
        try { module->Change(next); }
        catch (const std::exception& error) {
            if (!autoLoadChanged) throw;
            // A rejected startup registration is recoverable. Keep the module
            // running and restore the native checkbox to the committed state.
            Log(std::string("UI_CHANGE_REJECTED autoLoad: ") + error.what());
            Sync();
        }
    }
}
void OptionsUi::Sync() {
    if (!module || !ModelsLoaded(*this)) return;
    // OptionData owns the native updateValue callback for the current row.
    ApplySettings(*this, module->settings, true);
}
void OptionsUi::Restore() {
    // Owned key controls first: no pooled view may keep one once the hooks
    // stop; nor a hidden button of a statistics line.
    DropKeyFields(*this);
    DropStatsViews(*this);
    confirmForget = false; confirmDeadline = 0; keyStatus = metamob::KeyStatus::None;
    statsConfirm = false; statsResult = 0;
    pendingPage.reset();
    stableList.reset();
    std::exception_ptr failure;
    for (auto it = pages.begin(); it != pages.end();) {
        try {
            auto categories = api.Field(it->owner.get(), "m_options");
            int selected = SelectedCategory(it->owner.get());
            // The mod's parent or any of its sections: back to « Jeu ».
            if (selected >= ModCategory && selected <= ModCategory + ModSections)
                selected = GameCategory;
            api.Call(categories, "Remove", {it->category.get()});
            // Retry rebuilding even when removal succeeded on an earlier pump.
            Rebuild(it->owner.get(), selected);
            it = pages.erase(it);
        } catch (...) {
            if (!failure) failure = std::current_exception();
            ++it;
        }
    }
    if (failure) std::rethrow_exception(failure);
    modeOption.reset();
    modeSource.reset();
    for (auto& ref : combatMiniatureSources) ref.reset();
    for (auto& ref : combatMiniatureOptions) ref.reset();
    for (auto& ref : roleplayMiniatureSources) ref.reset();
    for (auto& ref : roleplayMiniatureOptions) ref.reset();
    for (auto& ref : animationSources) ref.reset();
    for (auto& ref : animationOptions) ref.reset();
    roleplayModeSource.reset();
    roleplayModeOption.reset();
    autoLoadSource.reset();
    githubSource.reset();
    githubOption.reset();
    autoLoadOption.reset();
    soulSource.reset();
    soulOption.reset();
    soulAutoSource.reset();
    soulAutoOption.reset();
    glowSource.reset();
    glowOption.reset();
    glowNpcsSource.reset(); glowNpcsOption.reset();
    glowInteractivesSource.reset(); glowInteractivesOption.reset();
    preloadSource.reset();
    preloadOption.reset();
    resourcesSource.reset();
    resourcesOption.reset();
    resourcesMiniSource.reset(); resourcesMiniOption.reset();
    miniCenterSource.reset(); miniCenterOption.reset();
    travelPlannerSource.reset(); travelPlannerOption.reset();
    chatTravelSource.reset(); chatTravelOption.reset();
    chatSoulSource.reset(); chatSoulOption.reset();
    chatOcreSource.reset(); chatOcreOption.reset();
    chatMetamobSource.reset(); chatMetamobOption.reset();
    archiZonesSource.reset(); archiZonesOption.reset(); archiZonesMiniSource.reset(); archiZonesMiniOption.reset();
    calendarKraloveSource.reset(); calendarKraloveOption.reset();
    templeSource.reset(); templeOption.reset();
    for (auto& ref : extraSources) ref.reset();
    for (auto& ref : extraOptions) ref.reset();
    perfCornerSource.reset(); perfCornerOption.reset();
    chatCoordsSource.reset(); chatCoordsOption.reset();
    creditsOption.reset(); creditsMarker.reset();
    metamobSyncSource.reset(); metamobSyncOption.reset();
    metamobKeyOption.reset(); metamobKeyMarker.reset();
    statsSource.reset(); statsOption.reset(); statsScopeSource.reset(); statsScopeOption.reset();
    statsRows.clear(); statsMarkers.clear(); statsTexts.clear();
    spouseSource.reset();
    spouseOption.reset();
    transitionsSource.reset();
    transitionsOption.reset();
    container.reset();
}
void OptionsUi::Abandon() {
    DropKeyFields(*this);
    DropStatsViews(*this);
    confirmForget = false; confirmDeadline = 0; keyStatus = metamob::KeyStatus::None;
    statsConfirm = false; statsResult = 0;
    pages.clear(); pendingPage.reset(); stableList.reset();
}
bool OptionsUi::KeepResidentHook(void*) const { return false; }
