#pragma once
#include "runtime.hpp"

namespace monsterGlow {
struct ScreenRect { float x = 0, y = 0, width = 0, height = 0; };
// Optional union mask: actual foreground decor alpha plus HUD rectangles.
// Rectangles use normalized bottom-left coordinates in the capture texture.
// The caller retains renderer references until the synchronous capture returns.
struct GlowCoverage {
    std::vector<managed::Obj> renderers;
    std::vector<ScreenRect> rectangles;
};
void InstallGlowRender();
// Rectangle uses Unity bottom-left screen pixels. Callers retain camera and
// renderer references for the call. Texture ownership remains with this slot.
managed::Obj RenderGlow(size_t slot, managed::Obj camera,
    const std::vector<managed::Obj>& renderers, ScreenRect rectangle, uint64_t nowMs = GetTickCount64(),
    const GlowCoverage* coverage = nullptr);
void ReleaseGlowSlot(size_t slot);
void ResetGlowRender();
}
