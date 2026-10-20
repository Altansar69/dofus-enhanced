#pragma once
#include "runtime.hpp"
#include "settings.hpp"
#include <atomic>
#include <memory>

struct Features;
struct OptionsUi;
struct Module {
    std::filesystem::path folder;
    std::filesystem::path settingsPath;
    bool autoLoadPreferenceRead = false;
    Settings settings;
    managed::Ref compositor, creatures, cached, fight, creatureOption, roleplayOption, spellEffectsOption;
    bool originalCreatureMode = false;
    bool originalRoleplayMode = false;
    ULONGLONG nextAutoLoadCheck = 0;
    bool servicesReady = false;
    bool startupConfirmed = false;
    bool waitingForAccountOptions = false;
    bool previousCombat = false;
    std::atomic_bool stopped{false};
    std::atomic_bool ready{false};
    unsigned pumpErrors = 0;
    std::atomic_bool stopRequested{false};
    std::atomic_bool stopCompleted{false};
    std::unique_ptr<Features> features;
    std::unique_ptr<OptionsUi> ui;
    explicit Module(std::filesystem::path path);
    bool InCombat() const;
    managed::Obj FindService(const char* name) const;
    void Discover(managed::Obj compositor);
    void Pump(managed::Obj compositor);
    void Change(Settings next, bool save = true);
    void Stop();
};
extern Module* module;
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original);
void AddNativeHook(uintptr_t rva, void* replacement, void** original);
