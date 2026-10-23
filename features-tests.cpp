#include "features.hpp"
#include "module.hpp"
#include "options-ui.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <source_location>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// Actual features.cpp with simulated managed objects; no game process or DLL.
namespace fake {
using namespace managed;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(float actual, float expected, std::source_location location = std::source_location::current()) { if (!std::isfinite(actual) || std::abs(actual - expected) > std::max(0.00001f, std::abs(expected) * 0.000001f)) throw std::runtime_error("Incorrect scale at line " + std::to_string(location.line()) + ": actual=" + std::to_string(actual) + " expected=" + std::to_string(expected)); }
struct Class { std::string name; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> memory{};
    Class* type = nullptr;
    bool boolean = false;
    int64_t integer = 0;
    int flags = 0;
    bool summoned = false, excluded = false, teamPresent = true;
    int8_t team = 0;
    int64_t id = 0, summoner = 0;       // EntityInfo.bajh, long metadata key 2 (balc); lg.oab
    bool summonerPresent = false;
    std::vector<Obj> items;             // party member list (fti.bfmh)
    std::map<int64_t, Obj> fighters;
    std::string text;
    std::map<std::string, Obj> fields;
    std::map<std::pair<int, int>, Obj> sublooks;
};
struct MethodInfo { Class* type; std::string name; int argc; uintptr_t rva; };
struct Declaration { const char* type; const char* name; int argc; uintptr_t rva; };
constexpr Declaration declarations[] = {
    {"EntityLook", "dbfj", 0, 0x79A3D0}, {"EntityLook", "dbev", 1, 0x799F70},
    {"EntityLook", "dbex", 1, 0x79A060}, {"EntityInfo", "bajs", 1, 0x19518F0},
    {"EntityInfo", "bake", 1, 0x1951DB0}, {"EntityInfo", "bajy", 2, 0x1951B60},
    {"evv", "bamm", 2, 0x1957A70}, {"esg", "zcx", 0, 0x18BB140},
    {"exf", "baza", 1, 0x1975060}, {"exf", "bazg", 2, 0x19755D0},
    {"mhy", "dcdf", 3, 0x76F790},
    {"AnimatedEntity", "rez", 0, 0x2614FF0},
    {"AnimatedEntity", "rfa", 0, 0x2615260},
    {"AnimatedEntity", "rgx", 0, 0x26173C0},
    {"yh", "rmo", 1, 0x2652CF0}, {"yh", "rmp", 1, 0x2652E50},
    {"EntityInfo", "bajh", 0, 0x1951440}, {"EntityInfo", "balc", 2, 0x1952710},
    {"fti", "bfmh", 0, 0x752E20}, {"lg", "oab", 0, 0x68D2B0},
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x123456780000ULL;
unsigned clones = 0, barriers = 0;
// Category reads: entity IDs, summoners, local character lookups out of combat.
unsigned idReads = 0, summonerReads = 0, characterLookups = 0;
Obj characterService = nullptr, partyService = nullptr;
unsigned nativeCreatureLookCalls = 0;
unsigned nativeCacheClears = 0, nativeCacheComputations = 0;
using CreatureLookFn = Obj(*)(Obj, Method);
CreatureLookFn creatureLookHook = nullptr;
using CreatureHideFn = bool(*)(Obj, Method);
CreatureHideFn creatureHideHook = nullptr, creatureForcedHideHook = nullptr;
using ConvertFn = void(*)(Obj, Obj, Obj*, Method);
ConvertFn convertHook = nullptr;
Obj activeRegistry = nullptr;
std::function<void()> duringRefresh, beforeNativeLookReturn;
void** lastBarrierAddress = nullptr;
std::vector<std::pair<Obj, bool>> refreshes;
Obj assetCache = nullptr, boneTable = nullptr, skinTable = nullptr;
std::map<std::string, Obj> assets;
Object& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Object*>(object); }
Class* Type(const std::string& name) {
    auto& entry = classes[name];
    if (!entry) entry = std::make_unique<Class>(Class{name});
    return entry.get();
}
Obj Make(const char* name) {
    auto value = std::make_unique<Object>(); value->type = Type(name);
    auto result = value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> void Put(Obj object, size_t offset, T value) {
    Check(offset + sizeof(T) <= Get(object).memory.size(), "Fake field exceeds storage");
    std::memcpy(Get(object).memory.data() + offset, &value, sizeof(value));
}
Obj Box(bool value) { auto result = Make("Boolean"); Get(result).boolean = value; return result; }
Obj BoxId(int64_t value) { auto result = Make("Int64"); Get(result).integer = value; return result; }
Obj Text(const std::string& value) { auto result = Make("String"); Get(result).text = value; return result; }
Obj SizedLook(int bone, float height, float scale = 1.0f) {
    auto look = Make("EntityLook"); Put(look, 0x1C, bone); Put(look, 0x2C, scale); Put(look, 0x30, scale);
    auto definition = Make("AnimatedObjectDefinition"), animations = Make("Array");
    Get(definition).fields["animations"] = animations; Get(animations).integer = 2;
    auto idleAnimation = Make("Animation"), attack = Make("Animation");
    Get(idleAnimation).fields["name"] = Text("AnimStatique_0"); Put(idleAnimation, 0x34, height);
    Get(attack).fields["name"] = Text("AnimAttaque_0"); Put(attack, 0x34, height * 100);
    Put(animations, 0x20, idleAnimation); Put(animations, 0x28, attack);
    assets[std::to_string(bone)] = definition;
    return look;
}
Obj Info(int flags, int team, bool summoned = false) {
    auto result = Make("EntityInfo"); Get(result).flags = flags;
    Get(result).team = static_cast<int8_t>(team); Get(result).summoned = summoned; return result;
}
Obj Look(float x = 2.0f, float y = 3.0f) {
    auto result = Make("EntityLook"); Put(result, 0x2C, x); Put(result, 0x30, y);
    Put<uint64_t>(result, 0x38, 0x1122334455667788ULL); return result;
}
Obj CloneLook(Obj source) {
    auto result = Make("EntityLook");
    Get(result).memory = Get(source).memory;
    Get(result).fields = Get(source).fields;
    ++clones;
    for (const auto& [slot, child] : Get(source).sublooks)
        Get(result).sublooks.emplace(slot, CloneLook(child));
    return result;
}
void InitializeApi() {
    api.string_new = [](const char* value) -> Obj { return Text(value); };
    api.object_get_class = [](Obj object) -> Klass { return Get(object).type; };
    api.class_get_name = [](Klass type) -> const char* { return static_cast<Class*>(type)->name.c_str(); };
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.object_unbox = [](Obj object) -> void* {
        auto& value = Get(object);
        if (value.type->name == "Boolean") return &value.boolean;
        if (value.type->name == "Int64" || value.type->name == "Int32") return &value.integer;
        throw std::runtime_error("Unexpected boxed type");
    };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        auto handle = ++nextHandle; handles.emplace(handle, object); return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        auto found = handles.find(handle); Check(found != handles.end(), "Invalid GC handle"); return found->second;
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC handle freed twice"); };
    api.gc_wbarrier_set_field = [](Obj, void** address, Obj value) {
        ++barriers; lastBarrierAddress = address; *address = value;
    };
    // lg.ddku, the accepted-member flag, verified at offset 0x20.
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        return static_cast<Class*>(type)->name == "lg" && std::string(name) == "ddku" ? reinterpret_cast<void*>(0x20) : nullptr;
    };
    api.field_get_offset = [](void* field) -> size_t { return reinterpret_cast<size_t>(field); };
}
Obj Member(int64_t id, bool accepted) {
    auto member = Make("lg"); Get(member).id = id; Put(member, 0x20, accepted); return member;
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto type = static_cast<fake::Class*>(klass);
    for (const auto& declaration : fake::declarations) {
        if (type->name == declaration.type && std::string(name) == declaration.name && argc == declaration.argc) {
            auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{type, name, argc, declaration.rva});
            auto result = method.get(); fake::methods.push_back(std::move(method)); return result;
        }
    }
    throw std::runtime_error("Unexpected method lookup: " + type->name + "." + name);
}
void* Api::Native(Method method, uintptr_t expectedRva) const {
    fake::Check(static_cast<const fake::MethodInfo*>(method)->rva == expectedRva, "Method RVA mismatch");
    return reinterpret_cast<void*>(expectedRva);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t expectedRva) const {
    auto method = Find(klass, name, argc);
    fake::Check(static_cast<const fake::MethodInfo*>(method)->rva == expectedRva, "Verified appearance contract mismatch");
    return method;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    using namespace fake;
    Check(method != nullptr, "Null managed method");
    const auto& call = *static_cast<const MethodInfo*>(method);
    Check(call.argc == static_cast<int>(args.size()), "Incorrect argument count");
    const auto* parameters = args.begin();
    if (call.name == "dcdf") {
        Check(!self && String(parameters[1]) == "AnimStatique", "Wrong idle resolver arguments");
        *static_cast<Obj*>(parameters[2]) = Text(std::to_string(Read<int>(parameters[0], 0x1C)));
        return Box(true);
    }
    auto& value = Get(self); Check(value.type == call.type, "Wrong managed receiver");
    if (call.name == "bajs") {
        const int mask = *static_cast<int*>(parameters[0]); return Box((value.flags & mask) == mask);
    }
    if (call.name == "bake") {
        const int key = *static_cast<int*>(parameters[0]);
        Check(key == 1 || key == 9, "Unexpected bool metadata key"); return Box(key == 1 ? value.summoned : value.excluded);
    }
    if (call.name == "bajy") {
        Check(*static_cast<int*>(parameters[0]) == 6, "Wrong team key");
        if (value.teamPresent) *static_cast<int8_t*>(parameters[1]) = value.team;
        return Box(value.teamPresent);
    }
    if (call.name == "bamm") {
        auto found = value.fighters.find(*static_cast<int64_t*>(parameters[0]));
        *static_cast<Obj*>(parameters[1]) = found == value.fighters.end() ? nullptr : found->second;
        return Box(found != value.fighters.end());
    }
    if (call.name == "zcx") return BoxId(Read<int64_t>(Read<Obj>(self, 0x128), 0x10));
    if (call.name == "bajh") { ++idReads; return BoxId(value.id); }
    if (call.name == "balc") {
        Check(*static_cast<int*>(parameters[0]) == 2, "The summoner is long metadata key 2");
        ++summonerReads;
        if (value.summonerPresent) *static_cast<int64_t*>(parameters[1]) = value.summoner;
        return Box(value.summonerPresent);
    }
    if (call.name == "bfmh") { auto found = value.fields.find("members"); return found == value.fields.end() ? nullptr : found->second; }
    if (call.name == "oab") return BoxId(value.id);
    if (call.name == "dbfj") {
        return CloneLook(self);
    }
    if (call.name == "dbev" || call.name == "dbex") {
        Put(self, call.name == "dbev" ? 0x2C : 0x30, *static_cast<float*>(parameters[0])); return nullptr;
    }
    if (call.name == "baza") {
        const bool active = *static_cast<bool*>(parameters[0]);
        refreshes.emplace_back(self, active); Put(self, 0x48, active);
        if (duringRefresh) duringRefresh();
        return nullptr;
    }
    if (call.name == "rfa") {
        ++nativeCacheClears; Put<Obj>(self, 0x310, nullptr); return nullptr;
    }
    throw std::runtime_error("Unexpected invocation: " + call.name);
}
Obj Api::StaticField(Klass klass, const char* name) const {
    fake::Check(static_cast<fake::Class*>(klass)->name == "mhy", "Wrong cache owner");
    if (std::string(name) == "fhod") return fake::assetCache;
    if (std::string(name) == "fhns") return fake::boneTable;
    if (std::string(name) == "fhnr") return fake::skinTable;
    throw std::runtime_error("Unexpected static field");
}
Obj Api::Field(Obj self, const char* name) const {
    auto& fields = fake::Get(self).fields; auto found = fields.find(name);
    return found == fields.end() ? nullptr : found->second;
}
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : ""; }
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    using namespace fake;
    if (Get(self).type->name == "PartyList") {
        if (std::string(name) == "get_Count") { auto result = BoxId(static_cast<int64_t>(Get(self).items.size())); Get(result).type = Type("Int32"); return result; }
        Check(std::string(name) == "get_Item", "Unexpected party list call");
        return Get(self).items.at(*static_cast<int*>(*args.begin()));
    }
    if (std::string(name) == "get_Length" || std::string(name) == "get_Count") {
        Check(Get(self).type->name == "Array" || Get(self).type->name == "Riders", "Length read from wrong type");
        auto result = BoxId(Get(self).integer); Get(result).type = Type("Int32"); return result;
    }
    if (std::string(name) == "TryGetValue") {
        if (Get(self).type->name == "Children") {
            const auto* p = args.begin(); Check(*static_cast<int*>(p[0]) == 2, "Wrong rider slot");
            *static_cast<Obj*>(p[1]) = Get(self).fields["riders"]; return Box(true);
        }
        Check(self == assetCache && args.size() == 2, "Wrong asset cache read");
        const auto* p = args.begin(); auto found = assets.find(String(p[0]));
        struct CachedDefinition { Obj definition; int lifetime; int padding; };
        *static_cast<CachedDefinition*>(p[1]) = {found == assets.end() ? nullptr : found->second, 12, 0};
        return Box(found != assets.end());
    }
    throw std::runtime_error("Unexpected call");
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
bool Module::InCombat() const { return servicesReady && fight.get() && managed::Read<bool>(fight.get(), 0xB0); }
managed::Obj Module::FindService(const char* name) const {
    const std::string service = name;
    if (service == "fng") { ++fake::characterLookups; return fake::characterService; }
    if (service == "fne") return fake::partyService;
    fake::Check(service == "fso", "Unexpected service lookup");
    return fake::activeRegistry;
}
void Log(const std::string&) {}
static void NativeConvert(managed::Obj, managed::Obj info, managed::Obj* look, managed::Method) {
    if (fake::Get(info).excluded) return;
    auto flattened = fake::CloneLook(*look);
    auto rider = fake::Get(flattened).sublooks.find({2, 0});
    if (rider != fake::Get(flattened).sublooks.end()) flattened = rider->second;
    fake::Put(flattened, 0x1C, 3451); fake::Get(flattened).sublooks.clear(); *look = flattened;
}
static managed::Obj NativeCreatureLook(managed::Obj self, managed::Method) {
    ++fake::nativeCreatureLookCalls;
    if (!self) return nullptr;
    auto cached = managed::Read<managed::Obj>(self, 0x310);
    if (cached) return cached;
    auto root = managed::Read<managed::Obj>(self, 0x1A8);
    if (root && root != self) return fake::creatureLookHook(root, nullptr);
    auto source = managed::Read<managed::Obj>(self, 0x170);
    if (!source) return nullptr;
    ++fake::nativeCacheComputations;
    auto result = fake::CloneLook(source);
    auto info = fake::Get(fake::activeRegistry).fighters.at(managed::Read<int64_t>(self, 0x1C8));
    if (module->creatures.get() && managed::Read<bool>(module->creatures.get(), 0x48))
        fake::convertHook(module->creatures.get(), info, &result, nullptr);
    fake::Put(self, 0x310, result);
    if (fake::beforeNativeLookReturn) fake::beforeNativeLookReturn();
    return result;
}
static bool NativeCreatureHide(managed::Obj self, managed::Method) {
    return self && managed::Read<managed::Obj>(self, 0x1A8) != self;
}
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    managed::api.Native(method, rva);
    if (rva == 0x19755D0) {
        fake::convertHook = reinterpret_cast<fake::ConvertFn>(replacement);
        *original = reinterpret_cast<void*>(NativeConvert);
    }
    else if (rva == 0x2614FF0) {
        fake::creatureLookHook = reinterpret_cast<fake::CreatureLookFn>(replacement);
        *original = reinterpret_cast<void*>(NativeCreatureLook);
    } else if (rva == 0x2652CF0 || rva == 0x2652E50) {
        auto& hook = rva == 0x2652CF0 ? fake::creatureHideHook : fake::creatureForcedHideHook;
        hook = reinterpret_cast<fake::CreatureHideFn>(replacement);
        *original = reinterpret_cast<void*>(NativeCreatureHide);
    } else throw std::runtime_error("Unexpected feature hook");
}
void Features::InstallAnimation() {}
void PumpInstantWaits(bool) {}
void InstallCombatAppearanceRefresh() {}
void RefreshCombatAppearance() {} // Actual native-root refresh has its own production-code test target.

