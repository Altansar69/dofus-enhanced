#include "glow/monster-glow-entities.hpp"
#include "glow/monster-glow-opacity.hpp"
#include "appearance/features.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace monsterGlow {
namespace {
using namespace managed;
constexpr int MaxRoster = 512;
constexpr size_t MaxTargets = 128, MaxParts = 32;
struct Methods {
    Klass information{}, registry{}, entity{}, creature{}, fight{}, context{}, rendered{}, world{};
    Klass animator{}, renderer{}, combatRegistry{};
    Method list{}, findInfo{}, id{}, flag{}, boolTry{}, byteTry{}, intTry{};
    Method findFight{}, findWorld{}, visible{}, alpha{}, children{};
    Method exists{}, enabled{}, gameObject{}, active{};
    bool installed = false;
} methods;
bool reported = false, identityReported = false;
bool Is(Obj obj, Klass klass) { return obj && api.object_get_class(obj) == klass; }
Method Verify(Klass klass, const char* name, int argc, uintptr_t rva) {
    return api.FindVerified(klass, name, argc, rva);
}
bool Exists(Obj obj) {
    return obj && api.Value<bool>(api.Invoke(methods.exists, nullptr, {obj}));
}
bool Flag(Obj info, int flags) {
    return api.Value<bool>(api.Invoke(methods.flag, info, {&flags}));
}
bool Monster(Obj info, bool combat) {
    if (!Is(info, methods.information) || !Flag(info, combat ? 9 : 10)) return false;
    for (int excluded : {0x80, 0x10, 0x20, 0x40}) if (Flag(info, excluded)) return false;
    if (!combat) return !Flag(info, 1);
    int visibilityKey = 2, aliveKey = 3;
    int8_t visibility = -1;
    bool alive = false;
    return api.Value<bool>(api.Invoke(methods.byteTry, info, {&visibilityKey, &visibility})) && visibility == 2 &&
        api.Value<bool>(api.Invoke(methods.boolTry, info, {&aliveKey, &alive})) && alive;
}
bool Npc(Obj info, bool combat) {
    if (combat || !Is(info, methods.information) || !Flag(info, 2)) return false;
    for (int excluded : {1, 8, 0x80, 0x10, 0x20}) if (Flag(info, excluded)) return false;
    // Native NpcMenuMaker reads integer property 15, populated by gwu.bmpa.
    // The combat humanoid flag 0x40 is NOT the roleplay NPC discriminator.
    int npcKey = 15, npcId = 0;
    return api.Value<bool>(api.Invoke(methods.intTry, info, {&npcKey, &npcId})) && npcId > 0;
}
// A player character of the roleplay scene: roleplay 2 and player 0x80, as
// « Apparence » tells a player (Features::Convert). The roleplay character
// mapper gwo.bmni sets 0x80 (bts 7) over gwr.bmni's roleplay humanoid flags
// (2 | 4, and 0x40 in gwr.bmot); NPCs (gwu, gww, gwx, gwz: 2), monster groups
// (gwp: 0xA) and tax collectors (gwy: 0x22) never have it. A fighter (1), a
// monster (8), a companion (0x10) or a tax collector (0x20) is never a player.
bool Player(Obj info, bool combat) {
    if (combat || !Is(info, methods.information) || !Flag(info, 0x82)) return false;
    for (int excluded : {1, 8, 0x10, 0x20}) if (Flag(info, excluded)) return false;
    return true;
}
// The local character and the accepted members of its party, read as
// « Apparence » reads them (Features::ReadRoleplayIdentity), each time: a
// party change shows at the next glow frame. Unknown local character (none
// yet, or a read error): no player is outlined, since the local character
// could not be told apart from the others.
Features::RoleplayIdentity ReadIdentity() {
    if (!module->features) return {};
    try {
        auto identity = module->features->ReadRoleplayIdentity();
        identityReported = false;
        return identity;
    } catch (const std::exception& error) {
        if (!identityReported) Log(std::string("MONSTER_GLOW_IDENTITY_ERROR ") + error.what());
        identityReported = true;
        return {};
    }
}
// A roster entry of a ticked category, before its rendered root is resolved.
struct Candidate { Ref info; GlowCategory category; };
struct Owners {
    Ref compositor, creature, services, registry, fight, context, rendered, world;
    bool combat = false;
    bool Current() const {
        if (!module || !module->servicesReady || module->stopped ||
            module->compositor.get() != compositor.get() || module->creatures.get() != creature.get() ||
            module->fight.get() != fight.get() || module->InCombat() != combat ||
            Read<Obj>(creature.get(), 0x10) != services.get()) return false;
        if (combat) return Read<Obj>(fight.get(), 0x18) == context.get() &&
            Read<Obj>(context.get(), 0x58) == registry.get() && Read<Obj>(context.get(), 0x68) == rendered.get() &&
            Read<Obj>(rendered.get(), 0x20) == services.get() &&
            Read<Obj>(rendered.get(), 0x150) == world.get();
        return Read<Obj>(world.get(), 0x20) == services.get();
    }
    Obj Find(int64_t id) const {
        if (combat) return api.Invoke(methods.findFight, world.get(), {&id});
        Obj result = nullptr;
        return api.Value<bool>(api.Invoke(methods.findWorld, world.get(), {&id, &result})) ? result : nullptr;
    }
};
bool VisibleOpacity(float alpha) { return std::isfinite(alpha) && alpha > GlowAlphaClip && alpha <= 1.001f; }
bool VisiblePart(Obj part, Obj root, Obj services, Ref& renderer, const char*& reason, float& rejectedAlpha) {
    rejectedAlpha = -1;
    reason = "part-type";
    if (!Is(part, methods.entity)) return false;
    reason = "part-root"; if (Read<Obj>(part, 0x1A8) != root) return false;
    reason = "part-services"; if (Read<Obj>(part, 0x398) != services) return false;
    reason = "part-hidden"; if (!api.Value<bool>(api.Invoke(methods.visible, part))) return false;
    reason = "part-alpha";
    rejectedAlpha = api.Value<float>(api.Invoke(methods.alpha, part));
    if (!VisibleOpacity(rejectedAlpha)) return false;
    rejectedAlpha = -1;
    Ref animator(Read<Obj>(part, 0xA8));
    renderer.reset(Read<Obj>(part, 0x1E8));
    reason = "animator-type"; if (!Is(animator.get(), methods.animator)) return false;
    reason = "renderer-type"; if (!Is(renderer.get(), methods.renderer)) return false;
    reason = "destroyed-source"; if (!Exists(animator.get()) || !Exists(renderer.get())) return false;
    reason = "animator-alpha";
    rejectedAlpha = Read<float>(animator.get(), 0x40);
    if (!VisibleOpacity(rejectedAlpha)) return false;
    rejectedAlpha = -1;
    reason = "renderer-disabled"; if (!api.Value<bool>(api.Invoke(methods.enabled, renderer.get()))) return false;
    Ref object(api.Invoke(methods.gameObject, renderer.get()));
    reason = "inactive-source";
    return Exists(object.get()) && api.Value<bool>(api.Invoke(methods.active, object.get()));
}
bool Collect(Obj root, Obj services, Target& target, const char*& lastRejected, float& lastRejectedAlpha) {
    std::vector<Ref> pending;
    pending.emplace_back(root);
    std::unordered_set<Obj> parts{root}, renderers;
    for (size_t at = 0; at < pending.size(); ++at) {
        auto part = pending[at].get();
        Ref renderer;
        const char* reason = "none";
        float rejectedAlpha = -1;
        if (!VisiblePart(part, root, services, renderer, reason, rejectedAlpha)) {
            lastRejected = reason;
            lastRejectedAlpha = rejectedAlpha;
            if (part == root) return false;
            continue;
        }
        if (renderers.insert(renderer.get()).second) target.renderers.push_back(std::move(renderer));
        Ref children(api.Invoke(methods.children, part));
        if (!children.get()) continue;
        Ref iterator(api.Call(children.get(), "GetEnumerator"));
        if (!iterator.get()) return false;
        try {
            size_t count = 0;
            while (api.Value<bool>(api.Call(iterator.get(), "MoveNext"))) {
                if (++count > MaxParts) { api.Call(iterator.get(), "Dispose"); return false; }
                Ref child(api.Call(iterator.get(), "get_Current"));
                if (!Is(child.get(), methods.entity) || Read<Obj>(child.get(), 0x1A8) != root ||
                    Read<Obj>(child.get(), 0x398) != services || !parts.insert(child.get()).second) continue;
                if (parts.size() > MaxParts) { api.Call(iterator.get(), "Dispose"); return false; }
                pending.push_back(std::move(child));
            }
        } catch (...) {
            try { api.Call(iterator.get(), "Dispose"); } catch (...) {}
            throw;
        }
        api.Call(iterator.get(), "Dispose");
    }
    return !target.renderers.empty();
}
}

