#include "appearance/features.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <intrin.h>
#include "appearance/size-policy.hpp"
#include "animations/instant-combat.hpp"
#include "appearance/appearance-refresh.hpp"

using namespace managed;
using ConvertFn = void(*)(Obj, Obj, Obj*, Method);
static ConvertFn originalConvert;
using CreatureLookFn = Obj(*)(Obj, Method);
static CreatureLookFn originalCreatureLook;
using CreatureHideFn = bool(*)(Obj, Method);
static CreatureHideFn originalCreatureHide, originalCreatureForcedHide;
static void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset)
        throw std::runtime_error(std::string("Appearance field layout: ") + name);
}

static bool CreatureHideResult(Obj entity, bool nativeHidden, uintptr_t caller) {
    if (nativeHidden && module && module->ready.load() && !module->stopped) {
        try {
            if (module->features->PreserveMiniatureAttachmentVisibility(entity, caller)) return false;
        } catch (const std::exception& error) {
            Log(std::string("MINIATURE_VISIBILITY_ERROR ") + error.what());
            module->stopRequested.store(true);
        }
    }
    return nativeHidden;
}
static bool CreatureHideHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return CreatureHideResult(entity, originalCreatureHide(entity, method), caller);
}
static bool CreatureForcedHideHook(Obj entity, Method method) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return CreatureHideResult(entity, originalCreatureForcedHide(entity, method), caller);
}

