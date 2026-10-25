#include "ui/preset-windows.hpp"
#include "ui/preset-windows-store.hpp"
#include "ui/tools-ui.hpp"
#include "notebook/notebook-panel.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <vector>

namespace {
using namespace managed;
using presetWindows::Panel;
using presetWindows::Place;
using presetWindows::Snapshot;
constexpr const char* CoreImage = "Core.dll";
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
// The panels with their own window and header, as tools-ui registers them.
constexpr const char* PanelKeys[] = {"stats", "notebook", "treasure"};
constexpr size_t PanelCount = std::size(PanelKeys);
// A window opened by a layout is placed as soon as its panel builds it (the
// next pump), or forgotten after this long (a panel that could not build it).
constexpr uint64_t PlaceWaitMs = 3000;
// Each list of the game read is bounded.
constexpr int MaxPresets = 4096;
enum class Event { Save, Add, Remove, Apply, Rename };
// What a hook saw, acted on at the next pump (main thread): the container it
// ran on (compared, never dereferenced), the layout's uid, its new name.
struct Pending {
    Event kind;
    Obj container = nullptr;
    int uid = -1;
    std::string name;
};
enum Property : size_t { Left, Top, Width, Height, PropertyCount };
struct Natives {
    Method presetContainer{}, usingFight{}, parent{}, layout{}, getStyle{}, setLength{};
    std::array<Method, PropertyCount> get{}, set{};
    Klass length{};
    size_t savedPresets = 0, fightPresetId = 0, uid = 0, name = 0;
} n;
struct Wanted {
    Place place;
    uint64_t until = 0;
    bool active = false;
};
struct State {
    bool installed = false;
    std::array<Wanted, PanelCount> places{};
    // The fight layout: known once read, in use, the mod's windows of before
    // the fight and whether the fight changed them.
    bool fightKnown = false, usingFight = false, fightActive = false, fightChanged = false;
    std::optional<Snapshot> beforeFight;
} state;
std::mutex queueMutex;
std::vector<Pending> queue;

using IntFn = void (*)(Obj, int, Method);
using AddFn = void (*)(Obj, Obj, Method);
using EditFn = void (*)(Obj, int, Obj, int, Method);
IntFn originalSave = nullptr, originalRemove = nullptr, originalApply = nullptr;
AddFn originalAdd = nullptr;
EditFn originalEdit = nullptr;

uint64_t Now() { return GetTickCount64(); }
bool Live() { return state.installed && module && module->ready.load() && !module->stopped && !module->stopRequested.load(); }
bool Session() { return Live() && module->servicesReady && !module->accountOptionsUnbound && module->cached.get(); }
bool Option() { return module->settings.presetModWindows; }
std::filesystem::path Path() { return presetWindows::DataPath(module->settingsPath); }
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Layout field changed: ") + name);
    return offset;
}
void Enqueue(Event kind, Obj container, int uid, std::string name = {}) {
    std::lock_guard<std::mutex> guard(queueMutex);
    if (queue.size() < 64) queue.push_back({kind, container, uid, std::move(name)});
}
// ------------------------------------------------------------------ the game's layouts
Obj SavedList(Obj container) {
    Obj option = Read<Obj>(container, n.savedPresets);
    if (!option || !api.Value<bool>(api.Call(option, "IsLoaded"))) return nullptr;
    return api.Call(option, "get_property");
}
int Count(Obj list) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > MaxPresets) throw std::runtime_error("Saved layouts bounds");
    return count;
}
Obj FindPreset(Obj list, int uid) {
    if (!list) return nullptr;
    for (int i = 0, count = Count(list); i < count; ++i) {
        Obj preset = api.Call(list, "get_Item", {&i});
        if (preset && Read<int>(preset, n.uid) == uid) return preset;
    }
    return nullptr;
}
int FightPresetId(Obj container) {
    Obj option = Read<Obj>(container, n.fightPresetId);
    if (!option || !api.Value<bool>(api.Call(option, "IsLoaded"))) return -1;
    return api.Value<int>(api.Call(option, "get_property"));
}
Obj Container() { return api.Invoke(n.presetContainer, module->cached.get()); }

