#include "souls/soul-layout.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace soul::layout_detail {
using namespace managed;
constexpr const char* Key = "DofusMiniatures.SoulStonePanel";
struct Record {
    bool present = false;
    float x = 0, y = 0;
    bool operator==(const Record&) const = default;
};
struct State {
    Ref compositor, cached, container, option, preset, root;
    Record record;
    float width = 0, height = 0;
    uint64_t revision = 1;
    bool installed = false, warned = false;
} state;
using ClearFn = void(*)(Obj, Method);
ClearFn originalClear = nullptr;
using CloneFn = Obj(*)(Obj, Obj, Method);
CloneFn originalClone = nullptr;
struct Context { Ref compositor, cached, container, option, preset; };
struct Origin { Ref cached, container, source, clone; };
std::vector<Origin> origins;
struct Bounds { float x, y, width, height; };
void Warn(const char* error) {
    if (!state.warned) { Log(std::string("SOUL_LAYOUT_UNAVAILABLE ") + error); state.warned = true; }
}
void Forget() {
    if (state.compositor.get() || state.cached.get() || state.container.get()) ++state.revision;
    state.preset.reset(); state.option.reset(); state.container.reset();
    state.cached.reset(); state.compositor.reset(); state.root.reset(); state.record = {};
    state.width = state.height = 0;
}
bool Ready() {
    return state.installed && module && module->ready.load() && module->servicesReady &&
        !module->stopped && !module->stopRequested.load() && module->compositor.get() && module->cached.get();
}
Context Current(bool allowMissingPreset = false) {
    Context result;
    if (!Ready()) return result;
    result.compositor.reset(module->compositor.get());
    result.cached.reset(module->FindService("fjy"));
    if (!result.cached.get() || result.cached.get() != module->cached.get()) return {};
    if (api.Value<bool>(api.Call(result.cached.get(), "get_noOptionSave"))) return {};
    result.container.reset(api.Call(result.cached.get(), "get_uiPresetOptionsContainer"));
    if (!result.container.get()) return {};
    Ref current(api.Field(result.container.get(), "currentPreset"));
    if (!current.get() || !api.Value<bool>(api.Call(current.get(), "IsLoaded"))) return {};
    result.option.reset(api.Call(result.container.get(), "get_currentContextualPreset"));
    if (!result.option.get() || !api.Value<bool>(api.Call(result.option.get(), "IsLoaded"))) return {};
    result.preset.reset(api.Call(result.container.get(), "get_currentContextualPresetData"));
    if (!result.preset.get() && !allowMissingPreset) return {};
    return result;
}
bool Same(const Context& value) {
    return value.compositor.get() == state.compositor.get() && value.cached.get() == state.cached.get() &&
        value.container.get() == state.container.get() && value.option.get() == state.option.get() &&
        value.preset.get() == state.preset.get();
}
int Count(Obj list) {
    if (!list) throw std::runtime_error("Native layout collection unavailable");
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > 4096) throw std::runtime_error("Native layout collection bounds");
    return count;
}
int Index(Obj list, Obj item) {
    int found = -1;
    for (int i = 0, count = Count(list); i < count; ++i) {
        if (api.Call(list, "get_Item", {&i}) != item) continue;
        if (found >= 0) throw std::runtime_error("Ambiguous saved disposition");
        found = i;
    }
    return found;
}
Obj SavedPresets(const Context& context) {
    Ref option(api.Field(context.container.get(), "savedPresets"));
    return option.get() && api.Value<bool>(api.Call(option.get(), "IsLoaded")) ?
        api.Call(option.get(), "get_property") : nullptr;
}
Obj CloneHook(Obj source, Obj provider, Method method) {
    Ref clone(originalClone(source, provider, method));
    try {
        auto context = Current(true);
        if (!clone.get() || !context.container.get()) return clone.get();
        Ref presets(SavedPresets(context));
        if (!presets.get() || Index(presets.get(), source) < 0) return clone.get();
        // Native clones deliberately reset uid to -1. Keep exact provenance;
        // matching names/icons could overwrite another named disposition.
        if (origins.size() == 8) origins.erase(origins.begin());
        origins.push_back({Ref(context.cached.get()), Ref(context.container.get()), Ref(source), Ref(clone.get())});
    } catch (const std::exception& e) { Log(std::string("SOUL_LAYOUT_ORIGIN_ERROR ") + e.what()); }
    return clone.get();
}
bool SyncAssignedFight(const Context& context, Obj tuple) {
    if (context.option.get() != api.Field(context.container.get(), "currentFightPreset")) return false;
    Ref idOption(api.Field(context.container.get(), "fightPresetId"));
    if (!idOption.get() || !api.Value<bool>(api.Call(idOption.get(), "IsLoaded"))) return false;
    const int id = api.Value<int>(api.Call(idOption.get(), "get_property"));
    const auto origin = std::find_if(origins.begin(), origins.end(), [&](const Origin& item) {
        return item.cached.get() == context.cached.get() && item.container.get() == context.container.get() &&
            item.clone.get() == context.preset.get();
    });
    if (origin == origins.end()) { Log("SOUL_LAYOUT_FIGHT_SAVE_SKIPPED reason=clone-origin-unavailable"); return false; }
    Ref source(origin->source.get());
    if (*static_cast<int*>(api.FieldAddress(source.get(), "uid")) != id) return false;
    Ref presets(SavedPresets(context));
    if (!presets.get()) return false;
    int index = Index(presets.get(), source.get());
    if (index < 0) return false;
    Ref positions(api.Field(source.get(), "uiPositionData"));
    int row = -1;
    for (int i = 0, count = Count(positions.get()); i < count; ++i) {
        Ref entry(api.Call(positions.get(), "get_Item", {&i}));
        if (api.String(api.Field(entry.get(), "Item1")) != Key) continue;
        if (row >= 0) throw std::runtime_error("Duplicate saved soul-panel position");
        row = i;
    }
    if (tuple && !api.class_is_valuetype(api.object_get_class(tuple)))
        throw std::runtime_error("Native layout tuple type unavailable");
    auto fresh = Current();
    if (!fresh.preset.get() || !Same(fresh) || SavedPresets(fresh) != presets.get() ||
        api.Field(fresh.container.get(), "fightPresetId") != idOption.get() ||
        api.Value<int>(api.Call(idOption.get(), "get_property")) != id ||
        Index(presets.get(), source.get()) != index || api.Field(source.get(), "uiPositionData") != positions.get()) return false;
    // Patch this single key, never SavePreset(), which snapshots every widget.
    if (tuple) {
        if (row >= 0) api.Call(positions.get(), "set_Item", {&row, api.object_unbox(tuple)});
        else api.Call(positions.get(), "Add", {api.object_unbox(tuple)});
    } else if (row >= 0) api.Call(positions.get(), "RemoveAt", {&row});
    else return false;
    fresh = Current();
    if (!fresh.preset.get() || !Same(fresh) || SavedPresets(fresh) != presets.get() ||
        Index(presets.get(), source.get()) != index) return false;
    // Notify the outer observable collection as native SavePreset does, so the
    // shared preset store persists a nested position edit through normal save.
    api.Call(presets.get(), "set_Item", {&index, source.get()});
    Log(tuple ? "SOUL_LAYOUT_FIGHT_SAVED key=SoulStonePanel" : "SOUL_LAYOUT_FIGHT_RESET key=SoulStonePanel");
    return true;
}
Bounds ReadBounds(Obj root) {
    if (!root || !api.Call(root, "get_panel")) throw std::runtime_error("UI root detached");
    Ref box(api.Call(root, "get_worldBound"));
    const auto result = api.Value<Bounds>(box.get());
    if (!std::isfinite(result.width) || !std::isfinite(result.height) || result.width <= 0 || result.height <= 0)
        throw std::runtime_error("UI root has no usable dimensions");
    return result;
}
Record ReadRecord(Obj container) {
    Ref key(api.string_new(Key));
    Ref tuple(api.Call(container, "LoadUiPositionById", {key.get()}));
    if (!tuple.get()) throw std::runtime_error("Native position returned no tuple");
    Ref id(api.Field(tuple.get(), "Item1")), point(api.Field(tuple.get(), "Item2"));
    if (!id.get() && !point.get()) return {};
    if (!point.get() || api.String(id.get()) != Key) throw std::runtime_error("Invalid native layout record");
    if (*static_cast<bool*>(api.FieldAddress(tuple.get(), "Item3")) ||
        *static_cast<bool*>(api.FieldAddress(tuple.get(), "Item4")))
        throw std::runtime_error("Unexpected anchor in custom layout record");
    Record record{true, *static_cast<float*>(api.FieldAddress(point.get(), "x")),
        *static_cast<float*>(api.FieldAddress(point.get(), "y"))};
    if (!std::isfinite(record.x) || !std::isfinite(record.y) || record.x < 0 || record.x > 100 || record.y < 0 || record.y > 100)
        throw std::runtime_error("Invalid native layout coordinates");
    return record;
}
void Observe(Context value, Record record) {
    if (!Same(value) || record != state.record) ++state.revision;
    state.compositor = std::move(value.compositor); state.cached = std::move(value.cached);
    state.container = std::move(value.container); state.option = std::move(value.option);
    state.preset = std::move(value.preset); state.record = record;
}
void ClearHook(Obj owner, Method method) {
    // Reset can clear an already-empty collection in place. Identity/coordinate
    // polling alone would miss that reset during the first user drag.
    const bool observed = owner && owner == state.container.get();
    if (observed) ++state.revision;
    originalClear(owner, method);
    if (!observed) return;
    try {
        auto context = Current();
        if (context.preset.get() && Same(context) && !ReadRecord(owner).present && SyncAssignedFight(context, nullptr)) {
            auto fresh = Current();
            if (fresh.preset.get() && Same(fresh)) api.Call(context.cached.get(), "RequestSaveUi", {nullptr});
        }
    } catch (const std::exception& e) { Log(std::string("SOUL_LAYOUT_RESET_ERROR ") + e.what()); }
}
int Pixel(float percentage, float extent) {
    return static_cast<int>(std::lround(std::clamp(percentage * (extent / 100.0f), 0.0f, 100000.0f)));
}
}

