#pragma once
#include "runtime.hpp"
#include "mode-policy.hpp"
#include "size-measurements.hpp"
#include <unordered_map>
#include <vector>

struct Features {
    managed::Klass lookAnimatedEntityClass{}, entityLookClass{}, lookEntityRegistryClass{};
    managed::Method cloneLook{}, scaleX{}, scaleY{}, flagsTest{}, excludedTest{}, refreshCreatures{};
    managed::Method teamTryGet{}, fighterTryGet{}, localIdGet{};
    managed::Method clearCreatureLook{};
    // « Apparence » categories: the fighter ID and summoner (EntityInfo long
    // metadata key 2), the local character service (esg) and the party (fti:
    // its accepted lg members).
    managed::Method infoIdGet{}, summonerTryGet{}, partyMembersGet{}, partyMemberIdGet{};
    managed::Klass characterClass{}, partyClass{}, partyMemberClass{};
    struct LookCachePolicy {
        managed::Obj service{}, context{};
        int mode = 0;
        unsigned categories = 0; // MiniatureCategoryMask
        bool combat = false, stopped = false, active = false;
        bool operator==(const LookCachePolicy&) const = default;
    };
    // The local fighter in the current combat: team -1 when unknown.
    struct LocalFighterIdentity { int team = -1; int64_t id = 0; };
    // Out of combat: the local character (0 when unknown) and the IDs of the
    // accepted members of its party, sorted. Read at most once a second.
    struct RoleplayIdentity {
        int64_t self = 0;
        std::vector<int64_t> party;
        bool operator==(const RoleplayIdentity&) const = default;
    };
    RoleplayIdentity roleplayIdentity;
    bool roleplayIdentityKnown = false;
    ULONGLONG nextRoleplayIdentityRead = 0;
    static constexpr size_t MaxLookCacheStamps = 4096;
    std::unordered_map<managed::Obj, managed::Ref> lookCacheStamps;
    LookCachePolicy lookCachePolicy;
    uint64_t lookCacheGeneration = 1;
    bool lookCachePolicyKnown = false;
    uint64_t miniatureCount = 0;
    int previousLocalTeam = -1;
    bool loggedPendingSize = false;
    SizeMeasurements sizes;
    void Install();
    void Tick();
    void Restore(bool clear = true);
    void Refresh(bool active);
    bool Convert(managed::Obj service, managed::Obj info, managed::Obj* look);
    managed::Obj SubentityLook(managed::Obj entity) const;
    bool PreserveMiniatureAttachmentVisibility(managed::Obj entity, uintptr_t caller) const;
    uint64_t PrepareCreatureLook(managed::Obj entity);
    void RememberCreatureLook(managed::Obj entity, uint64_t generation);
    void InvalidateLookCache();
    void SyncLookCachePolicy();
    bool IsRootEntity(managed::Obj entity) const;
    int LocalTeam() const;
    LocalFighterIdentity LocalFighter() const;
    MiniatureGroup Group(managed::Obj info) const;
    RoleplayIdentity ReadRoleplayIdentity() const;
    const RoleplayIdentity& CurrentRoleplayIdentity();
    void TrackRoleplayIdentity();
    MiniatureGroup RoleplayGroup(managed::Obj info, bool player, bool monster);
    void InstallAnimation();
};
