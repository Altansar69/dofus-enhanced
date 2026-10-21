#include "map-motion.hpp"
#include "notebook-native.hpp"
#include "module.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mapMotion {
namespace {
using namespace managed;
constexpr const char* UiAssembly = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr uint64_t CenterDuration = 2000;
constexpr float WheelShare = .3f;      // part of the remaining wheel step applied per frame
constexpr float WheelSettle = .02f;    // remainder (relative to the request) applied at once
constexpr double ZoomEpsilon = 1e-4;
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
using WheelFn = void(*)(Obj, float, Vec2, bool, Method);
using CenterFn = unsigned char(*)(Obj, Method);
WheelFn originalWheel = nullptr;
CenterFn originalCenter = nullptr;
struct Zooming { Ref view; float pending = 0, request = 0; Vec2 pivot{}; Method method = nullptr; };
struct Centering {
    Ref view, content, world;
    bool active = false;
    uint64_t start = 0;
    double zoomStart = 0, zoomTarget = 0, zoomApplied = 0;
    Vec2 centerStart{}, centerTarget{};
    Vec3 lastSet{};
    int targetX = 0, targetY = 0;
};
Klass viewClass{};
Method setZoom{}, moveTo{}, clamp{}, mapToPixel{};
std::vector<Zooming> zooms;
Centering centering;
bool installed = false, reported = false;
bool Ready() { return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested; }
void Report(const char* stage, const std::exception& error) {
    if (!reported) Log(std::string("MAP_MOTION_ERROR ") + stage + ": " + error.what());
    reported = true;
}
bool IsView(Obj view) { return view && viewClass && api.class_is_assignable_from(viewClass, api.object_get_class(view)); }
double Zoom(Obj view) { return Read<double>(view, 0x18); }
bool Finite(float v) { return std::isfinite(v) && std::fabs(v) < 1e7f; }
Vec3 Position(Obj content) { return api.Value<Vec3>(api.Call(content, "UnityEngine.UIElements.ITransform.get_position")); }
void SetPosition(Obj content, Vec3 position) { api.Call(content, "UnityEngine.UIElements.ITransform.set_position", {&position}); }
Vec2 Pixel(Obj world, float x, float y, double zoom) {
    return api.Value<Vec2>(api.Invoke(mapToPixel, nullptr, {&x, &y, world, &zoom}));
}
// Viewport size of the view's content parent, in that parent's units.
bool Viewport(Obj content, Vec2& size) {
    Ref parent(api.Call(content, "get_parent"));
    if (!parent.get()) return false;
    Ref resolved(api.Call(parent.get(), "get_resolvedStyle"));
    size = {api.Value<float>(api.Call(resolved.get(), "get_width")), api.Value<float>(api.Call(resolved.get(), "get_height"))};
    return Finite(size.x) && Finite(size.y) && size.x >= 8 && size.y >= 8;
}
bool Alive(const Ref& content) { return content.get() && api.Call(content.get(), "get_panel"); }
float Ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t < .5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
void Cancel() { centering = Centering{}; }
void StopZooming(Obj view) { std::erase_if(zooms, [&](const Zooming& z) { return z.view.get() == view; }); }

void WheelHook(Obj self, float delta, Vec2 pivot, bool force, Method method) {
    try {
        if (!Ready() || force || !originalWheel || !Finite(delta) || delta == 0) { originalWheel(self, delta, pivot, force, method); return; }
        if (centering.active && centering.view.get() == self) Cancel();
        auto found = std::find_if(zooms.begin(), zooms.end(), [&](const Zooming& z) { return z.view.get() == self; });
        if (found == zooms.end()) { if (zooms.size() >= 4) { originalWheel(self, delta, pivot, force, method); return; } zooms.emplace_back(); found = std::prev(zooms.end()); found->view.reset(self); }
        // Same direction: extend the glide. Opposite direction: start over from here.
        if (found->pending != 0 && (found->pending > 0) != (delta > 0)) found->pending = 0;
        found->pending += delta; found->request = found->pending; found->pivot = pivot; found->method = method;
    } catch (const std::exception& error) { Report("wheel", error); originalWheel(self, delta, pivot, force, method); }
}
unsigned char CenterHook(Obj self, Method method) {
    try { if (CenterOnPlayer(self)) return 0; }
    catch (const std::exception& error) { Report("center", error); }
    return originalCenter(self, method);
}
void TickZooms() {
    for (auto it = zooms.begin(); it != zooms.end();) {
        auto& z = *it;
        Ref content(z.view.get() ? api.Field(z.view.get(), "dugq") : nullptr);
        if (!Alive(content) || z.pending == 0) { it = zooms.erase(it); continue; }
        float step = z.pending * WheelShare;
        if (std::fabs(z.pending) <= std::fabs(z.request) * WheelSettle) step = z.pending;
        z.pending -= step;
        originalWheel(z.view.get(), step, z.pivot, false, z.method);
        if (z.pending == 0) it = zooms.erase(it); else ++it;
    }
}
void TickCentering(uint64_t now) {
    auto& c = centering;
    if (!c.active) return;
    if (!Alive(c.content) || !IsView(c.view.get()) || api.Field(c.view.get(), "dugq") != c.content.get()) { Cancel(); return; }
    const auto position = Position(c.content.get());
    // A drag or another native move since our last frame ends the glide there.
    if (std::fabs(position.x - c.lastSet.x) > .5f || std::fabs(position.y - c.lastSet.y) > .5f) { Cancel(); return; }
    if (!c.start) c.start = now;
    const float t = c.start + CenterDuration <= now ? 1.0f : float(now - c.start) / float(CenterDuration);
    const float e = Ease(t);
    if (c.zoomTarget != c.zoomStart) {
        const double zoom = c.zoomStart + (c.zoomTarget - c.zoomStart) * e;
        if (std::fabs(zoom - c.zoomApplied) > ZoomEpsilon || t >= 1) { float value = float(zoom); api.Invoke(setZoom, c.view.get(), {&value}); c.zoomApplied = zoom; }
    }
    const double zoom = Zoom(c.view.get());
    Vec2 viewport{};
    if (!std::isfinite(zoom) || zoom <= 0 || !Viewport(c.content.get(), viewport)) { Cancel(); return; }
    const auto origin = Pixel(c.world.get(), 0, 0, zoom), next = Pixel(c.world.get(), 1, 1, zoom);
    const Vec2 cell{next.x - origin.x, next.y - origin.y};
    if (!(cell.x > 0 && cell.y > 0)) { Cancel(); return; }
    const Vec2 centre{c.centerStart.x + (c.centerTarget.x - c.centerStart.x) * e, c.centerStart.y + (c.centerTarget.y - c.centerStart.y) * e};
    Vec3 target{viewport.x / 2 - (origin.x + centre.x * cell.x), viewport.y / 2 - (origin.y + centre.y * cell.y), 0};
    target = api.Value<Vec3>(api.Invoke(clamp, c.view.get(), {&target}));
    if (!Finite(target.x) || !Finite(target.y)) { Cancel(); return; }
    SetPosition(c.content.get(), target); c.lastSet = Position(c.content.get());
    if (t >= 1) {
        // Final placement through the game's own move, exactly as its button does.
        bool centre = true, autoZoom = false;
        api.Invoke(moveTo, c.view.get(), {&c.targetX, &c.targetY, &centre, &autoZoom});
        Cancel();
    }
}
}

