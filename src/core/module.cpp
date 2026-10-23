#include "core/module.hpp"
#include "maps/map-transitions.hpp"
#include "maps/map-preload.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "appearance/mode-policy.hpp"
#include "loader/autoload.hpp"
#include "core/version.hpp"
#include "appearance/display-shortcut.hpp"
#include "appearance/context-display.hpp"
#include "souls/soul-helper.hpp"
#include "souls/soul-widget.hpp"
#include "glow/monster-glow.hpp"
#include "travel/chat-tp-links.hpp"
#include "travel/chat-travel.hpp"
#include "travel/map-travel.hpp"
#include "maps/resource-map-ui.hpp"
#include "maps/resource-map-render.hpp"
#include "souls/soul-group-menu.hpp"
#include "social/spouse-party-menu.hpp"
#include "social/spouse-party-widget.hpp"
#include "appearance/startup-options.hpp"
#include "treasure/treasure-launch.hpp"
#include "treasure/treasure-hunt.hpp"
#include "ui/native-click.hpp"
#include "notebook/notebook-panel.hpp"
#include "ui/tools-ui.hpp"
#include "jobs/jobs-xp-panel.hpp"
#include "maps/archi-zones.hpp"
#include "metamob/calendar-kralove.hpp"
#include "metamob/metamob-sync.hpp"
#include "souls/capture-detect.hpp"
#include "maps/map-info.hpp"
#include "ui/perf-overlay.hpp"
#include <MinHook.h>
#include <fstream>
#include <mutex>
#include <unordered_map>

using namespace managed;
Module* module = nullptr;
static HMODULE selfModule;
static std::mutex logMutex;
static std::filesystem::path logPath;
static std::vector<void*> hookTargets;
using UpdateFn = void(*)(Obj, Method);
static UpdateFn originalUpdate;
static ULONGLONG nextPump = 0, nextRestore = 0;
static unsigned restoreFailures = 0;
// End-of-frame glow: UIElementsRuntimeUtility.RepaintPanels runs once per
// rendered frame, after every Update and LateUpdate (the game's camera zoom
// tween included) and before the UI Toolkit repaint of that frame.
using RepaintPanelsFn = void(*)(bool, Method);
static RepaintPanelsFn originalRepaintPanels;
static bool glowFrameHook = false, inGlowFrame = false, glowFrameError = false;
static ULONGLONG lastGlowFrame = 0;
static std::unordered_map<Obj, ULONGLONG> nextDiscovery;

