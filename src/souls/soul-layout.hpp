#pragma once
#include "core/runtime.hpp"

namespace soul {
struct LayoutPosition {
    int x = 24, y = 180;
    uint64_t revision = 0;
    bool writable = false;
};
void InstallLayout();
// Reads the native current/contextual disposition. Missing entries use the
// fixed default, never the last position from another disposition.
LayoutPosition ReadLayout(managed::Obj root);
// Only call for a completed user drag, with the revision captured at drag start.
bool SaveLayoutPosition(managed::Obj root, int x, int y, uint64_t expectedRevision);
// Session/disable cleanup only. Hiding the panel must not clear native records.
void ResetLayout();
}
