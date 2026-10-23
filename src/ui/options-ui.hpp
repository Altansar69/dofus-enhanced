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
    std::array<managed::Ref, 4> roleplayMiniatureSources, roleplayMiniatureOptions;
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
    // Credits row: project name, x.y.z version, author and a GitHub button.
    managed::Ref creditsOption, creditsMarker;
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