void Log(const std::string& message) {
    std::lock_guard<std::mutex> guard(logMutex);
    std::ofstream file(logPath, std::ios::app);
    file << GetTickCount64() << " pid=" << GetCurrentProcessId() << " " << message << std::endl;
}
void AddHook(Method method, uintptr_t rva, void* replacement, void** original) {
    api.Native(method, rva);
    AddNativeHook(rva, replacement, original);
}
void AddNativeHook(uintptr_t rva, void* replacement, void** original) {
    auto target = api.NativeAt(rva);
    auto result = MH_CreateHook(target, replacement, original);
    if (result != MH_OK) throw std::runtime_error(std::string("Cannot prepare hook: ") + MH_StatusToString(result));
    hookTargets.push_back(target);
}
static Obj Service(Obj dictionary, const char* name, bool diagnostic = true) {
    if (diagnostic) Log("DISCOVER_SERVICE lookup=" + std::string(name));
    auto type = api.Class("Core.dll", "", name);
    Ref key(api.type_get_object(api.class_get_type(type)));
    if (api.Value<bool>(api.Call(dictionary, "ContainsKey", {key.get()}))) return api.Call(dictionary, "get_Item", {key.get()});
    // Some compositors register a concrete type rather than its interface.
    Ref values(api.Call(dictionary, "get_Values"));
    Ref iterator(api.Call(values.get(), "GetEnumerator"));
    unsigned count = 0;
    while (api.Value<bool>(api.Call(iterator.get(), "MoveNext"))) {
        if (++count > 2048) throw std::runtime_error("Invalid service registry size");
        auto service = api.Call(iterator.get(), "get_Current");
        if (service && api.class_is_assignable_from(type, api.object_get_class(service))) {
            api.Call(iterator.get(), "Dispose");
            return service;
        }
    }
    api.Call(iterator.get(), "Dispose");
    return nullptr;
}
Module::Module(std::filesystem::path path) : folder(std::move(path)), settingsPath(ReleaseSettingsPath(folder)), settings(Settings::Read(settingsPath, &autoLoadPreferenceRead)),
    features(std::make_unique<Features>()), ui(std::make_unique<OptionsUi>()) {
    // Known only when the file holds the line; both copies then agree.
    try { sharedMetamobSync = Settings::ReadMetamobSync(settingsPath); } catch (...) {}
    if (sharedMetamobSync) settings.metamobSync = *sharedMetamobSync;
}
bool Module::InCombat() const {
    return servicesReady && fight.get() && Read<bool>(fight.get(), 0xB0);
}
Obj Module::FindService(const char* name) const {
    if (!servicesReady || !compositor.get() || stopped) return nullptr;
    auto dictionary = api.Field(compositor.get(), "eavv");
    return dictionary ? Service(dictionary, name, false) : nullptr;
}
void Module::Discover(Obj source) {
    if (servicesReady) return;
    if (!Read<bool>(source, 0x48)) return;
    auto dictionary = api.Field(source, "eavv");
    if (!dictionary) return;
    auto found = Service(dictionary, "fsl", !waitingForAccountOptions);
    if (!found) return;
    if (!waitingForAccountOptions) Log("DISCOVER_SERVICE_FOUND");
    creatures.reset(found);
    cached.reset(api.Field(found, "drii"));
    fight.reset(api.Field(found, "drih"));
    if (!cached.get() || !fight.get()) return;
    if (std::string(api.class_get_name(api.object_get_class(found))) != "exf" ||
        std::string(api.class_get_name(api.object_get_class(fight.get()))) != "fxc")
        throw std::runtime_error("Unexpected service implementation");
    auto container = api.Call(cached.get(), "get_dofusOptionsContainer");
    if (!container) return;
    if (!waitingForAccountOptions) Log("DISCOVER_OPTIONS_FOUND");
    creatureOption.reset(api.Field(container, "creatureModeFight"));
    roleplayOption.reset(api.Field(container, "creatureModeRoleplay"));
    spellEffectsOption.reset(api.Field(container, "allowSpellEffects"));
    if (!creatureOption.get() || !roleplayOption.get() || !spellEffectsOption.get()) return;
    InitialCreatureOptions initial;
    if (!ReadInitialCreatureOptions(creatureOption.get(), roleplayOption.get(), spellEffectsOption.get(), initial)) {
        if (!waitingForAccountOptions) {
            Log("WAITING_ACCOUNT_OPTIONS account binding is not loaded yet");
            WriteStartupStatus(folder, GetCurrentProcessId(), "WAITING_ACCOUNT_OPTIONS");
            waitingForAccountOptions = true;
        }
        return;
    }
    waitingForAccountOptions = false;
    originalCreatureMode = initial.combat;
    originalRoleplayMode = initial.roleplay;
    compositor.reset(source);
    servicesReady = true;
    if (settings.mode == -1) settings.mode = originalCreatureMode ? 1 : 0;
    if (settings.roleplayMode == -1) settings.roleplayMode = originalRoleplayMode ? 1 : 0;
    previousCombat = InCombat();
    // Existing installations keep the actual registration, including a manual
    // disable. First-use defaults are applied only after successful startup.
    try { settings.autoLoad = AutoLoadEnabled(folder, ModulePath(nullptr)); }
    catch (const std::exception& error) {
        settings.autoLoad = false;
        Log(std::string("AUTOLOAD_STATUS_ERROR ") + error.what());
    }
    Change(settings, false);
    Log("SERVICES_READY");
}
void Module::Change(Settings next, bool save) {
    if (next.mode < 0 || next.mode > 2 || next.roleplayMode < 0 || next.roleplayMode > 2)
        throw std::runtime_error("Display mode outside valid range");
    if (!Settings::ValidMonsterGlowMode(next.monsterGlowMode)) throw std::runtime_error("Invalid monster glow mode");
    if (!Settings::ValidResourceSelection(next.resourceMapItems)) throw std::runtime_error("Invalid resource map selection");
    const bool withMetamobSync = !save || KeepSharedMetamobSync(next);
    if (next.mode > 0) next.preferredMode = next.mode;
    if (next.roleplayMode > 0) next.preferredRoleplayMode = next.roleplayMode;
    if (next.autoLoad != settings.autoLoad) ConfigureAutoLoad(next.autoLoad, folder, ModulePath(nullptr));
    settings = next;
    const bool combat = InCombat();
    for (bool context : {true, false}) {
        auto option = context ? creatureOption.get() : roleplayOption.get();
        bool enabled = ActiveDisplayMode(settings, context) != 0;
        if (option && api.Value<bool>(api.Call(option, "get_property")) != enabled)
            api.Call(option, NotifyCreaturePreference(context, combat) ? "set_property" : "SetValueWithoutNotify", {&enabled});
    }
    features->Refresh(ActiveDisplayMode(settings, combat) != 0);
    ui->Sync();
    if (save) SaveSettings(withMetamobSync);
    // One digit per checkbox, in row order: « Apparence » combat and roleplay
    // categories, then the « Animations » toggles.
    const auto digits = [this](const auto& fields) {
        std::string text;
        for (auto field : fields) text += settings.*field ? '1' : '0';
        return text;
    };
    Log("SETTINGS combat=" + std::to_string(settings.mode) + " roleplay=" + std::to_string(settings.roleplayMode) +
        " miniatures=" + digits(CombatMiniatureToggles) + "/" + digits(RoleplayMiniatureToggles) +
        " animations=" + digits(AnimationToggles) + " autoLoad=" + std::to_string(settings.autoLoad) +
        " sizing=reduce percent=60 minimum=50");
}
// metamobSync is shared by every game client of this installation (one per
// character): a save keeps the file's value unless this client changed it
// since it last read or wrote it, so an older in-memory copy never reverts
// another client's choice. A file written by an older revision keeps ours, if
// this client has one: a client started after an older revision dropped the
// line leaves it out rather than write its default as every client's choice.
// Returns whether the save writes the line.
bool Module::KeepSharedMetamobSync(Settings& next) const {
    if (next.metamobSync != sharedMetamobSync.value_or(false)) return true; // changed here
    std::optional<bool> shared;
    try { shared = Settings::ReadMetamobSync(settingsPath); } catch (...) {}
    if (shared) next.metamobSync = *shared;
    return shared.has_value() || sharedMetamobSync.has_value();
}
void Module::SaveSettings(bool withMetamobSync) {
    settings.Save(settingsPath, withMetamobSync);
    if (withMetamobSync) sharedMetamobSync = settings.metamobSync;
}
// Another client's tick or untick reaches this one within 2 s: capture
// detection, the sync (settings.metamobSync) and the checkbox follow the file.
void Module::FollowSharedSettings() {
    std::optional<bool> shared;
    try { shared = Settings::ReadMetamobSync(settingsPath); } catch (...) { return; }
    if (!shared || *shared == sharedMetamobSync) return; // unchanged, or written by an older revision
    sharedMetamobSync = *shared;
    if (settings.metamobSync == *shared) return;
    settings.metamobSync = *shared;
    Log(std::string("SETTINGS_SHARED metamobSync=") + (*shared ? "1" : "0"));
    ui->Sync();
}
void Module::Pump(Obj source) {
    if (stopped) return;
    if (stopRequested.load() || std::filesystem::exists(folder / L"stop.request")) { Stop(); return; }
    Discover(source);
    if (!servicesReady) return;
    auto currentContainer = api.Call(cached.get(), "get_dofusOptionsContainer");
    if (!currentContainer || api.Field(currentContainer, "creatureModeFight") != creatureOption.get() ||
        api.Field(currentContainer, "creatureModeRoleplay") != roleplayOption.get() ||
        api.Field(currentContainer, "allowSpellEffects") != spellEffectsOption.get()) {
        Log("SESSION_CHANGED stopping before using a different settings owner");
        Stop();
        return;
    }
    // Every read of an unbound account option throws OptionNotLoadedException:
    // three of them in a row used to stop the module for the rest of the game
    // (a logout, an automatic reconnection, the character selection).
    const bool bound = api.Value<bool>(api.Call(creatureOption.get(), "IsLoaded")) &&
        api.Value<bool>(api.Call(roleplayOption.get(), "IsLoaded"));
    if (bound == accountOptionsUnbound) {
        accountOptionsUnbound = !bound;
        Log(bound ? "ACCOUNT_OPTIONS_BOUND" : "ACCOUNT_OPTIONS_UNBOUND waiting for the account binding");
        // Bound again: the account's reloaded creature modes are not the player's
        // toggles. The settings apply to them as at startup (Discover).
        if (bound) { previousCombat = InCombat(); Change(settings, false); }
    }
    if (!bound) {
        // Off-game screens: no stale figures nor the last character's hunt. Each
        // is cheap when nothing is shown; the overlay comes back with the binding.
        // The notebook stays: its entries are the player's, not the character's,
        // and a draft being typed must survive an automatic reconnection.
        RestorePerfOverlay(); RestoreTreasurePanel();
        return;
    }
    bool combat = InCombat();
    const bool transition = combat != previousCombat;
    previousCombat = combat;
    // Follow native shortcuts / context-menu changes instead of continuously overriding them.
    bool changed = false;
    for (bool context : {true, false}) {
        auto option = context ? creatureOption.get() : roleplayOption.get();
        bool nativeMode = api.Value<bool>(api.Call(option, "get_property"));
        int& mode = context ? settings.mode : settings.roleplayMode;
        const int preferred = context ? settings.preferredMode : settings.preferredRoleplayMode;
        if (nativeMode != (mode != 0)) {
            mode = ModeAfterNativeToggle(mode, preferred, nativeMode);
            changed = true;
        }
    }
    if (transition || changed) {
        features->Refresh(ActiveDisplayMode(settings, combat) != 0);
        if (transition) Log(combat ? "COMBAT_ENTER" : "COMBAT_LEAVE");
    }
    if (GetTickCount64() >= nextAutoLoadCheck) {
        nextAutoLoadCheck = GetTickCount64() + 2000;
        FollowSharedSettings();
        try {
            const bool registered = AutoLoadEnabled(folder, ModulePath(nullptr));
            if (registered != settings.autoLoad) { settings.autoLoad = registered; changed = true; }
        } catch (const std::exception& error) { Log(std::string("AUTOLOAD_STATUS_ERROR ") + error.what()); }
    }
    if (changed) {
        SaveSettings(KeepSharedMetamobSync(settings));
        ui->Sync();
    }
    const auto startupStage = [&](const char* name, const auto& action) {
        if (!startupConfirmed) Log(std::string("STARTUP_STAGE ") + name);
        action();
    };
    startupStage("features", [&] { features->Tick(); });
    startupStage("options", [&] { ui->Tick(); });
    startupStage("display-shortcut", [] { TickDisplayShortcut(); });
    startupStage("context-menu", [] { TickContextDisplayMenu(); });
    startupStage("soul-helper", [] { TickSoulHelper(); });
    startupStage("soul-widget", [] { TickSoulWidget(); });
    startupStage("tool-module-menu", [] { toolsUi::Tick(); });
    startupStage("treasure-panel", [] { TickTreasurePanel(); });
    startupStage("notebook-panel", [] { TickNotebookPanel(); });
    startupStage("jobs-xp", [] { TickJobsXp(); });
    startupStage("archi-zones", [] { TickArchiZones(); });
    startupStage("calendar-kralove", [] { TickCalendarKralove(); });
    startupStage("soul-group-menu", [] { TickSoulGroupMenu(); });
    startupStage("spouse-party-menu", [] { TickSpousePartyMenu(); });
    startupStage("spouse-party-widget", [] { TickSpousePartyWidget(); });
    startupStage("chat-links", [] { TickChatTpLinks(); });
    startupStage("chat-travel", [] { TickChatTravel(); });
    startupStage("capture-detect", [] { TickCaptureDetect(); });
    startupStage("metamob-sync", [&] { metamob::TickSync(settings.metamobSync); });
    startupStage("map-travel", [] { TickMapTravel(); });
    startupStage("resource-map-render", [] { resourceMap::PumpRender(GetTickCount64()); });
    startupStage("resource-map-ui", [] { resourceMap::TickUi(); });
    startupStage("map-info", [] { TickMapInfo(); });
    startupStage("perf-overlay", [] { TickPerfOverlay(); });
    if (!startupConfirmed) {
        WriteStartupStatus(folder, GetCurrentProcessId(), "READY");
        startupConfirmed = true;
        if (!autoLoadPreferenceRead) {
            autoLoadPreferenceRead = true;
            try {
                if (Settings{}.autoLoad && !settings.autoLoad) {
                    ConfigureAutoLoad(true, folder, ModulePath(nullptr));
                    settings.autoLoad = true;
                    Log("AUTOLOAD_DEFAULT_ENABLED");
                }
            } catch (const std::exception& error) {
                Log(std::string("AUTOLOAD_DEFAULT_ERROR ") + error.what());
            }
            // Persist both success and refusal so a disabled/failed setup is
            // never re-enabled automatically on a later launch.
            try { SaveSettings(KeepSharedMetamobSync(settings)); }
            catch (const std::exception& error) { Log(std::string("SETTINGS_DEFAULT_SAVE_ERROR ") + error.what()); }
            ui->Sync();
        }
        Log("RUNTIME_READY first main-thread update completed");
    }
}
void Module::Stop() {
    if (stopped) return;
    stopped = true;
    RestoreMapPreload();
    RestoreMapTravel();
    RestoreChatTravel();
    RestoreCaptureDetect();
    metamob::RestoreSync();
    RestoreChatTpLinks();
    RestoreSoulGroupMenu();
    RestoreSpousePartyMenu();
    RestoreSpousePartyWidget();
    resourceMap::RestoreRender();
    resourceMap::RestoreUi();
    RestoreMonsterGlow();
    RestoreTreasurePanel();
    RestoreNotebookPanel();
    toolsUi::Restore();
    RestoreJobsXp();
    RestoreArchiZones();
    RestoreCalendarKralove();
    RestoreMapInfo();
    RestorePerfOverlay();
    RestoreSoulHelper();
    RestoreSoulWidget();
    RestoreDisplayShortcut();
    RestoreContextDisplayMenu();
    try { ui->Restore(); } catch (const std::exception& e) { Log(std::string("UI_RESTORE_ERROR ") + e.what()); }
    try { features->Restore(); } catch (const std::exception& e) { Log(std::string("RENDER_RESTORE_ERROR ") + e.what()); }
    try {
        if (servicesReady) {
            const bool combat = InCombat();
            if (creatureOption.get()) api.Call(creatureOption.get(), combat ? "set_property" : "SetValueWithoutNotify", {&originalCreatureMode});
            if (roleplayOption.get()) api.Call(roleplayOption.get(), combat ? "SetValueWithoutNotify" : "set_property", {&originalRoleplayMode});
            features->Refresh(combat ? originalCreatureMode : originalRoleplayMode);
        }
    } catch (const std::exception& e) { Log(std::string("MODE_RESTORE_ERROR ") + e.what()); }
    // Native restore can synchronously ask for appearances. Drop the stamp
    // registry before leaving the UI thread; the stopped hook records no new ones.
    features->InvalidateLookCache();
    Log("STOPPED miniatureConversions=" + std::to_string(features->miniatureCount));
    // The worker disables hooks; the DLL remains resident to avoid unloading code on an active stack.
    stopRequested.store(true);
    stopCompleted.store(!ui->CleanupPending() && !MonsterGlowCleanupPending() && !SoulHelperCleanupPending() && !SoulWidgetCleanupPending() && !ChatTpLinksCleanupPending() && !SpousePartyWidgetCleanupPending() && !MapPreloadCleanupPending() && !resourceMap::UiCleanupPending() && !resourceMap::RenderCleanupPending() && !TreasurePanelCleanupPending() && !NotebookPanelCleanupPending() && !toolsUi::CleanupPending() && !JobsXpCleanupPending() && !ArchiZonesCleanupPending() && !CalendarKraloveCleanupPending() && !MapInfoCleanupPending() && !PerfOverlayCleanupPending());
    if (!stopCompleted.load()) Log("STOPPING awaiting owned UI cleanup on main thread");
    try { WriteStartupStatus(folder, GetCurrentProcessId(), "STOPPED"); }
    catch (const std::exception& e) { Log(std::string("STATUS_ERROR ") + e.what()); }
}
static void RepaintPanelsHook(bool onlyOffscreen, Method method) {
    if (RunGlowFrame(onlyOffscreen, inGlowFrame) && module && module->ready.load() && !module->stopped) {
        inGlowFrame = true;
        lastGlowFrame = GetTickCount64();
        try { TickMonsterGlow(); }
        catch (...) { if (!glowFrameError) Log("GLOW_FRAME_ERROR"); glowFrameError = true; }
        inGlowFrame = false;
    }
    // Always the engine's own repaint, whatever happened above.
    originalRepaintPanels(onlyOffscreen, method);
}
// The glow ticks from the compositor only when the end-of-frame hook is
// unavailable or idle (no rendered frame for a second).
static bool CompositorGlowTick(ULONGLONG now) { return !glowFrameHook || now - lastGlowFrame >= 1000; }
static void UpdateHook(Obj self, Method method) {
    originalUpdate(self, method);
    auto now = GetTickCount64();
    if (!module || !module->ready.load()) return;
    if (module->stopped) {
        // Keep the current compositor callback until transient owned-UI
        // detachment failures have drained; the worker then disables hooks.
        if (self == module->compositor.get() && !module->stopCompleted.load()) {
            if (module->ui->CleanupPending() && now >= nextRestore) {
                // Bounded: after a logout the game has already torn the options
                // page down and every retry throws. Retry twice a second for ten
                // seconds, log the first failures only, then drop the page.
                nextRestore = now + 500;
                try { module->ui->Restore(); restoreFailures = 0; }
                catch (const std::exception& error) {
                    if (++restoreFailures <= 3) Log(std::string("UI_RESTORE_ERROR ") + error.what());
                    if (restoreFailures >= 20) { Log("UI_RESTORE_ABANDONED options page already destroyed by the game"); module->ui->Abandon(); }
                }
            }
            TickMonsterGlow();
            if (resourceMap::RenderCleanupPending()) resourceMap::RestoreRender();
            if (resourceMap::UiCleanupPending()) resourceMap::RestoreUi();
            if (MapPreloadCleanupPending()) RestoreMapPreload();
            if (TreasurePanelCleanupPending()) RestoreTreasurePanel();
            if (NotebookPanelCleanupPending()) RestoreNotebookPanel();
            if (toolsUi::CleanupPending()) toolsUi::Restore();
            if (JobsXpCleanupPending()) RestoreJobsXp();
            if (ArchiZonesCleanupPending()) RestoreArchiZones();
            if (CalendarKraloveCleanupPending()) RestoreCalendarKralove();
            if (MapInfoCleanupPending()) RestoreMapInfo();
            if (PerfOverlayCleanupPending()) RestorePerfOverlay();
            if (SoulHelperCleanupPending()) RestoreSoulHelper();
            if (SoulWidgetCleanupPending()) RestoreSoulWidget();
            if (ChatTpLinksCleanupPending()) RestoreChatTpLinks();
            if (SpousePartyWidgetCleanupPending()) RestoreSpousePartyWidget();
            if (!module->ui->CleanupPending() && !MonsterGlowCleanupPending() && !SoulHelperCleanupPending() && !SoulWidgetCleanupPending() && !ChatTpLinksCleanupPending() && !SpousePartyWidgetCleanupPending() && !MapPreloadCleanupPending() && !resourceMap::UiCleanupPending() && !resourceMap::RenderCleanupPending() && !TreasurePanelCleanupPending() && !NotebookPanelCleanupPending() && !toolsUi::CleanupPending() && !JobsXpCleanupPending() && !ArchiZonesCleanupPending() && !CalendarKraloveCleanupPending() && !MapInfoCleanupPending() && !PerfOverlayCleanupPending()) module->stopCompleted.store(true);
        }
        return;
    }
    if (!module->servicesReady) {
        if (now < nextDiscovery[self]) return;
        nextDiscovery[self] = now + 1000;
    } else if (self != module->compositor.get()) return;
    if (module->servicesReady) TickMapPreload();
    if (module->servicesReady) TickChatTravelPlanning();
    if (module->servicesReady) { MapInfoFrame(now); PerfOverlayFrame(now); }
    if (module->servicesReady && now < nextPump) { if (CompositorGlowTick(now)) TickMonsterGlow(); resourceMap::PumpRender(now); return; }
    nextPump = now + 200;
    // Three failed pumps in a row stop the module; one that succeeds starts the count again.
    try { module->Pump(self); module->pumpErrors = 0; }
    catch (const std::exception& error) {
        Log(std::string("PUMP_ERROR ") + error.what());
        if (++module->pumpErrors >= 3) module->Stop();
    }
    if (CompositorGlowTick(now)) TickMonsterGlow();
}
static DWORD WINAPI Worker(void*) {
    logPath = ModulePath(selfModule).parent_path() / L"module.log";
    void* attached = nullptr;
    try {
        Log(std::string("START version=3.7.12.12 mod=") + ModVersion + " release=" + ModSemanticVersion + " prototype=" + std::to_string(ModRevision));
        WriteStartupStatus(ModulePath(selfModule).parent_path(), GetCurrentProcessId(), "STARTING");
        if (!SupportedClient(ModulePath(nullptr))) throw std::runtime_error("Unsupported client; hooks refused");
        auto game = GetModuleHandleW(L"GameAssembly.dll");
        if (!game) throw std::runtime_error("GameAssembly is unavailable");
        api.Load(game);
        Log("WAIT_RUNTIME_INITIALIZATION");
        api.WaitForInitialization();
        attached = api.thread_attach(api.domain_get());
        if (!attached) throw std::runtime_error("Cannot attach IL2CPP worker");
        Log("RUNTIME_SELF_TEST_BEGIN");
        api.SelfTest();
        Log("RUNTIME_SELF_TEST_OK handleBits=" + std::to_string(sizeof(GCHandle) * 8));
        module = new Module(ModulePath(selfModule).parent_path());
        if (MH_Initialize() != MH_OK) throw std::runtime_error("Cannot initialize hook library");
        auto compositor = api.Class("Core.dll", "Core.Engine.Compositor", "Compositor");
        AddHook(api.Find(compositor, "Update", 0), 0x20F9080, reinterpret_cast<void*>(UpdateHook), reinterpret_cast<void**>(&originalUpdate));
        try {
            auto panels = api.Class("UnityEngine.UIElementsModule.dll", "UnityEngine.UIElements", "UIElementsRuntimeUtility");
            AddHook(api.FindVerified(panels, "RepaintPanels", 1, 0x5660750), 0x5660750,
                reinterpret_cast<void*>(RepaintPanelsHook), reinterpret_cast<void**>(&originalRepaintPanels));
            glowFrameHook = true;
            Log("GLOW_FRAME_HOOK RepaintPanels");
        } catch (const std::exception& error) { Log(std::string("GLOW_FRAME_HOOK_UNAVAILABLE ") + error.what()); }
        module->features->Install();
        module->ui->Install();
        InstallFastMapTransitions();
        InstallMapPreload();
        InstallDisplayShortcut();
        InstallContextDisplayMenu();
        InstallSoulHelper();
        nativeClick::Install();
        toolsUi::Install();
        InstallTreasurePanel();
        treasure::InstallHunt();
        InstallNotebookPanel();
        InstallJobsXp();
        InstallArchiZones();
        InstallCalendarKralove();
        InstallSoulWidget();
        InstallMonsterGlow();
        InstallSoulGroupMenu();
        InstallSpousePartyMenu();
        InstallSpousePartyWidget();
        InstallChatTravel();
        // Metamob capture sync: optional, the rest of the mod runs without it. Its
        // chat part reads monsters and the character through capture detection.
        metamob::InstallSync(module->settingsPath, metamob::SendWinHttp, PostChatNotice, {CaptureDetectMonster, CaptureDetectCharacter});
        module->ui->metamob = {metamob::SubmitKey, metamob::ForgetStoredKey, metamob::StoredKeyStatus, metamob::CheckStoredKey, PostChatNotice};
        InstallCaptureDetect(metamob::EnqueueCapture); // never throws: CAPTURE_DETECT_DISABLED disables only this
        InstallMapTravel();
        resourceMap::InstallUi();
        resourceMap::InstallRender();
        InstallChatTpLinks(StartChatTravel);
        InstallMapInfo();
        InstallPerfOverlay();
        for (auto target : hookTargets) {
            auto result = MH_QueueEnableHook(target);
            if (result != MH_OK) throw std::runtime_error(MH_StatusToString(result));
        }
        auto result = MH_ApplyQueued();
        if (result != MH_OK) throw std::runtime_error(MH_StatusToString(result));
        Log("HOOKS_READY open or reopen game options");
        WriteStartupStatus(module->folder, GetCurrentProcessId(), "HOOKS_READY");
        module->ready.store(true);
        api.thread_detach(attached); attached = nullptr;
        // Stop is completed by the main-thread pump before any hooks are disabled.
        while (!module->stopCompleted.load()) Sleep(250);
        for (auto target : hookTargets)
            if (!module->ui->KeepResidentHook(target)) MH_QueueDisableHook(target);
        MH_ApplyQueued();
        Log("HOOKS_DISABLED passive treasure-click guard retained if needed; restart game before loading the module again");
    } catch (const std::exception& error) {
        Log(std::string("INIT_ERROR ") + error.what());
        for (auto target : hookTargets) MH_DisableHook(target);
        if (attached) api.thread_detach(attached);
        try { WriteStartupStatus(ModulePath(selfModule).parent_path(), GetCurrentProcessId(), "ERROR " + std::string(error.what())); }
        catch (...) { /* The detailed initialization error is already in module.log. */ }
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (!worker) return FALSE;
        CloseHandle(worker);
    }
    return TRUE;
}