void InstallEntities() {
    if (methods.installed) return;
    methods.information = api.Class("Core.dll", "Core.Services.Entities.EntityInformationServices", "EntityInfo");
    methods.registry = api.Class("Core.dll", "", "evv");
    methods.entity = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    methods.creature = api.Class("Core.dll", "", "exf");
    methods.fight = api.Class("Core.dll", "", "fxc");
    methods.context = api.Class("Core.dll", "", "fyb");
    methods.rendered = api.Class("Core.dll", "", "fxw");
    methods.world = api.Class("Core.dll", "", "eun");
    methods.combatRegistry = api.Class("Core.dll", "", "ebm");
    methods.animator = api.Class("Ankama.Animator2D.dll", "Ankama.Animations", "Animator2D");
    methods.renderer = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
    methods.list = Verify(methods.registry, "baml", 0, 0x19579E0);
    methods.findInfo = Verify(methods.registry, "bamm", 2, 0x1957A70);
    methods.id = Verify(methods.information, "bajh", 0, 0x1951440);
    methods.flag = Verify(methods.information, "bajs", 1, 0x19518F0);
    methods.boolTry = Verify(methods.information, "bakd", 2, 0x1951D50);
    methods.byteTry = Verify(methods.information, "bajy", 2, 0x1951B60);
    methods.intTry = Verify(methods.information, "baks", 2, 0x1952330);
    methods.findFight = Verify(methods.combatRegistry, "vkq", 1, 0x27695E0);
    methods.findWorld = Verify(methods.world, "zzm", 2, 0x190FE40);
    methods.visible = Verify(methods.entity, "qfv", 0, 0x260F8D0);
    methods.alpha = Verify(methods.entity, "rgq", 0, 0x26172B0);
    auto animated = api.Class("Ankama.Dofus.Core.Characters.dll", "CoreAnimated", "Animated");
    methods.children = Verify(animated, "dayr", 0, 0x782BE0);
    auto object = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    auto renderer = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer");
    auto component = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
    auto gameObject = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    methods.exists = Verify(object, "op_Implicit", 1, 0x5439190);
    methods.enabled = Verify(renderer, "get_enabled", 0, 0x53F2300);
    methods.gameObject = Verify(component, "get_gameObject", 0, 0x5429D00);
    methods.active = Verify(gameObject, "get_activeInHierarchy", 0, 0x5432080);
    methods.installed = true;
}

