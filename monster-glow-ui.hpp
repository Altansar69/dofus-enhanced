#pragma once
#include "monster-glow-render.hpp"
#include <vector>

namespace monsterGlow {
// UI rectangles use panel coordinates (origin at the top left). ScreenRect in
// the renderer contract uses physical screen pixels, origin at the bottom left.
struct UiRect { float x = 0, y = 0, width = 0, height = 0; };
struct UiFrame {
    managed::Ref camera, panel, root;
    ScreenRect screenViewport{};
    UiRect panelViewport{};
    uint64_t generation = 0, serial = 0;
    bool valid = false;
    const char* reason = "unavailable";
    size_t paintCount = 0, visitedNodes = 0;
    bool scanLimited = false;
};
struct UiProjection {
    ScreenRect screenRect{};
    UiRect panelRect{};
    std::vector<UiRect> clipRects;
    uint64_t generation = 0, serial = 0;
    bool valid = false;
    const char* reason = "unavailable";
};
void InstallGlowUi();
UiFrame BeginGlowUi(uint64_t nowMs = GetTickCount64());
UiProjection ProjectGlowRenderers(const UiFrame& frame, const std::vector<managed::Obj>& renderers, bool everywhere = false);
bool ShowGlowUi(const UiFrame& frame, int64_t id, const UiProjection& projection, managed::Obj texture);
void EndGlowUi();
// Call before releasing render textures, on disable, session/context changes,
// and shutdown. Only this module's noninteractive overlay is removed.
void ResetGlowUi();
// A failed native detach keeps the owned branch for a bounded controller retry.
// Keep render textures alive until this becomes false.
bool GlowUiCleanupPending();
// Read-only resolved layout counters; callers throttle these diagnostics.
std::string DescribeGlowUi();
}
