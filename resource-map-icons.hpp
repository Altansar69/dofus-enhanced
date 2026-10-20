#pragma once
#include "runtime.hpp"

namespace resourceMap {
// Reuse the game's item catalog and asynchronous sprite cache. No web images,
// inventory objects or unmanaged completion callbacks are created.
void InstallIcons();
managed::Obj ItemIconAddress(int iconId);
void LoadItemIcon(managed::Obj element, int iconId);
}