static void ConvertHook(Obj service, Obj info, Obj* look, Method method) {
    if (module && module->ready.load() && !module->stopped) {
        try {
            if (module->features->Convert(service, info, look)) return;
        } catch (const std::exception& error) {
            Log(std::string("MINIATURE_ERROR ") + error.what());
            module->stopRequested.store(true);
        }
    }
    originalConvert(service, info, look, method);
}
static Obj CreatureLookHook(Obj entity, Method method) {
    uint64_t generation = 0;
    if (module && module->ready.load()) {
        try {
            // Stop restores the native appearance before removing hooks, so
            // cache invalidation must remain active while stopped is true.
            generation = module->features->PrepareCreatureLook(entity);
            if (!module->stopped) {
                if (auto look = module->features->SubentityLook(entity)) return look;
            }
        } catch (const std::exception& error) {
            Log(std::string("MINIATURE_LOOK_ERROR ") + error.what());
            module->stopRequested.store(true);
        }
    }
    auto result = originalCreatureLook(entity, method);
    if (generation) {
        try { module->features->RememberCreatureLook(entity, generation); }
        catch (const std::exception& error) {
            Log(std::string("MINIATURE_LOOK_CACHE_ERROR ") + error.what());
            module->stopRequested.store(true);
        }
    }
    return result;
}
void Features::Install() {
    auto converter = api.Class("Core.dll", "", "exf");
    auto look = api.Class("Ankama.Dofus.Core.Characters.dll", "Core.Rendering.Look", "EntityLook");
    auto info = api.Class("Core.dll", "Core.Services.Entities.EntityInformationServices", "EntityInfo");
    lookAnimatedEntityClass = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    entityLookClass = look;
    lookEntityRegistryClass = api.Class("Core.dll", "", "evv");
    clearCreatureLook = api.Find(lookAnimatedEntityClass, "rfa", 0);
    api.Native(clearCreatureLook, 0x2615260);
    cloneLook = api.Find(look, "dbfj", 0);
    scaleX = api.Find(look, "dbev", 1);
    scaleY = api.Find(look, "dbex", 1);
    flagsTest = api.Find(info, "bajs", 1);
    excludedTest = api.Find(info, "bake", 1);
    teamTryGet = api.Find(info, "bajy", 2);
    fighterTryGet = api.Find(api.Class("Core.dll", "", "evv"), "bamm", 2);
    localIdGet = api.Find(api.Class("Core.dll", "", "esg"), "zcx", 0);
    refreshCreatures = api.Find(converter, "baza", 1);
    api.Native(cloneLook, 0x79A3D0); api.Native(scaleX, 0x799F70); api.Native(scaleY, 0x79A060);
    api.Native(flagsTest, 0x19518F0); api.Native(excludedTest, 0x1951DB0);
    api.Native(refreshCreatures, 0x1975060);
    api.Native(teamTryGet, 0x1951B60);
    api.Native(fighterTryGet, 0x1957A70);
    api.Native(localIdGet, 0x18BB140);
    // « Apparence » categories. EntityInfo.bajh is the entity ID; balc reads
    // long metadata, where gwg.bmnt stores the summoner under key 2 next to the
    // summoned flag (bool key 1). Party (fti, service fne): bfmh lists its lg
    // members, oab is a member's ID and lg.ddku marks an accepted member.
    infoIdGet = api.FindVerified(info, "bajh", 0, 0x1951440);
    summonerTryGet = api.FindVerified(info, "balc", 2, 0x1952710);
    characterClass = api.Class("Core.dll", "", "esg");
    partyClass = api.Class("Core.dll", "", "fti");
    partyMemberClass = api.Class("Core.dll", "", "lg");
    partyMembersGet = api.FindVerified(partyClass, "bfmh", 0, 0x752E20);
    partyMemberIdGet = api.FindVerified(partyMemberClass, "oab", 0, 0x68D2B0);
    RequireField(partyMemberClass, "ddku", 0x20);
    sizes.Install();
    AddHook(api.Find(converter, "bazg", 2), 0x19755D0, reinterpret_cast<void*>(ConvertHook), reinterpret_cast<void**>(&originalConvert));
    AddHook(api.Find(lookAnimatedEntityClass, "rez", 0), 0x2614FF0, reinterpret_cast<void*>(CreatureLookHook), reinterpret_cast<void**>(&originalCreatureLook));
    // These native predicates suppress attached renderers specifically for the
    // flattened creature appearance. Miniatures retains the attached looks.
    auto visibilityPolicy = api.Class("Core.dll", "", "yh");
    api.Native(api.Find(lookAnimatedEntityClass, "rgx", 0), 0x26173C0);
    AddHook(api.Find(visibilityPolicy, "rmo", 1), 0x2652CF0, reinterpret_cast<void*>(CreatureHideHook), reinterpret_cast<void**>(&originalCreatureHide));
    AddHook(api.Find(visibilityPolicy, "rmp", 1), 0x2652E50, reinterpret_cast<void*>(CreatureForcedHideHook), reinterpret_cast<void**>(&originalCreatureForcedHide));
    InstallCombatAppearanceRefresh();
    InstallAnimation();
}
void Features::InvalidateLookCache() {
    lookCacheStamps.clear();
    ++lookCacheGeneration;
    lookCachePolicyKnown = false;
}
void Features::SyncLookCachePolicy() {
    LookCachePolicy current;
    current.service = module->creatures.get();
    current.context = current.service ? Read<Obj>(current.service, 0x10) : nullptr;
    current.combat = module->InCombat();
    current.mode = ActiveDisplayMode(module->settings, current.combat);
    current.categories = MiniatureCategoryMask(module->settings);
    current.stopped = module->stopped.load();
    current.active = current.service && Read<bool>(current.service, 0x48);
    if (!lookCachePolicyKnown || !(current == lookCachePolicy)) {
        InvalidateLookCache();
        lookCachePolicy = current;
        lookCachePolicyKnown = true;
    }
}
bool Features::IsRootEntity(Obj entity) const {
    return module->servicesReady && lookCachePolicy.context && entity &&
        api.class_is_assignable_from(lookAnimatedEntityClass, api.object_get_class(entity)) &&
        Read<Obj>(entity, 0x1A8) == entity && Read<Obj>(entity, 0x398) == lookCachePolicy.context;
}
uint64_t Features::PrepareCreatureLook(Obj entity) {
    SyncLookCachePolicy();
    if (!IsRootEntity(entity)) return 0;
    const auto cachedLook = Read<Obj>(entity, 0x310);
    // rez returns +0x310 before inspecting the mode or source appearance. A
    // preloaded or previous-generation cache must be cleared through native rfa.
    if (cachedLook && !lookCacheStamps.contains(cachedLook))
        api.Invoke(clearCreatureLook, entity);
    return lookCacheGeneration;
}
void Features::RememberCreatureLook(Obj entity, uint64_t generation) {
    SyncLookCachePolicy();
    if (lookCachePolicy.stopped || generation != lookCacheGeneration || !IsRootEntity(entity)) return;
    const auto cachedLook = Read<Obj>(entity, 0x310);
    if (!cachedLook || lookCacheStamps.contains(cachedLook)) return;
    if (!api.class_is_assignable_from(entityLookClass, api.object_get_class(cachedLook))) return;
    if (lookCacheStamps.size() >= MaxLookCacheStamps) lookCacheStamps.erase(lookCacheStamps.begin());
    // Retain the look identity, never the renderer. Evicted entries are simply
    // recomputed on their next read; pointer reuse cannot validate an old stamp.
    lookCacheStamps.emplace(cachedLook, Ref(cachedLook));
}
Obj Features::SubentityLook(Obj entity) const {
    if (!module->servicesReady || ActiveDisplayMode(module->settings, module->InCombat()) != 2)
        return nullptr;
    auto service = module->creatures.get();
    if (!service || !Read<bool>(service, 0x48)) return nullptr;
    const auto is = [](Obj object, Klass type) {
        return object && api.class_is_assignable_from(type, api.object_get_class(object));
    };
    if (!is(entity, lookAnimatedEntityClass)) return nullptr;
    Ref root(Read<Obj>(entity, 0x1A8));
    if (root.get() == entity || !is(root.get(), lookAnimatedEntityClass) ||
        Read<Obj>(root.get(), 0x1A8) != root.get()) return nullptr;
    // An attachment must belong to this compositor's root and creature service.
    const auto context = Read<Obj>(service, 0x10);
    if (!context || Read<Obj>(entity, 0x398) != context || Read<Obj>(root.get(), 0x398) != context)
        return nullptr;
    auto rootId = Read<int64_t>(root.get(), 0x1C8);
    if (Read<int64_t>(entity, 0x1C8) == rootId) return nullptr;
    Ref registry(module->FindService("fso"));
    if (!is(registry.get(), lookEntityRegistryClass)) return nullptr;
    Obj found = nullptr;
    if (!api.Value<bool>(api.Invoke(fighterTryGet, registry.get(), {&rootId, &found})) || !found)
        return nullptr;
    Ref info(found);
    int exclusion = 9;
    if (api.Value<bool>(api.Invoke(excludedTest, info.get(), {&exclusion}))) return nullptr;
    auto look = Read<Obj>(entity, 0x170);
    if (!is(look, entityLookClass) || Read<int>(look, 0x1C) <= 0) return nullptr;
    const float x = Read<float>(look, 0x2C), y = Read<float>(look, 0x30);
    if (!std::isfinite(x) || !std::isfinite(y) || x == 0.0f || y == 0.0f)
        return nullptr;
    // Native rez substitutes root.daxq() for every attachment: correct only for
    // flattened creature looks. Miniatures preserves the mounted hierarchy, so
    // retain each child's authored look. Native rha controls attachment scale;
    // it compensates for parent lossyScale, so parenting alone does not prove
    // that a child inherits the root's miniature factor.
    // The child's +0x170 owns this object; do not write either original or cache.
    return look;
}
bool Features::PreserveMiniatureAttachmentVisibility(Obj entity, uintptr_t caller) const {
    // rmo is called from rgx after native visibility policies were evaluated;
    // rmp is called by ric to set its creature-only forced-hidden flag. Other
    // callers and all native modes keep the original predicates unchanged.
    if (!module || !module->ready.load() || module->stopped ||
        (caller != gameBase + 0x2617618 && caller != gameBase + 0x2619FDB)) return false;
    return SubentityLook(entity) != nullptr;
}
bool Features::Convert(Obj service, Obj info, Obj* look) {
    const bool combat = module->InCombat();
    if (ActiveDisplayMode(module->settings, combat) != 2 || !service || !info || !look || !*look) return false;
    if (service != module->creatures.get() || !Read<bool>(service, 0x48)) return false;
    int exclusion = 9;
    if (api.Value<bool>(api.Invoke(excludedTest, info, {&exclusion}))) return false;
    int playerFlag = 0x80, monsterFlag = 8, summonKey = 1;
    const bool player = api.Value<bool>(api.Invoke(flagsTest, info, {&playerFlag}));
    const bool summon = api.Value<bool>(api.Invoke(excludedTest, info, {&summonKey}));
    const bool monster = summon || (!player && api.Value<bool>(api.Invoke(flagsTest, info, {&monsterFlag})));
    // « Apparence »: only a checked category is reduced. The others keep the
    // native appearance, as an unclassified fighter always did (factor 1).
    const auto group = combat ? Group(info) : RoleplayGroup(info, player, monster);
    const bool reduced = MiniatureGroupEnabled(module->settings, group);
    const int percent = reduced ? SimpleReducedPercent : 100;
    const float naturalHeight = sizes.Measure(*look, player && !summon).value_or(0);
    auto factor = SizeFactor(percent, reduced ? SimpleMinimumMonsterPercent : 0, monster, naturalHeight, sizes.playerHeight);
    if (reduced && monster &&
        (naturalHeight <= 0 || sizes.playerHeight <= 0) && !loggedPendingSize) {
        Log("SIZE_MEASUREMENTS_PENDING proportionalReductionAvailable=1 reference=" +
            std::to_string(sizes.playerHeight) + " model=" + std::to_string(naturalHeight));
        loggedPendingSize = true;
    }
    // Reduction works before measurements load; refresh when the floor is ready.
    if (!factor || *factor == 1.0f) return true;
    Ref copy(api.Invoke(cloneLook, *look));
    float x = Read<float>(copy.get(), 0x2C) * *factor;
    float y = Read<float>(copy.get(), 0x30) * *factor;
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::runtime_error("Non-finite entity scale");
    api.Invoke(scaleX, copy.get(), {&x});
    api.Invoke(scaleY, copy.get(), {&y});
    // look can point directly into the owner's cached-look field: preserve the GC write barrier.
    api.gc_wbarrier_set_field(nullptr, look, copy.get());
    ++miniatureCount;
    return true;
}
void Features::Refresh(bool active) {
    // baza emits synchronous callbacks. Clear first so their rez calls cannot
    // observe a cache stamped under the previous size policy or display mode.
    InvalidateLookCache();
    if (module->creatures.get()) api.Invoke(refreshCreatures, module->creatures.get(), {&active});
    // Both creature and miniature modes set the same native bool. Explicitly
    // rebuild current combat roots instead of depending on event deduplication.
    RefreshCombatAppearance();
}
Features::LocalFighterIdentity Features::LocalFighter() const {
    LocalFighterIdentity unknown;
    if (!module->InCombat()) return unknown;
    auto state = Read<Obj>(module->fight.get(), 0x18); // fxc.dvzu : fyb
    if (!state) return unknown;
    if (std::string(api.class_get_name(api.object_get_class(state))) != "fyb") return unknown;
    auto character = Read<Obj>(state, 0xF0);
    auto fighters = Read<Obj>(state, 0x58);
    if (!character || !fighters) return unknown;
    if (std::string(api.class_get_name(api.object_get_class(character))) != "esg" ||
        std::string(api.class_get_name(api.object_get_class(fighters))) != "evv") return unknown;
    if (!Read<Obj>(character, 0x128)) return unknown;
    auto id = api.Value<int64_t>(api.Invoke(localIdGet, character));
    Obj local = nullptr;
    if (!api.Value<bool>(api.Invoke(fighterTryGet, fighters, {&id, &local})) || !local) return unknown;
    Ref held(local);
    int key = 6;
    int8_t team = -1;
    if (!api.Value<bool>(api.Invoke(teamTryGet, local, {&key, &team}))) return unknown;
    if (team < 0 || team > 3) return unknown;
    return {team, id};
}
int Features::LocalTeam() const { return LocalFighter().team; }
MiniatureGroup Features::Group(Obj info) const {
    int fighterFlag = 1, playerFlag = 0x80, monsterFlag = 8, summonKey = 1, teamKey = 6;
    if (!api.Value<bool>(api.Invoke(flagsTest, info, {&fighterFlag}))) return MiniatureGroup::Unknown;
    int8_t team = -1;
    if (!api.Value<bool>(api.Invoke(teamTryGet, info, {&teamKey, &team}))) return MiniatureGroup::Unknown;
    const auto local = LocalFighter();
    if (local.team < 0) return MiniatureGroup::Unknown;
    bool summon = api.Value<bool>(api.Invoke(excludedTest, info, {&summonKey}));
    bool player = api.Value<bool>(api.Invoke(flagsTest, info, {&playerFlag}));
    bool monster = api.Value<bool>(api.Invoke(flagsTest, info, {&monsterFlag}));
    // Own fighter and own summons, read only when their checkbox differs from
    // the other allies' one. A missing summoner keeps the allied summon group.
    bool self = false, ownSummon = false;
    const auto& settings = module->settings;
    if (team == local.team && local.id != 0) {
        if (summon && CombatOwnSummonMatters(settings)) {
            int summonerKey = 2;
            int64_t summoner = 0;
            ownSummon = api.Value<bool>(api.Invoke(summonerTryGet, info, {&summonerKey, &summoner})) && summoner == local.id;
        } else if (!summon && player && CombatSelfMatters(settings))
            self = api.Value<int64_t>(api.Invoke(infoIdGet, info)) == local.id;
    }
    return ClassifyMiniature(player, monster, summon, team, local.team, self, ownSummon);
}
Features::RoleplayIdentity Features::ReadRoleplayIdentity() const {
    RoleplayIdentity identity;
    Ref character(module->FindService("fng"));
    if (character.get() && api.object_get_class(character.get()) == characterClass && Read<Obj>(character.get(), 0x128))
        identity.self = api.Value<int64_t>(api.Invoke(localIdGet, character.get()));
    Ref party(module->FindService("fne"));
    if (!party.get() || api.object_get_class(party.get()) != partyClass) return identity;
    Ref members(api.Invoke(partyMembersGet, party.get()));
    if (!members.get()) return identity;
    const int count = api.Value<int>(api.Call(members.get(), "get_Count"));
    if (count < 0 || count > 128) throw std::runtime_error("Unexpected party member count");
    for (int index = 0; index < count; ++index) {
        Ref member(api.Call(members.get(), "get_Item", {&index}));
        // Accepted members only (lg.ddku): an invited guest is not in the party.
        if (!member.get() || api.object_get_class(member.get()) != partyMemberClass || !Read<bool>(member.get(), 0x20)) continue;
        const auto id = api.Value<int64_t>(api.Invoke(partyMemberIdGet, member.get()));
        if (id > 0) identity.party.push_back(id);
    }
    std::sort(identity.party.begin(), identity.party.end());
    identity.party.erase(std::unique(identity.party.begin(), identity.party.end()), identity.party.end());
    return identity;
}
const Features::RoleplayIdentity& Features::CurrentRoleplayIdentity() {
    if (!roleplayIdentityKnown) {
        roleplayIdentity = ReadRoleplayIdentity();
        roleplayIdentityKnown = true;
        nextRoleplayIdentityRead = GetTickCount64() + 1000;
    }
    return roleplayIdentity;
}
// Out of combat with Miniatures and an unticked player category: a party or
// character change re-renders the players, read at most once a second.
void Features::TrackRoleplayIdentity() {
    const auto now = GetTickCount64();
    if (roleplayIdentityKnown && now < nextRoleplayIdentityRead) return;
    nextRoleplayIdentityRead = now + 1000;
    auto current = ReadRoleplayIdentity();
    const bool changed = roleplayIdentityKnown && !(current == roleplayIdentity);
    roleplayIdentity = std::move(current);
    roleplayIdentityKnown = true;
    if (changed) Refresh(true);
}
MiniatureGroup Features::RoleplayGroup(Obj info, bool player, bool monster) {
    bool self = false, partyMember = false;
    if (!monster && RoleplayIdentityMatters(module->settings)) {
        const auto& identity = CurrentRoleplayIdentity();
        const auto id = api.Value<int64_t>(api.Invoke(infoIdGet, info));
        self = identity.self != 0 && id == identity.self;
        partyMember = player && !self && std::binary_search(identity.party.begin(), identity.party.end(), id);
    }
    return ClassifyRoleplayMiniature(player, monster, self, partyMember);
}
void Features::Tick() {
    const bool combat = module->InCombat();
    PumpInstantWaits(combat);
    const bool identity = !combat && module->settings.roleplayMode == 2 && RoleplayIdentityMatters(module->settings);
    if (ActiveDisplayMode(module->settings, combat) == 2) {
        if (sizes.Tick()) Refresh(true);
        if (identity) TrackRoleplayIdentity();
    } else sizes.pending.clear();
    // Read again once needed: no identity is kept while it is not used.
    if (!identity) roleplayIdentityKnown = false;
    const int team = LocalTeam();
    if (team != previousLocalTeam) {
        previousLocalTeam = team;
        if (module->InCombat() && module->settings.mode == 2) Refresh(true);
    }
}
