#include "map-motion.hpp"
#include "notebook-native.hpp"
#include "module.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mapMotion {
namespace {
using namespace managed;
constexpr const char* UiAssembly = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr uint64_t WheelDuration = 220;    // one wheel notch, ease-out
constexpr uint64_t CenterDuration = 2000;  // centering on the player, ease-in-out
constexpr size_t MaxViews = 4;
constexpr double ZoomEpsilon = 1e-6;
constexpr float PositionEpsilon = .5f;
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
using WheelFn = void(*)(Obj, float, Vec2, bool, Method);
using CenterFn = unsigned char(*)(Obj, Method);
WheelFn originalWheel = nullptr;
CenterFn originalCenter = nullptr;
// One glide per cartography view. The game's state (zoom, content position)
// is always the *target*; only the picture lags behind through the content's
// transform scale (about its top-left corner) and transform position.
struct Motion {
    Ref view, content, originBackup;
    bool active = false, centering = false, originOwned = false;
    uint64_t start = 0, duration = 0;
    double zoomFrom = 0, zoomTo = 0, lastZoom = 0;
    Vec3 posFrom{}, posTo{}, lastSet{};
};
Klass viewClass{}, lengthClass{}, originClass{}, styleOriginClass{};
Method setZoom{}, moveTo{}, getPosition{}, setPosition{}, setScale{}, getOrigin{}, setOrigin{};
std::vector<Motion> motions;
std::vector<std::string> skips;
bool installed = false, reported = false;
bool Ready() { return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested; }
void Report(const char* stage, const std::exception& error) {
    if (!reported) Log(std::string("MAP_MOTION_ERROR ") + stage + ": " + error.what());
    reported = true;
}
// Each refusal reason is logged once per session so an in-game log explains
// why the native instant centering ran instead of the glide.
void Skip(const char* reason) {
    if (std::find(skips.begin(), skips.end(), reason) != skips.end()) return;
    if (skips.size() < 16) skips.push_back(reason);
    Log(std::string("MAP_MOTION_SKIP reason=") + reason);
}
bool IsView(Obj view) { return view && viewClass && api.class_is_assignable_from(viewClass, api.object_get_class(view)); }
double Zoom(Obj view) { return Read<double>(view, 0x18); }
bool Finite(float v) { return std::isfinite(v) && std::fabs(v) < 1e7f; }
bool Finite(const Vec3& v) { return Finite(v.x) && Finite(v.y); }
bool Alive(Obj content) { return content && api.Call(content, "get_panel"); }
Vec3 Position(Obj content) { return api.Value<Vec3>(api.Invoke(getPosition, content)); }
void SetPosition(Obj content, Vec3 position) { api.Invoke(setPosition, content, {&position}); }
void SetScale(Obj content, float scale) { Vec3 value{scale, scale, 1}; api.Invoke(setScale, content, {&value}); }
float EaseOut(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - std::pow(1 - t, 3.0f); }
float EaseInOut(float t) { t = std::clamp(t, 0.0f, 1.0f); return t < .5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
bool Near(const Vec3& a, const Vec3& b) { return std::fabs(a.x - b.x) <= PositionEpsilon && std::fabs(a.y - b.y) <= PositionEpsilon; }
Motion* Find(Obj view) {
    for (auto& m : motions) if (m.view.get() == view) return &m;
    return nullptr;
}
// Scale about the content's top-left corner, where the game's own pixel
// conversions are anchored. The previous inline value is kept for Restore.
void EnsureOrigin(Motion& m) {
    if (m.originOwned) return;
    Ref style(api.Call(m.content.get(), "get_style"));
    m.originBackup.reset(api.Invoke(getOrigin, style.get()));
    float zero = 0; int pixels = 0;
    Ref x(api.New(lengthClass, {&zero, &pixels})), y(api.New(lengthClass, {&zero, &pixels}));
    Ref origin(api.New(originClass, {api.object_unbox(x.get()), api.object_unbox(y.get())}));
    Ref value(api.New(styleOriginClass, {api.object_unbox(origin.get())}));
    api.Invoke(setOrigin, style.get(), {api.object_unbox(value.get())});
    m.originOwned = true;
}
void RestoreOrigin(Motion& m) {
    if (!m.originOwned) return;
    m.originOwned = false;
    if (m.originBackup.get() && Alive(m.content.get())) {
        Ref style(api.Call(m.content.get(), "get_style"));
        api.Invoke(setOrigin, style.get(), {api.object_unbox(m.originBackup.get())});
    }
    m.originBackup.reset();
}
// Stop the glide. The game's position is restored unless the game (or the
// player's drag) already moved the content itself.
void Release(Motion& m, bool keepPosition) {
    if (!m.active) return;
    m.active = false;
    if (!Alive(m.content.get())) return;
    SetScale(m.content.get(), 1);
    if (!keepPosition) { SetPosition(m.content.get(), m.posTo); m.lastSet = m.posTo; }
    m.lastZoom = m.zoomTo;
}
void Apply(Motion& m, float e) {
    const double zoom = m.zoomFrom + (m.zoomTo - m.zoomFrom) * e;
    const Vec3 position{m.posFrom.x + (m.posTo.x - m.posFrom.x) * e, m.posFrom.y + (m.posTo.y - m.posFrom.y) * e, m.posTo.z};
    SetScale(m.content.get(), float(zoom / m.zoomTo));
    SetPosition(m.content.get(), position);
    m.lastSet = position; m.lastZoom = zoom;
}
Motion& Slot(Obj view, Obj content) {
    if (auto found = Find(view)) {
        if (found->content.get() != content) { *found = Motion{}; found->view.reset(view); found->content.reset(content); }
        return *found;
    }
    if (motions.size() >= MaxViews) {
        auto& reused = motions.front();
        try { Release(reused, true); RestoreOrigin(reused); } catch (const std::exception& error) { Report("slot", error); }
        reused = Motion{};
    } else motions.emplace_back();
    auto& m = motions.back(); m.view.reset(view); m.content.reset(content); return m;
}
// What the player currently sees for this view: the glide's last picture, or
// the game's own state when nothing is gliding. Native steps always run on the
// game's own position (posTo); callers restore it before calling them.
void Displayed(Obj view, Obj content, double& zoom, Vec3& position) {
    auto m = Find(view);
    if (m && m->active && m->content.get() == content) { zoom = m->lastZoom; position = m->lastSet; return; }
    zoom = Zoom(view); position = Position(content);
}
void Begin(Motion& m, double zoomFrom, Vec3 posFrom, double zoomTo, Vec3 posTo, uint64_t duration, bool centering) {
    if (!(zoomTo > 0) || !std::isfinite(zoomTo) || !std::isfinite(zoomFrom) || !Finite(posFrom) || !Finite(posTo)) { Release(m, true); return; }
    if (std::fabs(zoomTo - zoomFrom) < ZoomEpsilon && Near(posFrom, posTo)) { if (!m.active) m.lastZoom = zoomTo, m.lastSet = posTo; return; }
    m.zoomFrom = zoomFrom; m.posFrom = posFrom; m.zoomTo = zoomTo; m.posTo = posTo;
    m.duration = duration; m.centering = centering; m.start = 0; m.active = true;
    EnsureOrigin(m);
    Apply(m, 0); // The picture keeps the previous frame until the glide starts.
}
void TickMotion(Motion& m, uint64_t now) {
    if (!m.active) return;
    if (!Alive(m.content.get()) || !IsView(m.view.get()) || api.Field(m.view.get(), "dugq") != m.content.get()) { m.active = false; return; }
    // The game zoomed or moved the content itself (map change, drag): its
    // state wins and the picture snaps to it.
    if (std::fabs(Zoom(m.view.get()) - m.zoomTo) > ZoomEpsilon || !Near(Position(m.content.get()), m.lastSet)) { Release(m, true); return; }
    if (!m.start) m.start = now;
    const float t = m.start + m.duration <= now ? 1.0f : float(now - m.start) / float(m.duration);
    if (t >= 1) { Release(m, false); return; }
    Apply(m, m.centering ? EaseInOut(t) : EaseOut(t));
}
void WheelHook(Obj self, float delta, Vec2 pivot, bool force, Method method) {
    bool passed = false;
    try {
        if (!Ready() || !originalWheel || !IsView(self)) { passed = true; originalWheel(self, delta, pivot, force, method); return; }
        Ref content(api.Field(self, "dugq"));
        auto existing = Find(self);
        if (force || !Alive(content.get())) {
            passed = true; originalWheel(self, delta, pivot, force, method);
            if (existing) Release(*existing, true);
            return;
        }
        double zoomFrom; Vec3 posFrom; Displayed(self, content.get(), zoomFrom, posFrom);
        if (existing && existing->active) SetPosition(content.get(), existing->posTo);
        passed = true; originalWheel(self, delta, pivot, force, method);
        const double zoomTo = Zoom(self); const Vec3 posTo = Position(content.get());
        Begin(Slot(self, content.get()), zoomFrom, posFrom, zoomTo, posTo, WheelDuration, false);
    } catch (const std::exception& error) {
        Report("wheel", error);
        if (!passed) originalWheel(self, delta, pivot, force, method);
        if (auto m = Find(self)) try { Release(*m, true); } catch (...) {}
    }
}
unsigned char CenterHook(Obj self, Method method) {
    try { if (CenterOnPlayer(self)) return 0; }
    catch (const std::exception& error) { Report("center", error); }
    return originalCenter(self, method);
}
}

bool CenterOnPlayer(Obj view) {
    if (!Ready()) return false;
    if (!IsView(view)) { Skip("view"); return false; }
    Ref content(api.Field(view, "dugq")), world(api.Field(view, "<dugi>k__BackingField"));
    if (!Alive(content.get()) || !world.get()) { Skip("content"); return false; }
    const double zoom = Zoom(view);
    if (!std::isfinite(zoom) || zoom <= 0) { Skip("zoom"); return false; }
    notebook::Entry here;
    try { here = notebook::CurrentPosition(); } catch (const std::exception&) { Skip("position"); return false; }
    if (here.world != Read<int>(world.get(), 0x10)) { Skip("world"); return false; }
    double zoomFrom; Vec3 posFrom; Displayed(view, content.get(), zoomFrom, posFrom);
    if (auto existing = Find(view); existing && existing->active) SetPosition(content.get(), existing->posTo);
    // The game's own state moves first: zoom in to the world's start scale
    // when further out, then the native centred move to the player's cell.
    const float startScale = Read<float>(world.get(), 0x30);
    if (Finite(startScale) && startScale > zoom + ZoomEpsilon) { float value = startScale; api.Invoke(setZoom, view, {&value}); }
    int x = here.x, y = here.y; bool centre = true, autoZoom = false;
    api.Invoke(moveTo, view, {&x, &y, &centre, &autoZoom});
    const double zoomTo = Zoom(view); const Vec3 posTo = Position(content.get());
    Begin(Slot(view, content.get()), zoomFrom, posFrom, zoomTo, posTo, CenterDuration, true);
    Log("MAP_MOTION_CENTER cell=" + std::to_string(x) + "," + std::to_string(y) + " zoom=" + std::to_string(zoomFrom) + "->" + std::to_string(zoomTo));
    return true;
}
void Install() {
    if (installed) return;
    viewClass = api.Class("Core.dll", "", "fhw");
    auto element = api.Class(UiAssembly, UiSpace, "VisualElement");
    auto inlineStyle = api.Class(UiAssembly, UiSpace, "InlineStyleAccess");
    lengthClass = api.Class(UiAssembly, UiSpace, "Length");
    originClass = api.Class(UiAssembly, UiSpace, "TransformOrigin");
    styleOriginClass = api.Class(UiAssembly, UiSpace, "StyleTransformOrigin");
    setZoom = api.FindVerified(viewClass, "bexi", 1, 0x1C05470);
    moveTo = api.FindVerified(viewClass, "bexn", 4, 0x1C06060);
    getPosition = api.FindVerified(element, "UnityEngine.UIElements.ITransform.get_position", 0, 0x55F0F90);
    setPosition = api.FindVerified(element, "UnityEngine.UIElements.ITransform.set_position", 1, 0x55F12D0);
    setScale = api.FindVerified(element, "UnityEngine.UIElements.ITransform.set_scale", 1, 0x55F15C0);
    getOrigin = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.get_transformOrigin", 0, 0x55D1D10);
    setOrigin = api.FindVerified(inlineStyle, "UnityEngine.UIElements.IStyle.set_transformOrigin", 1, 0x55D5010);
    api.FindVerified(originClass, ".ctor", 2, 0x5628EA0);
    api.FindVerified(styleOriginClass, ".ctor", 1, 0x5620B50);
    // Wheel zoom (delta, pivot, forced) and the player button's CenterToPlayerPosition.
    AddHook(api.FindVerified(viewClass, "bexl", 3, 0x1C05B90), 0x1C05B90, reinterpret_cast<void*>(&WheelHook), reinterpret_cast<void**>(&originalWheel));
    AddHook(api.FindVerified(viewClass, "beuu", 0, 0x1BFA420), 0x1BFA420, reinterpret_cast<void*>(&CenterHook), reinterpret_cast<void**>(&originalCenter));
    installed = true;
}
void Tick(uint64_t now) {
    if (!Ready()) { Restore(); return; }
    for (auto& m : motions) {
        try { TickMotion(m, now); }
        catch (const std::exception& error) { Report("tick", error); m.active = false; }
    }
}
void Restore() {
    for (auto& m : motions) {
        try { Release(m, false); RestoreOrigin(m); }
        catch (const std::exception& error) { Report("restore", error); }
    }
    motions.clear();
}
bool CleanupPending() { return false; }
}
