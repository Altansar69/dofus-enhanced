#include "appearance/appearance-refresh.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>

// Production refresh controller against synthetic objects. Never loads a game
// DLL, reads a process, invokes input, or touches user/game settings.
namespace fake {
using namespace managed;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; uintptr_t rva; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> bytes{};
    Class* klass = nullptr;
    int64_t number = 0;
    int flags = 1, countOverride = -2;
    std::vector<Obj> items;
    std::map<int64_t, Obj> byId;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xF12345670000ULL;
std::vector<Obj> refreshes;
std::vector<std::string> logs;
std::function<void(Obj)> onRefresh;
std::function<void()> onItem;
bool throwOnList = false;
Class* Type(const std::string& name) {
    auto& result = classes[name];
    if (!result) result = std::make_unique<Class>(Class{name});
    return result.get();
}
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake appearance object"); return *static_cast<Object*>(obj); }
Obj Make(const char* name) {
    auto result = std::make_unique<Object>(); result->klass = Type(name);
    auto ptr = result.get(); objects.push_back(std::move(result)); return ptr;
}
template<class T> void Put(Obj obj, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(obj).bytes.size(), "Appearance fake field overflow");
    std::memcpy(Get(obj).bytes.data() + offset, &value, sizeof(value));
}
Obj Box(int64_t value) { auto result = Make("Box"); Get(result).number = value; return result; }
void Initialize() {
    api.object_get_class = [](Obj obj) -> Klass { return Get(obj).klass; };
    api.object_unbox = [](Obj obj) -> void* { return &Get(obj).number; };
    api.gchandle_new = [](Obj obj, bool) -> GCHandle { auto id = ++nextHandle; handles[id] = obj; return id; };
    api.gchandle_get_target = [](GCHandle id) -> Obj { Check(handles.contains(id), "Invalid appearance handle"); return handles.at(id); };
    api.gchandle_free = [](GCHandle id) { Check(handles.erase(id) == 1, "Appearance handle double free"); };
}
}

namespace managed {
Api api{};
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    static const std::map<std::string, uintptr_t> rvas = {
        {"baml", 0x19579E0}, {"bamm", 0x1957A70}, {"bajh", 0x1951440},
        {"bajs", 0x19518F0}, {"vkq", 0x27695E0}, {"ric", 0x2619F70}
    };
    auto item = std::make_unique<fake::MethodInfo>(fake::MethodInfo{
        static_cast<fake::Class*>(klass), name, argc, rvas.at(name)});
    auto result = item.get(); fake::methods.push_back(std::move(item)); return result;
}
void* Api::Native(Method method, uintptr_t rva) const {
    fake::Check(static_cast<const fake::MethodInfo*>(method)->rva == rva, "Incorrect appearance native RVA");
    return reinterpret_cast<void*>(rva);
}
Obj Api::Call(Obj obj, const char* name, std::initializer_list<void*> args) const {
    auto& object = fake::Get(obj);
    if (std::string(name) == "get_Count") return fake::Box(object.countOverride == -2 ?
        static_cast<int>(object.items.size()) : object.countOverride);
    if (std::string(name) == "get_Item") {
        auto result = object.items.at(*static_cast<int*>(*args.begin()));
        if (fake::onItem) fake::onItem();
        return result;
    }
    throw std::runtime_error("Unexpected appearance managed call");
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    fake::Check(static_cast<int>(args.size()) == m.argc, "Incorrect appearance managed arity");
    auto& object = fake::Get(self); const std::vector<void*> values(args);
    if (m.name == "baml") {
        if (fake::throwOnList) throw std::runtime_error("Synthetic roster failure");
        auto list = fake::Make("List"); fake::Get(list).items = object.items;
        fake::Get(list).countOverride = object.countOverride; return list;
    }
    if (m.name == "bajh") return fake::Box(object.number);
    if (m.name == "bajs") return fake::Box((object.flags & *static_cast<int*>(values[0])) != 0);
    if (m.name == "bamm" || m.name == "vkq") {
        auto id = *static_cast<int64_t*>(values[0]); auto found = object.byId.find(id);
        Obj result = found == object.byId.end() ? nullptr : found->second;
        if (m.name == "vkq") return result;
        *static_cast<Obj*>(values[1]) = result; return fake::Box(result != nullptr);
    }
    if (m.name == "ric") {
        fake::refreshes.push_back(self);
        if (fake::onRefresh) fake::onRefresh(self);
        return nullptr;
    }
    throw std::runtime_error("Unexpected appearance invoke");
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return servicesReady && fight.get() && managed::Read<bool>(fight.get(), 0xB0); }
void Log(const std::string& message) { fake::logs.push_back(message); }

