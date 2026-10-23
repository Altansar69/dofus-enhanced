#include "treasure/treasure-hunt.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

// Offline fixture for the hunt reader: a synthetic service table and step
// list laid out like the pinned game objects. No client or network access.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* type; std::string name; int argc; uintptr_t rva; };
struct Object {
    // Raw value area first: managed::Read<T>(object, offset) reads it directly.
    alignas(8) std::array<unsigned char, 64> value{};
    Class* type{};
    std::map<int, Obj> table;
    std::vector<Obj> items;
    std::map<std::string, Obj> refs;
    std::string text;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle, Obj> handles;
std::map<std::string, uintptr_t> guards;
std::map<std::string, size_t> offsets{{"<dted>k__BackingField", 0x48}, {"dcmw", 0x20}, {"dcmj", 0x10}, {"dcmk", 0x14}, {"dcml", 0x18}, {"dcmn", 0x20}, {"dcmp", 0x28}};
GCHandle nextHandle = 0xCAFE00000000ULL;
Obj service{}, poiRoot{};
bool poiLookupFails = false;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
Class* Type(const std::string& n) { auto& p = classes[n]; if (!p) p = std::make_unique<Class>(Class{n}); return p.get(); }
Object& Get(Obj o) { Check(o != nullptr, "Null fake object"); return *static_cast<Object*>(o); }
Obj Make(const std::string& n) { auto p = std::make_unique<Object>(); p->type = Type(n); auto result = p.get(); objects.push_back(std::move(p)); return result; }
template<class T> Obj Box(T value) { auto o = Make("Box"); std::memcpy(Get(o).value.data(), &value, sizeof(value)); return o; }
// Raw field reads use the pinned offsets inside the 64-byte fake value area.
template<class T> void Raw(Obj o, size_t offset, T value) { std::memcpy(Get(o).value.data() + offset, &value, sizeof(value)); }
Obj Step(unsigned index, int type, int direction, unsigned poi, unsigned other) {
    auto step = Make("gz"); Raw(step, 0x10, index); Raw(step, 0x14, type); Raw(step, 0x18, direction); Raw(step, 0x20, poi); Raw(step, 0x28, other); return step;
}
void Initialize() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).type; };
    api.object_unbox = [](Obj o) -> void* { return Get(o).value.data(); };
    api.string_new = [](const char* s) -> Obj { auto o = Make("String"); Get(o).text = s; return o; };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++nextHandle] = o; return nextHandle; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Double GC release"); };
    api.class_get_field_from_name = [](Klass, const char* n) -> void* { auto it = offsets.find(n); return it == offsets.end() ? nullptr : &it->second; };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
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
Method Api::Find(Klass type, const char* n, int argc) const { return FindVerified(type, n, argc, 1); }
Obj Api::Field(Obj o, const char* n) const { return fake::Get(o).refs[n]; }
std::string Api::String(Obj o) const { return o ? fake::Get(o).text : std::string{}; }
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake; const auto& m = *static_cast<const MethodInfo*>(method); std::vector<void*> a(args);
    Check(int(a.size()) == m.argc, "Native arity mismatch");
    if (m.name == "bdbm") { Check(m.rva == 0x69F1E0 && self == service, "Hunt table getter on the pinned service"); return Get(self).refs["hunts"]; }
    if (m.name == "get_pointsOfInterestDataRoot") { Check(m.rva == 0x7BFD80 && !self, "Static metadata root"); return poiRoot; }
    if (m.name == "get_name") { Check(m.rva == 0x7C7FC0, "Point of interest name"); return Get(self).refs["name"]; }
    throw std::runtime_error("Unexpected fake invocation: " + m.name);
}
Obj Api::Call(Obj self, const char* n, std::initializer_list<void*> args) const {
    using namespace fake; auto& o = Get(self); std::string name = n; std::vector<void*> a(args);
    if (name == "TryGetValue") { auto it = o.table.find(*static_cast<int*>(a[0])); *static_cast<Obj*>(a[1]) = it == o.table.end() ? nullptr : it->second; return Box(it != o.table.end()); }
    if (name == "get_Count") return Box(int(o.items.size()));
    if (name == "get_Item") return o.items.at(*static_cast<int*>(a[0]));
    if (name == "GetObjectById") {
        if (poiLookupFails) throw std::runtime_error("Synthetic metadata failure");
        const auto id = *static_cast<int64_t*>(a[0]); auto it = o.table.find(int(id)); return it == o.table.end() ? nullptr : it->second;
    }
    throw std::runtime_error("Unexpected fake call: " + name);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const { fake::Check(std::string(name) == "fov", "Hunt reader asks for the hunt service interface only"); return fake::service; }
void Log(const std::string&) {}
namespace {
using namespace fake;
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj hunts{}, classic{}, steps{};
    Fixture() {
        module = &state; state.ready = true; state.servicesReady = true;
        service = Make("fdl"); hunts = Make("Dictionary"); Get(service).refs["hunts"] = hunts;
        classic = Make("ha"); steps = Make("List"); Get(classic).refs["dcmw"] = steps; Get(hunts).table[0] = classic;
        poiRoot = Make("PointsOfInterestDataRoot");
        auto statue = Make("PointOfInterestData"); Get(statue).refs["name"] = api.string_new("Statue de Xélor"); Get(poiRoot).table[14] = statue;
        poiLookupFails = false;
    }
    ~Fixture() { module = nullptr; Check(handles.empty(), "Hunt reader leaked GC roots"); }
};
}
int main() {
    try {
        fake::Initialize();
        treasure::InstallHunt(); treasure::InstallHunt();
        Check(guards.at("fdl.bdbm") == 0x69F1E0 && guards.at("PointOfInterestData.get_name") == 0x7C7FC0 && guards.at("DataCenterModule.get_pointsOfInterestDataRoot") == 0x7BFD80, "Pinned native hunt contracts");
        {
            Fixture f;
            Check(!treasure::CurrentStep().active, "No known step means no active hunt");
            Get(f.steps).items = {Step(0, 0, 0, 0, 0), Step(1, 3, 2, 0, 5), Step(2, 1, 4, 14, 0), Step(3, 4, 0, 0, 0), Step(4, 2, 0, 0, 0)};
            const auto step = treasure::CurrentStep();
            Check(step.active && step.index == 2 && step.type == 1 && step.direction == 4 && step.poiId == 14 && step.hint == "Statue de Xélor", "Last known step wins over placeholders, with its direction and point of interest name");
            Get(f.steps).items = {Step(0, 0, 0, 0, 0), Step(1, 5, 6, 0, 77)};
            const auto hint = treasure::CurrentStep();
            Check(hint.active && hint.type == 5 && hint.direction == 6 && hint.npcId == 77 && hint.poiId == 0 && hint.hint.empty(), "NPC hint steps expose the NPC and no point of interest");
            Get(f.steps).items = {Step(0, 0, 0, 0, 0), Step(1, 1, 0, 99, 0)};
            Check(treasure::CurrentStep().hint.empty() && treasure::CurrentStep().poiId == 99, "Unknown points of interest keep their identifier without a name");
            poiLookupFails = true; Get(f.steps).items = {Step(1, 1, 2, 14, 0)};
            Check(treasure::CurrentStep().active && treasure::CurrentStep().hint.empty(), "A metadata failure never hides the step");
            poiLookupFails = false;
            auto legendary = Make("ha"), legendarySteps = Make("List"); Get(legendary).refs["dcmw"] = legendarySteps; Get(legendarySteps).items = {Step(1, 3, 0, 0, 3)};
            Get(f.hunts).table.erase(0); Get(f.hunts).table[2] = legendary;
            Check(treasure::CurrentStep().type == 3 && treasure::CurrentStep().mapCount == 3, "Legendary hunts are read when no classic hunt runs");
            Get(f.steps).items.resize(65, Step(1, 1, 0, 1, 0)); Get(f.hunts).table[0] = f.classic;
            bool rejected = false; try { treasure::CurrentStep(); } catch (const std::exception&) { rejected = true; }
            Check(rejected, "Oversized step lists are rejected");
            Get(f.steps).items = {Step(1, 1, 2, 14, 0)};
            const auto pinned = service; service = Make("Other"); Check(!treasure::CurrentStep().active, "A service of another type is never read"); service = pinned;
            f.state.stopRequested = true; Check(!treasure::CurrentStep().active, "A stopping module reads nothing");
        }
        std::cout << "TreasureHuntTests: hunt state reader passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