// ------------------------------------------------------------------ hooks (any thread)
void SaveHook(Obj self, int uid, Method method) {
    originalSave(self, uid, method);
    if (Live()) Enqueue(Event::Save, self, uid);
}
void AddPresetHook(Obj self, Obj preset, Method method) {
    originalAdd(self, preset, method);
    if (!Live()) return;
    try {
        // The layout added: the one given, else the one the game made (the last).
        int uid = -1;
        if (preset) uid = Read<int>(preset, n.uid);
        else if (Obj list = SavedList(self)) {
            int last = Count(list) - 1;
            if (last >= 0) if (Obj made = api.Call(list, "get_Item", {&last})) uid = Read<int>(made, n.uid);
        }
        Enqueue(Event::Add, self, uid);
    } catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR add ") + error.what()); }
    catch (...) { Log("PRESET_WINDOWS_ERROR add"); }
}
void RemoveHook(Obj self, int uid, Method method) {
    originalRemove(self, uid, method);
    if (Live()) Enqueue(Event::Remove, self, uid);
}
void ApplyHook(Obj self, int uid, Method method) {
    originalApply(self, uid, method);
    if (Live()) Enqueue(Event::Apply, self, uid);
}
void EditHook(Obj self, int uid, Obj name, int icon, Method method) {
    originalEdit(self, uid, name, icon, method);
    if (!Live()) return;
    try { Enqueue(Event::Rename, self, uid, api.String(name)); }
    catch (...) { Log("PRESET_WINDOWS_ERROR rename"); }
}

// ------------------------------------------------------------------ places
struct Rect { float x, y, width, height; };
Rect LayoutOf(Obj element) { return api.Value<Rect>(api.Invoke(n.layout, element)); }
bool Usable(const Rect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.width > 0 && r.height > 0;
}
int Scaled(float value, float extent) {
    return static_cast<int>(std::lround(std::clamp(value / extent * presetWindows::PlaceScale, 0.f, static_cast<float>(presetWindows::PlaceScale))));
}
// A window's place and size in its layer (the UI's root), as the game laid it out.
std::optional<Place> ReadPlace(Obj window) {
    Obj parent = api.Invoke(n.parent, window);
    if (!parent) return std::nullopt;
    const Rect box = LayoutOf(window), screen = LayoutOf(parent);
    if (!Usable(box) || !Usable(screen)) return std::nullopt;
    const Place place{Scaled(box.x, screen.width), Scaled(box.y, screen.height), Scaled(box.width, screen.width), Scaled(box.height, screen.height)};
    return presetWindows::ValidPlace(place) ? std::optional<Place>(place) : std::nullopt;
}
void Pixels(Obj style, Property property, float value) {
    Ref box(api.Invoke(n.get[property], style));
    if (!box.get() || !api.class_is_valuetype(api.object_get_class(box.get()))) throw std::runtime_error("Layout window style unavailable");
    int unit = 0; // LengthUnit.Pixel
    Ref length(api.New(n.length, {&value, &unit}));
    api.Invoke(n.setLength, api.object_unbox(box.get()), {api.object_unbox(length.get())});
    api.Invoke(n.set[property], style, {api.object_unbox(box.get())});
}
// The window at `place` on the screen of now, kept on it; false while it has no laid-out layer.
bool WritePlace(Obj window, const Place& place) {
    Obj parent = api.Invoke(n.parent, window);
    if (!parent) return false;
    const Rect screen = LayoutOf(parent);
    if (!Usable(screen)) return false;
    const float scale = static_cast<float>(presetWindows::PlaceScale);
    const float width = place.width / scale * screen.width, height = place.height / scale * screen.height;
    const float left = std::clamp(place.x / scale * screen.width, 0.f, std::max(0.f, screen.width - width));
    const float top = std::clamp(place.y / scale * screen.height, 0.f, std::max(0.f, screen.height - height));
    Obj style = api.Invoke(n.getStyle, window);
    if (!style) throw std::runtime_error("Layout window style unavailable");
    Pixels(style, Width, width);
    Pixels(style, Height, height);
    Pixels(style, Left, left);
    Pixels(style, Top, top);
    return true;
}
void PlacePending() {
    const auto now = Now();
    for (size_t i = 0; i < PanelCount; ++i) {
        auto& wanted = state.places[i];
        if (!wanted.active) continue;
        try {
            if (now >= wanted.until || !toolsUi::Visible(PanelKeys[i])) {
                wanted.active = false;
                Log(std::string("PRESET_WINDOWS_PLACE_DROPPED key=") + PanelKeys[i]);
                continue;
            }
            Obj window = toolsUi::Window(PanelKeys[i]);
            if (window && WritePlace(window, wanted.place)) {
                wanted.active = false;
                Log(std::string("PRESET_WINDOWS_PLACED key=") + PanelKeys[i] + " place=" + std::to_string(wanted.place.x) + "," +
                    std::to_string(wanted.place.y) + " size=" + std::to_string(wanted.place.width) + "x" + std::to_string(wanted.place.height));
            }
        } catch (const std::exception& error) {
            wanted.active = false;
            Log(std::string("PRESET_WINDOWS_ERROR place ") + PanelKeys[i] + " " + error.what());
        }
    }
}