namespace fake {
struct Fixture {
    Module state{L"offline-features"};
    Obj service, fight, aggregate, character, identity, registry, local, context;
    static constexpr int64_t LocalId = 0x123456789ABCLL;
    Fixture() {
        module = &state; clones = barriers = 0; lastBarrierAddress = nullptr; refreshes.clear();
        assets.clear(); assetCache = Make("Cache"); boneTable = Make("BoneTable"); skinTable = Make("SkinTable");
        state.features = std::make_unique<Features>(); state.features->Install();
        state.servicesReady = true; state.ready.store(true);
        state.settings.mode = 2; state.settings.roleplayMode = 0;
        service = Make("exf"); fight = Make("fxc"); aggregate = Make("fyb"); character = Make("esg");
        identity = Make("z"); registry = Make("evv"); local = Info(0x81, 0); Get(local).id = LocalId;
        idReads = summonerReads = characterLookups = 0;
        Put(service, 0x48, true); Put(fight, 0xB0, true); Put(fight, 0x18, aggregate);
        Put(aggregate, 0xF0, character); Put(aggregate, 0x58, registry);
        Put(character, 0x128, identity); Put(identity, 0x10, LocalId);
        Get(registry).fighters.emplace(LocalId, local);
        activeRegistry = registry; nativeCreatureLookCalls = 0;
        nativeCacheClears = nativeCacheComputations = 0;
        duringRefresh = beforeNativeLookReturn = {};
        context = Make("Context"); Put(service, 0x10, context);
        state.creatures.reset(service); state.fight.reset(fight);
        // Out of combat: the same character service, and a party whose list
        // starts empty (members added by the tests).
        characterService = character;
        partyService = Make("fti"); Get(partyService).fields["members"] = Make("PartyList");
    }
    ~Fixture() { module = nullptr; }
    Features& F() { return *state.features; }
    void Combat(bool value) { Put(fight, 0xB0, value); }
    Obj Root(Obj look) {
        auto root = Make("AnimatedEntity"); Put(root, 0x170, look); Put(root, 0x1A8, root);
        Put(root, 0x1C8, LocalId); Put(root, 0x398, context); return root;
    }
};
void GroupsAndOriginalScale() {
    Fixture f;
    struct Case { int flags, team; bool summon; MiniatureGroup group; };
    const Case cases[] = {
        {0x81, 0, false, MiniatureGroup::AlliedPlayer}, {0x81, 1, false, MiniatureGroup::Enemy},
        {0x09, 1, false, MiniatureGroup::Enemy}, {0x89, 1, true, MiniatureGroup::EnemySummon},
        {0x89, 0, true, MiniatureGroup::AlliedSummon},
    };
    for (const auto& item : cases) {
        auto info = Info(item.flags, item.team, item.summon);
        Check(f.F().Group(info) == item.group, "Wrong combat category");
        auto original = Look(); Obj displayed = original;
        auto oldClones = clones, oldBarriers = barriers;
        Check(f.F().Convert(f.service, info, &displayed), "Conversion not handled");
        Check(displayed != original && clones == oldClones + 1, "Must clone scaled appearance");
        Check(barriers == oldBarriers + 1 && lastBarrierAddress == &displayed, "GC write barrier bypassed");
        Near(Read<float>(displayed, 0x2C), 1.2f);
        Near(Read<float>(displayed, 0x30), 1.8f);
        Near(Read<float>(original, 0x2C), 2.0f); Near(Read<float>(original, 0x30), 3.0f);
        Check(Read<uint64_t>(displayed, 0x38) == 0x1122334455667788ULL, "Appearance data discarded");
        Obj second = original; Check(f.F().Convert(f.service, info, &second), "Repeated conversion failed");
        Near(Read<float>(second, 0x2C), Read<float>(displayed, 0x2C));
    }
    Check(f.F().miniatureCount == 10, "Wrong conversion counter");
    Check(idReads == 0 && summonerReads == 0, "With every category ticked no ID or summoner is read");
}
void UnknownAppearancesPreserved() {
    Fixture f;
    auto missing = Info(0x81, 1); Get(missing).teamPresent = false;
    const std::vector<Obj> unknowns = {missing, Info(0x81, -1), Info(0x81, 4), Info(0x09, 0),
        Info(0x80, 1), Info(0x03, 1), Info(0x41, 1), Info(0x11, 1)};
    for (auto info : unknowns) {
        Check(f.F().Group(info) == MiniatureGroup::Unknown, "Unknown fighter incorrectly classified");
        auto original = Look(); Obj displayed = original;
        Check(f.F().Convert(f.service, info, &displayed) && displayed == original, "Unknown fighter appearance changed");
    }
    Check(clones == 0 && barriers == 0 && f.F().miniatureCount == 0, "Unscaled appearance cloned/written");
}
void ContextAndEligibility() {
    Fixture f; auto info = Info(0x09, 1); auto original = Look(); Obj displayed = original;
    f.state.settings.mode = 0; f.state.settings.roleplayMode = 2;
    Check(!f.F().Convert(f.service, info, &displayed), "Outside mode leaked into combat");
    f.Combat(false); auto outside = Info(0, -1); Get(outside).teamPresent = false;
    Check(f.F().Convert(f.service, outside, &displayed), "Outside miniatures failed");
    Near(Read<float>(displayed, 0x2C), 1.2f); Near(Read<float>(displayed, 0x30), 1.8f);
    f.state.settings.mode = 2; f.state.settings.roleplayMode = 0; displayed = original;
    Check(!f.F().Convert(f.service, outside, &displayed), "Combat mode leaked outside");
    f.Combat(true); Get(info).excluded = true; const auto before = clones;
    Check(!f.F().Convert(f.service, info, &displayed), "Native exclusion ignored");
    Get(info).excluded = false; Put(f.service, 0x48, false);
    Check(!f.F().Convert(f.service, info, &displayed), "Inactive service bypassed");
    Put(f.service, 0x48, true);
    Check(!f.F().Convert(Make("exf"), info, &displayed), "Unrelated service changed");
    Check(!f.F().Convert(nullptr, info, &displayed) && !f.F().Convert(f.service, nullptr, &displayed) &&
        !f.F().Convert(f.service, info, nullptr), "Null argument not rejected");
    Obj empty = nullptr; Check(!f.F().Convert(f.service, info, &empty), "Null look not rejected");
    Check(clones == before && displayed == original, "Rejected conversion changed look");
}
void MountedSubentityKeepsItsOwnLook() {
    Fixture f; f.Combat(false); f.state.settings.roleplayMode = 2;
    auto mount = SizedLook(810, 180, 1.25f), rider = SizedLook(1, 100, 0.9f);
    auto decoration = SizedLook(811, 20, 1.1f);
    Get(mount).sublooks[{2, 0}] = rider;
    Get(rider).sublooks[{6, 0}] = decoration;
    Obj miniature = mount;
    Check(f.F().Convert(f.service, f.local, &miniature), "Mounted appearance not converted");
    auto copiedRider = Get(miniature).sublooks.at({2, 0});
    Check(copiedRider != rider && Get(copiedRider).sublooks.at({6, 0}) != decoration,
        "Native deep clone contract did not preserve nested attachments");
    Near(Read<float>(miniature, 0x30), 0.75f);
    Near(Read<float>(copiedRider, 0x30), 0.9f);
    Near(Read<float>(mount, 0x30), 1.25f);
    auto root = Make("AnimatedEntity"), child = Make("AnimatedEntity");
    Put(root, 0x1A8, root); Put(root, 0x170, mount); Put(root, 0x310, miniature);
    Put(root, 0x1C8, Fixture::LocalId); Put(root, 0x398, f.context);
    Put(child, 0x1A8, root); Put(child, 0x170, rider); Put<int64_t>(child, 0x1C8, 0);
    Put(child, 0x398, f.context);
    miniature = creatureLookHook(root, nullptr);
    const auto beforeClones = clones, beforeBarriers = barriers;
    for (int repeat = 0; repeat < 3; ++repeat) {
        // Stock rez feeds the root mount back to the rider, even from a cache
        // populated before Miniatures. The hook must bypass both situations.
        Put<Obj>(child, 0x310, repeat == 0 ? nullptr : miniature);
        Check(NativeCreatureLook(child, nullptr) == miniature, "Native child-path simulation is wrong");
        const auto calls = nativeCreatureLookCalls;
        Check(creatureLookHook(child, nullptr) == rider && nativeCreatureLookCalls == calls,
            "Rider was replaced by the mounted root appearance");
        Near(Read<float>(rider, 0x30), 0.9f);
    }
    Check(clones == beforeClones && barriers == beforeBarriers, "Child look was rescaled or mutated");
    Check(Read<Obj>(root, 0x310) == miniature && Read<Obj>(child, 0x310) == miniature,
        "Appearance reader rewrote native caches");
    auto replacementRider = SizedLook(1, 100, 1.1f);
    Put(child, 0x170, replacementRider);
    Check(creatureLookHook(child, nullptr) == replacementRider, "Rider appearance update was cached forever");
    const auto calls = nativeCreatureLookCalls;
    auto recomputedRoot = creatureLookHook(root, nullptr);
    Check(recomputedRoot == miniature && recomputedRoot == Read<Obj>(root, 0x310) &&
        nativeCreatureLookCalls == calls + 1, "Root must keep its native cache path");
    Near(Read<float>(recomputedRoot, 0x30), 0.75f);
}
void SubentityEligibilityAndNativeFallback() {
    Fixture f; f.Combat(false); f.state.settings.roleplayMode = 2;
    auto root = Make("AnimatedEntity"), child = Make("AnimatedEntity");
    auto rider = SizedLook(1, 100), nativeResult = Look();
    Put(root, 0x1A8, root); Put(root, 0x1C8, Fixture::LocalId); Put(root, 0x398, f.context);
    Put(child, 0x1A8, root); Put(child, 0x170, rider); Put(child, 0x310, nativeResult);
    Put<int64_t>(child, 0x1C8, 0); Put(child, 0x398, f.context);
    const auto native = [&] {
        const auto calls = nativeCreatureLookCalls;
        Check(creatureLookHook(child, nullptr) == nativeResult && nativeCreatureLookCalls == calls + 1,
            "Ineligible attachment did not use the native path");
        Check(!f.F().PreserveMiniatureAttachmentVisibility(child, gameBase + 0x2617618) &&
              !f.F().PreserveMiniatureAttachmentVisibility(child, gameBase + 0x2619FDB),
              "Ineligible attachment bypassed native visibility");
    };
    for (int mode : {0, 1}) { f.state.settings.roleplayMode = mode; native(); }
    f.state.settings.roleplayMode = 2; f.state.settings.mode = 0; f.Combat(true); native();
    f.state.settings.mode = 2;
    Check(creatureLookHook(child, nullptr) == rider, "Combat Miniatures attachment not handled");
    f.Combat(false); f.state.settings.roleplayMode = 0; native(); f.state.settings.roleplayMode = 2;
    Put(f.service, 0x48, false); native(); Put(f.service, 0x48, true);
    f.state.servicesReady = false; native(); f.state.servicesReady = true;
    f.state.ready.store(false); native(); f.state.ready.store(true);
    f.state.stopped = true; native(); f.state.stopped = false;
    f.state.creatures.reset(); native(); f.state.creatures.reset(f.service);
    Put<Obj>(child, 0x1A8, nullptr); native();
    Put(child, 0x1A8, root); Put<Obj>(root, 0x1A8, nullptr); native(); Put(root, 0x1A8, root);
    Get(root).type = Type("OtherRoot"); native(); Get(root).type = Type("AnimatedEntity");
    Put(child, 0x1C8, Fixture::LocalId); native(); Put<int64_t>(child, 0x1C8, 0);
    Put(child, 0x398, Make("OtherContext")); native(); Put(child, 0x398, f.context);
    Put(root, 0x398, Make("OtherContext")); native(); Put(root, 0x398, f.context);
    Put<Obj>(f.service, 0x10, nullptr); native(); Put(f.service, 0x10, f.context);
    activeRegistry = nullptr; native(); activeRegistry = Make("OtherRegistry"); native(); activeRegistry = f.registry;
    Get(f.registry).fighters.clear(); native(); Get(f.registry).fighters[Fixture::LocalId] = f.local;
    Get(f.local).excluded = true; native(); Get(f.local).excluded = false;
    Put<Obj>(child, 0x170, nullptr); native(); Put(child, 0x170, Make("OtherLook")); native();
    Put(child, 0x170, rider); Put(rider, 0x1C, 0); native(); Put(rider, 0x1C, 1);
    for (float invalid : {0.0f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        Put(rider, 0x2C, invalid); native();
    }
    Put(rider, 0x2C, -0.9f);
    Check(creatureLookHook(child, nullptr) == rider, "Mirrored valid rider appearance rejected");
    Put(rider, 0x2C, 0.1f); Put(rider, 0x30, 0.05f);
    Check(creatureLookHook(child, nullptr) == rider &&
          f.F().PreserveMiniatureAttachmentVisibility(child, gameBase + 0x2617618),
          "Small finite authored child scales must not invent a native visibility exclusion");
    Check(!f.state.stopRequested.load(), "Guarded unavailable data stopped the module");
}
void MountedVisibilityNativePolicies() {
    for (bool combat : {false, true}) {
        Fixture f; f.Combat(combat); f.state.settings.mode = f.state.settings.roleplayMode = 2;
        auto mount = SizedLook(810, 180), rider = SizedLook(1, 100);
        auto root = f.Root(mount), child = Make("AnimatedEntity");
        Put(child, 0x1A8, root); Put(child, 0x170, rider); Put(child, 0x398, f.context);
        Put<int64_t>(child, 0x1C8, 0);
        Get(mount).sublooks[{2, 0}] = rider;
        const uintptr_t hideCaller = gameBase + 0x2617618, forcedCaller = gameBase + 0x2619FDB;
        const auto visible = [&](bool nativeVisibility, bool correctLookOnly) {
            // Saved native order: ric stores rmp in +0x38D; rgx evaluates
            // visibility policy, then independently applies the rmo gate.
            const bool forced = NativeCreatureHide(child, nullptr) &&
                (correctLookOnly || !f.F().PreserveMiniatureAttachmentVisibility(child, forcedCaller));
            const bool creatureOnly = NativeCreatureHide(child, nullptr) &&
                (correctLookOnly || !f.F().PreserveMiniatureAttachmentVisibility(child, hideCaller));
            return nativeVisibility && !forced && !creatureOnly;
        };
        Check(creatureLookHook(child, nullptr) == rider && !visible(true, true),
              "Correct child look alone must reproduce native creature-mode disappearance");
        Check(visible(true, false), "Both creature-only visibility gates must preserve the mounted rider");
        Check(!visible(false, false), "Native invisibility/root-hidden policy must still hide rider");
        Check(!f.F().PreserveMiniatureAttachmentVisibility(root, hideCaller) &&
              !f.F().PreserveMiniatureAttachmentVisibility(root, forcedCaller),
              "Root creature policies must remain native");
        Check(!f.F().PreserveMiniatureAttachmentVisibility(child, hideCaller - 1) &&
              !f.F().PreserveMiniatureAttachmentVisibility(child, forcedCaller + 1),
              "Unknown native caller must not bypass visibility");
        // Direct test calls are outside both pinned native call sites: the
        // actual installed hook wrappers must retain their native bool ABI.
        Check(creatureHideHook(child, nullptr) && creatureForcedHideHook(child, nullptr) &&
              !creatureHideHook(root, nullptr) && !creatureForcedHideHook(root, nullptr),
              "Predicate hooks changed an unrelated caller or native false result");
        auto& mode = combat ? f.state.settings.mode : f.state.settings.roleplayMode;
        for (int nativeMode : {0, 1}) {
            mode = nativeMode;
            Check(!f.F().PreserveMiniatureAttachmentVisibility(child, hideCaller) &&
                  !f.F().PreserveMiniatureAttachmentVisibility(child, forcedCaller),
                  "Native display mode bypassed creature visibility");
        }
        mode = 2; f.state.stopped = true;
        Check(!visible(true, false), "Stop restoration must restore native child visibility policy");
        f.state.stopped = false;
        Check(visible(true, false), "Miniature visibility did not recover after native-mode transition");
        const auto beforeClones = clones, beforeBarriers = barriers;
        for (int repeat = 0; repeat < 10; ++repeat) Check(visible(true, false), "Repeated visibility predicate changed state");
        Check(clones == beforeClones && barriers == beforeBarriers,
              "Visibility predicate cloned looks or wrote native state");
    }
}
void CheckDisplayLook(Obj look, int mode, float miniatureScale = 0.6f) {
    Check(look != nullptr, "Missing rendered root appearance");
    Check(Read<int>(look, 0x1C) == (mode == 1 ? 3451 : 1), "Wrong root appearance mode");
    Near(Read<float>(look, 0x30), mode == 2 ? miniatureScale : 1.0f);
}
void RootCacheAllDisplayTransitions() {
    for (bool combat : {false, true}) {
        for (int from = 0; from <= 2; ++from) {
            for (int to = 0; to <= 2; ++to) {
                if (from == to) continue;
                Fixture f; f.Combat(combat);
                auto& mode = combat ? f.state.settings.mode : f.state.settings.roleplayMode;
                mode = from; Put(f.service, 0x48, from != 0);
                auto source = SizedLook(1, 100), root = f.Root(source);
                auto first = creatureLookHook(root, nullptr); CheckDisplayLook(first, from);
                const auto before = nativeCacheComputations;
                mode = to;
                duringRefresh = [&] {
                    Check(f.F().lookCacheStamps.empty(), "Cache generation was cleared after the native event");
                    CheckDisplayLook(creatureLookHook(root, nullptr), to);
                };
                f.F().Refresh(to != 0); duringRefresh = {};
                Check(nativeCacheComputations == before + 1 && nativeCacheClears == 1,
                    "Mode transition did not recompute the root once");
                auto second = creatureLookHook(root, nullptr); CheckDisplayLook(second, to);
                Check(second == Read<Obj>(root, 0x310) && nativeCacheComputations == before + 1,
                    "Stable cache was recomputed repeatedly");
                Near(Read<float>(source, 0x30), 1.0f);
            }
        }
    }
}
void PreloadedRootCachePolicyChangesAndStop() {
    Fixture f; f.Combat(false); f.state.settings.roleplayMode = 2;
    auto source = SizedLook(1, 100), root = f.Root(source), preloaded = Look(1, 1);
    Put(preloaded, 0x1C, 3451); Put(root, 0x310, preloaded);
    auto miniature = creatureLookHook(root, nullptr); CheckDisplayLook(miniature, 2);
    Check(nativeCacheClears == 1 && nativeCacheComputations == 1, "Preloaded creature cache survived Miniatures");
    for (int repeat = 0; repeat < 3; ++repeat)
        Check(creatureLookHook(root, nullptr) == miniature, "Same generation lost stable cache");
    Check(nativeCacheClears == 1 && nativeCacheComputations == 1, "Repeated reads accumulated scaling");
    // A callback may run before Module::Change reaches its final Refresh.
    f.state.settings.roleplayMode = 1;
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    Check(nativeCacheClears == 2 && nativeCacheComputations == 2, "Display mode change reused old cache");
    f.state.settings.mode = 1; f.Combat(true);
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    Check(nativeCacheClears == 3, "Combat context change reused outside cache");
    auto replacementContext = Make("Context"); Put(f.service, 0x10, replacementContext);
    Put(root, 0x398, replacementContext);
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    Check(nativeCacheClears == 4, "Compositor context change reused old cache");
    auto replacementService = Make("exf"); Put(replacementService, 0x10, replacementContext);
    Put(replacementService, 0x48, true); f.state.creatures.reset(replacementService);
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    Check(nativeCacheClears == 5, "Creature service replacement reused old cache");
    Put(replacementService, 0x48, false); CheckDisplayLook(creatureLookHook(root, nullptr), 0);
    Put(replacementService, 0x48, true); CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    Check(nativeCacheClears == 7, "Native service activity change reused old cache");
    f.state.settings.mode = 2;
    CheckDisplayLook(creatureLookHook(root, nullptr), 2);
    f.state.stopped = true;
    duringRefresh = [&] { CheckDisplayLook(creatureLookHook(root, nullptr), 1); };
    f.F().Refresh(true); duringRefresh = {};
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    duringRefresh = [&] { CheckDisplayLook(creatureLookHook(root, nullptr), 0); };
    f.F().Refresh(false); duringRefresh = {};
    CheckDisplayLook(creatureLookHook(root, nullptr), 0);
    Check(f.F().lookCacheStamps.empty(), "Stopped callbacks retained new look stamps");
    f.F().InvalidateLookCache();
    Check(f.F().lookCacheStamps.empty(), "Stop cache ownership was not released");
}
void RootCacheExclusionsAndReentrancy() {
    Fixture f; auto source = SizedLook(1, 100), root = f.Root(source);
    Get(f.local).excluded = true;
    CheckDisplayLook(creatureLookHook(root, nullptr), 0);
    Check(f.F().miniatureCount == 0, "Native exclusion was converted during cache refresh");
    Get(f.local).excluded = false; f.F().Refresh(true);
    CheckDisplayLook(creatureLookHook(root, nullptr), 2);
    f.F().InvalidateLookCache();
    beforeNativeLookReturn = [&] { f.state.settings.mode = 1; };
    CheckDisplayLook(creatureLookHook(root, nullptr), 2);
    beforeNativeLookReturn = {};
    Check(f.F().lookCacheStamps.empty(), "Result from an obsolete generation was stamped as current");
    CheckDisplayLook(creatureLookHook(root, nullptr), 1);
    const auto before = nativeCacheClears;
    auto unrelatedRoot = f.Root(source); Put(unrelatedRoot, 0x310, source);
    Put(unrelatedRoot, 0x398, Make("OtherContext"));
    Check(creatureLookHook(unrelatedRoot, nullptr) == source && nativeCacheClears == before,
        "Another compositor's cache was invalidated");
    auto invalidResultRoot = f.Root(nullptr);
    Check(!creatureLookHook(invalidResultRoot, nullptr), "Missing source invented a root appearance");
}
void RootCacheBoundedOwnership() {
    Fixture f; f.state.settings.mode = 0; Put(f.service, 0x48, false);
    auto source = SizedLook(1, 100);
    std::vector<Obj> roots;
    for (size_t i = 0; i <= Features::MaxLookCacheStamps; ++i) {
        auto root = f.Root(source); roots.push_back(root);
        CheckDisplayLook(creatureLookHook(root, nullptr), 0);
        Check(f.F().lookCacheStamps.size() <= Features::MaxLookCacheStamps, "Unbounded cache GC ownership");
    }
    Check(f.F().lookCacheStamps.size() == Features::MaxLookCacheStamps, "Wrong cache stamp bound");
    Obj evictedRoot = nullptr;
    for (auto root : roots) {
        if (!f.F().lookCacheStamps.contains(Read<Obj>(root, 0x310))) { evictedRoot = root; break; }
    }
    Check(evictedRoot != nullptr, "Cache bound did not evict any stamp");
    auto before = nativeCacheClears; CheckDisplayLook(creatureLookHook(evictedRoot, nullptr), 0);
    Check(nativeCacheClears == before + 1 && f.F().lookCacheStamps.size() == Features::MaxLookCacheStamps,
        "Evicted cache was reused or exceeded the bound");
    for (const auto& [look, held] : f.F().lookCacheStamps)
        Check(held.get() == look && Get(look).type == Type("EntityLook"), "Stamp retained a renderer or unowned address");
    f.F().InvalidateLookCache(); Check(f.F().lookCacheStamps.empty(), "Generation reset retained old looks");
}
void LocalTeamAndLateArrival() {
    Fixture f; Check(f.F().LocalTeam() == 0, "Local ID lookup failed");
    Get(f.registry).fighters.clear(); Check(f.F().LocalTeam() == -1, "Missing fighter assumed team zero");
    f.F().Tick(); Check(refreshes.empty(), "Spurious unknown-team refresh");
    Get(f.registry).fighters.emplace(Fixture::LocalId, f.local); f.F().Tick();
    Check(refreshes.size() == 1 && refreshes.back().first == f.service && refreshes.back().second, "Late team not refreshed");
    f.F().Tick(); Check(refreshes.size() == 1, "Unchanged team repeatedly refreshed");
    Get(f.local).team = 1; f.F().Tick(); Check(refreshes.size() == 2, "Changed team not refreshed");
    Get(f.local).teamPresent = false; Check(f.F().LocalTeam() == -1, "Missing team not unknown");
    Get(f.local).teamPresent = true;
    for (int invalid : {-1, 4}) { Get(f.local).team = static_cast<int8_t>(invalid); Check(f.F().LocalTeam() == -1, "Invalid team accepted"); }
    Get(f.local).team = 2; f.state.settings.mode = 1; f.F().Tick();
    Check(refreshes.size() == 2, "Non-miniature mode refreshed");
    f.Combat(false); Check(f.F().LocalTeam() == -1, "Combat team queried outside"); f.F().Tick();
    Check(refreshes.size() == 2, "Outside context refreshed combat");
    f.Combat(true); Put<Obj>(f.character, 0x128, nullptr); Check(f.F().LocalTeam() == -1, "Missing identity not guarded");
    Put(f.character, 0x128, f.identity); Get(f.aggregate).type = Type("UnexpectedAggregate");
    Check(f.F().LocalTeam() == -1, "Unknown aggregate accepted"); Get(f.aggregate).type = Type("fyb");
    Get(f.character).type = Type("UnexpectedCharacter"); Check(f.F().LocalTeam() == -1, "Unknown character accepted");
    Get(f.character).type = Type("esg"); Get(f.registry).type = Type("UnexpectedRegistry");
    Check(f.F().LocalTeam() == -1, "Unknown registry accepted");
}
void RefreshAndInvalidScale() {
    Fixture f; f.F().Refresh(false);
    Check(refreshes.size() == 1 && !refreshes.back().second && !Read<bool>(f.service, 0x48), "Refresh(false) failed");
    f.F().Refresh(true); Check(refreshes.size() == 2 && refreshes.back().second, "Refresh(true) failed");
    auto original = Look(std::numeric_limits<float>::infinity(), 3.0f); Obj displayed = original; bool rejected = false;
    try { f.F().Convert(f.service, Info(0x09, 1), &displayed); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected && displayed == original && barriers == 0, "Invalid scale committed");
    f.state.creatures.reset(); f.F().Refresh(true); Check(refreshes.size() == 2, "Refresh called missing service");
}
void ProportionalHeightAndReference() {
    Fixture f;
    auto giant = SizedLook(10, 400), tiny = SizedLook(11, 25, 2);
    Obj displayed = giant;
    Check(f.F().Convert(f.service, Info(0x09, 1), &displayed) && displayed != giant, "Missing reference must still reduce");
    auto summonedPlayer = SizedLook(1, 100); displayed = summonedPlayer;
    f.F().Convert(f.service, Info(0x81, 0, true), &displayed);
    Near(f.F().sizes.playerHeight, 0); // A double must not become the player reference.
    auto mounted = SizedLook(1, 100), children = Make("Children"), riders = Make("Riders");
    Get(mounted).fields["fhyu"] = children; Get(children).fields["riders"] = riders; Get(riders).integer = 1;
    displayed = mounted; f.F().Convert(f.service, Info(0x81, 0), &displayed);
    Near(f.F().sizes.playerHeight, 0);
    auto player = SizedLook(1, 100); displayed = player;
    f.F().Convert(f.service, Info(0x81, 0), &displayed);
    Check(displayed != player, "Reference player must use the same reduction as other fighters");
    Near(Read<float>(displayed, 0x30) * 100, 60);
    Near(Read<float>(player, 0x30), 1);
    Near(f.F().sizes.playerHeight, 100);
    for (auto original : {giant, tiny}) {
        displayed = original; f.F().Convert(f.service, Info(0x09, 1), &displayed);
        const float authored = original == giant ? 400.0f : 25.0f;
        Near(Read<float>(displayed, 0x30) * authored, original == giant ? 240.0f : 50.0f);
        Near(Read<float>(displayed, 0x2C), Read<float>(displayed, 0x30));
    }
    Near(Read<float>(giant, 0x30), 1); Near(Read<float>(tiny, 0x30), 2);

}
void MinimumAcrossContexts() {
    Fixture f;
    auto player = SizedLook(1, 100); Obj displayed = player;
    f.F().Convert(f.service, Info(0x81, 0), &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f);
    Near(f.F().sizes.playerHeight, 100); // Measure the original player, not its reduced appearance.
    auto tiny = SizedLook(12, 25); displayed = tiny;
    f.F().Convert(f.service, Info(0x09, 1), &displayed);
    Near(Read<float>(displayed, 0x30), 2.0f); // Enlarge a tiny monster to 50% of the original player.
    auto unknown = Info(0x09, 1); Get(unknown).teamPresent = false; displayed = tiny;
    f.F().Convert(f.service, unknown, &displayed); Check(displayed == tiny, "Unknown fighter scaled by minimum");
    displayed = tiny; f.F().Convert(f.service, Info(0x81, 1), &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f); // The monster floor must not enlarge players.
    for (int team : {0, 1}) {
        displayed = tiny; f.F().Convert(f.service, Info(0x81, team, true), &displayed);
        Near(Read<float>(displayed, 0x30), 2.0f); // Both allied and enemy summons use the floor.
    }
   
    f.Combat(false); f.state.settings.roleplayMode = 2;
    displayed = tiny; f.F().Convert(f.service, Info(0x0A, -1), &displayed); Near(Read<float>(displayed, 0x30), 2.0f);
    displayed = tiny; f.F().Convert(f.service, Info(0x06, -1), &displayed); Near(Read<float>(displayed, 0x30), 0.6f);
   
    f.state.settings.roleplayMode = 0; displayed = tiny;
    Check(!f.F().Convert(f.service, Info(0x0A, -1), &displayed) && displayed == tiny, "Floor escaped Miniatures mode");
}
void MissingAssetsAndDeferredRefresh() {
    Fixture f;
    auto player = SizedLook(1, 100), creature = SizedLook(13, 100);
    auto savedDefinition = assets.at("13"); assets.erase("13");
    Obj displayed = creature; f.F().Convert(f.service, Info(0x09, 1), &displayed);
    Check(displayed != creature && f.F().sizes.pending.size() == 1, "Missing asset not deferred");
    auto originalTable = skinTable; skinTable = nullptr; displayed = player;
    f.F().Convert(f.service, Info(0x81, 0), &displayed);
    Check(displayed != player && f.F().sizes.playerHeight == 0, "Missing skin table not guarded");
    skinTable = originalTable; assets["13"] = savedDefinition;
    f.F().previousLocalTeam = 0; f.F().Tick();
    Check(f.F().sizes.pending.empty() && refreshes.size() == 1, "Resolved sizes not refreshed once");
    Near(f.F().sizes.playerHeight, 100); f.F().Tick();
    Check(refreshes.size() == 1, "Stable sizes repeatedly refreshed");
    displayed = creature; f.F().Convert(f.service, Info(0x09, 1), &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f);
    auto odd = SizedLook(14, 40); auto array = Get(assets.at("14")).fields.at("animations");
    auto idle = Read<Obj>(array, 0x20);
    Get(idle).fields["name"] = Text("AnimStatiqueCarrying");
    displayed = odd; f.F().Convert(f.service, Info(0x09, 1), &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f); // Pending pose uses proportional fallback.
    Get(idle).fields["name"] = Text("AnimStatiqueCombat_1");
    f.F().sizes.nextRetry = 0; f.F().Tick();
    Check(f.F().sizes.pending.empty(), "Combat idle fallback ignored");
}
void ProportionalReductionBeforeMeasurements() {
    Fixture f;
    f.F().previousLocalTeam = 0;
    const auto monster = Info(0x09, 1);
    const auto giant = SizedLook(20, 400), tiny = SizedLook(21, 25);
    // The fixed floor must not disable Reduce when no player reference exists.
    for (auto original : {giant, tiny}) {
        Obj displayed = original;
        Check(f.F().Convert(f.service, monster, &displayed), "Proportional conversion not handled");
        Near(Read<float>(displayed, 0x30), 0.6f);
        Check(displayed != original, "Missing player reference disabled Reduce");
        Near(Read<float>(original, 0x30), 1);
    }
    f.F().Tick(); Check(refreshes.empty(), "Unresolved reference repeatedly refreshed");
    auto player = SizedLook(1, 100); Obj displayed = player;
    f.F().Convert(f.service, Info(0x81, 0), &displayed);
    f.F().Tick(); Check(refreshes.size() == 1, "Late reference did not activate floor");
    displayed = tiny; f.F().Convert(f.service, monster, &displayed);
    Near(Read<float>(displayed, 0x30), 2.0f); // Minimum: 50% of the measured original player.
    displayed = giant; f.F().Convert(f.service, monster, &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f);
    // A monster asset can arrive after the reference. Reduce still applies now.
    auto late = SizedLook(22, 25); auto definition = assets.at("22"); assets.erase("22");
    displayed = late; f.F().Convert(f.service, monster, &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f);
    Check(f.F().sizes.pending.size() == 1, "Missing monster measurement not retried");
    assets["22"] = definition; f.F().sizes.nextRetry = 0; f.F().Tick();
    Check(refreshes.size() == 2 && f.F().sizes.pending.empty(), "Late monster floor not refreshed");
    displayed = late; f.F().Convert(f.service, monster, &displayed);
    Near(Read<float>(displayed, 0x30), 2.0f); Near(Read<float>(late, 0x30), 1);
    f.F().Tick(); Check(refreshes.size() == 2, "Resolved measurements repeatedly refreshed");
    // Outside combat has its own mode and the same fixed preset/fallback.
    f.Combat(false); f.state.settings.roleplayMode = 2; f.F().sizes.playerHeight = 0;
    displayed = giant; f.F().Convert(f.service, Info(0x0A, -1), &displayed);
    Near(Read<float>(displayed, 0x30), 0.6f);
}
// « Apparence » in combat: each fighter category keeps its native look once
// its checkbox is unticked, for all 64 combinations; the own fighter and own
// summons are only told apart (ID, summoner reads) when their box differs.
void CombatCategoriesFollowTheirCheckbox() {
    Fixture f;
    constexpr int64_t AllyId = 0x1111, EnemyId = 0x2222;
    auto me = Info(0x81, 0); Get(me).id = Fixture::LocalId;
    auto allied = Info(0x81, 0); Get(allied).id = AllyId;
    auto mine = Info(0x89, 0, true); Get(mine).summoner = Fixture::LocalId; Get(mine).summonerPresent = true;
    auto theirs = Info(0x89, 0, true); Get(theirs).summoner = AllyId; Get(theirs).summonerPresent = true;
    auto unsummoned = Info(0x89, 0, true); // no summoner metadata: an allied summon
    auto enemyPlayer = Info(0x81, 1); Get(enemyPlayer).id = EnemyId;
    auto enemyMonster = Info(0x09, 1);
    auto enemySummon = Info(0x89, 1, true); Get(enemySummon).summoner = Fixture::LocalId; Get(enemySummon).summonerPresent = true;
    auto alliedMonster = Info(0x09, 0); // Unknown: never reduced, as before
    using G = MiniatureGroup;
    struct Case { Obj info; G group; };
    const Case cases[] = {{me, G::Self}, {allied, G::AlliedPlayer}, {mine, G::OwnSummon}, {theirs, G::AlliedSummon},
        {unsummoned, G::AlliedSummon}, {enemyPlayer, G::Enemy}, {enemyMonster, G::Enemy}, {enemySummon, G::EnemySummon},
        {alliedMonster, G::Unknown}};
    for (unsigned mask = 0; mask < 64; ++mask) {
        auto& settings = f.state.settings;
        for (size_t i = 0; i < std::size(CombatMiniatureToggles); ++i) settings.*CombatMiniatureToggles[i] = ((mask >> i) & 1) != 0;
        const auto reads = idReads + summonerReads;
        for (const auto& item : cases) {
            const auto group = f.F().Group(item.info);
            const bool told = (item.group != G::Self || CombatSelfMatters(settings)) && (item.group != G::OwnSummon || CombatOwnSummonMatters(settings));
            Check(told ? group == item.group : MiniatureGroupEnabled(settings, group) == MiniatureGroupEnabled(settings, item.group),
                "Wrong combat category, or a skipped identity read changed the result");
            const auto original = Look(); Obj displayed = original;
            Check(f.F().Convert(f.service, item.info, &displayed), "Miniatures handles every fighter");
            if (MiniatureGroupEnabled(settings, item.group)) {
                Check(displayed != original, "A ticked category must be reduced");
                Near(Read<float>(displayed, 0x2C), 1.2f); Near(Read<float>(displayed, 0x30), 1.8f);
            } else Check(displayed == original, "An unticked category keeps its native appearance");
        }
        if (!CombatSelfMatters(settings) && !CombatOwnSummonMatters(settings))
            Check(idReads + summonerReads == reads, "Matching ally boxes (the default included) read no ID or summoner");
    }
    Check(idReads != 0 && summonerReads != 0, "Own fighter and summons are told apart when it matters");
    // Créatures ignores the categories: Convert leaves the native conversion.
    f.state.settings.mode = 1; Obj displayed = Look();
    Check(!f.F().Convert(f.service, me, &displayed), "Créatures must stay native for every category");
}
// « Apparence » out of combat: the local character, accepted party members,
// other players and monster groups each follow their checkbox; NPCs and other
// entities stay reduced. Identity is only read once a player box is unticked.
void RoleplayCategoriesFollowTheirCheckbox() {
    Fixture f; f.Combat(false); f.state.settings.roleplayMode = 2;
    constexpr int64_t PartnerId = 0x3333, GuestId = 0x4444, StrangerId = 0x5555;
    Get(Get(partyService).fields.at("members")).items = {Member(Fixture::LocalId, true), Member(PartnerId, true), Member(GuestId, false)};
    auto me = Info(0x82, -1); Get(me).id = Fixture::LocalId;
    auto partner = Info(0x82, -1); Get(partner).id = PartnerId;
    auto guest = Info(0x82, -1); Get(guest).id = GuestId; // invited only: another player
    auto stranger = Info(0x82, -1); Get(stranger).id = StrangerId;
    auto monsters = Info(0x0A, -1); Get(monsters).id = -77;
    auto npc = Info(0x02, -1); Get(npc).id = -88;
    using G = MiniatureGroup;
    struct Case { Obj info; G group; };
    const Case cases[] = {{me, G::RoleplaySelf}, {partner, G::PartyMember}, {guest, G::OtherPlayer}, {stranger, G::OtherPlayer},
        {monsters, G::Monster}, {npc, G::RoleplayOther}};
    for (unsigned mask = 0; mask < 16; ++mask) {
        auto& settings = f.state.settings;
        for (size_t i = 0; i < std::size(RoleplayMiniatureToggles); ++i) settings.*RoleplayMiniatureToggles[i] = ((mask >> i) & 1) != 0;
        f.F().roleplayIdentityKnown = false;
        const auto lookups = characterLookups;
        for (const auto& item : cases) {
            const auto original = Look(); Obj displayed = original;
            Check(f.F().Convert(f.service, item.info, &displayed), "Miniatures handles every roleplay entity");
            if (MiniatureGroupEnabled(settings, item.group)) Check(displayed != original, "A ticked category must be reduced");
            else Check(displayed == original, "An unticked category keeps its native appearance");
        }
        Check((characterLookups != lookups) == RoleplayIdentityMatters(settings) && characterLookups - lookups <= 1,
            "The local identity is read once, and only when a player category is unticked");
    }
    // No party service or character: everyone is another player.
    partyService = nullptr; characterService = nullptr; f.F().roleplayIdentityKnown = false;
    f.state.settings.roleplayMiniaturePlayers = false; f.state.settings.roleplayMiniatureSelf = f.state.settings.roleplayMiniatureParty = true;
    for (auto info : {me, partner}) { const auto original = Look(); Obj displayed = original;
        Check(f.F().Convert(f.service, info, &displayed) && displayed == original, "Unknown identity: players follow Autres joueurs"); }
}
// A party change re-renders out of combat only while a player box is unticked,
// read at most once a second, never in combat.
void RoleplayIdentityChangesRefresh() {
    Fixture f; f.Combat(false); f.state.settings.roleplayMode = 2; f.F().previousLocalTeam = -1;
    auto& members = Get(Get(partyService).fields.at("members")).items;
    members = {Member(0x3333, true)};
    f.F().Tick();
    Check(characterLookups == 0 && refreshes.empty(), "Default choices track no identity");
    f.state.settings.roleplayMiniatureParty = false; f.F().Tick();
    Check(characterLookups == 1 && refreshes.empty() && f.F().roleplayIdentityKnown &&
        f.F().roleplayIdentity.self == Fixture::LocalId && f.F().roleplayIdentity.party == std::vector<int64_t>{0x3333},
        "First identity read refreshes nothing");
    f.F().Tick(); Check(characterLookups == 1, "Identity is read at most once a second");
    members.push_back(Member(0x4444, true)); f.F().nextRoleplayIdentityRead = 0; f.F().Tick();
    Check(characterLookups == 2 && refreshes.size() == 1, "A new party member re-renders the players");
    f.F().nextRoleplayIdentityRead = 0; f.F().Tick();
    Check(characterLookups == 3 && refreshes.size() == 1, "An unchanged party never refreshes");
    members.push_back(Member(0x5555, false)); f.F().nextRoleplayIdentityRead = 0; f.F().Tick();
    Check(characterLookups == 4 && refreshes.size() == 1, "An invited guest is not a party change");
    f.Combat(true); f.F().previousLocalTeam = 0; f.F().Tick();
    Check(!f.F().roleplayIdentityKnown && characterLookups == 4 && refreshes.size() == 1, "Combat keeps no roleplay identity");
    f.Combat(false); f.state.settings.roleplayMiniatureParty = true; f.F().Tick();
    Check(!f.F().roleplayIdentityKnown, "Default choices keep no roleplay identity");
}
// A checkbox changed before Module::Change's refresh still invalidates the
// cached root look, as a display mode change does.
void CategoryChangeInvalidatesCachedLooks() {
    Fixture f; auto source = SizedLook(1, 100), root = f.Root(source);
    CheckDisplayLook(creatureLookHook(root, nullptr), 2);
    f.state.settings.miniatureSelf = false;
    CheckDisplayLook(creatureLookHook(root, nullptr), 2, 1.0f);
    Check(nativeCacheClears == 1 && nativeCacheComputations == 2, "Category change reused the old cached look");
    f.state.settings.miniatureSelf = true;
    CheckDisplayLook(creatureLookHook(root, nullptr), 2);
    Check(nativeCacheClears == 2 && nativeCacheComputations == 3, "Category change back reused the normal look");
}
}
int main() {
    try {
        fake::InitializeApi();
        const auto run = [](void (*test)()) { test(); fake::Check(fake::handles.empty(), "Test leaked GC handles"); };
        run(fake::GroupsAndOriginalScale); run(fake::UnknownAppearancesPreserved); run(fake::ContextAndEligibility);
        run(fake::CombatCategoriesFollowTheirCheckbox); run(fake::RoleplayCategoriesFollowTheirCheckbox);
        run(fake::RoleplayIdentityChangesRefresh); run(fake::CategoryChangeInvalidatesCachedLooks);
        run(fake::MountedSubentityKeepsItsOwnLook); run(fake::SubentityEligibilityAndNativeFallback);
        run(fake::MountedVisibilityNativePolicies);
        run(fake::RootCacheAllDisplayTransitions); run(fake::PreloadedRootCachePolicyChangesAndStop);
        run(fake::RootCacheExclusionsAndReentrancy); run(fake::RootCacheBoundedOwnership);
        run(fake::LocalTeamAndLateArrival); run(fake::RefreshAndInvalidScale);
        run(fake::ProportionalHeightAndReference); run(fake::MinimumAcrossContexts); run(fake::MissingAssetsAndDeferredRefresh);
        run(fake::ProportionalReductionBeforeMeasurements);
        std::cout << "PASS: actual Features conversion, per-category miniatures in combat (64 combinations) and out of combat (16, party and identity reads), mounted child looks, root cache generations/transitions/Stop/bounds/category changes, classification, reduction and deferred refresh\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
