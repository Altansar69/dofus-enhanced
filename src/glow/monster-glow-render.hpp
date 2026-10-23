#pragma once
#include "core/runtime.hpp"
#include <cstring>

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
// The camera as the game renders it this frame: view and projection matrices
// and the pixel viewport. Any difference between two frames means the scene
// moved under the glow (zoom, pan), which the contour must follow at once.
struct CameraPose { float view[16]{}, projection[16]{}; ScreenRect rect{}; bool valid = false; };
CameraPose ReadCameraPose(managed::Obj camera);
inline bool SameCameraPose(const CameraPose& a, const CameraPose& b) {
    return a.valid == b.valid && std::memcmp(a.view, b.view, sizeof a.view) == 0 &&
        std::memcmp(a.projection, b.projection, sizeof a.projection) == 0 && std::memcmp(&a.rect, &b.rect, sizeof a.rect) == 0;
}
}
