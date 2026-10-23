#include "glow/monster-glow-ui.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>
#include <sstream>

namespace monsterGlow {
namespace {
using namespace managed;
constexpr const char* UiAssembly = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* EngineAssembly = "UnityEngine.CoreModule.dll";
constexpr size_t MaxNodes = 4096, MaxPaintRects = 512, MaxTargets = 32, MaxFragments = 8;
constexpr float AlphaThreshold = 0.02f, PaddingPixels = 14.0f;
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Bounds { Vec3 center, extents; };
struct Color { float r, g, b, a; };
struct Background { Obj texture, sprite, renderTexture, vectorImage; };
static_assert(sizeof(Bounds) == 24 && sizeof(Background) == 32 && sizeof(UiRect) == 16);
struct Fragment { Ref clip, image; };
struct Entry { int64_t id = 0; bool used = false, interactive = false; Ref texture; std::vector<Fragment> fragments; };
struct State {
    Ref document, root, panel, camera, cameraService, layerService, layers, compositor, overlay;
    std::vector<UiRect> paint;
    std::vector<Entry> entries;
    UiRect viewport{};
    ScreenRect screenViewport{};
    Vec2 overlayOrigin{};
    uint64_t generation = 0, serial = 0, scanTime = 0;
    bool installed = false, scanned = false, open = false, reported = false, cleanupPending = false;
    Klass imageClass = nullptr, cameraServiceClass = nullptr, layerServiceClass = nullptr, layerRootClass = nullptr, widgetClass = nullptr;
    Method rootGetter = nullptr, cameraGetter = nullptr, screenWidth = nullptr, screenHeight = nullptr;
    Method toScreen = nullptr, toPanel = nullptr, toLocal = nullptr;
    size_t visitedNodes = 0;
    bool scanLimited = false;
} state;
bool Finite(UiRect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
        r.width > 0 && r.height > 0 && std::isfinite(r.x + r.width) && std::isfinite(r.y + r.height) &&
        r.x + r.width > r.x && r.y + r.height > r.y;
}
bool Same(UiRect a, UiRect b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
UiRect Intersect(UiRect a, UiRect b) {
    const float values[] = {a.x,a.y,a.width,a.height,b.x,b.y,b.width,b.height};
    if (std::any_of(std::begin(values),std::end(values),[](float value){return std::isnan(value);}) ||
        a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) return {};
    // Native unbounded clip rectangles must be intersected with a finite own
    // bound/viewport first. Double precision also avoids overflowing huge clips.
    const auto end = [](float origin,float size) {
        return std::isinf(size) && size > 0 ? std::numeric_limits<double>::infinity() : static_cast<double>(origin)+size;
    };
    const double x = std::max(a.x,b.x), y = std::max(a.y,b.y);
    return {static_cast<float>(x),static_cast<float>(y),
        static_cast<float>(std::max(0.0,std::min(end(a.x,a.width),end(b.x,b.width))-x)),
        static_cast<float>(std::max(0.0,std::min(end(a.y,a.height),end(b.y,b.height))-y))};
}
UiRect Rect(ScreenRect r) { return {r.x, r.y, r.width, r.height}; }
ScreenRect Screen(UiRect r) { return {r.x, r.y, r.width, r.height}; }
UiRect Corners(Vec2 a, Vec2 b) {
    return {std::min(a.x,b.x), std::min(a.y,b.y), std::abs(a.x-b.x), std::abs(a.y-b.y)};
}
void Report(const char* operation, const std::exception& error) {
    if (!state.reported) Log(std::string("MONSTER_GLOW_UI_ERROR ") + operation + ": " + error.what());
    state.reported = true;
}
bool Session() {
    return state.installed && module && module->servicesReady && !module->stopped &&
        !module->stopRequested.load() && module->compositor.get();
}
void RequireValue(Obj value) {
    if (!value || !api.class_is_valuetype(api.object_get_class(value)))
        throw std::runtime_error("Glow UI expected a boxed style value");
}
Obj ReadStyle(Obj element, const char* name) {
    Ref style(api.Call(element,"get_style"));
    Obj result = api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+name).c_str());
    RequireValue(result); return result;
}
void WriteStyle(Obj element, const char* name, Obj value) {
    RequireValue(value); Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+name).c_str(),{api.object_unbox(value)});
}
void EnumStyle(Obj element, const char* name, int value) {
    Ref box(ReadStyle(element,name)); api.Call(box.get(),"set_value",{&value}); WriteStyle(element,name,box.get());
}
void Pixels(Obj element, const char* name, float value) {
    Ref box(ReadStyle(element,name)); int unit = 0;
    Ref length(api.New(api.Class(UiAssembly,UiSpace,"Length"),{&value,&unit}));
    RequireValue(length.get()); api.Call(box.get(),"set_value",{api.object_unbox(length.get())}); WriteStyle(element,name,box.get());
}
void Position(Obj element, UiRect rect) {
    Pixels(element,"left",rect.x); Pixels(element,"top",rect.y);
    Pixels(element,"width",rect.width); Pixels(element,"height",rect.height);
}
Obj NewElement(bool image) {
    Ref element(api.New(image ? state.imageClass : api.Class(UiAssembly,UiSpace,"VisualElement")));
    int ignore = 1; bool no = false;
    api.Call(element.get(),"set_pickingMode",{&ignore}); api.Call(element.get(),"set_focusable",{&no});
    EnumStyle(element.get(),"position",1);
    if (image) {
        int stretch = 0; Color amber{1.0f,0.72f,0.16f,0.92f};
        api.Call(element.get(),"set_scaleMode",{&stretch}); api.Call(element.get(),"set_tintColor",{&amber});
    }
    return element.get();
}
void HideEntry(Entry& entry) {
    entry.used = false;
    for (auto& fragment : entry.fragments) {
        EnumStyle(fragment.clip.get(),"display",1);
        api.Call(fragment.image.get(),"set_image",{nullptr});
    }
    entry.texture.reset();
}
bool Current(const UiFrame& frame) {
    return frame.valid && state.open && Session() && frame.generation == state.generation && frame.serial == state.serial &&
        state.compositor.get() == module->compositor.get() && frame.camera.get() == state.camera.get() &&
        frame.panel.get() == state.panel.get() && frame.root.get() == state.root.get() &&
        api.Call(frame.root.get(),"get_panel") == frame.panel.get() &&
        module->FindService("fqp") == state.layerService.get() &&
        api.Field(state.layerService.get(),"dzrv") == state.layers.get() &&
        api.Call(frame.root.get(),"get_parent") == state.layers.get() &&
        api.Call(state.layers.get(),"get_parent") == state.document.get() &&
        module->FindService("fpk") == state.cameraService.get() &&
        api.Invoke(state.cameraGetter,state.cameraService.get()) == frame.camera.get() &&
        api.Invoke(state.rootGetter,nullptr) == state.document.get();
}
Obj HudRoot(Obj layers) {
    // gmo creates the named native layers in draw order. Never fall back to the
    // global panel root: that would lift the glow over windows and tooltips.
    Ref hierarchy(api.Call(layers,"get_hierarchy")); RequireValue(hierarchy.get());
    const int count = api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
    if (count < 0 || count > 64) return nullptr;
    Obj found = nullptr;
    for (int i = 0; i < count; ++i) {
        Ref child(api.Call(hierarchy.get(),"get_Item",{&i}));
        if (!child.get() || api.String(api.Call(child.get(),"get_name")) != "RootLow") continue;
        if (found || api.Call(child.get(),"get_parent") != layers) return nullptr;
        found = child.get();
    }
    return found;
}
Vec2 ScreenToPanel(const UiFrame& frame, Vec2 point) {
    // pq.pcf applies the one required Y flip and native ScreenToPanel scale.
    return api.Value<Vec2>(api.Invoke(state.toPanel,nullptr,{&point,frame.panel.get()}));
}
UiRect PanelRect(const UiFrame& frame, ScreenRect rect) {
    return Corners(ScreenToPanel(frame,{rect.x,rect.y}), ScreenToPanel(frame,{rect.x+rect.width,rect.y+rect.height}));
}
UiRect LocalRect(Obj root, UiRect rect) {
    Vec2 a{rect.x,rect.y}, b{rect.x+rect.width,rect.y+rect.height};
    return Corners(api.Value<Vec2>(api.Invoke(state.toLocal,nullptr,{root,&a})),
        api.Value<Vec2>(api.Invoke(state.toLocal,nullptr,{root,&b})));
}
void AddPaint(UiRect rect, UiRect clip) {
    rect = Intersect(Intersect(rect,clip),state.viewport);
    if (Finite(rect) && state.paint.size() < MaxPaintRects) state.paint.push_back(rect);
}
float Alpha(Color color, float opacity) { return std::isfinite(color.a) ? color.a * opacity : 0; }
void Paint(Obj element, Obj style, float opacity) {
    if (!api.Value<bool>(api.Call(element,"get_visible"))) return;
    UiRect bound = api.Value<UiRect>(api.Call(element,"get_worldBound"));
    UiRect clip = api.Value<UiRect>(api.Call(element,"get_worldClip"));
    if (!Finite(Intersect(Intersect(bound,clip),state.viewport))) return;
    bool filled = Alpha(api.Value<Color>(api.Call(style,"get_backgroundColor")),opacity) > AlphaThreshold;
    if (!filled) {
        const auto background = api.Value<Background>(api.Call(style,"get_backgroundImage"));
        filled = (background.texture || background.sprite || background.renderTexture || background.vectorImage) &&
            Alpha(api.Value<Color>(api.Call(style,"get_unityBackgroundImageTintColor")),opacity) > AlphaThreshold;
    }
    if (!filled && api.class_is_assignable_from(state.imageClass,api.object_get_class(element))) {
        filled = Alpha(api.Value<Color>(api.Call(element,"get_tintColor")),opacity) > AlphaThreshold &&
            (api.Call(element,"get_image") || api.Call(element,"get_sprite") || api.Call(element,"get_vectorImage"));
    }
    if (filled) { AddPaint(bound,clip); return; }
    // A transparent, border-only container paints strips, never its empty area.
    const char* sides[] = {"Left","Right","Top","Bottom"};
    for (int i = 0; i != 4; ++i) {
        const auto prefix = std::string("get_border") + sides[i];
        const float width = api.Value<float>(api.Call(style,(prefix+"Width").c_str()));
        if (!std::isfinite(width) || width <= 0 || Alpha(api.Value<Color>(api.Call(style,(prefix+"Color").c_str())),opacity) <= AlphaThreshold) continue;
        UiRect strip = bound;
        if (i < 2) { strip.width = std::min(width,bound.width); if (i == 1) strip.x += bound.width-strip.width; }
        else { strip.height = std::min(width,bound.height); if (i == 3) strip.y += bound.height-strip.height; }
        AddPaint(strip,clip);
    }
}
void ScanPaint(Obj root) {
    state.paint.clear();
    struct Node { Ref element; float opacity; unsigned depth; bool widget; };
    std::vector<Node> pending; pending.push_back({Ref(root),1,0,false});
    size_t visited = 0;
    while (!pending.empty() && visited < MaxNodes && state.paint.size() < MaxPaintRects) {
        Node node = std::move(pending.back()); pending.pop_back(); ++visited;
        const auto element = node.element.get();
        if (!element || element == state.overlay.get() || node.depth > 96) continue;
        Ref style(api.Call(element,"get_resolvedStyle"));
        if (api.Value<int>(api.Call(style.get(),"get_display")) != 0) continue;
        const float opacity = node.opacity * api.Value<float>(api.Call(style.get(),"get_opacity"));
        if (!std::isfinite(opacity) || opacity <= AlphaThreshold) continue;
        // Includes descendants; unlike own worldBound it is safe for pruning
        // transparent parents whose children lie outside their own rectangle.
        if (!Finite(Intersect(api.Value<UiRect>(api.Call(element,"get_worldBoundingBox")),state.viewport))) continue;
        const bool widget = node.widget || api.class_is_assignable_from(state.widgetClass,api.object_get_class(element));
        if (widget) Paint(element,style.get(),opacity);
        Ref hierarchy(api.Call(element,"get_hierarchy")); RequireValue(hierarchy.get());
        const int count = api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
        if (count < 0) throw std::runtime_error("Invalid UI hierarchy count");
        // Hierarchy enumerates decorated controls too; contentContainer does not.
        // Bound queued work and visit the frontmost siblings first.
        const int first = std::max(0,count-static_cast<int>(MaxNodes-std::min(MaxNodes,pending.size()+visited)));
        for (int i = first; i < count; ++i)
            pending.push_back({Ref(api.Call(hierarchy.get(),"get_Item",{&i})),opacity,node.depth+1,widget});
    }
    state.visitedNodes = visited;
    state.scanLimited = !pending.empty() || visited >= MaxNodes || state.paint.size() >= MaxPaintRects;
}
std::vector<UiRect> Subtract(UiRect rect, UiRect covered) {
    const auto intersection = Intersect(rect,covered);
    if (!Finite(intersection)) return {rect};
    std::vector<UiRect> pieces;
    const UiRect candidates[] = {
        {rect.x,rect.y,rect.width,intersection.y-rect.y},
        {rect.x,intersection.y+intersection.height,rect.width,rect.y+rect.height-intersection.y-intersection.height},
        {rect.x,intersection.y,intersection.x-rect.x,intersection.height},
        {intersection.x+intersection.width,intersection.y,rect.x+rect.width-intersection.x-intersection.width,intersection.height}};
    for (auto candidate : candidates) if (Finite(candidate)) pieces.push_back(candidate);
    return pieces;
}
std::vector<UiRect> Clips(UiRect target) {
    std::vector<UiRect> result;
    std::vector<UiRect> overlaps;
    for (auto paint : state.paint) {
        auto overlap = Intersect(target,paint);
        if (Finite(overlap)) overlaps.push_back(overlap);
    }
    // The tree visits frontmost decorations first. Preserve broad opaque
    // backgrounds before spending the fragment budget on their tiny children.
    std::stable_sort(overlaps.begin(),overlaps.end(),[](UiRect a,UiRect b) {
        return static_cast<double>(a.width)*a.height > static_cast<double>(b.width)*b.height;
    });
    for (auto overlap : overlaps) {
        std::vector<UiRect> pieces{overlap};
        for (auto covered : result) {
            std::vector<UiRect> next;
            for (auto piece : pieces) {
                for (auto remaining : Subtract(piece,covered)) {
                    if (next.size() < MaxFragments) next.push_back(remaining);
                }
            }
            pieces = std::move(next);
        }
        for (auto piece : pieces) { if (result.size() == MaxFragments) break; result.push_back(piece); }
        if (result.size() == MaxFragments) break;
    }
    return result;
}
void Guard(Klass type, const char* name, int count, uintptr_t rva) { api.FindVerified(type,name,count,rva); }
}

