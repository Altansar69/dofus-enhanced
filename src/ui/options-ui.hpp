#pragma once
#include "metamob/metamob-sync.hpp"
#include "core/runtime.hpp"
#include <array>
#include <functional>
#include <string>
#include <vector>

// The Metamob rows of the options tab reach the capture sync only through
// these (set by the module at installation; the tests set fakes).
struct MetamobHooks {
    std::function<void(std::string)> submitKey;     // verify then store; the text is wiped once used
    std::function<void()> forgetKey;
    std::function<metamob::KeyStatus()> keyStatus;  // None: no key file, or one being forgotten
    std::function<void(bool)> checkKey;             // a Saved key shown: checked once; true: « Clé non vérifiée » clicked
    std::function<void(const std::string&)> notice; // local chat line, prefix included
};

struct OptionsUi {
    MetamobHooks metamob;
    struct Page { managed::Ref owner, category; };
    std::vector<Page> pages;
    managed::Ref pendingPage, stableList, container;
    managed::Ref modeSource, roleplayModeSource, autoLoadSource;
    managed::Ref modeOption, roleplayModeOption, autoLoadOption;
    // « Apparence »: a checkbox per miniature category after each context's
    // choice (Settings' CombatMiniatureToggles and RoleplayMiniatureToggles).
    std::array<managed::Ref, 6> combatMiniatureSources, combatMiniatureOptions;
    std::array<managed::Ref, 5> roleplayMiniatureSources, roleplayMiniatureOptions;
    // « Animations »: one checkbox per toggle (Settings' AnimationToggles).
    std::array<managed::Ref, 6> animationSources, animationOptions;
    managed::Ref githubSource, githubOption;
    managed::Ref soulSource, soulOption;
    managed::Ref soulAutoSource, soulAutoOption;
    managed::Ref glowSource, glowOption;
    managed::Ref glowNpcsSource, glowNpcsOption, glowInteractivesSource, glowInteractivesOption;
    managed::Ref transitionsSource, transitionsOption;
    managed::Ref preloadSource, preloadOption;
    managed::Ref spouseSource, spouseOption;
    managed::Ref resourcesSource, resourcesOption;
    managed::Ref resourcesMiniSource, resourcesMiniOption;
    managed::Ref miniCenterSource, miniCenterOption;
    // « Déplacements »: the mod's travel in place of the game's autopilot.
    managed::Ref travelPlannerSource, travelPlannerOption, templeSource, templeOption;
    // « Chat »: what the mod adds next to positions and monster groups shared in chat.
    managed::Ref chatTravelSource, chatTravelOption, chatSoulSource, chatSoulOption, chatOcreSource, chatOcreOption;
    managed::Ref chatMetamobSource, chatMetamobOption;
    // « Cartes »: zones of the missing archimonsters on each map.
    managed::Ref archiZonesSource, archiZonesOption, archiZonesMiniSource, archiZonesMiniOption;
    // « Calendrier »: Metamob's Kralamoure openings in the game's calendar.
    managed::Ref calendarKraloveSource, calendarKraloveOption;
    // The checkboxes added in 1.13.0 and 1.14.0 (Settings' ExtraToggles), spread
    // over « Cartes », « Carnet » and « Performances »; then the corner
    // choice of the performance overlay.
    std::array<managed::Ref, 10> extraSources, extraOptions;
    managed::Ref perfCornerSource, perfCornerOption;
    // « Chat »: the dimension of shared positions, a choice of three.
    managed::Ref chatCoordsSource, chatCoordsOption;
    // Credits row: project name, x.y.z version, author and a GitHub button.
    managed::Ref creditsOption, creditsMarker;
    // « Statistiques » (1.15.0): the counting checkbox, the choice of what is
    // shown (0 this character, 1 every character; kept for the session), then
    // one text-button row per line of statistics, known by its unique
    // description reference (statsMarkers); the last line resets what is shown.
    managed::Ref statsSource, statsOption, statsScopeSource, statsScopeOption;
    std::vector<managed::Ref> statsRows, statsMarkers;
    // Each pooled row view showing a statistics line: its label follows the
    // line while the page is open, its native button is hidden (except the
    // reset's, whose caption follows the reset) and given back as it was when
    // the view leaves (Restore, Abandon, or another row in that view).
    struct StatsView {
        managed::Ref element, button, buttonDisplay;
        size_t line = 0;
        std::string shown, caption;  // label and reset caption shown, empty before the first
        bool hidden = false;         // its native button hidden
    };
    std::vector<StatsView> statsViews;
    std::vector<std::string> statsTexts;  // each line's current label
    int statsScope = 0;                   // what the labels show
    // The reset: asked until statsConfirmDeadline (for the scope and the
    // character shown then), Confirmer taken only from statsConfirmFrom (the
    // same button: a double click never erases), its outcome (1 erased, 2
    // impossible) shown until statsResultUntil.
    bool statsConfirm = false, statsConfirmAll = false;
    int64_t statsConfirmCharacter = 0;
    int statsResult = 0;
    ULONGLONG statsConfirmDeadline = 0, statsConfirmFrom = 0, statsResultUntil = 0, nextStatsRefresh = 0, nextStatsLog = 0;
    // Metamob rows, after the two soul rows of the « Pierre d’âme » section:
    // capture sync toggle, then the API key row, a text-button row known by its
    // unique description reference.
    managed::Ref metamobSyncSource, metamobSyncOption;
    managed::Ref metamobKeyOption, metamobKeyMarker;
    // Owned controls of each pooled row view showing the key row, in one
    // container just before the row's native « Enregistrer » button: added or
    // removed at every Init of a view, refreshed each Tick, removed by
    // Restore/Abandon. No key saved: the masked field, Enregistrer shown once it
    // holds a character. Key saved: fixed stars, the indicator of its status
    // (« Clé valide » only for a key Metamob accepted) and the trash. Trash
    // clicked: the question, « Oui » and « Non ».
    struct KeyField {
        managed::Ref element, slot, input, stars, indicator, trash, question, yes, no;
        managed::Ref button, buttonDisplay; // the native Enregistrer button and its own display, put back when the view leaves
        int shown = -1;                     // state shown (KeyState in options-ui.cpp), -1 before the first
        int status = -1;                    // key status in the indicator (metamob::KeyStatus), -1 before the first
        bool saveShown = true;              // the native button has its own display
    };
    std::vector<KeyField> keyFields;
    // metamob.keyStatus(), None while a forget runs: at each Init of a key view, otherwise at most once a second.
    metamob::KeyStatus keyStatus = metamob::KeyStatus::None;
    bool confirmForget = false;             // the trash was clicked: its question stays until confirmDeadline
    ULONGLONG confirmDeadline = 0;
    ULONGLONG nextKeyFieldRefresh = 0, nextKeyFieldLog = 0;
    std::string stableSelection;
    unsigned stableTicks = 0;
    void Install();
    void CapturePage(managed::Obj page);
    bool Attach(managed::Obj page);
    void Tick();
    void Sync();
    void Restore();
    bool CleanupPending() const { return !pages.empty(); }
    // Drop pages the game already tore down (logout): nothing left to restore
    // but the owned key fields.
    void Abandon();
    bool KeepResidentHook(void* target) const;
};
