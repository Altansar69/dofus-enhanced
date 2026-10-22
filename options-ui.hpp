#pragma once
#include "runtime.hpp"
#include <functional>
#include <string>
#include <vector>

// The Metamob rows of the options tab reach the capture sync only through
// these (set by the module at installation; the tests set fakes).
struct MetamobHooks {
    std::function<void(std::string)> submitKey;     // verify then store; the text is wiped once used
    std::function<void()> forgetKey;
    std::function<bool()> keyStored;
    std::function<void(const std::string&)> notice; // local chat line, prefix included
};

struct OptionsUi {
    MetamobHooks metamob;
    struct Page { managed::Ref owner, category; };
    std::vector<Page> pages;
    managed::Ref pendingPage, stableList, container;
    managed::Ref modeSource, roleplayModeSource, animationSource, autoLoadSource;
    managed::Ref modeOption, roleplayModeOption, animationOption, autoLoadOption;
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
    // Credits row: project name, x.y.z version, author and a GitHub button.
    managed::Ref creditsOption, creditsMarker;
    // Metamob rows, after the two soul rows of the « Pierre d’âme » section:
    // capture sync toggle, API key row and forget row. The two text-button
    // rows are known by their unique description reference.
    managed::Ref metamobSyncSource, metamobSyncOption;
    managed::Ref metamobKeyOption, metamobKeyMarker, metamobForgetOption, metamobForgetMarker;
    // Masked native input owned in each pooled row view showing the key row:
    // added or removed at every Init of a view, removed by Restore/Abandon.
    struct KeyField { managed::Ref element, input; bool stored = false; };
    std::vector<KeyField> keyFields;
    ULONGLONG forgetDeadline = 0; // a second "Supprimer" click before this tick forgets the key
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
