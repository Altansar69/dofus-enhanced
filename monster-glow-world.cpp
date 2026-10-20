#include "monster-glow-world.hpp"
#include "module.hpp"
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace monsterGlow {
namespace {
using namespace managed;
constexpr int MaxDecor = 4096, MaxMaterials = 16;
constexpr size_t MaxForeground = 64, MaxMonsterParts = 32;
struct Methods {
    Klass service{}, map{}, loader{}, resources{}, element{}, renderer{}, materials{};
    Method ready{}, mapData{}, exists{}, enabled{}, gameObject{}, active{}, layer{}, layerValue{}, order{};
    Method sharedMaterials{}, queue{}, group{}, invalidGroup{};
    bool installed = false;
} methods;
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
bool Exists(Obj object) { return object && api.Value<bool>(api.Invoke(methods.exists, nullptr, {object})); }
bool Visible(Obj renderer) {
    if (!Is(renderer, methods.renderer) || !Exists(renderer) ||
        !api.Value<bool>(api.Invoke(methods.enabled, renderer))) return false;
    Ref object(api.Invoke(methods.gameObject, renderer));
    return Exists(object.get()) && api.Value<bool>(api.Invoke(methods.active, object.get()));
}
int Count(Obj list) { return api.Value<int>(api.Call(list, "get_Count")); }
int Version(Obj list) { return *static_cast<int*>(api.FieldAddress(list, "_version")); }
int Layer(Obj renderer) {
    int id = api.Value<int>(api.Invoke(methods.layer, renderer));
    return api.Value<int>(api.Invoke(methods.layerValue, nullptr, {&id}));
}
int Order(Obj renderer) { return api.Value<int>(api.Invoke(methods.order, renderer)); }
bool Current(const WorldFrame& f) {
    return module && module->servicesReady && !module->stopped &&
        module->compositor.get() == f.compositor.get() && module->FindService("fpm") == f.service.get() &&
        api.Value<bool>(api.Invoke(methods.ready, f.service.get())) &&
        api.Invoke(methods.mapData, f.service.get()) == f.map.get() && Exists(f.map.get()) &&
        Read<int>(f.map.get(), 0x18) == f.mapId &&
        Read<Obj>(f.service.get(), 0x110) == f.loader.get() &&
        Read<Obj>(f.loader.get(), 0x80) == f.resources.get() &&
        Read<int64_t>(f.resources.get(), 0x10) == f.mapId &&
        Read<Obj>(f.resources.get(), 0x20) == f.sprites.get() &&
        Count(f.sprites.get()) == f.count && Version(f.sprites.get()) == f.version;
}
bool Valid(ScreenRect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
        std::isfinite(r.height) && r.width > 0 && r.height > 0 &&
        std::isfinite(r.x + r.width) && std::isfinite(r.y + r.height);
}
bool Overlap(ScreenRect a, ScreenRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
        a.y < b.y + b.height && b.y < a.y + a.height;
}
struct SortKey { int layer = 0, order = 0, queue = 0; bool known = false; };
SortKey Sorting(Obj renderer, Obj materialList, int invalidGroup) {
    SortKey result;
    if (!Visible(renderer)) return result;
    // A SortingGroup can override the renderer's own layer/order. Do not infer
    // the group's world ordering from the child's local values.
    if (api.Value<int>(api.Invoke(methods.group, renderer)) != invalidGroup) return result;
    result.layer = Layer(renderer); result.order = Order(renderer);
    api.Call(materialList, "Clear");
    api.Invoke(methods.sharedMaterials, renderer, {materialList});
    const int count = Count(materialList);
    if (count < 1 || count > MaxMaterials) return result;
    for (int i = 0; i < count; ++i) {
        Ref material(api.Call(materialList, "get_Item", {&i}));
        if (!Exists(material.get())) return result;
        int queue = api.Value<int>(api.Invoke(methods.queue, material.get()));
        if (queue < 0 || queue > 5000 || (i && queue != result.queue)) return result;
        result.queue = queue;
    }
    result.known = true;
    return result;
}
bool InFront(const SortKey& decor, const SortKey& monster) {
    if (!decor.known || !monster.known) return false;
    // Equal queue: native map qjr / entity rau order is unambiguous. For
    // different queues accept only agreement between both sorting criteria;
    // never let a higher queue override an explicitly rear sorting layer.
    const int ordered = decor.layer != monster.layer ? (decor.layer > monster.layer ? 1 : -1) :
        decor.order != monster.order ? (decor.order > monster.order ? 1 : -1) : 0;
    if (decor.queue == monster.queue) return ordered > 0;
    return decor.queue > monster.queue && ordered > 0;
}
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset)
        throw std::runtime_error(std::string("Glow world field layout: ") + name);
}
}