void InstallGlowUi() {
    if (state.installed) return;
    auto ui = api.Class(UiAssembly,UiSpace,"VisualElement");
    const std::tuple<const char*,int,uintptr_t> uiMethods[] = {
        {".ctor",0,0x55F50B0},{"get_hierarchy",0,0x185C4A0},{"get_resolvedStyle",0,0x55F72E0},
        {"get_worldBound",0,0x55F7CC0},{"get_worldBoundingBox",0,0x55F7E10},{"get_worldClip",0,0x55F7EE0},
        {"get_visible",0,0x55F7BC0},{"get_panel",0,0x55F7170},{"get_parent",0,0x55F7180},
        {"get_name",0,0x3158710},{"Add",1,0x55DFAD0},
        {"BringToFront",0,0x55E08E0},{"RemoveFromHierarchy",0,0x55E7BD0},{"set_pickingMode",1,0x55F97B0}};
    for (const auto& [name,count,rva] : uiMethods) Guard(ui,name,count,rva);
    Guard(api.Class(UiAssembly,UiSpace,"Focusable"),"set_focusable",1,0x5777EF0);
    auto hierarchy = api.NestedClass(ui,"Hierarchy");
    Guard(hierarchy,"get_childCount",0,0x55FFA80); Guard(hierarchy,"get_Item",1,0x55FE670);
    Guard(api.Class(UiAssembly,UiSpace,"Panel"),"get_visualTree",0,0x57AFA00);
    auto resolved = api.Class(UiAssembly,UiSpace,"ResolvedStyleAccess");
    const std::pair<const char*,uintptr_t> styleMethods[] = {
        {"get_display",0x55D9890},{"get_opacity",0x55D9CD0},{"get_backgroundColor",0x55D93B0},
        {"get_backgroundImage",0x55D93F0},{"get_unityBackgroundImageTintColor",0x55DA050},
        {"get_borderLeftColor",0x55D9650},{"get_borderLeftWidth",0x55D9690},
        {"get_borderRightColor",0x55D96C0},{"get_borderRightWidth",0x55D9700},
        {"get_borderTopColor",0x55D9730},{"get_borderTopWidth",0x55D97F0},
        {"get_borderBottomColor",0x55D9560},{"get_borderBottomWidth",0x55D9620}};
    for (auto [name,rva] : styleMethods) Guard(resolved,name,0,rva);
    state.imageClass = api.Class(UiAssembly,UiSpace,"Image");
    const std::tuple<const char*,int,uintptr_t> imageMethods[] = {
        {".ctor",0,0x56D3750},{"set_image",1,0x56D3A70},{"set_scaleMode",1,0x56D3B80},{"set_tintColor",1,0x56D4080},
        {"get_image",0,0x56D3910},{"get_sprite",0,0x56D39D0},{"get_vectorImage",0,0x56D3A20},{"get_tintColor",0,0x1581A50}};
    for (const auto& [name,count,rva] : imageMethods) Guard(state.imageClass,name,count,rva);
    auto utility = api.Class("Core.dll","","pq");
    state.rootGetter = api.FindVerified(utility,"pbs",0,0x2E93CC0);
    state.toPanel = api.FindVerified(utility,"pcf",2,0x2E94C60);
    state.cameraServiceClass = api.Class("Core.dll","","eow");
    state.cameraGetter = api.FindVerified(state.cameraServiceClass,"xhs",0,0x68D2C0);
    auto camera = api.Class(EngineAssembly,"UnityEngine","Camera");
    state.toScreen = api.FindVerified(camera,"WorldToScreenPoint",1,0x53B65B0);
    Guard(camera,"get_pixelRect",0,0x53B8AA0); Guard(camera,"get_targetTexture",0,0x53B9B50);
    Guard(api.Class(EngineAssembly,"UnityEngine","Renderer"),"get_bounds",0,0x53F2220);
    state.toLocal = api.FindVerified(api.Class(UiAssembly,UiSpace,"VisualElementExtensions"),"WorldToLocal",2,0x56776A0);
    auto screen = api.Class(EngineAssembly,"UnityEngine","Screen");
    state.screenWidth = api.FindVerified(screen,"get_width",0,0x53F4060);
    state.screenHeight = api.FindVerified(screen,"get_height",0,0x53F3DF0);
    state.layerServiceClass = api.Class("Core.dll","","gmo");
    state.layerRootClass = api.Class("Core.dll","","gmy");
    state.widgetClass = api.Class("Core.dll","Core.UILogic.Components","MagneticDragWindow");
    state.installed = true;
}
UiFrame BeginGlowUi(uint64_t nowMs) {
    UiFrame frame;
    try {
        frame.reason = "session";
        if (!Session()) { ResetGlowUi(); return frame; }
        if (state.cleanupPending) { ResetGlowUi(); if (state.cleanupPending) return frame; }
        Ref document(api.Invoke(state.rootGetter,nullptr));
        Ref panel(document.get() ? api.Call(document.get(),"get_panel") : nullptr);
        Ref panelRoot(panel.get() ? api.Call(panel.get(),"get_visualTree") : nullptr);
        Ref cameraService(module->FindService("fpk"));
        frame.reason = "document-or-camera-service";
        if (!panelRoot.get() || !cameraService.get() || !api.class_is_assignable_from(state.cameraServiceClass,api.object_get_class(cameraService.get()))) {
            ResetGlowUi(); return frame;
        }
        frame.reason = "hud-layer-unavailable";
        Ref layerService(module->FindService("fqp"));
        if (!layerService.get() || api.object_get_class(layerService.get()) != state.layerServiceClass) { ResetGlowUi(); return frame; }
        Ref layers(api.Field(layerService.get(),"dzrv"));
        if (!layers.get() || api.object_get_class(layers.get()) != state.layerRootClass ||
            api.Call(layers.get(),"get_parent") != document.get() || api.Call(layers.get(),"get_panel") != panel.get()) {
            ResetGlowUi(); return frame;
        }
        Ref root(HudRoot(layers.get()));
        if (!root.get() || api.Call(root.get(),"get_panel") != panel.get()) { ResetGlowUi(); return frame; }
        Ref camera(api.Invoke(state.cameraGetter,cameraService.get()));
        frame.reason = "camera-missing";
        if (!camera.get()) { ResetGlowUi(); return frame; }
        frame.reason = "camera-target-texture";
        if (api.Call(camera.get(),"get_targetTexture")) { ResetGlowUi(); return frame; }
        const float width = static_cast<float>(api.Value<int>(api.Invoke(state.screenWidth,nullptr)));
        const float height = static_cast<float>(api.Value<int>(api.Invoke(state.screenHeight,nullptr)));
        const auto screenViewport = Screen(Intersect({0,0,width,height},api.Value<UiRect>(api.Call(camera.get(),"get_pixelRect"))));
        const auto viewport = api.Value<UiRect>(api.Call(panelRoot.get(),"get_worldBound"));
        frame.reason = "empty-viewport";
        if (!Finite(viewport) || !Finite(Rect(screenViewport))) { ResetGlowUi(); return frame; }
        if (state.root.get() != root.get() || state.document.get() != document.get() || state.panel.get() != panel.get() ||
            state.camera.get() != camera.get() || state.cameraService.get() != cameraService.get() ||
            state.layerService.get() != layerService.get() || state.layers.get() != layers.get() ||
            state.compositor.get() != module->compositor.get() || !Same(state.viewport,viewport) ||
            !Same(Rect(state.screenViewport),Rect(screenViewport))) {
            ResetGlowUi();
            if (state.overlay.get()) return frame;
            state.document.reset(document.get()); state.root.reset(root.get()); state.panel.reset(panel.get());
            state.layerService.reset(layerService.get()); state.layers.reset(layers.get());
            state.camera.reset(camera.get()); state.cameraService.reset(cameraService.get()); state.compositor.reset(module->compositor.get());
            state.viewport = viewport; state.screenViewport = screenViewport;
        }
        if (!state.scanned || nowMs < state.scanTime || nowMs-state.scanTime >= 100) {
            ScanPaint(root.get()); state.scanTime = nowMs; state.scanned = true;
        }
        for (auto& entry : state.entries) entry.used = false;
        ++state.serial; state.open = true;
        frame.camera.reset(camera.get()); frame.panel.reset(panel.get()); frame.root.reset(root.get());
        frame.generation = state.generation; frame.serial = state.serial; frame.panelViewport = viewport;
        frame.screenViewport = screenViewport; frame.valid = true;
        frame.reason = state.paint.empty() ? "no-painted-ui" : "ready";
        frame.paintCount = state.paint.size(); frame.visitedNodes = state.visitedNodes; frame.scanLimited = state.scanLimited;
    } catch (const std::exception& error) { Report("begin",error); ResetGlowUi(); }
    return frame;
}
UiProjection ProjectGlowRenderers(const UiFrame& frame, const std::vector<Obj>& renderers, bool everywhere) {
    UiProjection result;
    result.reason = "renderer-count";
    if (renderers.empty() || renderers.size() > 32) return result;
    try {
        result.reason = "stale-frame"; if (!Current(frame)) return result;
        result.reason = "no-painted-ui"; if (!everywhere && state.paint.empty()) return result;
        float left = std::numeric_limits<float>::infinity(), bottom = left, right = -left, top = -left;
        for (auto renderer : renderers) {
            if (!renderer) return result;
            const auto bounds = api.Value<Bounds>(api.Call(renderer,"get_bounds"));
            result.reason = "invalid-bounds";
            const float values[] = {bounds.center.x,bounds.center.y,bounds.center.z,bounds.extents.x,bounds.extents.y,bounds.extents.z};
            if (!std::all_of(std::begin(values),std::end(values),[](float value){return std::isfinite(value);}) ||
                bounds.extents.x < 0 || bounds.extents.y < 0 || bounds.extents.z < 0) return result;
            for (unsigned corner = 0; corner != 8; ++corner) {
                Vec3 world{bounds.center.x + (corner&1 ? bounds.extents.x : -bounds.extents.x),
                    bounds.center.y + (corner&2 ? bounds.extents.y : -bounds.extents.y),
                    bounds.center.z + (corner&4 ? bounds.extents.z : -bounds.extents.z)};
                const auto point = api.Value<Vec3>(api.Invoke(state.toScreen,frame.camera.get(),{&world}));
                result.reason = "behind-camera-or-nonfinite";
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) || point.z <= 0) return result;
                left = std::min(left,point.x); right = std::max(right,point.x);
                bottom = std::min(bottom,point.y); top = std::max(top,point.y);
            }
        }
        result.reason = "empty-screen-bounds"; if (right <= left || top <= bottom) return result;
        const auto crop = Intersect({left-PaddingPixels,bottom-PaddingPixels,right-left+2*PaddingPixels,top-bottom+2*PaddingPixels},Rect(frame.screenViewport));
        result.reason = "offscreen"; if (!Finite(crop)) return result;
        result.screenRect = Screen(crop); result.panelRect = PanelRect(frame,result.screenRect);
        result.reason = "invalid-panel-bounds"; if (!Finite(result.panelRect)) return result;
        const auto visible = Intersect(result.panelRect,frame.panelViewport);
        if (everywhere && Finite(visible)) result.clipRects.push_back(visible);
        else result.clipRects = Clips(visible);
        result.reason = "no-ui-overlap"; if (result.clipRects.empty()) return result;
        result.generation = frame.generation; result.serial = frame.serial; result.valid = true;
        result.reason = "ready";
    } catch (const std::exception& error) { Report("project",error); }
    return result;
}
bool ShowGlowUi(const UiFrame& frame, int64_t id, const UiProjection& projection, Obj texture, bool interactive) {
    try {
        if (!Current(frame) || !projection.valid || projection.generation != frame.generation || projection.serial != frame.serial ||
            !texture || projection.clipRects.empty() || projection.clipRects.size() > MaxFragments) return false;
        if (state.overlay.get() && api.Call(state.overlay.get(),"get_panel") != frame.panel.get()) { ResetGlowUi(); return false; }
        if (!state.overlay.get()) {
            state.overlay.reset(NewElement(false));
            const auto overlayRect = LocalRect(frame.root.get(),frame.panelViewport);
            state.overlayOrigin = {overlayRect.x,overlayRect.y};
            Position(state.overlay.get(),overlayRect);
            api.Call(frame.root.get(),"Add",{state.overlay.get()});
        }
        // Reorder only within RootLow. RootMedium and higher native layers keep
        // inventory, spells, the world map, menus and tooltips above the glow.
        api.Call(state.overlay.get(),"BringToFront");
        auto found = std::find_if(state.entries.begin(),state.entries.end(),[&](const Entry& entry){return entry.id == id && entry.interactive == interactive;});
        if (found == state.entries.end()) {
            found = std::find_if(state.entries.begin(),state.entries.end(),[](const Entry& entry){return !entry.used;});
            if (found == state.entries.end()) {
                if (state.entries.size() >= MaxTargets) return false;
                state.entries.emplace_back(); found = std::prev(state.entries.end());
            }
            found->id = id; found->interactive = interactive;
        }
        auto& entry = *found;
        // Mark used only after every fragment has a valid binding and geometry.
        entry.used = false;
        for (auto& fragment : entry.fragments) EnumStyle(fragment.clip.get(),"display",1);
        const auto imageRect = LocalRect(frame.root.get(),projection.panelRect);
        if (!Finite(imageRect)) return false;
        std::vector<UiRect> localClips;
        for (auto rect : projection.clipRects) {
            if (!Finite(rect)) continue;
            const auto clip = LocalRect(frame.root.get(),rect);
            if (Finite(clip)) localClips.push_back(clip);
        }
        // Floating point subtraction/scale can collapse a sliver to zero.
        // Skip it without clearing other monsters or their valid fragments.
        if (localClips.empty()) return false;
        while (entry.fragments.size() < localClips.size()) {
            entry.fragments.emplace_back(); auto& fragment = entry.fragments.back();
            fragment.clip.reset(NewElement(false)); EnumStyle(fragment.clip.get(),"overflow",1);
            EnumStyle(fragment.clip.get(),"display",1);
            fragment.image.reset(NewElement(true));
            api.Call(fragment.clip.get(),"Add",{fragment.image.get()});
            api.Call(state.overlay.get(),"Add",{fragment.clip.get()});
        }
        for (size_t i = 0; i < localClips.size(); ++i) {
            const auto clip = localClips[i];
            auto& fragment = entry.fragments[i];
            Position(fragment.clip.get(),{clip.x-state.overlayOrigin.x,clip.y-state.overlayOrigin.y,clip.width,clip.height});
            Position(fragment.image.get(),{imageRect.x-clip.x,imageRect.y-clip.y,imageRect.width,imageRect.height});
            if (entry.texture.get() != texture || api.Call(fragment.image.get(),"get_image") != texture)
                api.Call(fragment.image.get(),"set_image",{texture});
            EnumStyle(fragment.clip.get(),"display",0);
        }
        entry.texture.reset(texture); entry.used = true; state.reported = false; return true;
    } catch (const std::exception& error) { Report("show",error); ResetGlowUi(); return false; }
}
void EndGlowUi() {
    state.open = false;
    try { for (auto& entry : state.entries) if (!entry.used) HideEntry(entry); }
    catch (const std::exception& error) { Report("end",error); ResetGlowUi(); }
}
void ResetGlowUi() {
    state.open = false; ++state.generation; state.scanned = false; state.paint.clear(); state.cleanupPending = true;
    // A failed detach retains the owned branch for a later cleanup retry. Hide
    // first so a transient hierarchy exception cannot leave a visible texture.
    if (state.overlay.get()) {
        try {
            EnumStyle(state.overlay.get(),"display",1);
            for (auto& entry : state.entries) HideEntry(entry);
        } catch (const std::exception& error) { Report("hide",error); }
        try { api.Call(state.overlay.get(),"RemoveFromHierarchy"); }
        catch (const std::exception& error) { Report("detach",error); return; }
    }
    state.entries.clear(); state.overlay.reset(); state.document.reset(); state.root.reset(); state.panel.reset();
    state.layerService.reset(); state.layers.reset();
    state.camera.reset(); state.cameraService.reset(); state.compositor.reset(); state.viewport = {}; state.screenViewport = {};
    state.cleanupPending = false;
}
bool GlowUiCleanupPending() { return state.cleanupPending; }
std::string DescribeGlowUi() {
    if (!state.overlay.get()) return "overlay=absent";
    try {
        Ref style(api.Call(state.overlay.get(),"get_resolvedStyle"));
        const bool attached = api.Call(state.overlay.get(),"get_panel") == state.panel.get();
        const bool visible = api.Value<bool>(api.Call(state.overlay.get(),"get_visible")) &&
            api.Value<int>(api.Call(style.get(),"get_display")) == 0;
        const float opacity = api.Value<float>(api.Call(style.get(),"get_opacity"));
        Ref hierarchy(api.Call(state.root.get(),"get_hierarchy"));
        int last = api.Value<int>(api.Call(hierarchy.get(),"get_childCount")) - 1;
        const bool front = last >= 0 && api.Call(hierarchy.get(),"get_Item",{&last}) == state.overlay.get();
        size_t images = 0, laidOut = 0, painted = 0;
        for (const auto& entry : state.entries) {
            if (!entry.used) continue;
            for (const auto& fragment : entry.fragments) {
                Ref clipStyle(api.Call(fragment.clip.get(),"get_resolvedStyle"));
                if (api.Value<int>(api.Call(clipStyle.get(),"get_display")) != 0) continue;
                ++images;
                const auto bound = api.Value<UiRect>(api.Call(fragment.image.get(),"get_worldBound"));
                const auto clip = api.Value<UiRect>(api.Call(fragment.image.get(),"get_worldClip"));
                if (Finite(bound)) ++laidOut;
                Ref imageStyle(api.Call(fragment.image.get(),"get_resolvedStyle"));
                const float alpha = opacity * api.Value<float>(api.Call(clipStyle.get(),"get_opacity")) *
                    api.Value<float>(api.Call(imageStyle.get(),"get_opacity"));
                if (visible && alpha > AlphaThreshold && api.Value<bool>(api.Call(fragment.image.get(),"get_visible")) &&
                    api.Value<int>(api.Call(imageStyle.get(),"get_display")) == 0 &&
                    Finite(Intersect(Intersect(bound,clip),state.viewport))) ++painted;
            }
        }
        std::ostringstream text;
        text << "overlayAttached=" << attached << " overlayFront=" << front << " overlayVisible=" << visible
             << " overlayOpacity=" << opacity << " images=" << images << " laidOut=" << laidOut << " unclipped=" << painted
             << " overlayLayer=RootLow";
        return text.str();
    } catch (const std::exception& error) {
        return std::string("overlay-diagnostic-error=") + error.what();
    }
}
}
