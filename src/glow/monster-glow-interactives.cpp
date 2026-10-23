#include "glow/monster-glow-interactives.hpp"
#include "glow/monster-glow-opacity.hpp"
#include "core/module.hpp"
#include <cmath>
#include <unordered_set>

namespace monsterGlow {
namespace {
using namespace managed;
constexpr int MaxObjects = 512, MaxSkills = 64;
constexpr size_t MaxTargets = 128, MaxParts = 32;
struct Methods {
    Klass service{}, wrapper{}, data{}, fixed{}, moving{}, animation{}, animated{}, animator{}, element{}, renderer{};
    Klass mapService{}, map{};
    Method list{}, find{}, ready{}, mapData{}, exists{}, enabled{}, gameObject{}, active{}, children{};
    bool installed = false;
} methods;
bool reported = false;
bool Is(Obj value, Klass type) { return value && api.object_get_class(value) == type; }
bool Exists(Obj value) { return value && api.Value<bool>(api.Invoke(methods.exists, nullptr, {value})); }
int Count(Obj list) { return list ? api.Value<int>(api.Call(list, "get_Count")) : 0; }
bool Visible(Obj renderer) {
    if (!Is(renderer, methods.renderer) || !Exists(renderer) ||
        !api.Value<bool>(api.Invoke(methods.enabled, renderer))) return false;
    Ref object(api.Invoke(methods.gameObject, renderer));
    return Exists(object.get()) && api.Value<bool>(api.Invoke(methods.active, object.get()));
}
struct Owners {
    Ref compositor, service, registry, mapService, map;
    int version = 0, count = 0, mapId = 0;
    bool Current() const {
        return module && module->servicesReady && !module->stopped && !module->stopRequested &&
            !module->InCombat() && module->compositor.get() == compositor.get() &&
            module->FindService("fro") == service.get() && module->FindService("fpm") == mapService.get() &&
            Read<Obj>(service.get(), 0x40) == mapService.get() &&
            Read<Obj>(service.get(), 0xC8) == registry.get() &&
            *static_cast<int*>(api.FieldAddress(registry.get(), "_version")) == version &&
            Count(registry.get()) == count &&
            api.Value<bool>(api.Invoke(methods.ready, mapService.get())) &&
            api.Invoke(methods.mapData, mapService.get()) == map.get() &&
            Exists(map.get()) && Read<int>(map.get(), 0x18) == mapId;
    }
    bool Contains(int64_t id, Obj wrapper) const {
        Obj current = nullptr;
        return api.Value<bool>(api.Invoke(methods.find, service.get(), {&id, &current})) && current == wrapper;
    }
};
bool CollectAnimated(Obj root, Target& target) {
    std::vector<Ref> pending;
    pending.emplace_back(root);
    std::unordered_set<Obj> visited{root}, renderers;
    for (size_t at = 0; at < pending.size(); ++at) {
        const auto part = pending[at].get();
        if (!part || !api.class_is_assignable_from(methods.animated, api.object_get_class(part))) return false;
        Ref animator(Read<Obj>(part, 0xA8)), renderer(Read<Obj>(part, 0x1E8));
        const float alpha = Is(animator.get(), methods.animator) && Exists(animator.get()) ?
            Read<float>(animator.get(), 0x40) : 0;
        if (!std::isfinite(alpha) || alpha <= GlowAlphaClip || alpha > 1.001f || !Visible(renderer.get())) {
            if (part == root) return false;
            continue;
        }
        if (renderers.insert(renderer.get()).second) target.renderers.push_back(std::move(renderer));
        Ref children(api.Invoke(methods.children, part));
        if (!children.get()) continue;
        Ref iterator(api.Call(children.get(), "GetEnumerator"));
        if (!iterator.get()) return false;
        bool bounded = true;
        try {
            size_t count = 0;
            while (api.Value<bool>(api.Call(iterator.get(), "MoveNext"))) {
                if (++count > MaxParts) { bounded = false; break; }
                Ref child(api.Call(iterator.get(), "get_Current"));
                if (!child.get() || !visited.insert(child.get()).second) continue;
                if (visited.size() > MaxParts) { bounded = false; break; }
                pending.push_back(std::move(child));
            }
        } catch (...) {
            try { api.Call(iterator.get(), "Dispose"); } catch (...) {}
            throw;
        }
        api.Call(iterator.get(), "Dispose");
        if (!bounded) return false;
    }
    return !target.renderers.empty();
}
bool Collect(Obj focus, uint32_t id, Target& target) {
    if (Is(focus, methods.fixed)) {
        if (Read<uint32_t>(focus, 0x30) != id) return false;
        Ref element(Read<Obj>(focus, 0x20));
        if (!Is(element.get(), methods.element) || !Exists(element.get())) return false;
        Ref renderer(Read<Obj>(element.get(), 0x20));
        if (!Visible(renderer.get())) return false;
        target.renderers.push_back(std::move(renderer));
        return true;
    }
    if (Is(focus, methods.moving)) {
        if (Read<uint32_t>(focus, 0x20) != id) return false;
        Ref animation(Read<Obj>(focus, 0x10));
        if (!Is(animation.get(), methods.animation)) return false;
        Ref root(Read<Obj>(animation.get(), 0x18));
        return CollectAnimated(root.get(), target);
    }
    return false; // Unknown displayable types are never treated as raw renderers.
}
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset)
        throw std::runtime_error(std::string("Glow interactive field layout: ") + name);
}
}