void InstallGlowWorld() {
    if (methods.installed) return;
    methods.service = api.Class("Core.dll", "", "epf");
    methods.loader = api.Class("Core.dll", "", "ue");
    methods.resources = api.Class("Core.dll", "", "tt");
    methods.map = api.Class("AleCore.dll", "AleCore.Data.UnityClient", "ClientMapData");
    methods.element = api.Class("Core.dll", "Core.Rendering.GameObjects", "MapElement");
    methods.renderer = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
    RequireField(methods.service, "doqa", 0x110);
    RequireField(methods.loader, "dgjz", 0x80);
    RequireField(methods.resources, "dgfm", 0x10);
    RequireField(methods.resources, "dgfo", 0x20);
    RequireField(methods.map, "id", 0x18);
    RequireField(methods.element, "meshRenderer", 0x20);
    methods.ready = api.FindVerified(methods.service, "xkj", 0, 0x18389C0);
    methods.mapData = api.FindVerified(methods.service, "xkd", 0, 0x11ECD00);
    auto guard = [](const char* type, const char* name, int argc, uintptr_t rva, const char* space = "UnityEngine") {
        return api.FindVerified(api.Class("UnityEngine.CoreModule.dll", space, type), name, argc, rva);
    };
    methods.exists = guard("Object", "op_Implicit", 1, 0x5439190);
    methods.enabled = guard("Renderer", "get_enabled", 0, 0x53F2300);
    methods.gameObject = guard("Component", "get_gameObject", 0, 0x5429D00);
    methods.active = guard("GameObject", "get_activeInHierarchy", 0, 0x5432080);
    methods.layer = guard("Renderer", "get_sortingLayerID", 0, 0x53F2600);
    methods.layerValue = guard("SortingLayer", "GetLayerValueFromID", 1, 0x53C6FE0);
    methods.order = guard("Renderer", "get_sortingOrder", 0, 0x53F2820);
    methods.sharedMaterials = guard("Renderer", "GetSharedMaterials", 1, 0x53F1AD0);
    methods.queue = guard("Material", "get_renderQueue", 0, 0x53E55A0);
    methods.group = guard("Renderer", "get_sortingGroupID", 0, 0x53F2480);
    methods.invalidGroup = guard("SortingGroup", "get_invalidSortingGroupID", 0, 0x54777A0, "UnityEngine.Rendering");
    methods.materials = api.class_from_type(api.method_get_param(methods.sharedMaterials, 0));
    if (!methods.materials) throw std::runtime_error("Glow world material list unavailable");
    methods.installed = true;
}

WorldFrame ReadGlowWorld() {
    WorldFrame f;
    if (!methods.installed || !module || !module->servicesReady || module->stopped || !module->compositor.get()) return f;
    try {
        f.compositor.reset(module->compositor.get()); f.service.reset(module->FindService("fpm"));
        f.reason = "map-service"; if (!Is(f.service.get(), methods.service)) return f;
        f.reason = "map-loading"; if (!api.Value<bool>(api.Invoke(methods.ready, f.service.get()))) return f;
        f.map.reset(api.Invoke(methods.mapData, f.service.get()));
        f.reason = "map-data"; if (!Is(f.map.get(), methods.map) || !Exists(f.map.get())) return f;
        f.mapId = Read<int>(f.map.get(), 0x18);
        f.loader.reset(Read<Obj>(f.service.get(), 0x110));
        f.reason = "map-loader"; if (!Is(f.loader.get(), methods.loader)) return f;
        // qjh retains the map resources in ue.dgjz. At load completion qhw
        // replaces ue.dgjy.dggz with a fresh build buffer, so that buffer must
        // never be interpreted as the active rendered map.
        f.resources.reset(Read<Obj>(f.loader.get(), 0x80));
        f.reason = "map-resources"; if (!Is(f.resources.get(), methods.resources)) return f;
        f.sprites.reset(Read<Obj>(f.resources.get(), 0x20));
        f.reason = "map-elements"; if (!f.sprites.get()) return f;
        f.count = Count(f.sprites.get()); f.version = Version(f.sprites.get());
        f.reason = "decor-limit"; if (f.count < 0 || f.count > MaxDecor) return f;
        f.reason = "stale-map"; if (!Current(f)) return f;
        std::unordered_set<Obj> seen;
        for (int i = 0; i < f.count; ++i) {
            Ref element(api.Call(f.sprites.get(), "get_Item", {&i}));
            if (!element.get()) continue;
            if (!Is(element.get(), methods.element)) throw std::runtime_error("Unexpected map element type");
            if (!Exists(element.get())) continue;
            Ref renderer(Read<Obj>(element.get(), 0x20));
            if (!Visible(renderer.get()) || !seen.insert(renderer.get()).second) continue;
            const int layer = Layer(renderer.get()), order = Order(renderer.get());
            WorldDecor item; item.element = std::move(element); item.renderer = std::move(renderer);
            item.layer = layer; item.order = order;
            f.decor.push_back(std::move(item));
        }
        if (!Current(f)) { f.decor.clear(); f.reason = "stale-map"; return f; }
        f.valid = true; f.reason = "ready";
    } catch (...) { f.decor.clear(); f.reason = "world-read-failed"; }
    return f;
}

