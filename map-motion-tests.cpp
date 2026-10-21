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
// fake zoom/move/clamp natives and hook capture. No client is touched.
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
    float width = 800, height = 600;
    Vec3 position{};
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
std::vector<float> zoomCalls; struct Move { int x, y; bool centre, autoZoom; }; std::vector<Move> moves; std::vector<Vec3> positions;
bool positionAvailable = true; int playerX = 20, playerY = 15, playerWorld = 1;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
Class* Type(const std::string& n) { auto& p = classes[n]; if (!p) p = std::make_unique<Class>(Class{n}); return p.get(); }
Object& Get(Obj o) { Check(o != nullptr, "Null fake object"); return *static_cast<Object*>(o); }
Obj Make(const std::string& n) { auto p = std::make_unique<Object>(); p->type = Type(n); auto result = p.get(); objects.push_back(std::move(p)); return result; }
template<class T> Obj Box(T value) { auto o = Make("Box"); std::memcpy(Get(o).value.data(), &value, sizeof(value)); return o; }
template<class T> void Raw(Obj o, size_t offset, T value) { std::memcpy(Get(o).value.data() + offset, &value, sizeof(value)); }
template<class T> T RawRead(Obj o, size_t offset) { T v; std::memcpy(&v, Get(o).value.data() + offset, sizeof(v)); return v; }
Obj Panel(Obj o) { while (o) { if (Get(o).panel) return Get(o).panel; o = Get(o).parent; } return nullptr; }
void OriginalWheel(Obj view, float delta, Vec2 pivot, bool force, Method) { wheelCalls.push_back({view, delta, pivot, force}); Raw(view, 0x18, RawRead<double>(view, 0x18) - delta * 0.1); }
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
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake; const auto& m = *static_cast<const MethodInfo*>(method); std::vector<void*> a(args);
    Check(int(a.size()) == m.argc, "Native arity mismatch");
    if (m.name == "bepx") { Check(!self, "Static conversion"); const double zoom = *static_cast<double*>(a[3]); return Box(Vec2{float((*static_cast<float*>(a[0]) + .5) * 100 * zoom), float((*static_cast<float*>(a[1]) + .5) * 100 * zoom)}); }
    Check(Get(self).type->name != "fhw" || true, "view");
    if (m.name == "bexi") { const float zoom = *static_cast<float*>(a[0]); zoomCalls.push_back(zoom); Raw(self, 0x18, double(zoom)); return nullptr; }
    if (m.name == "bexn") { moves.push_back({*static_cast<int*>(a[0]), *static_cast<int*>(a[1]), *static_cast<bool*>(a[2]), *static_cast<bool*>(a[3])}); return nullptr; }
    if (m.name == "bexc") { auto v = *static_cast<Vec3*>(a[0]); v.x = std::min(v.x, 0.0f); v.y = std::min(v.y, 0.0f); return Box(v); }
    throw std::runtime_error("Unexpected fake invocation: " + m.name);
}
Obj Api::Call(Obj self, const char* n, std::initializer_list<void*> args) const {
    using namespace fake; auto& o = Get(self); std::string name = n; std::vector<void*> a(args);
    if (name == "get_parent") return o.parent;
    if (name == "get_panel") return Panel(self);
    if (name == "get_resolvedStyle") return self;
    if (name == "get_width") return Box(o.width);
    if (name == "get_height") return Box(o.height);
    if (name == "UnityEngine.UIElements.ITransform.get_position") return Box(o.position);
    if (name == "UnityEngine.UIElements.ITransform.set_position") { o.position = *static_cast<Vec3*>(a[0]); positions.push_back(o.position); return nullptr; }
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
        // Viewport centre on cell (10,10): pixel = (10.5*100*zoom) with the fixture's conversion.
        Get(content).position = {400 - float(10.5 * 100 * zoom), 300 - float(10.5 * 100 * zoom), 0};
        wheelCalls.clear(); zoomCalls.clear(); moves.clear(); positions.clear(); logs.clear(); nativeCenters = 0;
        positionAvailable = true; playerX = 20; playerY = 15; playerWorld = 1;
    }
    ~Fixture() { mapMotion::Restore(); module = nullptr; Check(handles.empty(), "Map motion leaked GC roots"); }
};
float Sum() { float s = 0; for (const auto& c : wheelCalls) s += c.delta; return s; }
void WheelGlides() {
    Fixture f;
    wheelHook(f.view, 1.0f, {10, 20}, false, nullptr);
    Check(wheelCalls.empty(), "A wheel step is not applied at once");
    for (int i = 0; i < 40; ++i) mapMotion::Tick(1000 + i * 16);
    Check(wheelCalls.size() >= 4 && std::fabs(Sum() - 1.0f) < 1e-5f, "The step is spread over several frames and fully applied");
    for (const auto& c : wheelCalls) Check(c.view == f.view && c.pivot.x == 10 && c.pivot.y == 20 && !c.force && c.delta > 0, "Every partial step keeps the wheel pivot, view and direction");
    Check(std::fabs(wheelCalls.front().delta - .3f) < 1e-5f && wheelCalls.front().delta > wheelCalls.back().delta, "Ease-out: large first step, small last one");
    const auto done = wheelCalls.size(); mapMotion::Tick(5000); Check(wheelCalls.size() == done, "A finished glide stops calling the game");
    wheelHook(f.view, .5f, {1, 1}, true, nullptr); Check(wheelCalls.size() == done + 1 && wheelCalls.back().force, "Forced native zooms pass straight through");
    wheelHook(f.view, 1.0f, {0, 0}, false, nullptr); mapMotion::Tick(6000); const float applied = Sum();
    wheelHook(f.view, -1.0f, {0, 0}, false, nullptr);
    for (int i = 0; i < 40; ++i) mapMotion::Tick(6100 + i * 16);
    Check(std::fabs(Sum() - (applied - 1.0f)) < 1e-4f, "Reversing the wheel restarts the glide from the current zoom");
    wheelHook(f.view, 2.0f, {3, 3}, false, nullptr); mapMotion::Tick(9000); wheelHook(f.view, 2.0f, {3, 3}, false, nullptr);
    const float before = Sum(); for (int i = 0; i < 60; ++i) mapMotion::Tick(9100 + i * 16);
    Check(std::fabs(Sum() - before - (4.0f - 0.6f)) < 1e-3f, "Repeated same-direction notches accumulate into one glide");
    Get(f.parent).panel = nullptr; wheelHook(f.view, 1.0f, {0, 0}, false, nullptr); mapMotion::Tick(12000); Check(std::fabs(Sum() - (before + 3.4f)) < 1e-3f, "A detached view drops its pending steps");
}
void CenteringGlidesAndZooms() {
    Fixture f;
    Check(mapMotion::CenterOnPlayer(f.view), "Centering starts on a readable view and player position");
    Check(moves.empty() && zoomCalls.empty(), "Nothing jumps when the glide starts");
    mapMotion::Tick(1000);
    Check(zoomCalls.empty() && positions.size() == 1 && std::fabs(positions.back().x - Get(f.content).position.x) < 1e-3f, "First frame holds the start");
    mapMotion::Tick(2000);
    Check(zoomCalls.size() == 1 && std::fabs(zoomCalls.back() - .75f) < 1e-5f, "Halfway, the zoom is halfway to the world's start scale");
    Check(std::fabs(positions.back().x - (400 - 15.5f * 75)) < 1e-2f && std::fabs(positions.back().y - (300 - 13.0f * 75)) < 1e-2f, "Halfway, the viewport centre is halfway to the player cell at the current zoom");
    mapMotion::Tick(2500); Check(zoomCalls.size() == 2 && zoomCalls.back() > .75f && zoomCalls.back() < 1.0f, "Zoom keeps easing");
    mapMotion::Tick(3000);
    Check(std::fabs(zoomCalls.back() - 1.0f) < 1e-5f, "The glide ends at the start scale");
    Check(std::fabs(positions.back().x - (400 - 20.5f * 100)) < 1e-2f && std::fabs(positions.back().y - (300 - 15.5f * 100)) < 1e-2f, "The glide ends centred on the player cell");
    Check(moves.size() == 1 && moves.back().x == 20 && moves.back().y == 15 && moves.back().centre && !moves.back().autoZoom, "The final placement uses the game's own move");
    const auto count = positions.size(); mapMotion::Tick(4000); Check(positions.size() == count && moves.size() == 1, "A finished glide stays idle");
}
void CenteringKeepsCloserZoomAndStopsOnInteraction() {
    {
        Fixture f(1.5, 1.0f);
        Check(mapMotion::CenterOnPlayer(f.view), "Start");
        mapMotion::Tick(1000); mapMotion::Tick(3000);
        Check(zoomCalls.empty() && moves.size() == 1, "A map zoomed in beyond the start scale keeps its zoom");
    }
    {
        Fixture f;
        Check(mapMotion::CenterOnPlayer(f.view), "Start"); mapMotion::Tick(1000);
        Get(f.content).position.x += 40; mapMotion::Tick(1500);
        Check(positions.size() == 1 && moves.empty(), "A drag during the glide cancels it without a final jump");
        mapMotion::Tick(3000); Check(positions.size() == 1, "Cancelled glides stay cancelled");
    }
    {
        Fixture f;
        Check(mapMotion::CenterOnPlayer(f.view), "Start"); mapMotion::Tick(1000);
        wheelHook(f.view, 1.0f, {0, 0}, false, nullptr); mapMotion::Tick(1016);
        Check(positions.size() == 1 && !wheelCalls.empty() && moves.empty(), "A wheel step during the glide cancels the glide and zooms instead");
    }
    {
        Fixture f;
        Get(f.content).position = {400 - 20.5f * 100, 300 - 15.5f * 100, 0}; Raw(f.view, 0x18, 1.0);
        Check(mapMotion::CenterOnPlayer(f.view) && moves.size() == 1 && positions.empty(), "An already centred view just confirms through the native move");
    }
    {
        Fixture f; playerWorld = 2;
        Check(!mapMotion::CenterOnPlayer(f.view) && moves.empty(), "Another world is left to the game's own centering");
        playerWorld = 1; positionAvailable = false;
        Check(!mapMotion::CenterOnPlayer(f.view), "An unknown player position is left to the game");
        positionAvailable = true; Get(f.parent).panel = nullptr;
        Check(!mapMotion::CenterOnPlayer(f.view), "A detached view is refused");
    }
}
void PlayerButtonHookAndLifecycle() {
    Fixture f;
    Check(centerHook(f.view, nullptr) == 0 && nativeCenters == 0, "The full map's player button starts the glide instead of the native jump");
    Fixture g; playerWorld = 2; Check(centerHook(g.view, nullptr) == 0 && nativeCenters == 1, "When the glide cannot start the native centering runs");
    Fixture h; Check(mapMotion::CenterOnPlayer(h.view), "Start"); mapMotion::Tick(1000);
    h.state.stopRequested = true; mapMotion::Tick(1500); Check(positions.size() == 1 && !mapMotion::CleanupPending(), "Stopping drops the glide without touching the view again");
}
}
int main() {
    try {
        fake::Initialize();
        mapMotion::Install(); mapMotion::Install();
        Check(guards.at("fhw.bexi") == 0x1C05470 && guards.at("fhw.bexn") == 0x1C06060 && guards.at("fhw.bexc") == 0x1C04D20 && guards.at("fhl.bepx") == 0x1C4EB30 && guards.at("fhw.bexl") == 0x1C05B90 && guards.at("fhw.beuu") == 0x1BFA420 && guards.at("VisualElement.UnityEngine.UIElements.ITransform.set_position") == 0x55F12D0, "Pinned native motion contracts");
        Check(wheelHook && centerHook, "Both native entry points are hooked once");
        for (auto test : {WheelGlides, CenteringGlidesAndZooms, CenteringKeepsCloserZoomAndStopsOnInteraction, PlayerButtonHookAndLifecycle}) test();
        std::cout << "MapMotionTests: smooth wheel zoom and animated centering passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; for (const auto& l : fake::logs) std::cerr << l << '\n'; return 1; }
}