// ------------------------------------------------------------------ capture and apply
bool Settings::* const SmallHeaders[PanelCount] = {&Settings::smallHeaderStats, &Settings::smallHeaderNotebook, &Settings::smallHeaderTreasure};
std::optional<Panel>& PanelOf(Snapshot& snapshot, size_t index) {
    return index == 0 ? snapshot.stats : index == 1 ? snapshot.notebook : snapshot.treasure;
}
const std::optional<Panel>& PanelOf(const Snapshot& snapshot, size_t index) {
    return index == 0 ? snapshot.stats : index == 1 ? snapshot.notebook : snapshot.treasure;
}
Snapshot Capture() {
    const auto& settings = module->settings;
    Snapshot snapshot;
    for (size_t i = 0; i < PanelCount; ++i) {
        try {
            Panel panel;
            panel.open = toolsUi::Visible(PanelKeys[i]);
            panel.smallHeader = settings.*SmallHeaders[i];
            if (panel.open) {
                if (state.places[i].active) panel.place = state.places[i].place; // not placed yet: the place asked
                else if (Obj window = toolsUi::Window(PanelKeys[i])) panel.place = ReadPlace(window);
            }
            PanelOf(snapshot, i) = panel;
        } catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR capture ") + PanelKeys[i] + " " + error.what()); }
    }
    snapshot.notebookCompact = settings.notebookCompact;
    snapshot.soul = presetWindows::Soul{settings.soulReminder, settings.smallHeaderSoul};
    snapshot.perf = presetWindows::Perf{settings.perfOverlayShown, settings.perfOverlay, settings.perfOverlayGpu, settings.perfOverlayDisk,
        settings.perfOverlayX, settings.perfOverlayY, settings.perfOverlayScaleX, settings.perfOverlayScaleY};
    snapshot.gauges = presetWindows::Gauges{settings.gaugeVertical, settings.gaugeVerticalBoxes};
    return snapshot;
}
std::string Describe(const Snapshot& snapshot) {
    std::string text;
    for (size_t i = 0; i < PanelCount; ++i) {
        const auto& panel = PanelOf(snapshot, i);
        text += std::string(i ? " " : "") + PanelKeys[i] + "=" + (!panel ? "-" : panel->open ? (panel->place ? "open+place" : "open") : "closed");
    }
    text += std::string(" compact=") + (!snapshot.notebookCompact ? "-" : *snapshot.notebookCompact ? "1" : "0");
    text += std::string(" soul=") + (!snapshot.soul ? "-" : snapshot.soul->enabled ? "1" : "0");
    text += std::string(" perf=") + (!snapshot.perf ? "-" : snapshot.perf->shown ? "shown" : "hidden");
    text += " gauges=" + (!snapshot.gauges ? std::string("-") : std::to_string(snapshot.gauges->vertical));
    return text;
}
void Apply(const Snapshot& snapshot, const std::string& reason) {
    // The settings first, in one change: the headers, Capture d’âme, the
    // overlay, the gauges (each read at its next tick).
    Settings next = module->settings;
    for (size_t i = 0; i < PanelCount; ++i)
        if (const auto& panel = PanelOf(snapshot, i)) next.*SmallHeaders[i] = panel->smallHeader;
    if (snapshot.soul) { next.soulReminder = snapshot.soul->enabled; next.smallHeaderSoul = snapshot.soul->smallHeader; }
    if (const auto& perf = snapshot.perf) {
        next.perfOverlayShown = perf->shown; next.perfOverlay = perf->fps; next.perfOverlayGpu = perf->gpu; next.perfOverlayDisk = perf->disk;
        next.perfOverlayX = perf->x; next.perfOverlayY = perf->y; next.perfOverlayScaleX = perf->scaleX; next.perfOverlayScaleY = perf->scaleY;
    }
    if (snapshot.gauges) { next.gaugeVertical = snapshot.gauges->vertical; next.gaugeVerticalBoxes = snapshot.gauges->boxes; }
    if (!(next == module->settings)) {
        try { module->Change(next); }
        catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR settings ") + error.what()); }
    }
    try { toolsUi::ApplyHeaderSizes(); }
    catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR headers ") + error.what()); }
    // The notebook's unsaved edits are never dropped: a notebook being edited
    // keeps its version and stays open (its own confirmation is the player's).
    bool editing = false;
    try { editing = NotebookPanelEditing(); } catch (...) {}
    if (snapshot.notebookCompact && *snapshot.notebookCompact != module->settings.notebookCompact) {
        if (editing) Log("PRESET_WINDOWS_KEPT key=notebook reason=unsaved-edits");
        else try { toolsUi::ChooseLayout("notebook", *snapshot.notebookCompact ? 1 : 0); }
        catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR notebook-layout ") + error.what()); }
    }
    // Each window opened or closed, placed once built.
    for (size_t i = 0; i < PanelCount; ++i) {
        const auto& panel = PanelOf(snapshot, i);
        if (!panel) continue;
        try {
            if (toolsUi::Visible(PanelKeys[i]) != panel->open) {
                if (!panel->open && i == 1 && editing) { Log("PRESET_WINDOWS_KEPT key=notebook reason=unsaved-edits"); continue; }
                if (!toolsUi::Toggle(PanelKeys[i])) Log(std::string("PRESET_WINDOWS_KEPT key=") + PanelKeys[i] + " reason=refused");
            }
            auto& wanted = state.places[i];
            wanted.active = panel->open && panel->place && toolsUi::Visible(PanelKeys[i]);
            if (wanted.active) { wanted.place = *panel->place; wanted.until = Now() + PlaceWaitMs; }
        } catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR panel ") + PanelKeys[i] + " " + error.what()); }
    }
    PlacePending(); // an open window: at once
    Log("PRESET_WINDOWS_APPLIED " + reason + " " + Describe(snapshot));
}