void InstallGlowInteractives() {
    if (methods.installed) return;
    methods.service = api.Class("Core.dll", "", "wp");
    methods.wrapper = api.Class("Core.dll", "", "wm");
    methods.data = api.Class("Core.dll", "", "dq");
    methods.fixed = api.Class("Core.dll", "", "wl");
    methods.moving = api.Class("Core.dll", "", "wk");
    methods.animation = api.Class("Core.dll", "", "wq");
    methods.mapService = api.Class("Core.dll", "", "epf");
    methods.map = api.Class("AleCore.dll", "AleCore.Data.UnityClient", "ClientMapData");
    methods.element = api.Class("Core.dll", "Core.Rendering.GameObjects", "MapElement");
    methods.animated = api.Class("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated");
    methods.animator = api.Class("Ankama.Animator2D.dll", "Ankama.Animations", "Animator2D");
    methods.renderer = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
    RequireField(methods.service, "dhbg", 0x40);
    RequireField(methods.service, "dhbx", 0xC8);
    RequireField(methods.wrapper, "dhat", 0x10);
    RequireField(methods.wrapper, "dhau", 0x18);
    RequireField(methods.wrapper, "dhav", 0x20);
    RequireField(methods.data, "dcan", 0x10);
    RequireField(methods.data, "dcap", 0x18);
    RequireField(methods.fixed, "dham", 0x20);
    RequireField(methods.fixed, "<dhap>k__BackingField", 0x30);
    RequireField(methods.moving, "dhaa", 0x10);
    RequireField(methods.moving, "<dhad>k__BackingField", 0x20);
    RequireField(methods.animation, "dhcd", 0x18);
    RequireField(methods.animated, "fhro", 0xA8);
    RequireField(methods.animated, "fhtg", 0x1E8);
    RequireField(methods.animator, "colorModifier", 0x34); // Color.a is +12.
    RequireField(methods.element, "meshRenderer", 0x20);
    RequireField(methods.map, "id", 0x18);
    methods.list = api.FindVerified(methods.service, "qyb", 0, 0x2629090);
    methods.find = api.FindVerified(methods.service, "qyg", 2, 0x2629380);
    methods.ready = api.FindVerified(methods.mapService, "xkj", 0, 0x18389C0);
    methods.mapData = api.FindVerified(methods.mapService, "xkd", 0, 0x11ECD00);
    methods.children = api.FindVerified(methods.animated, "dayr", 0, 0x782BE0);
    auto unity = [](const char* type) { return api.Class("UnityEngine.CoreModule.dll", "UnityEngine", type); };
    methods.exists = api.FindVerified(unity("Object"), "op_Implicit", 1, 0x5439190);
    methods.enabled = api.FindVerified(unity("Renderer"), "get_enabled", 0, 0x53F2300);
    methods.gameObject = api.FindVerified(unity("Component"), "get_gameObject", 0, 0x5429D00);
    methods.active = api.FindVerified(unity("GameObject"), "get_activeInHierarchy", 0, 0x5432080);
    methods.installed = true;
}

