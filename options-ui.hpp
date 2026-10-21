#pragma once
#include "runtime.hpp"
#include <vector>

struct OptionsUi {
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
    std::string stableSelection;
    unsigned stableTicks = 0;
    void Install();
    void CapturePage(managed::Obj page);
    bool Attach(managed::Obj page);
    void Tick();
    void Sync();
    void Restore();
    bool CleanupPending() const { return !pages.empty(); }
    bool KeepResidentHook(void* target) const;
};