namespace {
using namespace fake;
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj fight, context, fighters, rendered, registry, creature, services;
    Fixture() {
        refreshes.clear(); logs.clear(); onRefresh = {}; onItem = {}; throwOnList = false;
        module = &state; state.servicesReady = true;
        fight = Make("fxc"); context = Make("fyb"); fighters = Make("evv");
        rendered = Make("fxw"); registry = Make("ebm"); creature = Make("exf"); services = Make("fsm");
        Put(fight, 0x18, context); Put(fight, 0xB0, true);
        Put(context, 0x58, fighters); Put(context, 0x68, rendered);
        Put(rendered, 0x150, registry); Put(rendered, 0x20, services); Put(creature, 0x10, services);
        state.fight.reset(fight); state.creatures.reset(creature);
    }
    ~Fixture() {
        state.fight.reset(); state.creatures.reset(); module = nullptr;
        Check(handles.empty(), "Appearance refresh leaked managed references");
    }
    Obj Entity(int64_t id) {
        auto info = Make("EntityInfo"); Get(info).number = id;
        Get(fighters).items.push_back(info); Get(fighters).byId[id] = info;
        auto entity = Make("AnimatedEntity"); Get(registry).byId[id] = entity;
        Put(entity, 0x1C8, id); Put(entity, 0x1A8, entity); Put(entity, 0x398, services);
        Put(entity, 0x170, Make("EntityLook")); Put(entity, 0xA8, Make("Animator2D"));
        Put(entity, 0x1E0, Make("Transform")); return entity;
    }
};
void RepeatedTrueAndStop() {
    Fixture f; auto first = f.Entity(1234567890123LL); auto second = f.Entity(-9876543210123LL);
    Get(f.fighters).items.push_back(Get(f.fighters).items.front());
    RefreshCombatAppearance();
    Check(refreshes == std::vector<Obj>{first, second}, "Roots were skipped, duplicated or IDs truncated");
    RefreshCombatAppearance();
    Check(refreshes.size() == 4, "Repeated true refresh was incorrectly deduplicated");
    f.state.stopped.store(true); RefreshCombatAppearance();
    Check(refreshes.size() == 6, "Stop restoration did not refresh native appearance");
}
void MembershipAndAvailability() {
    Fixture f; auto good = f.Entity(1); auto child = f.Entity(2); Put(child, 0x1A8, good);
    auto wrongContext = f.Entity(3); Put(wrongContext, 0x398, Make("OtherContext"));
    auto wrongId = f.Entity(4); Put(wrongId, 0x1C8, int64_t{99});
    auto incomplete = f.Entity(5); Put<Obj>(incomplete, 0xA8, nullptr);
    f.Entity(6); Get(Get(f.fighters).byId.at(6)).flags = 0;
    f.Entity(7); Get(f.registry).byId.erase(7);
    f.Entity(8); Get(Get(f.fighters).byId.at(8)).klass = Type("UnexpectedInfo");
    RefreshCombatAppearance(); Check(refreshes == std::vector<Obj>{good}, "Invalid entity was refreshed");
    refreshes.clear(); Put(f.rendered, 0x20, Make("WrongServices"));
    RefreshCombatAppearance(); Check(refreshes.empty(), "Wrong renderer service context accepted");
    Put(f.rendered, 0x20, f.services); Put(f.fight, 0xB0, false);
    RefreshCombatAppearance(); Check(refreshes.empty(), "Appearance refreshed outside combat");
    Put(f.fight, 0xB0, true); Get(f.registry).klass = Type("UnexpectedRegistry");
    RefreshCombatAppearance(); Check(refreshes.empty(), "Wrong registry layout accepted");
}
void SnapshotAndRevalidation() {
    Fixture f; auto first = f.Entity(1); f.Entity(2);
    onRefresh = [&](Obj entity) {
        Check(entity == first, "Stale second root was refreshed");
        Get(f.registry).byId.erase(2);
        f.Entity(3); // Cannot be processed during this already-built snapshot.
        RefreshCombatAppearance(); // Native callback reentrancy is suppressed.
    };
    RefreshCombatAppearance(); Check(refreshes == std::vector<Obj>{first}, "Snapshot/reentrancy protection failed");
    onRefresh = {}; refreshes.clear();
    onItem = [&] { Put(f.fight, 0x18, Make("fyb")); };
    RefreshCombatAppearance(); Check(refreshes.empty(), "Changed fight context refreshed old roots");
}
void BoundsAndErrors() {
    Fixture f; f.Entity(1);
    for (int count : {-1, 513}) {
        Get(f.fighters).countOverride = count; RefreshCombatAppearance();
        Check(refreshes.empty(), "Unsafe roster count was enumerated");
    }
    Get(f.fighters).countOverride = -2; throwOnList = true;
    RefreshCombatAppearance(); RefreshCombatAppearance();
    Check(logs.size() == 1, "Identical refresh errors spammed the log");
    throwOnList = false; RefreshCombatAppearance(); Check(refreshes.size() == 1, "Refresh did not recover after error");
    throwOnList = true; RefreshCombatAppearance(); Check(logs.size() == 2, "Recovered refresh did not reset diagnostic throttle");
}
}
int main() {
    try {
        fake::Initialize(); InstallCombatAppearanceRefresh();
        RepeatedTrueAndStop(); MembershipAndAvailability(); SnapshotAndRevalidation(); BoundsAndErrors();
        std::cout << "Combat appearance refresh offline fixtures passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