// ------------------------------------------------------------------ the fight layout
void FollowFight(Obj container) {
    bool using_ = false;
    try { using_ = api.Value<bool>(api.Invoke(n.usingFight, container)); }
    catch (const std::exception&) { return; } // the game's context not there (a loading screen): known at the next pump
    if (!state.fightKnown) { state.fightKnown = true; state.usingFight = using_; return; }
    if (using_ == state.usingFight) return;
    state.usingFight = using_;
    if (using_) {
        state.fightActive = state.fightChanged = false;
        state.beforeFight.reset();
        if (!Option()) return;
        state.beforeFight = Capture();
        state.fightActive = true;
        int uid = -1;
        std::optional<Snapshot> kept;
        try { uid = FightPresetId(container); kept = presetWindows::Load(Path(), uid); }
        catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR fight ") + error.what()); }
        if (!kept) { Log("PRESET_WINDOWS_NONE uid=" + std::to_string(uid) + " reason=fight"); return; }
        state.fightChanged = true;
        Apply(*kept, "uid=" + std::to_string(uid) + " reason=fight");
        return;
    }
    const bool restore = state.fightActive && state.fightChanged && state.beforeFight && Option();
    auto before = std::move(state.beforeFight);
    state.fightActive = state.fightChanged = false;
    state.beforeFight.reset();
    if (restore) Apply(*before, "reason=after-fight");
}
void HandleEvent(const Pending& event, Obj container) {
    const auto uid = std::to_string(event.uid);
    if (event.container != container) { Log("PRESET_WINDOWS_SKIPPED uid=" + uid + " reason=other-container"); return; }
    if (!presetWindows::ValidUid(event.uid)) { Log("PRESET_WINDOWS_SKIPPED uid=" + uid + " reason=uid"); return; }
    Obj preset = FindPreset(SavedList(container), event.uid);
    switch (event.kind) {
    case Event::Save:
    case Event::Add: {
        if (!preset) { Log("PRESET_WINDOWS_SKIPPED uid=" + uid + " reason=layout-missing"); return; }
        if (!Option()) {
            // A new layout never takes what a former one with its uid kept.
            if (event.kind == Event::Add && presetWindows::Erase(Path(), event.uid)) Log("PRESET_WINDOWS_REMOVED uid=" + uid + " reason=option-off");
            return;
        }
        const auto snapshot = Capture();
        const auto name = api.String(Read<Obj>(preset, n.name));
        presetWindows::Put(Path(), event.uid, name, snapshot);
        Log("PRESET_WINDOWS_SAVED uid=" + uid + (event.kind == Event::Add ? " new=1 " : " new=0 ") + Describe(snapshot));
        return;
    }
    case Event::Remove:
        if (preset) { Log("PRESET_WINDOWS_SKIPPED uid=" + uid + " reason=layout-still-there"); return; }
        if (presetWindows::Erase(Path(), event.uid)) Log("PRESET_WINDOWS_REMOVED uid=" + uid);
        return;
    case Event::Apply: {
        if (!Option() || !preset) return;
        auto kept = presetWindows::Load(Path(), event.uid);
        if (!kept) { Log("PRESET_WINDOWS_NONE uid=" + uid + " reason=not-kept"); return; }
        if (state.fightActive) state.fightChanged = true; // undone with the fight, as the game's own layout
        Apply(*kept, "uid=" + uid + " reason=layout");
        return;
    }
    case Event::Rename:
        if (presetWindows::Rename(Path(), event.uid, event.name)) Log("PRESET_WINDOWS_RENAMED uid=" + uid);
        return;
    }
}
}

