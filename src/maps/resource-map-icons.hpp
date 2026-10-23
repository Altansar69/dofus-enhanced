#pragma once
#include "core/runtime.hpp"
#include "maps/resource-map-data.hpp"

namespace resourceMap {
inline int RepresentativeResourceIcon(std::span<const Resource> catalog, int jobId) {
    const Resource* first = nullptr;
    for (const auto& resource : catalog) {
        if (resource.jobId != jobId || resource.iconId <= 0) continue;
        if (!first || resource.level < first->level ||
            (resource.level == first->level && resource.itemId < first->itemId))
            first = &resource;
    }
    return first ? first->iconId : 0;
}
// Reuse the game's item catalog and asynchronous sprite cache. No web images,
// inventory objects or unmanaged completion callbacks are created.
void InstallIcons();
managed::Obj ItemIconAddress(int iconId);
void LoadItemIcon(managed::Obj element, int iconId);
void LoadJobIcon(managed::Obj element, int jobId);
}
