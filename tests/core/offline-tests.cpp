#include "core/settings.hpp"
#include "core/common.hpp"
#include "appearance/mode-policy.hpp"
#include "appearance/size-policy.hpp"
#include <cmath>
#include <MinHook.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <future>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string FileText(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
// Settings with explicit fields, in place of the positional aggregates the
// removed animationMode field used to shift.
static Settings Profile(int mode, int legacyAnimations, int roleplayMode = 0, int preferredMode = 2, int preferredRoleplayMode = 1) {
    Settings settings;
    settings.mode = mode; settings.roleplayMode = roleplayMode;
    settings.preferredMode = preferredMode; settings.preferredRoleplayMode = preferredRoleplayMode;
    settings.ApplyLegacyAnimationMode(legacyAnimations);
    return settings;
}
// Independent reference of the « Apparence » and « Animations » policies.
static MiniatureGroup ReferenceCombatGroup(bool player, bool monster, bool summon, int team, int localTeam, bool self, bool ownSummon) {
    if (team < 0 || team > 3 || localTeam < 0 || localTeam > 3) return MiniatureGroup::Unknown;
    if (team != localTeam) {
        if (summon) return MiniatureGroup::EnemySummon;
        return player || monster ? MiniatureGroup::Enemy : MiniatureGroup::Unknown;
    }
    if (summon) return ownSummon ? MiniatureGroup::OwnSummon : MiniatureGroup::AlliedSummon;
    if (player) return self ? MiniatureGroup::Self : MiniatureGroup::AlliedPlayer;
    return MiniatureGroup::Unknown;
}
static void PolicyTests() {
    using G = MiniatureGroup;
    // Combat classification, exhaustively against the reference.
    unsigned combatCases = 0;
    for (int flags = 0; flags < 32; ++flags) for (int team = -2; team <= 5; ++team) for (int localTeam = -2; localTeam <= 5; ++localTeam) {
        const bool player = (flags & 1) != 0, monster = (flags & 2) != 0, summon = (flags & 4) != 0, self = (flags & 8) != 0, own = (flags & 16) != 0;
        Require(ClassifyMiniature(player, monster, summon, team, localTeam, self, own) ==
            ReferenceCombatGroup(player, monster, summon, team, localTeam, self, own), "Combat category differs from its reference");
        ++combatCases;
    }
    Require(combatCases == 32 * 8 * 8, "Every combat classification case ran");
    Require(ClassifyMiniature(true, false, false, 0, 0) == G::AlliedPlayer && ClassifyMiniature(true, false, false, 0, 0, true) == G::Self &&
        ClassifyMiniature(true, false, false, 1, 0, true) == G::Enemy, "Moi only within the local team");
    Require(ClassifyMiniature(false, true, true, 0, 0, false, true) == G::OwnSummon && ClassifyMiniature(false, true, true, 0, 0) == G::AlliedSummon &&
        ClassifyMiniature(false, true, true, 1, 0, false, true) == G::EnemySummon, "Mes invocations only within the local team");
    Require(ClassifyMiniature(true, true, true, 1, 0) == G::EnemySummon, "Summon takes precedence over player and monster flags");
    Require(ClassifyMiniature(false, true, false, 0, 0) == G::Unknown, "Unclassified allied non-summoned monster stays unchanged");
    Require(ClassifyMiniature(true, false, false, -1, 0) == G::Unknown && ClassifyMiniature(true, false, false, 0, 4) == G::Unknown,
        "Unknown or unsupported teams keep original appearance");
    // Out of combat.
    for (int flags = 0; flags < 32; ++flags) {
        const bool player = (flags & 1) != 0, monster = (flags & 2) != 0, self = (flags & 4) != 0, party = (flags & 8) != 0, npc = (flags & 16) != 0;
        const G expected = self ? G::RoleplaySelf : player ? (party ? G::PartyMember : G::OtherPlayer) : monster ? G::Monster :
            npc ? G::Npc : G::RoleplayOther;
        Require(ClassifyRoleplayMiniature(player, monster, self, party, npc) == expected, "Roleplay category differs from its reference");
        if (!npc) Require(ClassifyRoleplayMiniature(player, monster, self, party) == expected, "An entity not told an NPC keeps its group");
    }
    // Each group follows exactly its own checkbox, for all 2048 combinations;
    // Unknown never and RoleplayOther always reduced.
    const std::pair<G, bool Settings::*> owners[] = {
        {G::Self, &Settings::miniatureSelf}, {G::AlliedPlayer, &Settings::miniatureAllies}, {G::AlliedSummon, &Settings::miniatureAllySummons},
        {G::OwnSummon, &Settings::miniatureOwnSummons}, {G::Enemy, &Settings::miniatureEnemies}, {G::EnemySummon, &Settings::miniatureEnemySummons},
        {G::RoleplaySelf, &Settings::roleplayMiniatureSelf}, {G::PartyMember, &Settings::roleplayMiniatureParty},
        {G::OtherPlayer, &Settings::roleplayMiniaturePlayers}, {G::Monster, &Settings::roleplayMiniatureMonsters},
        {G::Npc, &Settings::roleplayMiniatureNpcs}};
    std::vector<unsigned> masks;
    for (unsigned mask = 0; mask < 2048; ++mask) {
        Settings s;
        for (size_t i = 0; i < std::size(CombatMiniatureToggles); ++i) s.*CombatMiniatureToggles[i] = ((mask >> i) & 1) != 0;
        for (size_t i = 0; i < std::size(RoleplayMiniatureToggles); ++i) s.*RoleplayMiniatureToggles[i] = ((mask >> (6 + i)) & 1) != 0;
        for (const auto& [group, field] : owners) Require(MiniatureGroupEnabled(s, group) == s.*field, "A category does not follow its own checkbox");
        Require(!MiniatureGroupEnabled(s, G::Unknown) && MiniatureGroupEnabled(s, G::RoleplayOther), "Unknown and other entities keep their policy");
        Require(MiniatureCategoryMask(s) == mask, "Category mask must hold exactly the eleven checkboxes");
        // An NPC is told apart from the other entities whenever « PNJ » can change its look.
        if (!RoleplayNpcMatters(s)) Require(MiniatureGroupEnabled(s, G::Npc) == MiniatureGroupEnabled(s, G::RoleplayOther), "NPC skipped while it matters");
        // The local identity is read whenever it can change a result.
        if (!CombatSelfMatters(s)) Require(MiniatureGroupEnabled(s, G::Self) == MiniatureGroupEnabled(s, G::AlliedPlayer), "Own fighter skipped while it matters");
        if (!CombatOwnSummonMatters(s)) Require(MiniatureGroupEnabled(s, G::OwnSummon) == MiniatureGroupEnabled(s, G::AlliedSummon), "Own summon skipped while it matters");
        if (!RoleplayIdentityMatters(s)) for (G group : {G::RoleplaySelf, G::PartyMember, G::OtherPlayer})
            Require(MiniatureGroupEnabled(s, group) == MiniatureGroupEnabled(s, G::RoleplayOther), "Roleplay identity skipped while it matters");
        masks.push_back(MiniatureCategoryMask(s));
    }
    Require(!CombatSelfMatters(Settings{}) && !CombatOwnSummonMatters(Settings{}) && !RoleplayIdentityMatters(Settings{}),
        "The default (every player box ticked) needs no extra identity read");
    Require(RoleplayNpcMatters(Settings{}) && !MiniatureGroupEnabled(Settings{}, G::Npc),
        "By default NPCs keep their normal size, as before « PNJ »: they are told apart from the other entities");
    // « Animations »: every toggle combination against the reference.
    const uintptr_t effectCaller = 0x1ECE82C, castCallers[] = {0x272E250, 0x2744303};
    for (unsigned mask = 0; mask < 64; ++mask) {
        Settings s;
        for (size_t i = 0; i < std::size(AnimationToggles); ++i) s.*AnimationToggles[i] = ((mask >> i) & 1) != 0;
        Require(OverrideSpellEffectsRead(s, effectCaller) == s.noSpellEffects, "biwv read follows Sans effets des sorts only");
        for (auto caller : castCallers) Require(OverrideSpellEffectsRead(s, caller) == s.shortSpellCasts, "Cast reads follow Lancers raccourcis only");
        for (uintptr_t other : {uintptr_t{0}, effectCaller - 1, effectCaller + 1, uintptr_t{0x272E24B}, uintptr_t{0x2744304}, ~uintptr_t{0}})
            Require(!OverrideSpellEffectsRead(s, other), "Any other option read keeps the account value");
        const bool instant = s.instantMovements || s.noVisualWaits;
        Require(InstantCombatVisuals(s) == instant, "Instant visuals are movements or waits");
        Require(SkipFighterAnimation(s, AnimationStepKind::Ordinary) == s.noAttackAnimations, "Ordinary step follows its toggle only");
        Require(SkipFighterAnimation(s, AnimationStepKind::Death) == (s.noDeathAnimations && !instant), "Death step stays native with instant visuals");
        Require(SkipFighterAnimation(s, AnimationStepKind::Unknown) == (s.noAttackAnimations && s.noDeathAnimations && !instant),
            "A step of unknown provenance is skipped only when every kind would be");
        Require(AnyFighterAnimationSkipped(s) == (SkipFighterAnimation(s, AnimationStepKind::Ordinary) ||
            SkipFighterAnimation(s, AnimationStepKind::Death) || SkipFighterAnimation(s, AnimationStepKind::Unknown)), "Hook gate matches the kinds");
        // Legacy level: the highest level whose toggles are all on.
        const int level = !(s.noSpellEffects && s.shortSpellCasts) ? 0 : !(s.noAttackAnimations && s.noDeathAnimations) ? 1 :
            !(s.instantMovements && s.noVisualWaits) ? 2 : 3;
        Require(s.LegacyAnimationMode() == level, "Legacy animation level");
    }
    // The former four modes keep exactly their behaviour.
    for (int legacy = 0; legacy <= 3; ++legacy) {
        Settings s; s.ApplyLegacyAnimationMode(legacy);
        Require(s.LegacyAnimationMode() == legacy, "Legacy modes round trip");
        Require(OverrideSpellEffectsRead(s, effectCaller) == (legacy >= 1) && OverrideSpellEffectsRead(s, castCallers[1]) == (legacy >= 1),
            "Raccourcies and above shorten every verified read");
        for (auto kind : {AnimationStepKind::Ordinary, AnimationStepKind::Death, AnimationStepKind::Unknown}) {
            const bool before = legacy >= 2 && (legacy == 2 || kind == AnimationStepKind::Ordinary);
            Require(SkipFighterAnimation(s, kind) == before, "Former mode skip policy changed");
        }
        Require(s.instantMovements == (legacy == 3) && s.noVisualWaits == (legacy == 3), "Instantané alone enables instant movements and waits");
    }
}
using TestFunction = int(*)(int);
static TestFunction originalTestFunction;
__declspec(noinline) static int TestTarget(int input) { return input * 9 + 7; }
static int TestReplacement(int input) { return originalTestFunction(input) + 21; }
int main() {
    try {
        auto folder = std::filesystem::current_path() / "offline-test-data";
        std::filesystem::create_directories(folder);
        auto path = folder / "settings.ini";
        bool autoLoadSpecified = true;
        const auto defaults = Settings::Read(folder / "absent.ini", &autoLoadSpecified);
        Require(defaults.mode == 2 && defaults.roleplayMode == 0 && defaults.LegacyAnimationMode() == 1 &&
            defaults.noSpellEffects && defaults.shortSpellCasts && !defaults.noAttackAnimations && !defaults.noDeathAnimations &&
            !defaults.instantMovements && !defaults.noVisualWaits &&
            defaults.autoLoad && defaults.githubUpdates && defaults.soulReminder && defaults.soulAutoEquip && defaults.monsterGlowMode == 1 &&
            defaults.fastMapTransitions && defaults.preloadNextMap && defaults.spouseTeleport && !autoLoadSpecified,
            "New profiles must use all ten defaults without an explicit startup preference");
        for (const char* key : {"githubUpdates", "autoLoad", "soulReminder", "soulAutoEquip", "monsterUiGlow", "fastMapTransitions", "preloadNextMap", "spouseTeleport"}) {
            { std::ofstream file(path); file << key << "=0\n" << key << "=2\n"; }
            const auto disabled = Settings::Read(path, &autoLoadSpecified);
            const bool actual = std::string(key)=="githubUpdates" ? disabled.githubUpdates :
                std::string(key)=="autoLoad" ? disabled.autoLoad :
                std::string(key)=="soulReminder" ? disabled.soulReminder :
                std::string(key)=="soulAutoEquip" ? disabled.soulAutoEquip :
                std::string(key)=="monsterUiGlow" ? disabled.monsterGlowMode != 0 :
                std::string(key)=="fastMapTransitions" ? disabled.fastMapTransitions :
                std::string(key)=="preloadNextMap" ? disabled.preloadNextMap : disabled.spouseTeleport;
            Require(!actual, "Explicit opt-outs survive new defaults and later malformed values");
            Require(autoLoadSpecified == (std::string(key)=="autoLoad"), "Only valid auto-load values count as explicit preferences");
        }
        {
            // « Surbrillance »: six independent categories, in the options order.
            // Monstres alone starts ticked: the monsters were the only outlined
            // actors before these boxes.
            const char* keys[] = {"glowMonsters", "glowNpcs", "glowInteractives", "glowSelf", "glowParty", "glowPlayers"};
            bool Settings::* const fields[] = {&Settings::glowMonsters, &Settings::glowNpcs, &Settings::glowInteractives,
                &Settings::glowSelf, &Settings::glowParty, &Settings::glowPlayers};
            Require(std::size(GlowCategoryToggles) == 6, "Six glow categories");
            for (size_t i = 0; i < 6; ++i)
                Require(GlowCategoryToggles[i] == fields[i] && defaults.*GlowCategoryToggles[i] == (i == 0),
                    "Glow categories in the options order; Monstres alone ticked by default");
            for (unsigned pattern = 0; pattern < 64; ++pattern) {
                auto categories = defaults;
                for (size_t i = 0; i < 6; ++i) categories.*GlowCategoryToggles[i] = ((pattern >> i) & 1) != 0;
                categories.Save(path);
                Require(Settings::Read(path) == categories, "Independent glow categories must persist");
                const auto saved = FileText(path);
                for (size_t i = 0; i < 6; ++i)
                    Require(saved.find(std::string("\n") + keys[i] + "=" + (((pattern >> i) & 1) ? "1" : "0") + "\n") != std::string::npos,
                        "Each glow category is saved on its own line");
                { std::ofstream invalid(path, std::ios::app); invalid << "glowMonsters=2\nglowNpcs=2\nglowInteractives=-1\nglowSelf=3\nglowParty=x\nglowPlayers=\n"; }
                Require(Settings::Read(path) == categories, "Malformed flags must preserve saved choices");
            }
            // A file of an earlier revision: the monsters stay outlined, the NPC and
            // interactive choices are kept, the player categories start unticked.
            { std::ofstream file(path); file << "monsterGlowMode=2\nglowNpcs=1\nglowInteractives=0\n"; }
            const auto old = Settings::Read(path);
            Require(old.monsterGlowMode == 2 && old.glowMonsters && old.glowNpcs && !old.glowInteractives && !old.glowSelf &&
                !old.glowParty && !old.glowPlayers, "Older files keep the monsters and their NPC choice; the players start unticked");
            // Every box unticked is kept (nothing outlined), and the last valid line wins.
            { std::ofstream file(path); file << "glowMonsters=0\nglowPlayers=1\nglowPlayers=0\nglowSelf=1\nglowSelf=0\n"; }
            const auto none = Settings::Read(path);
            Require(!none.glowMonsters && !none.glowNpcs && !none.glowInteractives && !none.glowSelf && !none.glowParty && !none.glowPlayers,
                "Every glow category unticked is kept; the last valid line wins");
        }
        Require(defaults.resourceMapEnabled && defaults.resourceMiniMapEnabled && defaults.resourceMapItems.empty(), "No resource annotations before a selection");
        Require(!defaults.smallHeaderSoul && !defaults.smallHeaderTreasure && !defaults.smallHeaderNotebook && !defaults.smallHeaderStats,
            "Panel headers start at the native size");
        Require(!defaults.notebookCompact, "The notebook starts in its full layout");
        {
            // 1.16.0: the copy and travel buttons of the notebook's position lines
            // (in place of « Copier » and « Voyager » under the form), shown by
            // default, each its own 0/1 line; a file written before them shows both.
            Require(defaults.notebookCopyButton && defaults.notebookTravelButton, "The notebook's copy and travel buttons start shown");
            for (int copyValue : {0, 1}) for (int travelValue : {0, 1}) {
                auto buttons = defaults; buttons.notebookCopyButton = copyValue != 0; buttons.notebookTravelButton = travelValue != 0;
                buttons.Save(path);
                const auto saved = FileText(path);
                Require(saved.find("\nnotebookCopyButton=" + std::to_string(copyValue) + "\n") != std::string::npos &&
                    saved.find("\nnotebookTravelButton=" + std::to_string(travelValue) + "\n") != std::string::npos && Settings::Read(path) == buttons,
                    "The copy and travel buttons are saved on their own lines and read back");
                { std::ofstream invalid(path, std::ios::app); invalid << "notebookCopyButton=2\nnotebookTravelButton=-1\nnotebookCopyButton=x\nnotebookTravelButton=\nnotebookCopyButton= 0\n"; }
                Require(Settings::Read(path) == buttons, "Malformed copy and travel lines preserve the saved choices");
            }
            { std::ofstream older(path); older << "mode=2\nnotebookPingButton=0\n"; }
            const auto beforeButtons = Settings::Read(path);
            Require(!beforeButtons.notebookPingButton && beforeButtons.notebookCopyButton && beforeButtons.notebookTravelButton,
                "A file written before them shows both buttons");
            { std::ofstream twice(path); twice << "notebookCopyButton=0\nnotebookTravelButton=0\nnotebookCopyButton=1\n"; }
            const auto lastLine = Settings::Read(path);
            Require(lastLine.notebookCopyButton && !lastLine.notebookTravelButton, "The last valid copy or travel line wins");
        }
        Require(!defaults.metamobSync, "Metamob capture sync starts disabled");
        Require(defaults.chatTravelLinks && defaults.chatSoulInventory && defaults.chatOcreStatus && defaults.chatMetamobStatus && defaults.mapTravelPlanner,
            "Chat additions and the mod's map travel start enabled, as they were before they had options");
        Require(defaults.archiZonesMap && !defaults.archiZonesMiniMap && defaults.calendarKralamoure && defaults.travelRoublardTemple,
            "Missing-archimonster zones start on the big map only; Kralamoure openings start shown");
        for (auto field : CombatMiniatureToggles) Require(defaults.*field, "Every combat category starts in Miniatures, as before its checkbox");
        for (auto field : RoleplayMiniatureToggles)
            Require(defaults.*field == (field != &Settings::roleplayMiniatureNpcs),
                "Every roleplay category starts in Miniatures, as before its checkbox, except the NPCs the game never reduced");
        {
            // The seventeen new checkboxes: each one alone away from its default,
            // all at once, and mixed patterns, through Save and Read.
            std::vector<bool Settings::*> flags(std::begin(CombatMiniatureToggles), std::end(CombatMiniatureToggles));
            flags.insert(flags.end(), std::begin(RoleplayMiniatureToggles), std::end(RoleplayMiniatureToggles));
            flags.insert(flags.end(), std::begin(AnimationToggles), std::end(AnimationToggles));
            Require(flags.size() == 17, "Seventeen appearance and animation checkboxes");
            std::vector<unsigned> patterns{0u, 0x1FFFFu, 0x15555u, 0x0AAAAu, 0x10F0Fu, 0x0F0F0u, 0x11234u, 0x0FEDCu};
            for (unsigned bit = 0; bit < 17; ++bit) { patterns.push_back(1u << bit); patterns.push_back(0x1FFFFu ^ (1u << bit)); }
            for (unsigned pattern : patterns) {
                auto choices = defaults;
                for (size_t i = 0; i < flags.size(); ++i) choices.*flags[i] = ((pattern >> i) & 1) != 0;
                choices.Save(path);
                Require(Settings::Read(path) == choices, "Appearance and animation checkboxes persist independently");
                const auto saved = FileText(path);
                Require(saved.find("\nanimationMode=" + std::to_string(choices.LegacyAnimationMode()) + "\n") != std::string::npos &&
                    saved.find(std::string("\nnoAnimations=") + (choices.LegacyAnimationMode() ? "1" : "0") + "\n") != std::string::npos,
                    "Saved files keep the closest legacy animation mode for older revisions");
                { std::ofstream invalid(path, std::ios::app); invalid << "miniatureSelf=2\nnoSpellEffects=-1\ninstantMovements=yes\n"; }
                Require(Settings::Read(path) == choices, "Malformed checkbox values preserve the saved choices");
            }
            // « PNJ » (1.14.0): its own line; a file written before it keeps the NPCs at their size.
            auto npcs = defaults; npcs.roleplayMiniatureNpcs = true; npcs.Save(path);
            Require(FileText(path).find("\nroleplayMiniatureNpcs=1\n") != std::string::npos && Settings::Read(path) == npcs, "« PNJ » saved on its own line");
            { std::ofstream file(path); file << "mode=2\nroleplayMode=2\nroleplayMiniatureMonsters=0\n"; }
            const auto older = Settings::Read(path);
            Require(!older.roleplayMiniatureNpcs && !older.roleplayMiniatureMonsters && older.roleplayMiniaturePlayers, "A file without « PNJ » keeps NPCs unreduced");
            { std::ofstream file(path); file << "roleplayMiniatureNpcs=1\nroleplayMiniatureNpcs=2\n"; }
            Require(Settings::Read(path).roleplayMiniatureNpcs, "A malformed « PNJ » line preserves the saved choice");
        }
        // Migration: a file without the new keys takes the toggles of its legacy
        // animationMode (or noAnimations); present keys win whatever the order.
        for (int legacy = 0; legacy <= 3; ++legacy) {
            { std::ofstream file(path); file << "mode=2\nanimationMode=" << legacy << "\n"; }
            auto migrated = Settings::Read(path);
            Require(migrated == Profile(2, legacy), "Each former animation mode migrates to its exact toggles");
            for (bool newFirst : {false, true}) {
                std::ofstream file(path);
                const std::string legacyLine = "animationMode=" + std::to_string(legacy) + "\nnoAnimations=1\n";
                const std::string newLines = "noSpellEffects=0\ninstantMovements=1\n";
                file << (newFirst ? newLines + legacyLine : legacyLine + newLines);
                file.close();
                auto mixed = Settings::Read(path);
                auto expected = Profile(2, legacy); expected.noSpellEffects = false; expected.instantMovements = true;
                Require(mixed == expected, "Explicit toggles win; the missing ones come from the legacy mode");
            }
        }
        for (int legacyBool : {0, 1}) {
            { std::ofstream file(path); file << "noAnimations=" << legacyBool << "\n"; }
            Require(Settings::Read(path).LegacyAnimationMode() == legacyBool, "Oldest noAnimations migrates to Normal or Raccourcies");
        }
        // The 1.13.0 and 1.14.0 checkboxes: map, notebook, chat and overlay
        // additions on by default, the overlay's three parts off (each one shows
        // it since 1.16.0) and its corner the top left; each one persists alone
        // and together, with the overlay's corner and place.
        Require(defaults.resourceMiniMapBar && defaults.miniMapZoneName && defaults.coordsWorldMapMode == 1 && defaults.coordsWorldMiniMapMode == 1 &&
            defaults.notebookPingButton && defaults.mapAddPosition && defaults.coordsWorldChatMode == 1 && !defaults.perfOverlay &&
            defaults.perfOverlayCorner == 0 && !defaults.perfOverlayGpu && !defaults.perfOverlayDisk && defaults.perfOverlayDrag &&
            defaults.perfOverlayX == -1 && defaults.perfOverlayY == -1 && Settings{} == defaults,
            "The 1.13.0 and 1.14.0 additions start shown, the performance overlay hidden (no part ticked) in the top left corner");
        static_assert(std::size(ExtraToggles) == 11); // with 1.16.0's: the notebook's copy and travel buttons, the overlay's top-bar button
        for (unsigned mask = 0; mask < 256; ++mask) {
            auto extra = defaults;
            // Every combination of the first five, many times; the others follow inverted bits.
            for (size_t i = 0; i < std::size(ExtraToggles); ++i) extra.*ExtraToggles[i] = (((mask >> (i % 5)) & 1) != 0) != (i >= 5);
            extra.perfOverlayCorner = static_cast<int>(mask % 4);
            // The three dimension choices: each of their 27 combinations.
            extra.coordsWorldChatMode = static_cast<int>(mask % 3);
            extra.coordsWorldMapMode = static_cast<int>(mask / 3 % 3);
            extra.coordsWorldMiniMapMode = static_cast<int>(mask / 9 % 3);
            if (mask % 3) {
                extra.perfOverlayX = static_cast<int>(mask * 39 % 10001);
                extra.perfOverlayY = static_cast<int>(10000 - mask * 23 % 10001);
            }
            extra.Save(path);
            Require(Settings::Read(path) == extra, "Each checkbox, the overlay corner and its place persist independently");
            { std::ofstream invalid(path, std::ios::app); invalid << "perfOverlayCorner=4\nperfOverlayCorner=-1\nminiMapZoneName=2\nperfOverlay=x\n"
                "perfOverlayX=10001\nperfOverlayY=-2\nperfOverlayGpu=2\nperfOverlayDrag=-1\ncoordsWorldChatMode=3\ncoordsWorldChat=2\n"
                "coordsWorldMapMode=3\ncoordsWorldMiniMapMode=-1\ncoordsWorldMap=2\ncoordsWorldMiniMap=x\ndimensionChoices=0\n"; }
            Require(Settings::Read(path) == extra, "Malformed values preserve the saved choices");
        }
        {
            { std::ofstream half(path); half << "perfOverlayX=1200\n"; }
            const auto read = Settings::Read(path);
            Require(read.perfOverlayX == -1 && read.perfOverlayY == -1, "A lone overlay coordinate falls back to the corner");
            auto lone = defaults; lone.perfOverlayY = 5;
            bool rejected = false;
            try { lone.Save(path); } catch (const std::exception&) { rejected = true; }
            Require(rejected, "A lone overlay coordinate is never saved");
        }
        {
            // 1.16.0: the overlay's three boxes are independent, and every file it
            // writes says so (perfOverlayParts=1): « Charge du GPU » or « Activité
            // du disque » alone stays ticked.
            auto parts = defaults; parts.perfOverlayGpu = parts.perfOverlayDisk = true; parts.perfOverlayX = 2500; parts.perfOverlayY = 7500;
            parts.perfOverlayDrag = false; parts.perfOverlayCorner = 3;
            parts.Save(path);
            Require(FileText(path).find("\nperfOverlayParts=1\n") != std::string::npos && Settings::Read(path) == parts,
                "perfOverlayParts=1 written: the GPU and the disk alone, the corner of an earlier revision and a locked place kept");
            // A file written before 1.16.0 (no perfOverlayParts) keeps the GPU and
            // the disk only with « Afficher les FPS et la mémoire » ticked: without
            // it they showed nothing, and must not show an overlay now.
            for (int fps : {0, 1}) for (int gpu : {0, 1}) for (int disk : {0, 1}) {
                { std::ofstream older(path); older << "mode=2\nperfOverlay=" << fps << "\nperfOverlayGpu=" << gpu << "\nperfOverlayDisk=" << disk
                    << "\nperfOverlayDrag=0\nperfOverlayCorner=1\nperfOverlayX=1200\nperfOverlayY=300\n"; }
                const auto older = Settings::Read(path);
                Require(older.perfOverlay == (fps != 0) && older.perfOverlayGpu == (fps != 0 && gpu != 0) &&
                    older.perfOverlayDisk == (fps != 0 && disk != 0) && !older.perfOverlayDrag && older.perfOverlayCorner == 1 &&
                    older.perfOverlayX == 1200 && older.perfOverlayY == 300,
                    "Before 1.16.0: the GPU and the disk kept only with the FPS box, the rest as saved");
                { std::ofstream marked(path, std::ios::app); marked << "perfOverlayParts=1\n"; }
                const auto marked = Settings::Read(path);
                Require(marked.perfOverlay == (fps != 0) && marked.perfOverlayGpu == (gpu != 0) && marked.perfOverlayDisk == (disk != 0),
                    "Written by 1.16.0: each box as saved, whatever the others");
            }
            // 1.13.x had no GPU or disk line: their default, off.
            { std::ofstream older(path); older << "perfOverlay=1\n"; }
            const auto first = Settings::Read(path);
            Require(first.perfOverlay && !first.perfOverlayGpu && !first.perfOverlayDisk, "A 1.13.x file: the FPS part alone");
            // Only a positive perfOverlayParts marks a file of 1.16.0 or later.
            { std::ofstream bad(path); bad << "perfOverlayGpu=1\nperfOverlayDisk=1\nperfOverlayParts=0\nperfOverlayParts=x\nperfOverlayParts=-1\n"; }
            Require(!Settings::Read(path).perfOverlayGpu && !Settings::Read(path).perfOverlayDisk, "A zero or malformed marker is no marker");
            { std::ofstream later(path); later << "perfOverlayDisk=1\nperfOverlayParts=2\n"; }
            Require(Settings::Read(path).perfOverlayDisk, "A later marker keeps the boxes as saved");
            // The update's first save writes the migrated choice with the marker:
            // a box ticked afterwards stays ticked.
            { std::ofstream older(path); older << "perfOverlay=0\nperfOverlayGpu=1\nperfOverlayDisk=1\n"; }
            auto migrated = Settings::Read(path);
            migrated.Save(path);
            const auto saved = FileText(path);
            Require(saved.find("\nperfOverlayGpu=0\n") != std::string::npos && saved.find("\nperfOverlayDisk=0\n") != std::string::npos &&
                saved.find("\nperfOverlayParts=1\n") != std::string::npos && Settings::Read(path) == migrated, "Saved migrated, with the marker");
            migrated.perfOverlayGpu = true;
            migrated.Save(path);
            const auto ticked = Settings::Read(path);
            Require(ticked.perfOverlayGpu && !ticked.perfOverlay && !ticked.perfOverlayDisk, "Ticked after the update: kept");
        }
        {
            // 1.16.0: the overlay's top-bar button (a checkbox, on by default),
            // whether that button hid it (perfOverlayShown, not hidden by default)
            // and its size in percent of its natural size (perfOverlayScale, 100 by
            // default, 50 to 300). Each persists alone and together; anything else
            // is never read, and a size outside 50..300 is never saved.
            Require(defaults.perfOverlayButton && defaults.perfOverlayShown && defaults.perfOverlayScale == 100 &&
                Settings::ValidPerfOverlayScale(50) && Settings::ValidPerfOverlayScale(300) && !Settings::ValidPerfOverlayScale(49) &&
                !Settings::ValidPerfOverlayScale(301), "The top-bar button shown, the overlay not hidden by it, at its natural size");
            for (int scale : {50, 77, 100, 150, 300})
                for (int mask = 0; mask < 4; ++mask) {
                    auto overlay = defaults;
                    overlay.perfOverlayScale = scale;
                    overlay.perfOverlayShown = (mask & 1) != 0;
                    overlay.perfOverlayButton = (mask & 2) != 0;
                    overlay.Save(path);
                    const auto saved = FileText(path);
                    Require(Settings::Read(path) == overlay && saved.find("\nperfOverlayScale=" + std::to_string(scale) + "\n") != std::string::npos &&
                        saved.find(std::string("\nperfOverlayShown=") + ((mask & 1) ? "1" : "0") + "\n") != std::string::npos &&
                        saved.find(std::string("\nperfOverlayButton=") + ((mask & 2) ? "1" : "0") + "\n") != std::string::npos,
                        "The overlay's size, its hiding by the button and the button itself persist");
                    { std::ofstream invalid(path, std::ios::app); invalid << "perfOverlayScale=49\nperfOverlayScale=301\nperfOverlayScale=0\n"
                        "perfOverlayScale=-100\nperfOverlayScale=x\nperfOverlayShown=2\nperfOverlayShown=-1\nperfOverlayButton=2\nperfOverlayButton=x\n"; }
                    Require(Settings::Read(path) == overlay, "Malformed overlay values keep the saved ones");
                }
            // A file without them (before 1.16.0): shown, with its button, at its natural size.
            { std::ofstream older(path); older << "perfOverlay=1\nperfOverlayX=1200\nperfOverlayY=300\n"; }
            const auto older = Settings::Read(path);
            Require(older.perfOverlay && older.perfOverlayShown && older.perfOverlayButton && older.perfOverlayScale == 100,
                "Before 1.16.0: shown, with its button, at its natural size");
            for (const char* scale : {"49", "301", "0", "-1", "1000", "150x", ""}) {
                { std::ofstream odd(path); odd << "perfOverlayScale=" << scale << "\n"; }
                Require(Settings::Read(path).perfOverlayScale == 100, "An overlay size outside 50..300, or not a number, reads as 100");
            }
            // Hidden with the button: still hidden at the next start, its parts still ticked.
            auto hidden = defaults;
            hidden.perfOverlay = hidden.perfOverlayGpu = true;
            hidden.perfOverlayShown = false;
            hidden.Save(path);
            const auto restarted = Settings::Read(path);
            Require(!restarted.perfOverlayShown && restarted.perfOverlay && restarted.perfOverlayGpu && restarted == hidden,
                "Hidden with its button: hidden at the next start, its parts still ticked");
            for (int invalid : {49, 301, 0, -100}) {
                auto refused = defaults;
                refused.perfOverlayScale = invalid;
                std::string refusal;
                try { refused.Save(path); } catch (const std::runtime_error& error) { refusal = error.what(); }
                Require(refusal == "Invalid performance overlay scale" && Settings::Read(path) == hidden, "An overlay size outside 50..300 is never saved");
            }
        }
        {
            // The dimension with the coordinates, three choices each (0 « Non », 1 « Oui,
            // sauf dans le Monde des Douze », 2 « Oui »), « Oui, sauf » by default since
            // 1.16.0. 1.13.x had a checkbox for the chat: checked is « Oui », unchecked
            // « Non »; the maps had theirs until 1.16.0: checked is « Oui, sauf ». A file
            // written before 1.16.0 (no dimensionChoices=1) also turns the chat's « Oui »,
            // its default then, into the new default; a file written since keeps every
            // choice. The choices win over the checkboxes, still written for older revisions.
            const auto read = [&](const std::string& text) {
                { std::ofstream file(path); file << text; }
                const auto settings = Settings::Read(path);
                return std::vector<int>{settings.coordsWorldChatMode, settings.coordsWorldMapMode, settings.coordsWorldMiniMapMode};
            };
            Require(read("mode=2\n") == std::vector<int>{1, 1, 1}, "Without any of their lines: the three defaults, « Oui, sauf »");
            for (int legacy : {0, 1}) {
                const auto box = std::to_string(legacy), other = std::to_string(1 - legacy);
                Require(read("coordsWorldChat=" + box + "\n")[0] == legacy,
                    "The 1.13.x chat checkbox: « Oui », then the « Oui, sauf » of 1.16.0, or « Non »");
                Require(read("dimensionChoices=1\ncoordsWorldChat=" + box + "\n")[0] == 2 * legacy,
                    "Marked since 1.16.0, a chat checkbox alone still reads « Oui » or « Non »");
                Require(read("coordsWorldMap=" + box + "\ncoordsWorldMiniMap=" + other + "\n") == std::vector<int>{1, legacy, 1 - legacy},
                    "The maps' checkboxes before 1.16.0: « Oui, sauf » or « Non », each its own");
                const std::string modes = "coordsWorldChatMode=0\ncoordsWorldMapMode=2\ncoordsWorldMiniMapMode=0\n";
                const std::string boxes = "coordsWorldChat=" + box + "\ncoordsWorldMap=" + box + "\ncoordsWorldMiniMap=" + box + "\n";
                for (const char* marker : {"", "dimensionChoices=1\n"})
                    for (bool modesFirst : {false, true})
                        Require(read(marker + (modesFirst ? modes + boxes : boxes + modes)) == std::vector<int>{0, 2, 0},
                            "The choices win over the checkboxes, whatever the order");
            }
            // The chat's choice in a file of 1.14.0 to 1.15.x: « Oui », its default then, becomes « Oui, sauf ».
            for (int mode = 0; mode <= 2; ++mode) {
                const auto chat = "coordsWorldChatMode=" + std::to_string(mode) + "\ncoordsWorldChat=" + (mode ? "1" : "0") + "\n";
                Require(read(chat)[0] == (mode == 2 ? 1 : mode), "Before 1.16.0, the chat's « Oui » becomes « Oui, sauf », the others stay");
                Require(read("dimensionChoices=1\n" + chat)[0] == mode && read(chat + "dimensionChoices=1\n")[0] == mode,
                    "Marked since 1.16.0, every chat choice stays as it is");
                Require(read("dimensionChoices=0\n" + chat + "dimensionChoices=x\n")[0] == (mode == 2 ? 1 : mode),
                    "Only dimensionChoices=1 marks a file written since 1.16.0");
            }
            // What a 1.15.0 client writes with its defaults gives the defaults of 1.16.0.
            Require(read("coordsWorldMap=1\ncoordsWorldMiniMap=1\ncoordsWorldChatMode=2\ncoordsWorldChat=1\n") == std::vector<int>{1, 1, 1},
                "The defaults of 1.15.0 become the defaults of 1.16.0");
            // Every combination saved and read back as it is, with the checkboxes and the mark.
            auto last = defaults;
            for (int mask = 0; mask < 27; ++mask) {
                last = defaults;
                last.coordsWorldChatMode = mask % 3; last.coordsWorldMapMode = mask / 3 % 3; last.coordsWorldMiniMapMode = mask / 9;
                last.Save(path);
                Require(Settings::Read(path) == last, "Every dimension choice persists as it is");
                const auto saved = FileText(path);
                const std::pair<const char*, int> written[] = {{"coordsWorldChat", last.coordsWorldChatMode},
                    {"coordsWorldMap", last.coordsWorldMapMode}, {"coordsWorldMiniMap", last.coordsWorldMiniMapMode}};
                for (const auto& [key, mode] : written)
                    Require(saved.find("\n" + std::string(key) + "Mode=" + std::to_string(mode) + "\n") != std::string::npos &&
                        saved.find("\n" + std::string(key) + "=" + (mode ? "1" : "0") + "\n") != std::string::npos,
                        "Each choice is written with the checkbox of the revisions before it, ticked for any choice but « Non »");
                Require(saved.find("\ndimensionChoices=1\n") != std::string::npos, "A saved file is marked as written since 1.16.0");
            }
            for (int invalid : {-1, 3})
                for (auto field : {&Settings::coordsWorldChatMode, &Settings::coordsWorldMapMode, &Settings::coordsWorldMiniMapMode}) {
                    auto refused = defaults; refused.*field = invalid;
                    std::string refusal;
                    try { refused.Save(path); } catch (const std::runtime_error& error) { refusal = error.what(); }
                    Require(refusal == (field == &Settings::coordsWorldChatMode ? "Invalid chat dimension choice" : "Invalid map dimension choice") &&
                        Settings::Read(path) == last, "An unknown dimension choice is never saved");
                }
        }
        {
            Require(defaults.statsEnabled, "Statistics are counted by default");
            auto off = defaults; off.statsEnabled = false; off.Save(path);
            Require(!Settings::Read(path).statsEnabled, "Turning the statistics off persists");
            { std::ofstream bad(path, std::ios::app); bad << "statsEnabled=2\n"; }
            Require(!Settings::Read(path).statsEnabled, "A malformed statistics value keeps the saved choice");
        }
        {
            // The statistics window's top-bar button (1.16.0): shown by default, on
            // its own line, independent of the counting.
            Require(defaults.statsButton, "The statistics window's top-bar button is shown by default");
            for (bool counting : {false, true})
                for (bool button : {false, true}) {
                    auto both = defaults; both.statsEnabled = counting; both.statsButton = button; both.Save(path);
                    Require(Settings::Read(path) == both && FileText(path).find(std::string("\nstatsButton=") + (button ? "1" : "0") + "\n") != std::string::npos,
                        "The statistics button persists on its own line, independently of the counting");
                }
            auto hidden = defaults; hidden.statsButton = false; hidden.Save(path);
            { std::ofstream bad(path, std::ios::app); bad << "statsButton=2\nstatsButton=-1\nstatsButton=oui\n"; }
            Require(!Settings::Read(path).statsButton, "A malformed statistics button value keeps the saved choice");
            { std::ofstream older(path, std::ios::trunc); older << "statsEnabled=0\n"; }
            const auto older = Settings::Read(path);
            Require(older.statsButton && !older.statsEnabled, "A file written before 1.16.0 shows the button");
            // « Réduire les titres des modules » for the statistics window: its own line too.
            for (bool small : {false, true}) {
                auto header = defaults; header.smallHeaderStats = small; header.Save(path);
                Require(Settings::Read(path) == header && FileText(path).find(std::string("\nsmallHeaderStats=") + (small ? "1" : "0") + "\n") != std::string::npos,
                    "The statistics window's reduced header persists on its own line");
            }
            { std::ofstream bad(path, std::ios::app); bad << "smallHeaderStats=2\nsmallHeaderStats=-1\n"; }
            Require(Settings::Read(path).smallHeaderStats, "A malformed header value keeps the saved choice");
        }
        {
            auto corner = defaults; corner.perfOverlayCorner = 4;
            bool rejected = false;
            try { corner.Save(path); } catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "An invalid overlay corner is never saved");
        }
        for (int mask = 0; mask < 512; ++mask) {
            auto flags = defaults;
            flags.chatTravelLinks = (mask & 1) != 0; flags.chatSoulInventory = (mask & 2) != 0;
            flags.chatOcreStatus = (mask & 4) != 0; flags.mapTravelPlanner = (mask & 8) != 0;
            flags.chatMetamobStatus = (mask & 16) != 0; flags.archiZonesMap = (mask & 32) != 0;
            flags.archiZonesMiniMap = (mask & 64) != 0; flags.calendarKralamoure = (mask & 128) != 0;
            flags.travelRoublardTemple = (mask & 256) != 0;
            flags.Save(path);
            Require(Settings::Read(path) == flags, "Chat and map travel choices persist independently");
        }
        {
            // « Réglages des cartes au clic droit » (1.16.0): on by default, its own
            // 0/1 line, independent of « Ajouter aux positions »; a file written
            // before it shows each map's settings in its right-click menu.
            Require(defaults.mapMenuSettings, "Each map's settings are in its right-click menu by default");
            for (bool rows : {false, true})
                for (bool position : {false, true}) {
                    auto menus = defaults; menus.mapMenuSettings = rows; menus.mapAddPosition = position; menus.Save(path);
                    Require(Settings::Read(path) == menus && FileText(path).find(std::string("\nmapMenuSettings=") + (rows ? "1" : "0") + "\n") != std::string::npos,
                        "The right-click map settings persist on their own line, independently of « Ajouter aux positions »");
                }
            auto off = defaults; off.mapMenuSettings = false; off.Save(path);
            { std::ofstream bad(path, std::ios::app); bad << "mapMenuSettings=2\nmapMenuSettings=-1\nmapMenuSettings=oui\nmapMenuSettings=\n"; }
            Require(Settings::Read(path) == off, "A malformed right-click map settings value keeps the saved choice");
            { std::ofstream on(path, std::ios::app); on << "mapMenuSettings=1\n"; }
            Require(Settings::Read(path).mapMenuSettings, "The last valid right-click map settings line wins");
            { std::ofstream older(path, std::ios::trunc); older << "mapAddPosition=0\n"; }
            const auto older = Settings::Read(path);
            Require(older.mapMenuSettings && !older.mapAddPosition, "A file written before 1.16.0 shows each map's settings in its right-click menu");
        }
        // metamobSync is shared state between the game clients: read straight from the file.
        Require(!Settings::ReadMetamobSync(folder / "absent.ini"), "No shared sync value without a settings file");
        {
            auto shared = defaults; shared.metamobSync = true; shared.Save(path);
            Require(Settings::ReadMetamobSync(path) == std::optional<bool>(true), "The shared sync value reads back on");
            shared.metamobSync = false; shared.Save(path);
            Require(Settings::ReadMetamobSync(path) == std::optional<bool>(false), "The shared sync value reads back off");
            { std::ofstream older(path, std::ios::trunc); older << "mode=1" << std::endl << "autoLoad=1" << std::endl; }
            Require(!Settings::ReadMetamobSync(path), "A file written by an older revision has no shared sync value");
            defaults.Save(path, false);
            Require(!Settings::ReadMetamobSync(path) && Settings::Read(path) == defaults,
                "A client that has not seen the shared sync value leaves its line out and saves the rest");
        }
        for(bool full:{false,true})for(bool mini:{false,true}){
            auto maps=defaults;maps.resourceMapEnabled=full;maps.resourceMiniMapEnabled=mini;maps.resourceMapItems={303,421};maps.smallHeaderNotebook=true;maps.smallHeaderSoul=mini;maps.smallHeaderTreasure=full;maps.notebookCompact=full;maps.metamobSync=mini;
            maps.Save(path);
            Require(Settings::Read(path)==maps,"Map visibility flags persist independently without losing the shared selection");
            {std::ofstream file(path,std::ios::app);file<<"resourceMiniMapEnabled=7\n";}
            Require(Settings::Read(path)==maps,"Invalid mini-map preference preserves the saved value");
        }
        {
            Settings many;
            many.resourceMapEnabled = false;
            for (int i = 1; i <= 128; ++i) many.resourceMapItems.push_back(i * 17);
            many.Save(path);
            Require(Settings::Read(path) == many, "128 selected resources must roundtrip beyond one INI line");
            for (const std::vector<int> invalid : {std::vector<int>{0}, {303, 303}, {421, 303}, {1000001}}) {
                auto bad = many; bad.resourceMapItems = invalid;
                bool rejected = false;
                try { bad.Save(path); } catch (const std::exception&) { rejected = true; }
                Require(rejected && Settings::Read(path) == many, "Invalid resource selection must preserve the existing file");
            }
            auto tooMany = many; tooMany.resourceMapItems.push_back(9999);
            bool rejected = false;
            try { tooMany.Save(path); } catch (const std::exception&) { rejected = true; }
            Require(rejected && Settings::Read(path) == many, "Oversized resource selection must be rejected before writing");
            { std::ofstream file(path); file << "resourceMapEnabled=0\nresourceMapEnabled=7\nresourceMapItem=421\nresourceMapItem=303\nresourceMapItem=421\nresourceMapItem=-1\nresourceMapItem=1000001\nresourceMapItem=no\n"; }
            auto normalized = Settings::Read(path);
            Require(!normalized.resourceMapEnabled && !normalized.resourceMiniMapEnabled && normalized.resourceMapItems == std::vector<int>({303,421}),
                "Resource IDs must normalize and malformed entries must not undo opt-out");
            { std::ofstream file(path); for (int i=1;i<=200;++i) file << "resourceMapItem=" << i << '\n'; }
            Require(Settings::Read(path).resourceMapItems.size() == 128, "Resource selection read must be bounded");
        }
        { std::ofstream file(path); file << "mode=0\nroleplayMode=2\nanimationMode=3\nautoLoad=1\n"; }
        const auto existing = Settings::Read(path, &autoLoadSpecified);
        Require(existing.mode==0 && existing.roleplayMode==2 && existing.LegacyAnimationMode()==3 && existing.autoLoad && autoLoadSpecified,
            "Existing display and animation selections are never migrated to new defaults");
        for (int mode = 0; mode < 3; ++mode) {
            for (int animations : {0, 1, 2, 3}) {
                Settings wanted = Profile(mode, animations); wanted.Save(path);
                Require(Settings::Read(path) == wanted, "Preferences must survive atomic overwrite and reload");
            }
        }
        { std::ofstream file(path); file << "mode=99\nnoAnimations=maybe\nmode=-1\n"; }
        Require(Settings::Read(path) == Settings{}, "Malformed settings must retain the requested defaults");
        { std::ofstream file(path); file << "mode=2\nnoAnimations=1\n"; }
        auto legacy = Settings::Read(path);
        Require(legacy.mode == 2 && legacy.preferredMode == 2 && legacy.roleplayMode == 0 && legacy.autoLoad,
            "Legacy unspecified values use current defaults; outside remains Normal");
        Require(legacy.LegacyAnimationMode() == 1, "Old no-animation preferences migrate to the previously supported mode");
        { std::ofstream file(path); file << "mode=2\nanimationMode=2\nnoAnimations=0\n"; }
        Require(Settings::Read(path).LegacyAnimationMode() == 2, "Explicit animation mode overrides legacy bool independent of line order");
        { std::ofstream file(path); file << "mode=2\nanimationMode=3\nnoAnimations=0\n"; }
        Require(Settings::Read(path).LegacyAnimationMode() == 3, "Instant visual mode persists independently of legacy bool");
        for (int invalid : {-1, 4, 99}) {
            { std::ofstream file(path); file << "animationMode=" << invalid << '\n'; }
            Require(Settings::Read(path) == Settings{}, "Invalid former animation mode must preserve the shortened defaults");
        }
        Settings custom;
        custom.mode = 2;
        custom.autoLoad = true; custom.roleplayMode = 2;
        custom.preferredMode = 2; custom.preferredRoleplayMode = 2;
        custom.ApplyLegacyAnimationMode(2);
        custom.miniatureEnemies = false; custom.roleplayMiniatureParty = false;
        custom.soulReminder = false;
        custom.soulAutoEquip = false;
        custom.spouseTeleport = false;
        custom.monsterGlowMode = 3;
        custom.fastMapTransitions = true;
        custom.preloadNextMap = true;
        Require(Settings{}.preloadNextMap, "Preload defaults on");
        custom.soulPanelX = 700; custom.soulPanelY = 340;
        custom.Save(path);
        Require(Settings::Read(path) == custom, "Independent contexts, sizing preset and autoload roundtrip");
        { std::ofstream file(path); file << "soulReminder=7\nsoulPanelX=-1\nsoulPanelY=999999\n"; }
        Require(Settings::Read(path).soulReminder && Settings::Read(path).soulPanelX == 24 &&
            Settings::Read(path).soulPanelY == 180, "Invalid reminder preferences retain visible safe defaults");
        Require(Settings{}.soulAutoEquip && legacy.soulAutoEquip, "Automatic soul equip defaults on for new and legacy preferences");
        for (int enabled : {0,1}) {
            { std::ofstream file(path); file << "soulAutoEquip=" << enabled << '\n'; }
            Require(Settings::Read(path).soulAutoEquip == (enabled != 0), "Soul auto equip boolean read failed");
        }
        for (const char* invalid : {"-1", "2", "true", "1junk"}) {
            { std::ofstream file(path); file << "soulAutoEquip=" << invalid << '\n'; }
            Require(Settings::Read(path).soulAutoEquip, "Invalid soul auto equip value preserves default");
            { std::ofstream file(path); file << "soulAutoEquip=0\nsoulAutoEquip=" << invalid << '\n'; }
            Require(!Settings::Read(path).soulAutoEquip, "Malformed setting must not overwrite an explicit opt-out");
            { std::ofstream file(path); file << "monsterUiGlow=" << invalid << '\n'; }
            Require(Settings::Read(path).monsterGlowMode == 1, "Invalid legacy glow preference retains UI default");
        }
        for (int mode : {0, 1, 2, 3}) {
            auto glowSettings = custom;
            glowSettings.monsterGlowMode = mode;
            glowSettings.Save(path);
            Require(Settings::Read(path) == glowSettings, "All glow modes must roundtrip without changing other preferences");
            std::ifstream file(path);
            const std::string savedGlow((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            Require(savedGlow.find(std::string("\nmonsterUiGlow=") + (mode == 0 ? "0" : "1")) != std::string::npos,
                "Saving must retain a compatible legacy enable bit");
            for (int legacyGlow : {0, 1}) for (bool newFirst : {false, true}) {
                const auto oldLine = "monsterUiGlow=" + std::to_string(legacyGlow) + "\n";
                const auto newLine = "monsterGlowMode=" + std::to_string(mode) + "\n";
                { std::ofstream output(path); output << (newFirst ? newLine + oldLine : oldLine + newLine); }
                Require(Settings::Read(path).monsterGlowMode == mode,
                    "Explicit glow mode must override legacy enable regardless of line order");
            }
        }
        for (const char* invalid : {"-1", "4", "99", "true", "1junk"}) {
            { std::ofstream file(path); file << "monsterGlowMode=" << invalid << '\n'; }
            Require(Settings::Read(path).monsterGlowMode == 1, "Malformed glow mode keeps UI default");
            for (int valid : {0, 2, 3}) {
                { std::ofstream file(path); file << "monsterGlowMode=" << valid << "\nmonsterGlowMode=" << invalid << "\nmonsterUiGlow=1\n"; }
                Require(Settings::Read(path).monsterGlowMode == valid, "Malformed or legacy values must not replace explicit glow mode");
            }
            { std::ofstream file(path); file << "monsterGlowMode=" << invalid << "\nmonsterUiGlow=0\n"; }
            Require(Settings::Read(path).monsterGlowMode == 0, "Malformed new glow mode must not block legacy opt-out migration");
        }
        for (int invalid : {-1, 4, 99}) {
            auto refusedGlow = custom;
            refusedGlow.monsterGlowMode = invalid;
            bool rejected = false;
            try { refusedGlow.Save(path); } catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "Invalid glow modes cannot be saved");
        }
        for (const char* invalid : {"-1", "2", "true", "1junk"}) {
            { std::ofstream file(path); file << "fastMapTransitions=" << invalid << '\n'; }
            Require(Settings::Read(path).fastMapTransitions, "Invalid transition preference retains enabled default");
            { std::ofstream file(path); file << "preloadNextMap=" << invalid << '\n'; }
            Require(Settings::Read(path).preloadNextMap, "Invalid preload preference retains enabled default");
            { std::ofstream file(path); file << "spouseTeleport=" << invalid << '\n'; }
            Require(Settings::Read(path).spouseTeleport, "Invalid spouse preference retains enabled default");
            { std::ofstream file(path); file << "spouseTeleport=0\nspouseTeleport=" << invalid << '\n'; }
            Require(!Settings::Read(path).spouseTeleport, "Invalid spouse preference preserves explicit opt-out");
        }
        std::vector<std::future<void>> writers;
        for (int writer = 0; writer < 4; ++writer) {
            writers.push_back(std::async(std::launch::async, [&, writer] {
                auto concurrent = custom;
                concurrent.ApplyLegacyAnimationMode(writer);
                concurrent.roleplayMode = writer % 3;
                for (int iteration = 0; iteration < 8; ++iteration) concurrent.Save(path);
            }));
        }
        for (auto& writer : writers) writer.get();
        const auto concurrent = Settings::Read(path);
        auto expectedConcurrent = custom;
        expectedConcurrent.ApplyLegacyAnimationMode(concurrent.LegacyAnimationMode());
        expectedConcurrent.roleplayMode = concurrent.LegacyAnimationMode() % 3;
        Require(concurrent == expectedConcurrent, "Concurrent saves must leave one complete coherent settings file");
        { std::ofstream file(path); file << "roleplayMode=3\nsizeMethod=9\npreferredMode=0\npreferredRoleplayMode=3\nautoLoad=2\n"; }
        Require(Settings::Read(path) == Settings{}, "Malformed context, preset and autoload preferences keep defaults");
        { std::ofstream file(path); file << "mode=2\nroleplayMode=2\nalliedPlayers=80\nenemyMonsters=50\n"; }
        auto revision4 = Settings::Read(path);
        Require(revision4 == Profile(2, 1, 2, 2, 2),
            "r4 migration preserves display modes while replacing category sizes with the Reduce preset");
        const std::vector<std::string> legacySizeKeys = {
            "sizeMethod", "alliedPlayers", "enemyPlayers", "enemyMonsters", "enemySummons", "alliedSummons", "roleplayPercent",
            "normalizedAlliedPlayers", "normalizedEnemyPlayers", "normalizedEnemyMonsters", "normalizedEnemySummons",
            "normalizedAlliedSummons", "roleplayNormalizedPercent", "minimumMonsterPercent"
        };
        {
            std::ofstream file(path);
            file << "mode=2\nanimationMode=3\nautoLoad=1\nroleplayMode=0\npreferredMode=1\npreferredRoleplayMode=2\nsizeMethod=1\n";
            for (const auto& key : legacySizeKeys) file << key << "=100\n";
        }
        const Settings migrated = Profile(2, 3, 0, 1, 2);
        Require(Settings::Read(path) == migrated, "Migration ignores every legacy size key and preserves other preferences while removing Uniformiser");
        migrated.Save(path);
        std::ifstream savedFile(path);
        const std::string saved((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
        for (const auto& key : legacySizeKeys)
            Require(saved.find("\n" + key + "=") == std::string::npos, "Saved preferences must not retain legacy size keys");
        savedFile.close();
        Require(Settings::Read(path) == migrated, "Migrated preferences survive saving without legacy sizes");
        Require(ModeAfterNativeToggle(2, 2, false) == 0 && ModeAfterNativeToggle(0, 2, true) == 2,
            "Native shortcuts must restore miniature mode after off/on");
        Require(ModeAfterNativeToggle(0, 1, true) == 1, "Native shortcut can still enable stock creatures");
        custom.roleplayMode = 0;
        Require(ActiveDisplayMode(custom, true) == 2 && ActiveDisplayMode(custom, false) == 0,
            "Combat miniatures must not affect roleplay");
        for (bool context : {false, true}) for (bool combat : {false, true})
            Require(NotifyCreaturePreference(context, combat) == (context == combat), "Only current-context events may affect global rendering");
        using G = MiniatureGroup;
        Require(ClassifyMiniature(true, false, false, 0, 0) == G::AlliedPlayer, "Allied player");
        Require(ClassifyMiniature(true, false, false, 1, 0) == G::Enemy, "Enemy player");
        Require(ClassifyMiniature(false, true, false, 1, 0) == G::Enemy, "Enemy monster");
        Require(ClassifyMiniature(false, true, true, 0, 0) == G::AlliedSummon, "Allied summon");
        Require(ClassifyMiniature(true, false, false, 2, 2) == G::AlliedPlayer, "Additional native team values accepted");
        PolicyTests();
        Require(SimpleReducedPercent == 60 && SimpleMinimumMonsterPercent == 50,
            "Reduce to 60% with a monster floor of 50% of the original reference player");
        auto close = [](float x, float y) { return std::abs(x-y) < 0.0001f; };
        Require(close(*SizeFactor(60, 50, true, 20, 100) * 20, 50), "Floor enlarges tiny monsters");
        Require(close(*SizeFactor(60, 50, true, 200, 100), .6f), "Large monsters keep proportional sizing");
        Require(close(*SizeFactor(60, 50, false, 20, 100), .6f), "Floor must not enlarge players");
        for (auto dimensions : {std::pair{0.f,100.f}, std::pair{100.f,0.f}, std::pair{0.f,0.f}})
            Require(close(*SizeFactor(60, 50, true, dimensions.first, dimensions.second), .6f),
                "Missing measurements must not disable proportional reduction");
        { std::ofstream file(path); file << "mode=2\nroleplayMode=2\nsizeMethod=1\n"; }
        Require(Settings::Read(path) == Profile(2, 1, 2, 2, 2), "Legacy Uniformiser must migrate to reduction");
        bool refused = false;
        try { Profile(17, 1).Save(path); } catch (const std::exception&) { refused = true; }
        Require(refused, "Invalid setting must never be persisted");
        auto sample = folder / "hash-fixture.bin";
        { std::ofstream file(sample, std::ios::binary); file << "abc"; }
        Require(FileHash(sample) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known-answer test");
        Require(!SupportedClient(folder / "Other.exe"), "A non-Dofus target must be rejected before fingerprint reads");
        { std::ofstream file(folder / "GameAssembly.dll", std::ios::binary); file << "unsupported-client"; }
        Require(!SupportedClient(folder / "Dofus.exe"), "Wrong client fingerprint must be rejected");
        WriteStartupStatus(folder, 101, "STARTING");
        WriteStartupStatus(folder, 202, "ERROR sample");
        Require(ReadStartupStatus(folder, 101) == "STARTING", "Readiness must be isolated by process ID");
        Require(ReadStartupStatus(folder, 202) == "ERROR sample", "Initialization failures must reach the loader");
        WriteStartupStatus(folder, 101, "READY");
        Require(ReadStartupStatus(folder, 101) == "READY", "Readiness publication must replace prior startup stage");
        auto pointer = reinterpret_cast<void*>(TestTarget);
        Require(MH_Initialize() == MH_OK, "MinHook initialization");
        Require(MH_CreateHook(pointer, reinterpret_cast<void*>(TestReplacement), reinterpret_cast<void**>(&originalTestFunction)) == MH_OK, "Local hook creation");
        volatile TestFunction invoke = TestTarget;
        Require(invoke(3) == 34, "Local baseline");
        Require(MH_EnableHook(pointer) == MH_OK && invoke(3) == 55, "Local hook dispatch and trampoline");
        Require(MH_DisableHook(pointer) == MH_OK && invoke(3) == 34, "Local hook restoration");
        MH_Uninitialize();
        std::cout << "PASS: settings migration (former animation modes to toggles, legacy keys kept), seventeen appearance/animation checkboxes round trip (« PNJ » off by default), exhaustive category and animation policies, modes, fixed sizing presets, version refusal, startup status and local hook roundtrip. No game process accessed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
