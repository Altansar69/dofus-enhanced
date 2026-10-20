#include "appearance-refresh.hpp"
#include "module.hpp"
#include <unordered_set>
#include <vector>

namespace {
using namespace managed;
struct Methods {
    Klass fight{}, context{}, fighters{}, rendered{}, registry{}, creature{}, info{}, entity{};
    Method list{}, findInfo{}, id{}, flag{}, findRendered{}, refresh{};
    bool installed = false;
} methods;
bool errorReported = false;
thread_local bool refreshing = false;
bool Is(Obj object, Klass klass) { return object && api.object_get_class(object) == klass; }
Method Verified(Klass klass, const char* name, int argc, uintptr_t rva) {
    auto method = api.Find(klass, name, argc);
    api.Native(method, rva);
    return method;
}
struct Owners {
    Ref fight, context, fighters, rendered, registry, creature, services;
    bool Current() const {
        return module && module->servicesReady && module->fight.get() == fight.get() &&
            module->creatures.get() == creature.get() && module->InCombat() &&
            Read<Obj>(fight.get(), 0x18) == context.get() &&
            Read<Obj>(context.get(), 0x58) == fighters.get() &&
            Read<Obj>(context.get(), 0x68) == rendered.get() &&
            Read<Obj>(rendered.get(), 0x150) == registry.get() &&
            Read<Obj>(rendered.get(), 0x20) == services.get() &&
            Read<Obj>(creature.get(), 0x10) == services.get();
    }
};
struct Target {
    int64_t id;
    Ref info, entity;
    Target(int64_t value, Obj information, Obj rendered) : id(value), info(information), entity(rendered) {}
};
bool Root(Obj entity, int64_t id, Obj services) {
    return Is(entity, methods.entity) && Read<Obj>(entity, 0x1A8) == entity &&
        Read<Obj>(entity, 0x398) == services && Read<int64_t>(entity, 0x1C8) == id &&
        Read<Obj>(entity, 0x170) && Read<Obj>(entity, 0xA8) && Read<Obj>(entity, 0x1E0);
}
bool CurrentTarget(const Owners& owners, const Target& target) {
    auto id = target.id;
    Obj info = nullptr;
    int fighter = 1;
    return owners.Current() &&
        api.Value<bool>(api.Invoke(methods.findInfo, owners.fighters.get(), {&id, &info})) &&
        info == target.info.get() && api.Value<bool>(api.Invoke(methods.flag, info, {&fighter})) &&
        api.Invoke(methods.findRendered, owners.registry.get(), {&id}) == target.entity.get() &&
        Root(target.entity.get(), id, owners.services.get());
}
}

void InstallCombatAppearanceRefresh() {
    if (methods.installed) return;
    methods.fight = api.Class("Core.dll", "", "fxc");
    methods.context = api.Class("Core.dll", "", "fyb");
    methods.fighters = api.Class("Core.dll", "", "evv");
    methods.rendered = api.Class("Core.dll", "", "fxw");
    methods.registry = api.Class("Core.dll", "", "ebm");
    methods.creature = api.Class("Core.dll", "", "exf");
    methods.info = api.Class("Core.dll", "Core.Services.Entities.EntityInformationServices", "EntityInfo");
    methods.entity = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    methods.list = Verified(methods.fighters, "baml", 0, 0x19579E0);
    methods.findInfo = Verified(methods.fighters, "bamm", 2, 0x1957A70);
    methods.id = Verified(methods.info, "bajh", 0, 0x1951440);
    methods.flag = Verified(methods.info, "bajs", 1, 0x19518F0);
    methods.findRendered = Verified(methods.registry, "vkq", 1, 0x27695E0);
    methods.refresh = Verified(methods.entity, "ric", 0, 0x2619F70);
    methods.installed = true;
}

void RefreshCombatAppearance() {
    if (!methods.installed || refreshing || !module || !module->servicesReady ||
        !Is(module->fight.get(), methods.fight) || !module->InCombat() ||
        !Is(module->creatures.get(), methods.creature)) return;
    // Stopped is intentionally accepted: Stop's final native refresh must also
    // replace miniature looks while custom conversion is already disabled.
    struct Scope { Scope() { refreshing = true; } ~Scope() { refreshing = false; } } scope;
    try {
        Owners owners;
        owners.fight.reset(module->fight.get()); owners.creature.reset(module->creatures.get());
        owners.context.reset(Read<Obj>(owners.fight.get(), 0x18));
        if (!Is(owners.context.get(), methods.context)) return;
        owners.fighters.reset(Read<Obj>(owners.context.get(), 0x58));
        owners.rendered.reset(Read<Obj>(owners.context.get(), 0x68));
        if (!Is(owners.fighters.get(), methods.fighters) || !Is(owners.rendered.get(), methods.rendered)) return;
        owners.registry.reset(Read<Obj>(owners.rendered.get(), 0x150));
        owners.services.reset(Read<Obj>(owners.creature.get(), 0x10));
        if (!Is(owners.registry.get(), methods.registry) || !owners.services.get() || !owners.Current()) return;
        Ref list(api.Invoke(methods.list, owners.fighters.get()));
        if (!list.get()) return;
        const int count = api.Value<int>(api.Call(list.get(), "get_Count"));
        if (count < 0 || count > 512) return;
        std::vector<Target> targets;
        std::unordered_set<Obj> seen;
        targets.reserve(count);
        // Finish reading the native roster before any appearance callback can
        // create/remove rendered children or otherwise change a registry.
        for (int i = 0; i < count; ++i) {
            Ref info(api.Call(list.get(), "get_Item", {&i}));
            if (!Is(info.get(), methods.info)) continue;
            int fighter = 1;
            if (!api.Value<bool>(api.Invoke(methods.flag, info.get(), {&fighter}))) continue;
            int64_t id = api.Value<int64_t>(api.Invoke(methods.id, info.get()));
            if (!id) continue;
            Ref entity(api.Invoke(methods.findRendered, owners.registry.get(), {&id}));
            if (!Root(entity.get(), id, owners.services.get()) || !seen.insert(entity.get()).second) continue;
            targets.emplace_back(id, info.get(), entity.get());
        }
        for (auto& target : targets) {
            if (!owners.Current()) return;
            if (!CurrentTarget(owners, target)) continue;
            // This is the native appearance-refresh method used by the stock
            // creature toggle. It owns cancellation/replacement of look loads;
            // no animation, sequencer, transform or original look is patched.
            api.Invoke(methods.refresh, target.entity.get());
        }
        errorReported = false;
    } catch (const std::exception& error) {
        if (!errorReported) Log(std::string("COMBAT_APPEARANCE_REFRESH_ERROR ") + error.what());
        errorReported = true;
    }
}