void InstallPresetWindows() {
    try {
        auto cached = api.Class(CoreImage, "Core.Services.Player.OptionsService", "CachedDataService");
        n.presetContainer = api.FindVerified(cached, "get_uiPresetOptionsContainer", 0, 0x7E0890);
        auto presets = api.Class(CoreImage, "Core.Engine.Options.OptionsContainers", "UiPresetOptionsContainer");
        n.fightPresetId = RequireField(presets, "fightPresetId", 0x30);
        n.savedPresets = RequireField(presets, "savedPresets", 0x48);
        auto data = api.Class(CoreImage, "Core.Engine.Options", "PresetData");
        n.name = RequireField(data, "presetName", 0x18);
        n.uid = RequireField(data, "uid", 0xA8);
        n.usingFight = api.FindVerified(presets, "get_isUsingFightPreset", 0, 0x20939D0);
        auto element = api.Class(UiImage, UiSpace, "VisualElement");
        n.parent = api.FindVerified(element, "get_parent", 0, 0x55F7180);
        n.layout = api.FindVerified(element, "get_layout", 0, 0x55F6930);
        n.getStyle = api.FindVerified(element, "get_style", 0, 0x55F74A0);
        auto style = api.Class(UiImage, UiSpace, "InlineStyleAccess");
        const struct { const char* name; uintptr_t get, set; } accessors[PropertyCount] = {
            {"left", 0x55D1610, 0x55D3EB0}, {"top", 0x55D1CD0, 0x55D4F70}, {"width", 0x55D2740, 0x55D63F0}, {"height", 0x55D1540, 0x55D3D60}};
        for (size_t i = 0; i < PropertyCount; ++i) {
            n.get[i] = api.FindVerified(style, (std::string("UnityEngine.UIElements.IStyle.get_") + accessors[i].name).c_str(), 0, accessors[i].get);
            n.set[i] = api.FindVerified(style, (std::string("UnityEngine.UIElements.IStyle.set_") + accessors[i].name).c_str(), 1, accessors[i].set);
        }
        n.length = api.Class(UiImage, UiSpace, "Length");
        api.FindVerified(n.length, ".ctor", 2, 0x561D3D0);
        n.setLength = api.FindVerified(api.Class(UiImage, UiSpace, "StyleLength"), "set_value", 1, 0x5623940);
        // Last: the detours, each running the game's own first.
        ::AddHook(api.FindVerified(presets, "SavePreset", 1, 0x20926E0), 0x20926E0, reinterpret_cast<void*>(&SaveHook), reinterpret_cast<void**>(&originalSave));
        ::AddHook(api.FindVerified(presets, "AddPreset", 1, 0x2090D40), 0x2090D40, reinterpret_cast<void*>(&AddPresetHook), reinterpret_cast<void**>(&originalAdd));
        ::AddHook(api.FindVerified(presets, "RemovePreset", 1, 0x2091F60), 0x2091F60, reinterpret_cast<void*>(&RemoveHook), reinterpret_cast<void**>(&originalRemove));
        ::AddHook(api.FindVerified(presets, "ApplyPreset", 1, 0x2090E40), 0x2090E40, reinterpret_cast<void*>(&ApplyHook), reinterpret_cast<void**>(&originalApply));
        ::AddHook(api.FindVerified(presets, "EditPreset", 3, 0x20916A0), 0x20916A0, reinterpret_cast<void*>(&EditHook), reinterpret_cast<void**>(&originalEdit));
        state.installed = true;
        Log("PRESET_WINDOWS_READY hooks=5");
    } catch (const std::exception& error) {
        Log(std::string("PRESET_WINDOWS_UNAVAILABLE ") + error.what());
    }
}
void TickPresetWindows() {
    if (!state.installed) return;
    std::vector<Pending> events;
    {
        std::lock_guard<std::mutex> guard(queueMutex);
        events.swap(queue);
    }
    if (!Session()) { RestorePresetWindows(); return; }
    try {
        Ref container(Container());
        if (!container.get()) return;
        try { FollowFight(container.get()); }
        catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR fight ") + error.what()); }
        for (const auto& event : events) {
            try { HandleEvent(event, container.get()); }
            catch (const std::exception& error) { Log("PRESET_WINDOWS_ERROR uid=" + std::to_string(event.uid) + " " + error.what()); }
        }
        PlacePending();
    } catch (const std::exception& error) { Log(std::string("PRESET_WINDOWS_ERROR ") + error.what()); }
}
void RestorePresetWindows() {
    {
        std::lock_guard<std::mutex> guard(queueMutex);
        queue.clear();
    }
    for (auto& wanted : state.places) wanted.active = false;
    state.fightKnown = state.usingFight = state.fightActive = state.fightChanged = false;
    state.beforeFight.reset();
}