namespace soul {
void InstallLayout() {
    using namespace layout_detail;
    if (state.installed) return;
    auto cached = api.Class("Core.dll", "Core.Services.Player.OptionsService", "CachedDataService");
    api.Native(api.Find(cached, "get_uiPresetOptionsContainer", 0), 0x7E0890);
    api.Native(api.Find(cached, "get_noOptionSave", 0), 0x752E10);
    api.Native(api.Find(cached, "RequestSaveUi", 1), 0x18B6B80);
    auto container = api.Class("Core.dll", "Core.Engine.Options.OptionsContainers", "UiPresetOptionsContainer");
    api.Native(api.Find(container, "get_currentContextualPreset", 0), 0x20938A0);
    api.Native(api.Find(container, "get_currentContextualPresetData", 0), 0x20937C0);
    api.Native(api.Find(container, "LoadUiPositionById", 1), 0x2091B60);
    api.Native(api.Find(container, "SaveUiPositionById", 4), 0x2092DE0);
    api.Native(api.Find(container, "SetResetUiPosition", 0), 0x2092E50);
    api.Native(api.Find(api.Class("Core.dll", "Core.UI", "UICoordinates"), ".ctor", 2), 0x14F8840);
    AddHook(api.Find(container, "ClearAllSavedUi", 0), 0x2091060,
        reinterpret_cast<void*>(&ClearHook), reinterpret_cast<void**>(&originalClear));
    auto preset = api.Class("Core.dll", "Core.Engine.Options", "PresetData");
    AddHook(api.Find(preset, "bmce", 1), 0x207F0C0,
        reinterpret_cast<void*>(&CloneHook), reinterpret_cast<void**>(&originalClone));
    state.installed = true;
}
LayoutPosition ReadLayout(managed::Obj root) {
    using namespace layout_detail;
    LayoutPosition result;
    try {
        auto context = Current();
        if (!context.preset.get()) { Forget(); result.revision = state.revision; return result; }
        const auto bounds = ReadBounds(root);
        auto record = ReadRecord(context.container.get());
        Observe(std::move(context), record);
        if (root != state.root.get() || bounds.width != state.width || bounds.height != state.height) ++state.revision;
        state.root.reset(root); state.width = bounds.width; state.height = bounds.height;
        if (record.present) { result.x = Pixel(record.x, bounds.width); result.y = Pixel(record.y, bounds.height); }
        result.writable = true;
    } catch (const std::exception& e) { Forget(); Warn(e.what()); }
    result.revision = state.revision;
    return result;
}
bool SaveLayoutPosition(managed::Obj root, int x, int y, uint64_t expectedRevision) {
    using namespace layout_detail;
    try {
        const auto current = ReadLayout(root);
        if (!current.writable || current.revision != expectedRevision || x < 0 || y < 0) return false;
        const auto bounds = ReadBounds(root);
        if (x > bounds.width || y > bounds.height) return false;
        float px = static_cast<float>(x) / bounds.width * 100.0f;
        float py = static_cast<float>(y) / bounds.height * 100.0f;
        Ref key(api.string_new(Key));
        Ref point(api.New(api.Class("Core.dll", "Core.UI", "UICoordinates"), {&px, &py}));
        auto context = Current();
        if (!context.preset.get() || !Same(context) || expectedRevision != state.revision) return false;
        bool unlocked = false;
        api.Call(context.container.get(), "SaveUiPositionById", {key.get(), point.get(), &unlocked, &unlocked});
        const auto saved = ReadRecord(context.container.get());
        if (!saved.present || saved.x != px || saved.y != py) throw std::runtime_error("Native layout save was not accepted");
        auto confirmed = Current();
        if (!confirmed.preset.get() || !Same(confirmed)) { Forget(); return false; }
        Observe(std::move(confirmed), saved);
        Ref tuple(api.Call(context.container.get(), "LoadUiPositionById", {key.get()}));
        SyncAssignedFight(context, tuple.get());
        confirmed = Current();
        if (!confirmed.preset.get() || !Same(confirmed)) { Forget(); return false; }
        api.Call(context.container.get(), "SetResetUiPosition");
        auto beforeFlush = Current();
        if (!beforeFlush.preset.get() || !Same(beforeFlush)) { Forget(); return false; }
        // RequestSaveUi ignores its message argument and uses the native
        // five-second debounce/cancellation lifecycle, as ordinary UI drags do.
        api.Call(context.cached.get(), "RequestSaveUi", {nullptr});
        Log("SOUL_LAYOUT_SAVED x=" + std::to_string(x) + " y=" + std::to_string(y));
        return true;
    } catch (const std::exception& e) { Forget(); Warn(e.what()); return false; }
}
void ResetLayout() { layout_detail::Forget(); layout_detail::origins.clear(); }
}
