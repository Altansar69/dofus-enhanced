#include "settings.hpp"
#include "common.hpp"
#include "mode-policy.hpp"
#include "size-policy.hpp"
#include <cmath>
#include <MinHook.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <future>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
        Require(defaults.mode == 2 && defaults.roleplayMode == 0 && defaults.animationMode == 1 &&
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
        Require(!defaults.glowNpcs && !defaults.glowInteractives,"Extra glow categories default off");
        for (bool npcs : {false,true}) for (bool interactives : {false,true}) {
            auto categories=defaults; categories.glowNpcs=npcs; categories.glowInteractives=interactives;
            categories.Save(path);
            Require(Settings::Read(path)==categories,"Independent glow categories must persist");
            { std::ofstream invalid(path,std::ios::app); invalid << "glowNpcs=2\nglowInteractives=-1\n"; }
            Require(Settings::Read(path)==categories,"Malformed flags must preserve saved choices");
        }
        Require(defaults.resourceMapEnabled && defaults.resourceMapItems.empty(), "No resource annotations before a selection");
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
            Require(!normalized.resourceMapEnabled && normalized.resourceMapItems == std::vector<int>({303,421}),
                "Resource IDs must normalize and malformed entries must not undo opt-out");
            { std::ofstream file(path); for (int i=1;i<=200;++i) file << "resourceMapItem=" << i << '\n'; }
            Require(Settings::Read(path).resourceMapItems.size() == 128, "Resource selection read must be bounded");
        }
        { std::ofstream file(path); file << "mode=0\nroleplayMode=2\nanimationMode=3\nautoLoad=1\n"; }
        const auto existing = Settings::Read(path, &autoLoadSpecified);
        Require(existing.mode==0 && existing.roleplayMode==2 && existing.animationMode==3 && existing.autoLoad && autoLoadSpecified,
            "Existing display and animation selections are never migrated to new defaults");
        for (int mode = 0; mode < 3; ++mode) {
            for (int animations : {0, 1, 2, 3}) {
                Settings wanted{mode, animations}; wanted.Save(path);
                Require(Settings::Read(path) == wanted, "Preferences must survive atomic overwrite and reload");
            }
        }
        { std::ofstream file(path); file << "mode=99\nnoAnimations=maybe\nmode=-1\n"; }
        Require(Settings::Read(path) == Settings{}, "Malformed settings must retain the requested defaults");
        { std::ofstream file(path); file << "mode=2\nnoAnimations=1\n"; }
        auto legacy = Settings::Read(path);
        Require(legacy.mode == 2 && legacy.preferredMode == 2 && legacy.roleplayMode == 0 && legacy.autoLoad,
            "Legacy unspecified values use current defaults; outside remains Normal");
        Require(legacy.animationMode == 1, "Old no-animation preferences migrate to the previously supported mode");
        { std::ofstream file(path); file << "mode=2\nanimationMode=2\nnoAnimations=0\n"; }
        Require(Settings::Read(path).animationMode == 2, "Explicit animation mode overrides legacy bool independent of line order");
        { std::ofstream file(path); file << "mode=2\nanimationMode=3\nnoAnimations=0\n"; }
        Require(Settings::Read(path).animationMode == 3, "Instant visual mode persists independently of legacy bool");
        for (int invalid : {-1, 4, 99}) {
            { std::ofstream file(path); file << "animationMode=" << invalid << '\n'; }
            Require(Settings::Read(path).animationMode == 1, "Invalid instant mode must preserve shortened default");
            Settings refused{0, invalid}; bool threw = false;
            try { refused.Save(path); } catch (const std::runtime_error&) { threw = true; }
            Require(threw, "Invalid animation mode must not be saved");
        }
        Settings custom{2, true};
        custom.autoLoad = true; custom.roleplayMode = 2;
        custom.preferredMode = 2; custom.preferredRoleplayMode = 2;
        custom.animationMode = 2;
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
                concurrent.animationMode = writer;
                concurrent.roleplayMode = writer % 3;
                for (int iteration = 0; iteration < 8; ++iteration) concurrent.Save(path);
            }));
        }
        for (auto& writer : writers) writer.get();
        const auto concurrent = Settings::Read(path);
        auto expectedConcurrent = custom;
        expectedConcurrent.animationMode = concurrent.animationMode;
        expectedConcurrent.roleplayMode = concurrent.animationMode % 3;
        Require(concurrent == expectedConcurrent, "Concurrent saves must leave one complete coherent settings file");
        { std::ofstream file(path); file << "roleplayMode=3\nsizeMethod=9\npreferredMode=0\npreferredRoleplayMode=3\nautoLoad=2\n"; }
        Require(Settings::Read(path) == Settings{}, "Malformed context, preset and autoload preferences keep defaults");
        { std::ofstream file(path); file << "mode=2\nroleplayMode=2\nalliedPlayers=80\nenemyMonsters=50\n"; }
        auto revision4 = Settings::Read(path);
        Require(revision4 == Settings{2, 1, true, 2, 2, 2},
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
        const Settings migrated{2, 3, true, 0, 1, 2};
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
        Require(ClassifyMiniature(true, false, false, 1, 0) == G::EnemyPlayer, "Enemy player");
        Require(ClassifyMiniature(false, true, false, 1, 0) == G::EnemyMonster, "Enemy monster");
        Require(ClassifyMiniature(true, true, true, 1, 0) == G::EnemySummon, "Summon takes precedence over player and monster flags");
        Require(ClassifyMiniature(false, true, true, 0, 0) == G::AlliedSummon, "Allied summon");
        Require(ClassifyMiniature(false, true, false, 0, 0) == G::Unknown, "Unclassified allied NPC stays unchanged");
        Require(ClassifyMiniature(true, false, false, 2, 2) == G::AlliedPlayer, "Additional native team values accepted");
        Require(ClassifyMiniature(true, false, false, -1, 0) == G::Unknown &&
            ClassifyMiniature(true, false, false, 0, 4) == G::Unknown, "Unknown or unsupported teams keep original appearance");
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
        Require(Settings::Read(path) == Settings{2,1,true,2,2,2}, "Legacy Uniformiser must migrate to reduction");
        bool refused = false;
        try { Settings{17, true}.Save(path); } catch (const std::exception&) { refused = true; }
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
        std::cout << "PASS: settings migration, modes, fixed sizing presets, version refusal, startup status and local hook roundtrip. No game process accessed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