Snapshot ReadEntities() {
    Snapshot result;
    result.reason = "session";
    if (!methods.installed || !module || !module->servicesReady || module->stopped ||
        !module->compositor.get() || !Is(module->creatures.get(), methods.creature) ||
        !Is(module->fight.get(), methods.fight)) return result;
    try {
        Owners owners;
        owners.compositor.reset(module->compositor.get()); owners.creature.reset(module->creatures.get());
        owners.fight.reset(module->fight.get()); owners.combat = module->InCombat();
        result.combat = owners.combat;
        owners.services.reset(Read<Obj>(owners.creature.get(), 0x10));
        result.reason = "services";
        if (!owners.services.get()) return result;
        if (owners.combat) {
            owners.context.reset(Read<Obj>(owners.fight.get(), 0x18));
            result.reason = "fight-context";
            if (!Is(owners.context.get(), methods.context)) return result;
            owners.registry.reset(Read<Obj>(owners.context.get(), 0x58));
            owners.rendered.reset(Read<Obj>(owners.context.get(), 0x68));
            result.reason = "fight-render-context";
            if (!Is(owners.rendered.get(), methods.rendered)) return result;
            owners.world.reset(Read<Obj>(owners.rendered.get(), 0x150));
            result.reason = "fight-render-registry";
            if (!Is(owners.world.get(), methods.combatRegistry)) return result;
        } else {
            owners.registry.reset(module->FindService("fso")); owners.world.reset(module->FindService("fsi"));
            result.reason = "roleplay-render-registry";
            if (!Is(owners.world.get(), methods.world)) return result;
        }
        result.reason = "information-registry";
        if (!Is(owners.registry.get(), methods.registry)) return result;
        result.reason = "registry-ownership";
        if (!owners.Current()) return result;
        Ref list(api.Invoke(methods.list, owners.registry.get()));
        result.reason = "roster-list";
        if (!list.get()) return result;
        int count = api.Value<int>(api.Call(list.get(), "get_Count"));
        result.rosterCount = count; result.reason = "roster-limit";
        if (count < 0 || count > MaxRoster) return result;
        // The category of every roster entry first (flags and IDs only), then
        // the rendered roots in budget order: the accepted targets go to the
        // first categories, whatever the roster order of a crowded map.
        const auto& settings = module->settings;
        const bool players = !owners.combat && (settings.glowSelf || settings.glowParty || settings.glowPlayers);
        const auto identity = players ? ReadIdentity() : Features::RoleplayIdentity{};
        std::vector<Candidate> candidates;
        for (int i = 0; i < count; ++i) {
            if (!owners.Current()) return {};
            Ref info(api.Call(list.get(), "get_Item", {&i}));
            GlowCategory category = GlowCategory::Monster;
            if (settings.glowMonsters && Monster(info.get(), owners.combat)) ++result.monsterCount;
            else if (settings.glowNpcs && Npc(info.get(), owners.combat)) { category = GlowCategory::Npc; ++result.npcCount; }
            else if (identity.self && Is(info.get(), methods.information)) {
                // The ID first, then the player flags only for a ticked
                // category: with « Moi » alone, each other player of a crowded
                // map costs one call (its ID) instead of six.
                const auto id = api.Value<int64_t>(api.Invoke(methods.id, info.get()));
                category = id == identity.self ? GlowCategory::Self :
                    std::binary_search(identity.party.begin(), identity.party.end(), id) ? GlowCategory::Party : GlowCategory::Player;
                if (!(category == GlowCategory::Self ? settings.glowSelf :
                      category == GlowCategory::Party ? settings.glowParty : settings.glowPlayers) ||
                    !Player(info.get(), owners.combat)) continue;
                ++result.playerCount;
            } else continue;
            candidates.push_back({std::move(info), category});
        }
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.category < b.category; });
        std::unordered_set<Obj> roots;
        for (const auto& candidate : candidates) {
            if (result.targets.size() >= MaxTargets) break;
            if (!owners.Current()) return {};
            const auto info = candidate.info.get();
            int64_t id = api.Value<int64_t>(api.Invoke(methods.id, info));
            if (!id) continue;
            Obj current = nullptr;
            if (!api.Value<bool>(api.Invoke(methods.findInfo, owners.registry.get(), {&id, &current})) ||
                current != info) continue;
            Ref entity(owners.Find(id));
            if (!Is(entity.get(), methods.entity) || Read<int64_t>(entity.get(), 0x1C8) != id ||
                !roots.insert(entity.get()).second) continue;
            ++result.matchedRoots;
            Target target; target.id = id; target.category = candidate.category; target.entity = std::move(entity);
            if (!Collect(target.entity.get(), owners.services.get(), target, result.lastRejectedPart, result.lastRejectedAlpha)) continue;
            if (!owners.Current()) return {};
            if (owners.Find(id) != target.entity.get()) continue;
            result.targets.push_back(std::move(target));
        }
        if (!owners.Current()) return {};
        result.context.reset(owners.compositor.get()); result.combat = owners.combat; result.valid = true;
        result.reason = result.targets.empty() ? "no-visible-actors" : "ready";
        reported = false;
    } catch (const std::exception& error) {
        if (!reported) Log(std::string("MONSTER_GLOW_ENTITIES_ERROR ") + error.what());
        reported = true; return {};
    }
    return result;
}
}