Snapshot ReadGlowInteractives() {
    Snapshot result;
    result.reason = "interactive-session";
    if (!methods.installed || !module || !module->servicesReady || module->stopped || module->stopRequested ||
        !module->settings.glowInteractives || module->InCombat() || !module->compositor.get()) return result;
    try {
        Owners owners;
        owners.compositor.reset(module->compositor.get());
        owners.service.reset(module->FindService("fro")); owners.mapService.reset(module->FindService("fpm"));
        if (!Is(owners.service.get(), methods.service) || !Is(owners.mapService.get(), methods.mapService)) return result;
        if (!api.Value<bool>(api.Invoke(methods.ready, owners.mapService.get()))) return result;
        owners.map.reset(api.Invoke(methods.mapData, owners.mapService.get()));
        if (!Is(owners.map.get(), methods.map) || !Exists(owners.map.get())) return result;
        owners.mapId = Read<int>(owners.map.get(), 0x18);
        owners.registry.reset(Read<Obj>(owners.service.get(), 0xC8));
        if (!owners.mapId || !owners.registry.get()) return result;
        owners.count = Count(owners.registry.get());
        owners.version = *static_cast<int*>(api.FieldAddress(owners.registry.get(), "_version"));
        result.reason = "interactive-limit";
        if (owners.count < 0 || owners.count > MaxObjects) return result;
        if (!owners.Current()) return result;
        Ref list(api.Invoke(methods.list, owners.service.get()));
        if (!list.get() || Count(list.get()) != owners.count) return result;
        std::unordered_set<int64_t> seen;
        for (int i = 0; i < owners.count && result.targets.size() < MaxTargets; ++i) {
            if (!owners.Current()) return {};
            Ref wrapper(api.Call(list.get(), "get_Item", {&i}));
            if (!Is(wrapper.get(), methods.wrapper)) continue;
            const auto id = Read<int64_t>(wrapper.get(), 0x10);
            if (id <= 0 || id > UINT32_MAX || !seen.insert(id).second || !owners.Contains(id, wrapper.get())) continue;
            Ref data(Read<Obj>(wrapper.get(), 0x20)), focus(Read<Obj>(wrapper.get(), 0x18));
            if (!Is(data.get(), methods.data) || static_cast<uint32_t>(Read<int>(data.get(), 0x10)) != id) continue;
            Ref skills(Read<Obj>(data.get(), 0x18));
            const int skillCount = Count(skills.get());
            if (skillCount <= 0 || skillCount > MaxSkills) continue;
            Target target; target.id = id; target.interactive = true; target.entity.reset(focus.get());
            if (!Collect(focus.get(), static_cast<uint32_t>(id), target)) continue;
            if (!owners.Current()) return {};
            if (!owners.Contains(id, wrapper.get()) || Read<Obj>(wrapper.get(), 0x18) != focus.get() ||
                Read<Obj>(wrapper.get(), 0x20) != data.get()) continue;
            result.targets.push_back(std::move(target));
        }
        if (!owners.Current()) return {};
        result.context.reset(owners.compositor.get()); result.valid = true;
        result.interactiveCount = static_cast<int>(result.targets.size());
        result.reason = result.targets.empty() ? "no-visible-interactives" : "ready";
        reported = false;
    } catch (const std::exception& error) {
        if (!reported) Log(std::string("GLOW_INTERACTIVES_ERROR ") + error.what());
        reported = true; return {};
    }
    return result;
}
}
