#pragma once
#include "runtime.hpp"
#include "mode-policy.hpp"
#include "size-measurements.hpp"
#include <unordered_map>

struct Features {
    managed::Klass lookAnimatedEntityClass{}, entityLookClass{}, lookEntityRegistryClass{};
    managed::Method cloneLook{}, scaleX{}, scaleY{}, flagsTest{}, excludedTest{}, refreshCreatures{};
    managed::Method teamTryGet{}, fighterTryGet{}, localIdGet{};
    managed::Method clearCreatureLook{};
    struct LookCachePolicy {
        managed::Obj service{}, context{};
        int mode = 0;
        bool combat = false, stopped = false, active = false;
        bool operator==(const LookCachePolicy&) const = default;
    };
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
    MiniatureGroup Group(managed::Obj info) const;
    void InstallAnimation();
};