WorldSelection ForegroundGlowDecor(const WorldFrame& world, const UiFrame& ui,
    const std::vector<Obj>& monsters, ScreenRect targetBounds) {
    WorldSelection result;
    if (!world.valid || !ui.valid || monsters.empty() || monsters.size() > MaxMonsterParts || !Valid(targetBounds)) return result;
    try {
        result.reason = "stale-map"; if (!Current(world)) return result;
        Ref materialList(api.New(methods.materials));
        const int invalidGroup = api.Value<int>(api.Invoke(methods.invalidGroup, nullptr));
        std::vector<SortKey> targets;
        for (Obj monster : monsters) {
            auto key = Sorting(monster, materialList.get(), invalidGroup);
            result.reason = "target-order-unknown"; if (!key.known) return result;
            targets.push_back(key);
        }
        for (const auto& decor : world.decor) {
            bool orderedAhead = true;
            for (const auto& target : targets)
                orderedAhead = orderedAhead && (decor.layer > target.layer ||
                    (decor.layer == target.layer && decor.order > target.order));
            if (!orderedAhead) continue;
            if (!Exists(decor.element.get()) || Read<Obj>(decor.element.get(), 0x20) != decor.renderer.get() ||
                !Visible(decor.renderer.get())) continue;
            if (!decor.projection || decor.serial != ui.serial || decor.generation != ui.generation) {
                auto projection = ProjectGlowRenderers(ui, {decor.renderer.get()}, true);
                decor.serial = ui.serial; decor.generation = ui.generation;
                decor.projection = projection.valid ? 1 : std::strcmp(projection.reason, "offscreen") == 0 ? 2 : 3;
                decor.bounds = projection.screenRect;
            }
            if (decor.projection == 2) continue;
            if (decor.projection != 1 || !Valid(decor.bounds)) {
                result.reason = "decor-projection-unavailable"; result.renderers.clear(); return result;
            }
            if (!Overlap(decor.bounds, targetBounds)) continue;
            auto key = Sorting(decor.renderer.get(), materialList.get(), invalidGroup);
            // Unknown groups/material ordering and ties are omitted. They may
            // hide additional monsters but must not reveal unobscured ones.
            bool front = key.known;
            for (const auto& target : targets) front = front && InFront(key, target);
            if (!front) continue;
            if (key.layer != decor.layer || key.order != decor.order) {
                result.reason = "decor-order-changed"; result.renderers.clear(); return result;
            }
            if (result.renderers.size() == MaxForeground) {
                result.reason = "foreground-limit"; result.renderers.clear(); return result;
            }
            result.renderers.emplace_back(decor.renderer.get());
        }
        if (!Current(world)) { result.reason = "stale-map"; result.renderers.clear(); return result; }
        // The UI provider also checks the frame serial and camera identity.
        auto finalProjection = ProjectGlowRenderers(ui, monsters, true);
        if (!finalProjection.valid) { result.reason = "stale-ui-frame"; result.renderers.clear(); return result; }
        result.valid = true; result.reason = "ready";
    } catch (...) { result.renderers.clear(); result.reason = "world-selection-failed"; }
    return result;
}
}
