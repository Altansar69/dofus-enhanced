#include "map-motion.hpp"
#include "notebook-native.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

// Offline fixture: a synthetic cartography view with the pinned raw fields,
// fake zoom/move natives, transform state and hook capture. No client is touched.
namespace fake {
using namespace managed;
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Class { std::string name; };
struct MethodInfo { Class* type; std::string name; int argc; uintptr_t rva; };
struct Object {
    alignas(8) std::array<unsigned char, 64> value{};   // raw fields read by managed::Read
    Class* type{};
    std::map<std::string, Obj> refs;
    Obj parent{}, panel{};
    Vec3 position{}, scale{1, 1, 1};
    Obj origin{};        // boxed StyleTransformOrigin currently set inline
    int originWrites = 0;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle, Obj> handles;
std::map<std::string, uintptr_t> guards;
std::vector<std::string> logs;
GCHandle nextHandle = 0xD0C000000000ULL;
using WheelFn = void(*)(Obj, float, Vec2, bool, Method);
using CenterFn = unsigned char(*)(Obj, Method);
WheelFn wheelHook = nullptr; CenterFn centerHook = nullptr;
struct WheelCall { Obj view; float delta; Vec2 pivot; bool force; };
std::vector<WheelCall> wheelCalls; int nativeCenters = 0;
std::vector<float> zoomCalls; struct Move { int x, y; bool centre, autoZoom; }; std::vector<Move> moves;
std::vector<Vec3> positions, scales;
bool positionAvailable = true; int playerX = 20, playerY = 15, playerWorld = 1;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
Class* Type(const std::string& n) { auto& p = classes[n]; if (!p) p = std::make_unique<Class>(Class{n}); return p.get(); }
Object& Get(Obj o) { Check(o != nullptr, "Null fake object"); return *static_cast<Object*>(o); }
Obj Make(const std::string& n) { auto p = std::make_unique<Object>(); p->type = Type(n); auto result = p.get(); objects.push_back(std::move(p)); return result; }
template<class T> Obj Box(T value) { auto o = Make("Box"); std::memcpy(Get(o).value.data(), &value, sizeof(value)); return o; }
template<class T> void Raw(Obj o, size_t offset, T value) { std::memcpy(Get(o).value.data() + offset, &value, sizeof(value)); }
template<class T> T RawRead(Obj o, size_t offset) { T v; std::memcpy(&v, Get(o).value.data() + offset, sizeof(v)); return v; }
Obj Panel(Obj o) { while (o) { if (Get(o).panel) return Get(o).panel; o = Get(o).parent; } return nullptr; }
// Fixture conversion: map point m shows at content pixel m*100*zoom; the content
// is 800x600 in its parent. The native wheel keeps the pivot fixed, like bexl.
double ZoomOf(Obj view) { return RawRead<double>(view, 0x18); }
void OriginalWheel(Obj view, float delta, Vec2 pivot, bool force, Method) {
    wheelCalls.push_back({view, delta, pivot, force});
    auto content = Get(view).refs.at("dugq"); auto& c = Get(content);
    const double before = ZoomOf(view), after = std::max(.1, before - delta * .1);
    Raw(view, 0x18, after);
    const float ratio = float(after / before);
    c.position = {pivot.x - (pivot.x - c.position.x) * ratio, pivot.y - (pivot.y - c.position.y) * ratio, 0};
}
unsigned char OriginalCenter(Obj, Method) { ++nativeCenters; return 0; }
void Initialize() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).type; };
    api.class_is_assignable_from = [](Klass a, Klass b) { const auto& x = static_cast<Class*>(a)->name, & y = static_cast<Class*>(b)->name; return a == b || (x == "fhw" && (y == "CartographyMapView" || y == "CartographyBannerView")); };
    api.object_unbox = [](Obj o) -> void* { return Get(o).value.data(); };
    api.string_new = [](const char* s) -> Obj { auto o = Make("String"); (void)s; return o; };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++nextHandle] = o; return nextHandle; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Double GC release"); };
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* n) const { return fake::Type(n); }
Method Api::FindVerified(Klass type, const char* n, int argc, uintptr_t rva) const {
    fake::guards[static_cast<fake::Class*>(type)->name + "." + n] = rva;
    auto p = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(type), n, argc, rva}); auto result = p.get(); fake::methods.push_back(std::move(p)); return result;
}
Obj Api::Field(Obj o, const char* n) const { return fake::Get(o).refs[n]; }
Obj Api::New(Klass type, std::initializer_list<void*> args) const {
    using namespace fake; const auto& name = static_cast<fake::Class*>(type)->name; std::vector<void*> a(args);
    if (name == "Length") { Check(a.size() == 2 && *static_cast<float*>(a[0]) == 0 && *static_cast<int*>(a[1]) == 0, "Zero pixel length"); return Make("Length"); }
    if (name == "TransformOrigin") { Check(a.size() == 2, "TransformOrigin(Length,Length)"); return Make("TransformOrigin"); }
    if (name == "StyleTransformOrigin") { Check(a.size() == 1, "StyleTransformOrigin(TransformOrigin)"); return Make("StyleTransformOrigin"); }
    throw std::runtime_error("Unexpected fake construction: " + name);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake; const auto& m = *static_cast<const MethodInfo*>(method); std::vector<void*> a(args);
    Check(int(a.size()) == m.argc, "Native arity mismatch");
    Check(self != nullptr, "Instance native call");
    auto& o = Get(self);
    if (m.name == "bexi") { const float zoom = *static_cast<float*>(a[0]); zoomCalls.push_back(zoom); Raw(self, 0x18, double(zoom)); return nullptr; }
    if (m.name == "bexn") {
        moves.push_back({*static_cast<int*>(a[0]), *static_cast<int*>(a[1]), *static_cast<bool*>(a[2]), *static_cast<bool*>(a[3])});
        auto& c = Get(o.refs.at("dugq")); const double zoom = ZoomOf(self);
        c.position = {400 - float((moves.back().x + .5) * 100 * zoom), 300 - float((moves.back().y + .5) * 100 * zoom), 0};
        return nullptr;
    }
    if (m.name == "UnityEngine.UIElements.ITransform.get_position") return Box(o.position);
    if (m.name == "UnityEngine.UIElements.ITransform.set_position") { o.position = *static_cast<Vec3*>(a[0]); positions.push_back(o.position); return nullptr; }
    if (m.name == "UnityEngine.UIElements.ITransform.set_scale") { o.scale = *static_cast<Vec3*>(a[0]); Check(o.scale.z == 1 && o.scale.x == o.scale.y && o.scale.x > 0, "Uniform plane scale"); scales.push_back(o.scale); return nullptr; }
    if (m.name == "UnityEngine.UIElements.IStyle.get_transformOrigin") { Check(o.type->name == "Style", "Origin read on the inline style"); return Get(o.refs.at("owner")).origin ? Get(o.refs.at("owner")).origin : Make("NativeOrigin"); }
    if (m.name == "UnityEngine.UIElements.IStyle.set_transformOrigin") { auto& owner = Get(o.refs.at("owner")); owner.origin = static_cast<Obj>(static_cast<void*>(static_cast<unsigned char*>(a[0]))); ++owner.originWrites; return nullptr; }
    throw std::runtime_error("Unexpected fake invocation: " + m.name);
}
Obj Api::Call(Obj self, const char* n, std::initializer_list<void*>) const {
    using namespace fake; auto& o = Get(self); std::string name = n;
    if (name == "get_parent") return o.parent;
    if (name == "get_panel") return Panel(self);
    if (name == "get_style") { auto style = Make("Style"); Get(style).refs["owner"] = self; return style; }
    throw std::runtime_error("Unexpected fake call: " + name);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char*) const { return nullptr; }
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method, uintptr_t rva, void* replacement, void** original) {
    if (rva == 0x1C05B90) { fake::wheelHook = reinterpret_cast<fake::WheelFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalWheel); return; }
    if (rva == 0x1BFA420) { fake::centerHook = reinterpret_cast<fake::CenterFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalCenter); return; }
    throw std::runtime_error("Unexpected hook");
}
namespace notebook {
Entry CurrentPosition() { if (!fake::positionAvailable) throw std::runtime_error("Position actuelle indisponible."); Entry e; e.map = 1; e.x = fake::playerX; e.y = fake::playerY; e.world = fake::playerWorld; return e; }
}
namespace {
using namespace fake;
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj view{}, content{}, parent{}, world{};
    Fixture(double zoom = .5, float startScale = 1.0f) {
        module = &state; state.ready = true; state.servicesReady = true;
        view = Make("CartographyMapView"); content = Make("VisualElement"); parent = Make("Viewport"); world = Make("WorldMapData");
        Get(parent).panel = Make("Panel"); Get(content).parent = parent;
        Get(view).refs["dugq"] = content; Get(view).refs["<dugi>k__BackingField"] = world;
        Raw(view, 0x18, zoom); Raw(world, 0x10, 1); Raw(world, 0x30, startScale);
        // Viewport centre on cell (10,10).
        Get(content).position = {400 - float(10.5 * 100 * zoom), 300 - float(10.5 * 100 * zoom), 0};
        wheelCalls.clear(); zoomCalls.clear(); moves.clear(); positions.clear(); scales.clear(); logs.clear(); nativeCenters = 0;
        positionAvailable = true; playerX = 20; playerY = 15; playerWorld = 1;
    }
    ~Fixture() { mapMotion::Restore(); module = nullptr; Check(handles.empty(), "Map motion leaked GC roots"); }
    Vec3 Pos() const { return Get(content).position; }
    float Scale() const { return Get(content).scale.x; }
    // Where the map point (mx,my) shows on screen with the current picture.
    Vec2 Screen(float mx, float my) const { const double z = ZoomOf(view); return {Pos().x + float(mx * 100 * z) * Scale(), Pos().y + float(my * 100 * z) * Scale()}; }
};
bool Close(float a, float b, float eps = 1e-2f) { return std::fabs(a - b) <= eps; }
void WheelAppliesNativelyThenGlides() {
    Fixture f;
    const Vec3 before = f.Pos();
    const Vec2 pivot{250, 180};
    const Vec2 pivotMapPoint{(pivot.x - before.x) / 50, (pivot.y - before.y) / 50};
    wheelHook(f.view, 1.0f, pivot, false, nullptr);
    Check(wheelCalls.size() == 1 && Close(wheelCalls[0].delta, 1.0f) && !wheelCalls[0].force && Close(float(ZoomOf(f.view)), .4f), "A wheel notch is applied at once by the game's own zoom");
    // The fixture's native wheel keeps the pivot fixed: zoom .5 -> .4.
    const Vec3 target{pivot.x - (pivot.x - before.x) * .8f, pivot.y - (pivot.y - before.y) * .8f, 0};
    Check(positions.size() == 1 && Close(positions.back().x, before.x) && Close(positions.back().y, before.y), "The picture keeps the previous position right after the native jump");
    Check(scales.size() == 1 && Close(f.Scale(), .5f / .4f, 1e-4f), "The content is scaled so the previous zoom level stays on screen");
    Check(Get(f.content).originWrites == 1 && Get(f.content).origin, "The transform origin is anchored once at the content's corner");
    const Vec2 anchor = f.Screen(pivotMapPoint.x, pivotMapPoint.y);
    mapMotion::Tick(1000);
    for (int i = 1; i <= 20; ++i) {
        mapMotion::Tick(1000 + i * 16);
        const Vec2 now = f.Screen(pivotMapPoint.x, pivotMapPoint.y);
        Check(Close(now.x, anchor.x, .5f) && Close(now.y, anchor.y, .5f), "The point under the mouse never moves during the glide");
        Check(f.Scale() <= 1.25f + 1e-4f && f.Scale() >= 1.0f - 1e-4f, "The scale eases from the old zoom to the new one");
    }
    mapMotion::Tick(1400);
    Check(Close(f.Scale(), 1.0f, 1e-6f) && Close(f.Pos().x, target.x, 1e-4f) && Close(f.Pos().y, target.y, 1e-4f), "The glide ends exactly on the game's own state");
    const auto sets = positions.size(); mapMotion::Tick(2000); mapMotion::Tick(2500);
    Check(positions.size() == sets && wheelCalls.size() == 1, "A finished glide touches nothing and never zooms again");
    Check(Get(f.content).originWrites == 1, "The origin stays anchored for the next notch");
}
void NotchesChainFromTheCurrentPicture() {
    Fixture f;
    wheelHook(f.view, 1.0f, {100, 100}, false, nullptr); mapMotion::Tick(1000); mapMotion::Tick(1100);
    const float displayedZoom = f.Scale() * float(ZoomOf(f.view));
    Check(displayedZoom > .4f + 1e-3f && displayedZoom < .5f - 1e-3f, "Mid-glide the displayed zoom is between the two levels");
    wheelHook(f.view, 1.0f, {100, 100}, false, nullptr);
    Check(wheelCalls.size() == 2 && Close(float(ZoomOf(f.view)), .3f), "The second notch is also applied natively at once");
    Check(Close(f.Scale() * float(ZoomOf(f.view)), displayedZoom, 1e-4f), "The new glide continues from the displayed picture, without a jump");
    mapMotion::Tick(1116); mapMotion::Tick(1200);
    wheelHook(f.view, -1.0f, {100, 100}, false, nullptr);
    Check(wheelCalls.size() == 3 && Close(float(ZoomOf(f.view)), .4f), "Reversing applies natively too");
    const float reversedFrom = f.Scale() * float(ZoomOf(f.view));
    Check(reversedFrom < .4f, "Reversal starts from the picture that was still zooming out");
    for (int i = 0; i < 40; ++i) mapMotion::Tick(1216 + i * 16);
    Check(Close(f.Scale(), 1.0f, 1e-6f), "Chained notches settle on the game's state");
    wheelHook(f.view, .5f, {1, 1}, true, nullptr);
    Check(wheelCalls.size() == 4 && wheelCalls.back().force && Close(f.Scale(), 1.0f, 1e-6f) && scales.size() >= 1, "Forced native zooms pass straight through without a glide");
    mapMotion::Tick(2000); mapMotion::Tick(1900 + 220 + 16);
    wheelHook(f.view, 1.0f, {1, 1}, false, nullptr); mapMotion::Tick(3000);
    const auto forcedSets = positions.size(); wheelHook(f.view, 1.0f, {1, 1}, true, nullptr);
    Check(Close(f.Scale(), 1.0f, 1e-6f) && positions.size() == forcedSets, "A forced zoom during a glide snaps the picture to the game's state without moving it again");
}
void InteractionAndNativeChangesWin() {
    {
        Fixture f;
        wheelHook(f.view, 1.0f, {100, 100}, false, nullptr); mapMotion::Tick(1000); mapMotion::Tick(1050);
        Get(f.content).position.x += 40; const Vec3 dragged = f.Pos();
        mapMotion::Tick(1066);
        Check(Close(f.Scale(), 1.0f, 1e-6f) && Close(f.Pos().x, dragged.x) && Close(f.Pos().y, dragged.y), "A drag during the glide keeps the dragged position and drops the scale");
        const auto sets = positions.size(); mapMotion::Tick(1500); Check(positions.size() == sets, "A cancelled glide stays cancelled");
    }
    {
        Fixture f;
        wheelHook(f.view, 1.0f, {100, 100}, false, nullptr); mapMotion::Tick(1000); mapMotion::Tick(1050);
        Raw(f.view, 0x18, 1.0); const Vec3 kept = f.Pos();
        mapMotion::Tick(1066);
        Check(Close(f.Scale(), 1.0f, 1e-6f) && Close(f.Pos().x, kept.x), "A native zoom change (map change, zoom button) snaps the picture without moving it");
    }
    {
        Fixture f;
        wheelHook(f.view, 1.0f, {100, 100}, false, nullptr); mapMotion::Tick(1000);
        Get(f.parent).panel = nullptr; const auto sets = positions.size(), scaleSets = scales.size();
        mapMotion::Tick(1100); mapMotion::Tick(1500);
        Check(positions.size() == sets && scales.size() == scaleSets, "A detached view is never touched again");
    }
}
void CenteringGlidesAndZooms() {
    Fixture f;
    const Vec3 before = f.Pos();
    Check(mapMotion::CenterOnPlayer(f.view), "Centering starts on a readable view and player position");
    Check(zoomCalls.size() == 1 && Close(zoomCalls[0], 1.0f) && moves.size() == 1 && moves[0].x == 20 && moves[0].y == 15 && moves[0].centre && !moves[0].autoZoom, "The game's own state moves first: zoom to the start scale, then the native centred move");
    const Vec3 target{400 - 20.5f * 100, 300 - 15.5f * 100, 0};
    Check(Close(f.Pos().x, before.x) && Close(f.Pos().y, before.y) && Close(f.Scale(), .5f, 1e-4f), "The picture still shows the previous view at the previous zoom");
    mapMotion::Tick(1000);
    Check(Close(f.Scale(), .5f, 1e-3f) && Close(f.Pos().x, before.x, .5f), "The first frame holds the start");
    mapMotion::Tick(2000);
    Check(Close(f.Scale(), .75f, 1e-3f), "Halfway, the displayed zoom is halfway to the start scale");
    Check(Close(f.Pos().x, (before.x + target.x) / 2, .5f) && Close(f.Pos().y, (before.y + target.y) / 2, .5f), "Halfway, the picture is halfway to the player cell");
    mapMotion::Tick(2500); Check(f.Scale() > .75f && f.Scale() < 1.0f, "The zoom keeps easing");
    mapMotion::Tick(3000);
    Check(Close(f.Scale(), 1.0f, 1e-6f) && Close(f.Pos().x, target.x, 1e-3f) && Close(f.Pos().y, target.y, 1e-3f), "The glide ends on the native centred placement");
    const auto sets = positions.size(); mapMotion::Tick(4000); Check(positions.size() == sets && moves.size() == 1 && zoomCalls.size() == 1, "A finished glide stays idle");
    Check(!logs.empty() && logs.back().rfind("MAP_MOTION_CENTER", 0) == 0, "Centering is logged for in-game diagnosis");
}
void CenteringKeepsCloserZoomAndRefusals() {
    {
        Fixture f(1.5, 1.0f);
        Check(mapMotion::CenterOnPlayer(f.view) && zoomCalls.empty() && moves.size() == 1, "A map zoomed in beyond the start scale keeps its zoom");
        mapMotion::Tick(1000); mapMotion::Tick(1500);
        Check(Close(f.Scale(), 1.0f, 1e-6f) && !positions.empty(), "Only the position glides when the zoom is kept");
        mapMotion::Tick(3000); Check(Close(f.Pos().x, 400 - 20.5f * 150, 1e-2f), "The glide ends centred on the player at the kept zoom");
    }
    {
        Fixture f;
        Check(mapMotion::CenterOnPlayer(f.view), "Start"); mapMotion::Tick(1000); mapMotion::Tick(1500);
        wheelHook(f.view, 1.0f, {0, 0}, false, nullptr);
        Check(wheelCalls.size() == 1 && moves.size() == 1, "A wheel notch during the glide zooms natively from the displayed picture");
        for (int i = 0; i < 30; ++i) mapMotion::Tick(1516 + i * 16);
        Check(Close(f.Scale(), 1.0f, 1e-6f), "The wheel glide replaces the centering glide and settles");
    }
    {
        Fixture f;
        Get(f.content).position = {400 - 20.5f * 100, 300 - 15.5f * 100, 0}; Raw(f.view, 0x18, 1.0);
        Check(mapMotion::CenterOnPlayer(f.view) && moves.size() == 1 && positions.empty() && scales.empty(), "An already centred view just confirms through the native move");
    }
    {
        Fixture f; playerWorld = 2;
        Check(!mapMotion::CenterOnPlayer(f.view) && moves.empty(), "Another world is left to the game's own centering");
        playerWorld = 1; positionAvailable = false;
        Check(!mapMotion::CenterOnPlayer(f.view), "An unknown player position is left to the game");
        positionAvailable = true; Get(f.parent).panel = nullptr;
        Check(!mapMotion::CenterOnPlayer(f.view), "A detached view is refused");
        int skipLogs = 0; for (const auto& l : logs) if (l.rfind("MAP_MOTION_SKIP", 0) == 0) ++skipLogs;
        Check(skipLogs == 3, "Each refusal reason is logged once");
        Get(f.parent).panel = Make("Panel"); playerWorld = 2;
        Check(!mapMotion::CenterOnPlayer(f.view) && logs.size() == 3, "A repeated reason is not logged again");
    }
}
void PlayerButtonHookAndLifecycle() {
    Fixture f;
    Check(centerHook(f.view, nullptr) == 0 && nativeCenters == 0 && moves.size() == 1, "The full map's player button centres natively then glides, skipping the native instant jump");
    Fixture g; playerWorld = 2; Check(centerHook(g.view, nullptr) == 0 && nativeCenters == 1, "When the glide cannot start the native centering runs");
    Fixture h; Check(mapMotion::CenterOnPlayer(h.view), "Start"); mapMotion::Tick(1000); mapMotion::Tick(1500);
    const Vec3 target{400 - 20.5f * 100, 300 - 15.5f * 100, 0};
    h.state.stopRequested = true; mapMotion::Tick(1516);
    Check(Close(h.Scale(), 1.0f, 1e-6f) && Close(h.Pos().x, target.x, 1e-3f) && !mapMotion::CleanupPending(), "Stopping snaps the picture to the game's state");
    Check(Get(h.content).originWrites == 2 && Get(h.content).origin && Get(Get(h.content).origin).type->name == "NativeOrigin", "Stopping restores the content's own transform origin");
    h.state.stopRequested = false;
}
}
int main() {
    try {
        fake::Initialize();
        mapMotion::Install(); mapMotion::Install();
        Check(guards.at("fhw.bexi") == 0x1C05470 && guards.at("fhw.bexn") == 0x1C06060 && guards.at("fhw.bexl") == 0x1C05B90 && guards.at("fhw.beuu") == 0x1BFA420 &&
            guards.at("VisualElement.UnityEngine.UIElements.ITransform.set_position") == 0x55F12D0 && guards.at("VisualElement.UnityEngine.UIElements.ITransform.set_scale") == 0x55F15C0 &&
            guards.at("InlineStyleAccess.UnityEngine.UIElements.IStyle.set_transformOrigin") == 0x55D5010 && guards.at("TransformOrigin..ctor") == 0x5628EA0, "Every native entry point is pinned to the supported client");
        Check(wheelHook && centerHook, "Both native entry points are hooked once");
        for (auto test : {WheelAppliesNativelyThenGlides, NotchesChainFromTheCurrentPicture, InteractionAndNativeChangesWin, CenteringGlidesAndZooms, CenteringKeepsCloserZoomAndRefusals, PlayerButtonHookAndLifecycle}) test();
        std::cout << "MapMotionTests: native-first wheel zoom and centering glides passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; for (const auto& l : fake::logs) std::cerr << l << '\n'; return 1; }
}
