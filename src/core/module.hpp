#pragma once
#include "core/runtime.hpp"
#include "core/settings.hpp"
#include <atomic>
#include <memory>

struct Features;
struct OptionsUi;
struct Module {
    std::filesystem::path folder;
    std::filesystem::path settingsPath;
    bool autoLoadPreferenceRead = false;
    Settings settings;
    // metamobSync as this client last read or wrote it in the shared settings
    // file; nullopt while it has seen no metamobSync line there (an older
    // revision wrote the file last, before this client started).
    std::optional<bool> sharedMetamobSync;
    managed::Ref compositor, creatures, cached, fight, creatureOption, roleplayOption, spellEffectsOption;
    bool originalCreatureMode = false;
    bool originalRoleplayMode = false;
    ULONGLONG nextAutoLoadCheck = 0;
    bool servicesReady = false;
    bool startupConfirmed = false;
    bool waitingForAccountOptions = false;
    // Logged out, reconnecting or choosing a character: the account options
    // are unbound until the next binding; Pump waits instead of failing.
    bool accountOptionsUnbound = false;
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
    bool KeepSharedMetamobSync(Settings& next) const;
    void SaveSettings(bool withMetamobSync);
    void FollowSharedSettings();
    void Stop();
};
extern Module* module;
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original);
void AddNativeHook(uintptr_t rva, void* replacement, void** original);