bool CenterOnPlayer(Obj view) {
    if (!Ready() || !IsView(view)) return false;
    Ref content(api.Field(view, "dugq")), world(api.Field(view, "<dugi>k__BackingField"));
    if (!Alive(content) || !world.get()) return false;
    const double zoom = Zoom(view);
    Vec2 viewport{};
    if (!std::isfinite(zoom) || zoom <= 0 || !Viewport(content.get(), viewport)) return false;
    notebook::Entry here;
    try { here = notebook::CurrentPosition(); } catch (const std::exception&) { return false; }
    if (here.world != Read<int>(world.get(), 0x10)) return false;
    const float startScale = Read<float>(world.get(), 0x30);
    const auto origin = Pixel(world.get(), 0, 0, zoom), next = Pixel(world.get(), 1, 1, zoom);
    const Vec2 cell{next.x - origin.x, next.y - origin.y};
    if (!(cell.x > 0 && cell.y > 0)) return false;
    const auto position = Position(content.get());
    StopZooming(view);
    Centering c;
    c.view.reset(view); c.content.reset(content.get()); c.world.reset(world.get());
    c.zoomStart = c.zoomApplied = zoom;
    c.zoomTarget = Finite(startScale) && startScale > zoom ? startScale : zoom;
    c.centerStart = {(viewport.x / 2 - position.x - origin.x) / cell.x, (viewport.y / 2 - position.y - origin.y) / cell.y};
    c.centerTarget = {float(here.x), float(here.y)};
    c.targetX = here.x; c.targetY = here.y;
    const bool already = c.zoomTarget == c.zoomStart && std::fabs(c.centerStart.x - c.centerTarget.x) < .05f && std::fabs(c.centerStart.y - c.centerTarget.y) < .05f;
    if (already) { bool centre = true, autoZoom = false; api.Invoke(moveTo, view, {&c.targetX, &c.targetY, &centre, &autoZoom}); Cancel(); return true; }
    c.lastSet = position; c.active = true; c.start = 0;
    centering = std::move(c);
    Log("MAP_MOTION_CENTER zoom=" + std::to_string(zoom) + " target=" + std::to_string(centering.zoomTarget));
    return true;
}
void Install() {
    if (installed) return;
    viewClass = api.Class("Core.dll", "", "fhw");
    auto conversion = api.Class("Core.dll", "", "fhl");
    auto element = api.Class(UiAssembly, UiSpace, "VisualElement");
    setZoom = api.FindVerified(viewClass, "bexi", 1, 0x1C05470);
    moveTo = api.FindVerified(viewClass, "bexn", 4, 0x1C06060);
    clamp = api.FindVerified(viewClass, "bexc", 1, 0x1C04D20);
    mapToPixel = api.FindVerified(conversion, "bepx", 4, 0x1C4EB30);
    api.FindVerified(element, "UnityEngine.UIElements.ITransform.get_position", 0, 0x55F0F90);
    api.FindVerified(element, "UnityEngine.UIElements.ITransform.set_position", 1, 0x55F12D0);
    // Wheel zoom (delta, pivot, forced) and the player button's CenterToPlayerPosition.
    AddHook(api.FindVerified(viewClass, "bexl", 3, 0x1C05B90), 0x1C05B90, reinterpret_cast<void*>(&WheelHook), reinterpret_cast<void**>(&originalWheel));
    AddHook(api.FindVerified(viewClass, "beuu", 0, 0x1BFA420), 0x1BFA420, reinterpret_cast<void*>(&CenterHook), reinterpret_cast<void**>(&originalCenter));
    installed = true;
}
void Tick(uint64_t now) {
    if (!Ready()) { Restore(); return; }
    try { TickZooms(); } catch (const std::exception& error) { Report("zoom", error); zooms.clear(); }
    try { TickCentering(now); } catch (const std::exception& error) { Report("centering", error); Cancel(); }
}
void Restore() { zooms.clear(); Cancel(); }
bool CleanupPending() { return false; }
}
