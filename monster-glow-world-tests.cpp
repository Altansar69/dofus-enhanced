#include "monster-glow-world.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>

// Production provider with synthetic objects only; no game binary or process.
namespace fake {
using namespace managed;
using namespace monsterGlow;
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Class { std::string name; };
struct MethodInfo { std::string name; int argc; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> bytes{};
    Class* klass{};
    int64_t value = 0;
    int layer = 1000, order = 0, queue = 3000, group = -42, version = 0, countOverride = -1;
    bool exists = true, enabled = true, active = true, ready = true;
    Obj gameObject{};
    std::vector<Obj> items, materials;
    ScreenRect bounds{0, 0, 100, 100};
    const char* projectionReason = "ready";
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xE321000000ULL;
Obj service{};
std::function<void()> onItem, onProject;
int projections = 0;
bool throwMaterials = false;
std::map<std::string, size_t> offsets{{"doqa",0x110},{"dgjy",0x78},{"dgjz",0x80},{"dgfq",0x10},
    {"dggz",0x2E0},{"dgfm",0x10},{"dgfo",0x20},{"id",0x18},{"meshRenderer",0x20}};
Class* Type(const std::string& name) {
    auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get();
}
Object& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Object*>(object); }
Obj Make(const char* name) {
    auto object = std::make_unique<Object>(); object->klass = Type(name);
    auto result = object.get(); objects.push_back(std::move(object)); return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).bytes.size(), "Fake field overflow");
    std::memcpy(Get(object).bytes.data() + offset, &value, sizeof(value));
}
template<class T> Obj Box(T value) {
    auto result = Make("Box"); std::memcpy(&Get(result).value, &value, sizeof(value)); return result;
}
void Initialize() {
    api.object_get_class = [](Obj object)->Klass { return Get(object).klass; };
    api.object_unbox = [](Obj object)->void* { return &Get(object).value; };
    api.gchandle_new = [](Obj object,bool)->GCHandle { handles[++nextHandle] = object; return nextHandle; };
    api.gchandle_get_target = [](GCHandle h)->Obj { Check(handles.contains(h), "Stale GC root"); return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Double GC free"); };
    api.class_get_field_from_name = [](Klass,const char* name)->void* {
        auto found = offsets.find(name); return found == offsets.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field)->size_t { return *static_cast<size_t*>(field); };
    api.method_get_param = [](Method, uint32_t)->const void* { return Type("MaterialList"); };
    api.class_from_type = [](const void* type)->Klass { return const_cast<void*>(type); };
}
}
namespace managed {
Api api{};
Klass Api::Class(const char*,const char*,const char* name) const { return fake::Type(name); }
Method Api::FindVerified(Klass,const char* name,int argc,uintptr_t rva) const {
    static const std::map<std::string,uintptr_t> expected{{"xkj",0x18389C0},{"xkd",0x11ECD00},
        {"op_Implicit",0x5439190},{"get_enabled",0x53F2300},{"get_gameObject",0x5429D00},
        {"get_activeInHierarchy",0x5432080},{"get_sortingLayerID",0x53F2600},
        {"GetLayerValueFromID",0x53C6FE0},{"get_sortingOrder",0x53F2820},
        {"GetSharedMaterials",0x53F1AD0},{"get_renderQueue",0x53E55A0},
        {"get_sortingGroupID",0x53F2480},{"get_invalidSortingGroupID",0x54777A0}};
    fake::Check(expected.at(name) == rva, "Unexpected world RVA");
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{name,argc});
    auto result = method.get(); fake::methods.push_back(std::move(method)); return result;
}
Obj Api::New(Klass type,std::initializer_list<void*>) const { return fake::Make(static_cast<fake::Class*>(type)->name.c_str()); }
void* Api::FieldAddress(Obj object,const char* name) const {
    fake::Check(std::string(name) == "_version", "Unexpected reflected field"); return &fake::Get(object).version;
}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args) const {
    auto& object = fake::Get(self); std::string method = name;
    if (method == "get_Count") return fake::Box(object.countOverride < 0 ? static_cast<int>(object.items.size()) : object.countOverride);
    if (method == "get_Item") {
        auto result = object.items.at(*static_cast<int*>(*args.begin()));
        if (fake::onItem) fake::onItem(); return result;
    }
    if (method == "Clear") { object.items.clear(); return nullptr; }
    throw std::runtime_error("Unexpected world call");
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method); std::vector<void*> p(args);
    fake::Check(static_cast<int>(p.size()) == m.argc, "World arity mismatch");
    if (m.name == "op_Implicit") return fake::Box(p[0] && fake::Get(p[0]).exists);
    if (m.name == "get_invalidSortingGroupID") return fake::Box(-42);
    if (m.name == "GetLayerValueFromID") {
        const int id = *static_cast<int*>(p[0]);
        return fake::Box(id == 1000 ? 10 : id == 2 ? 20 : id == 2000 ? 0 : id);
    }
    auto& object = fake::Get(self);
    if (m.name == "xkj") return fake::Box(object.ready);
    if (m.name == "xkd") return Read<Obj>(self,0x118);
    if (m.name == "get_enabled") return fake::Box(object.enabled);
    if (m.name == "get_gameObject") return object.gameObject;
    if (m.name == "get_activeInHierarchy") return fake::Box(object.active);
    if (m.name == "get_sortingLayerID") return fake::Box(object.layer);
    if (m.name == "get_sortingOrder") return fake::Box(object.order);
    if (m.name == "get_sortingGroupID") return fake::Box(object.group);
    if (m.name == "get_renderQueue") return fake::Box(object.queue);
    if (m.name == "GetSharedMaterials") {
        if (fake::throwMaterials) throw std::runtime_error("Synthetic material failure");
        fake::Get(p[0]).items = object.materials; return nullptr;
    }
    throw std::runtime_error("Unexpected world invocation");
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const { fake::Check(std::string(name) == "fpm", "Unexpected service"); return fake::service; }
void Log(const std::string&) {}
namespace monsterGlow {
UiProjection ProjectGlowRenderers(const UiFrame& ui,const std::vector<managed::Obj>& renderers,bool everywhere) {
    fake::Check(everywhere, "Decor projection must use whole viewport");
    fake::Check(!renderers.empty(), "Empty projection input"); ++fake::projections;
    if (fake::onProject) fake::onProject();
    UiProjection result; auto& object = fake::Get(renderers.front());
    result.valid = ui.valid && std::string(object.projectionReason) == "ready";
    result.reason = object.projectionReason; result.screenRect = object.bounds; return result;
}
}
namespace {
using namespace fake;
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj map{}, loader{}, loading{}, resources{}, sprites{}, monster{};
    UiFrame ui;
    Fixture() {
        module = &state; state.servicesReady = true; onItem = {}; onProject = {}; projections = 0; throwMaterials = false;
        state.compositor.reset(Make("Compositor")); service = Make("epf"); map = Make("ClientMapData");
        loader = Make("ue"); loading = Make("tu"); resources = Make("tt"); sprites = Make("List");
        Put(service,0x110,loader); Put(service,0x118,map); Put(map,0x18,123);
        Put(loader,0x78,loading); Put(loader,0x80,resources); Put(loading,0x10,int64_t{123});
        // Native qhw leaves a fresh empty tt as the next preload buffer.
        Put(loading,0x2E0,Make("tt"));
        Put(resources,0x10,int64_t{123}); Put(resources,0x20,sprites);
        ui.valid = true; ui.serial = 1; ui.generation = 1; monster = Renderer();
    }
    ~Fixture() { state.compositor.reset(); module = nullptr; Check(handles.empty(), "World reader leaked GC references"); }
    Obj Renderer(int order = 0) {
        auto r = Make("MeshRenderer"); Get(r).order = order; Get(r).gameObject = Make("GameObject");
        Get(r).materials = {Make("Material")}; return r;
    }
    Obj Decor(int order = 1) {
        auto e = Make("MapElement"), r = Renderer(order); Put(e,0x20,r); Get(sprites).items.push_back(e); return r;
    }
    WorldSelection Select(const WorldFrame& world) { return ForegroundGlowDecor(world,ui,{monster},{0,0,100,100}); }
};
void Ordering() {
    Fixture f;
    auto front = f.Decor(2); f.Decor(-1); f.Decor(0);
    auto upper = f.Decor(-100); Get(upper).layer = 2;
    auto rear = f.Decor(100); Get(rear).layer = 2000;
    auto w = ReadGlowWorld(); Check(w.valid, "Ready map rejected");
    auto result = f.Select(w); Check(result.valid && result.renderers.size() == 2, "Native layer value/order selection failed");
    Check(result.renderers[0].get() == front && result.renderers[1].get() == upper, "Layer ID compared instead of layer value");
}
void VisibilityAndBounds() {
    Fixture f;
    Get(f.Decor()).enabled = false;
    auto inactive = f.Decor(); Get(Get(inactive).gameObject).active = false;
    Get(f.Decor()).exists = false;
    f.Decor(); Get(Get(f.sprites).items.back()).exists = false;
    auto outside = f.Decor(); Get(outside).bounds = {200,200,10,10};
    auto offscreen = f.Decor(); Get(offscreen).projectionReason = "offscreen";
    auto w = ReadGlowWorld(); auto result = f.Select(w);
    Check(w.valid && result.valid && result.renderers.empty(), "Hidden/outside decor included");
}
void StaleAndLoading() {
    Fixture f; f.Decor();
    Get(service).ready = false; Check(!ReadGlowWorld().valid, "Loading map accepted"); Get(service).ready = true;
    Put(f.resources,0x10,int64_t{124}); Check(!ReadGlowWorld().valid, "Old resource map accepted"); Put(f.resources,0x10,int64_t{123});
    Put(f.loading,0x2E0,Make("NextMapBuildBuffer")); Check(ReadGlowWorld().valid, "Cleared preload buffer used instead of active map");
    Put(f.loading,0x10,int64_t{999}); Check(ReadGlowWorld().valid, "Next map preload invalidated active map");
    auto w = ReadGlowWorld(); ++Get(f.sprites).version; Check(!f.Select(w).valid, "Version-mutated map accepted");
}
void ConcurrentMutation() {
    Fixture f; f.Decor(); onItem = [&] { ++Get(f.sprites).version; };
    auto w = ReadGlowWorld(); Check(!w.valid && w.decor.empty(), "Partially read map accepted");
    onItem = {}; w = ReadGlowWorld();
    onProject = [&] { Put(f.resources,0x20,Make("ReplacementList")); };
    auto result = f.Select(w); Check(!result.valid && result.renderers.empty(), "Replaced map survived projection");
}
void Limits() {
    Fixture f; f.Decor(); auto element = Get(f.sprites).items[0];
    Get(f.sprites).items.assign(4096,element); auto w = ReadGlowWorld(); Check(w.valid && w.decor.size() == 1, "4096 scan or dedup failed");
    Get(f.sprites).items.push_back(element); Check(!ReadGlowWorld().valid, "Unbounded map scan accepted");
    Get(f.sprites).items.clear(); for (int i=0;i<65;++i) f.Decor();
    w = ReadGlowWorld(); auto result = f.Select(w);
    Check(!result.valid && result.renderers.empty() && std::string(result.reason) == "foreground-limit", "Partial foreground overflow accepted");
    Get(f.sprites).items.pop_back(); ++Get(f.sprites).version; w = ReadGlowWorld(); result = f.Select(w);
    Check(result.valid && result.renderers.size() == 64, "64 foreground limit rejected");
}
void UnknownOrdering() {
    Fixture f; auto decor = f.Decor(); Get(decor).group = 3;
    auto w = ReadGlowWorld(); Check(f.Select(w).renderers.empty(), "SortingGroup child treated as world order");
    Get(decor).group = -42; Get(Get(decor).materials[0]).queue = 2999;
    Check(f.Select(w).renderers.empty(), "Contradictory queue accepted");
    Get(Get(decor).materials[0]).queue = 3001;
    Check(f.Select(w).renderers.size() == 1, "Consistent queue/order rejected");
    auto extra = Make("Material"); Get(extra).queue = 3100; Get(decor).materials.push_back(extra);
    Check(f.Select(w).renderers.empty(), "Mixed queue renderer assumed ordered");
    Get(f.monster).group = 3; Check(!f.Select(w).valid, "Unknown target group accepted");
}
void ProjectionAndCleanup() {
    Fixture f; auto decor = f.Decor(); auto w = ReadGlowWorld();
    for (int i=0;i<8;++i) Check(f.Select(w).renderers.size() == 1, "Cached selection failed");
    Check(projections == 9, "World bounds projected repeatedly for each monster");
    ++f.ui.serial; Get(decor).projectionReason = "invalid-bounds";
    auto result = f.Select(w); Check(!result.valid && result.renderers.empty(), "Invalid bounds treated complete");
    throwMaterials = true; Check(!f.Select(w).valid, "Managed error escaped provider");
}
void RevalidationAndParts() {
    Fixture f; auto decor = f.Decor(2); auto w = ReadGlowWorld();
    auto second = f.Renderer(3);
    auto result = ForegroundGlowDecor(w,f.ui,{f.monster,second},{0,0,100,100});
    Check(result.valid && result.renderers.empty(), "Decor behind a mount part accepted");
    Get(decor).order = 4; result = f.Select(w); Check(!result.valid, "Changed decor order accepted");
    Get(decor).order = 2; Get(decor).enabled = false; Check(f.Select(w).renderers.empty(), "Disabled after snapshot retained");
    Get(decor).enabled = true; Get(f.map).exists = false; Check(!f.Select(w).valid, "Destroyed map retained");
}
}
int main() {
    try {
        Initialize(); monsterGlow::InstallGlowWorld();
        Ordering(); VisibilityAndBounds(); StaleAndLoading(); ConcurrentMutation(); Limits();
        UnknownOrdering(); ProjectionAndCleanup(); RevalidationAndParts();
        std::cout << "MonsterGlowWorldTests: 8 suites passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
