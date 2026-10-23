#include "stats/stats-economy.hpp"
#include "stats/stats.hpp"
#include "appearance/features.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Real stats-economy.cpp linked against a managed fixture laid out as IL2CPP
// lays its objects (the class first, then the fields at their pinned 3.7.12.12
// offsets): no game, process, file or network. The store is a fake that
// records what the tracker counts; each hook is called as the game's handler
// would be, and its "original" does what the game's code does there.
namespace fake {
using namespace managed;
constexpr const char* Core = "Core.dll";
constexpr const char* Protocol = "Ankama.Dofus.Protocol.Game.dll";
constexpr const char* InventorySpace = "Core.UILogic.Inventory";
// Character ids long enough never to be mistaken for an amount in a log line.
constexpr int64_t A = 77001001, B = 77002002, C = 77003003;
constexpr uintptr_t SetKamasRva = 0x2AF6F20, ContentRva = 0x1B07A50, StorageRva = 0x190B360, TradeRva = 0x1A05F60;
constexpr uintptr_t HarvestRva = 0x1A87B60, CraftRva = 0x19D18D0, QuestRva = 0x1AA7380, AchievementRva = 0x1A90D70;
struct Class { std::string image, space, name; std::map<std::string, size_t> layout; std::map<std::string, Class*> nested; };
struct MethodInfo { Class* klass; std::string name; int argc; uintptr_t rva; };
struct Raw {
    Class* klass = nullptr;
    void* monitor = nullptr;
    alignas(8) unsigned char fields[0x1F0]{};
};
static_assert(offsetof(Raw, fields) == 0x10, "IL2CPP fields start after the object header");
std::vector<std::unique_ptr<Class>> types;
std::map<std::string, Class*> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Raw>> objects;
std::map<GCHandle, Obj> handles;
std::map<std::string, Obj> services;
std::map<uintptr_t, void*> detours;   // the tracker's hooks, by target
std::set<std::string> layouts;        // class.field offsets checked at installation
std::set<uintptr_t> guarded;          // natives verified at installation
std::set<std::string> fails;          // "class.field", "class.method" or "hook:<rva>" that fail
std::vector<std::string> logs, earlier;
GCHandle nextHandle = 0x5EC000000000ULL;
unsigned lookups = 0;
uint64_t now = 5000000;
// The store.
struct Added { int64_t character; stats::Counter counter; int64_t amount; };
std::vector<Added> added;
bool counting = true;
// The game's side of the hooked methods.
std::map<uintptr_t, unsigned> calls;
bool handled = true;                  // what the game's handlers return
std::string throwIn;                  // the game's handler that throws (a managed exception)

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Raw& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Raw*>(object); }
Class* Define(const std::string& image, const std::string& space, const std::string& name, std::map<std::string, size_t> layout = {}) {
    types.push_back(std::make_unique<Class>(Class{image, space, name, std::move(layout), {}}));
    return classes[name] = types.back().get();
}
Obj Make(const std::string& type) {
    auto object = std::make_unique<Raw>();
    object->klass = classes.at(type);
    const Obj result = object.get();
    objects.push_back(std::move(object));
    return result;
}
template<class T> void Set(Obj object, size_t offset, T value) {
    Check(object && offset >= 0x10 && offset + sizeof(T) <= sizeof(Raw), "Field outside the fake object");
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(T));
}
template<class T> T Peek(Obj object, size_t offset) {
    Check(object && offset >= 0x10 && offset + sizeof(T) <= sizeof(Raw), "Field outside the fake object");
    T value{};
    std::memcpy(&value, static_cast<unsigned char*>(object) + offset, sizeof(T));
    return value;
}
std::string Hex(uintptr_t value) { char text[32]; std::snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(value)); return text; }
void Initialize() {
    Define("", "", "Other");
    // Pinned 3.7.12.12 contracts (work/r74-analysis/contracts-new.json).
    Define(Core, "", "esg", {{"dpzj", 0x128}, {"<dqai>k__BackingField", 0x1D8}});
    Define(Core, "", "z", {{"dbrl", 0x10}});
    Define(Core, InventorySpace, "Inventory", {{"<kamas>k__BackingField", 0x30}});
    Define(Core, InventorySpace, "PlayerInventory", {{"m_playedCharacterService", 0x60}});
    Define(Core, "", "fef", {{"dthz", 0x28}});
    Define(Core, "", "feh", {{"<dtjh>k__BackingField", 0xF8}, {"dtji", 0x100}});
    Define(Core, "", "euk", {{"dqpb", 0x10}});
    Define(Core, "", "eul", {{"dqpe", 0x28}, {"dqpg", 0x38}});
    Define(Core, "", "faj", {{"dsgo", 0x38}});
    Define(Core, "", "fcp");
    Define(Core, "", "ezt", {{"dsdy", 0x30}, {"dsef", 0x68}, {"dseg", 0x70}});
    Define(Core, "", "bgw", {{"dhrz", 0x10}});
    Define(Core, "", "fde", {{"dtcf", 0x20}});
    Define(Core, "", "fcv", {{"dtaa", 0x28}});
    auto result = Define(Protocol, "", "kco", {{"ettz", 0x18}, {"etuc", 0x20}, {"etud", 0x28}});
    auto holder = Define(Protocol, "", "kco+kcn");
    auto description = Define(Protocol, "", "kco+kcn+kcm", {{"ettm", 0x18}, {"ettv", 0x30}});
    result->nested["kcn"] = holder;
    holder->nested["kcm"] = description;
    Define(Protocol, "", "lef", {{"fade", 0x28}, {"fadg", 0x2C}, {"fadi", 0x30}});
    Define(Protocol, "", "ibc", {{"ehbe", 0x18}});
    Define(Protocol, "", "mdi", {{"fgqo", 0x28}});
    Define(Protocol, "", "mdl", {{"fgrh", 0x1C}, {"fgrj", 0x20}});
    Define(Protocol, "", "irl", {{"ekui", 0x30}}); // InventoryContentEvent: kamas
    Define(Protocol, "", "ita", {{"eldm", 0x18}}); // StorageKamasUpdateEvent: the storage's kamas
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { handles.emplace(++nextHandle, object); return nextHandle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Unknown economy GC handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Economy GC handle released twice"); };
    api.class_get_field_from_name = [](Klass klass, const char* name) -> void* {
        auto& type = *static_cast<Class*>(klass);
        const auto found = type.layout.find(name);
        if (fails.contains(type.name + "." + name) || found == type.layout.end()) return nullptr;
        layouts.insert(type.name + "." + name);
        return &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return *static_cast<size_t*>(field); };
}
uint64_t Clock() { return now; }

// The game's code behind each hook.
using SetKamasFn = void(*)(Obj, int64_t, Method);
using HandlerFn = bool(*)(Obj, Obj, Method);
using TradeFn = void(*)(Obj, int32_t, int64_t, int64_t, int32_t, int32_t, int32_t, int32_t, Method);
using HarvestFn = bool(*)(Obj, int32_t, int32_t, int32_t, Method);
void Throws(const char* handler) { if (throwIn == handler) throw std::runtime_error("Injected managed exception"); }
void GameSetKamas(Obj inventory, int64_t kamas, Method) { ++calls[SetKamasRva]; Set<int64_t>(inventory, 0x30, kamas); }
// The virtual SetKamas as the game reaches it: its (hooked) body.
void SetKamas(Obj inventory, int64_t kamas) {
    const auto hook = detours.find(SetKamasRva);
    if (hook == detours.end()) GameSetKamas(inventory, kamas, nullptr);
    else reinterpret_cast<SetKamasFn>(hook->second)(inventory, kamas, nullptr);
}
// fef.bdmu: InventoryContentEvent, the character's kamas through its inventory's SetKamas.
bool GameContent(Obj handler, Obj message, Method) {
    ++calls[ContentRva];
    const Obj inventory = Peek<Obj>(Peek<Obj>(handler, 0x28), 0xF8);
    SetKamas(inventory, Peek<int64_t>(message, 0x30));
    Throws("content");
    return handled;
}
// euk.zyv: the storage inventory (feh.bdrc creates it when missing), then its SetKamas.
bool GameStorage(Obj handler, Obj message, Method) {
    ++calls[StorageRva];
    const Obj inventories = Peek<Obj>(Peek<Obj>(handler, 0x10), 0x28);
    Obj storage = Peek<Obj>(inventories, 0x100);
    if (!storage) { storage = Make("Inventory"); Set<Obj>(inventories, 0x100, storage); }
    Set<int64_t>(storage, 0x30, Peek<int64_t>(message, 0x18));
    Throws("storage");
    return handled;
}
// faj.bcan, then faj.bcak: the exchange is open (esg.zfg(true)).
void GameTrade(Obj frame, int32_t, int64_t, int64_t, int32_t, int32_t, int32_t, int32_t, Method) {
    ++calls[TradeRva];
    Set<bool>(Peek<Obj>(frame, 0x38), 0x1D8, true);
    Throws("trade");
}
bool GameHarvest(Obj, int32_t, int32_t, int32_t, Method) { ++calls[HarvestRva]; Throws("harvest"); return handled; }
bool GameCraft(Obj, Obj, Method) { ++calls[CraftRva]; Throws("craft"); return handled; }
bool GameQuest(Obj, Obj, Method) { ++calls[QuestRva]; Throws("quest"); return handled; }
bool GameAchievement(Obj, Obj, Method) { ++calls[AchievementRva]; Throws("achievement"); return handled; }
void* Original(uintptr_t rva) {
    switch (rva) {
    case SetKamasRva: return reinterpret_cast<void*>(&GameSetKamas);
    case ContentRva: return reinterpret_cast<void*>(&GameContent);
    case StorageRva: return reinterpret_cast<void*>(&GameStorage);
    case TradeRva: return reinterpret_cast<void*>(&GameTrade);
    case HarvestRva: return reinterpret_cast<void*>(&GameHarvest);
    case CraftRva: return reinterpret_cast<void*>(&GameCraft);
    case QuestRva: return reinterpret_cast<void*>(&GameQuest);
    case AchievementRva: return reinterpret_cast<void*>(&GameAchievement);
    }
    throw std::runtime_error("Hook outside the economy contract");
}
template<class F> F Hook(uintptr_t rva) {
    const auto found = detours.find(rva);
    Check(found != detours.end(), "Hook not installed");
    return reinterpret_cast<F>(found->second);
}

// The played-character service (esg) and its character (z), the inventory
// service (feh) with the character's inventory, and the game's handlers.
struct World {
    Obj character, info, inventories, inventory, content, storageFrame, storage, exchange, harvest, craft, crafter, customer, quests, achievements;
    explicit World(int64_t id, int64_t kamas = 0) {
        character = Make("esg"); inventories = Make("feh");
        Become(id);
        inventory = Make("PlayerInventory");
        Set<Obj>(inventory, 0x60, character); Set<int64_t>(inventory, 0x30, kamas);
        Set<Obj>(inventories, 0xF8, inventory);
        content = Make("fef"); Set<Obj>(content, 0x28, inventories);
        storageFrame = Make("eul"); Set<Obj>(storageFrame, 0x28, inventories); Set<Obj>(storageFrame, 0x38, character);
        storage = Make("euk"); Set<Obj>(storage, 0x10, storageFrame);
        exchange = Make("faj"); Set<Obj>(exchange, 0x38, character);
        harvest = Make("fcp");
        craft = Make("ezt"); crafter = Make("bgw"); customer = Make("bgw");
        Set<Obj>(craft, 0x30, character); Set<Obj>(craft, 0x68, crafter); Set<Obj>(craft, 0x70, customer);
        quests = Make("fde"); Set<Obj>(quests, 0x20, character);
        achievements = Make("fcv"); Set<Obj>(achievements, 0x28, character);
        services = {{"fng", character}, {"fmo", inventories}};
    }
    // A new z, as the game builds one at character selection, reconnection or a look change.
    void Become(int64_t id) { info = Make("z"); Set<int64_t>(info, 0x10, id); Set<Obj>(character, 0x128, info); }
    void Leave() { Set<Obj>(character, 0x128, nullptr); }
    int64_t Kamas() const { return Peek<int64_t>(inventory, 0x30); }
};
// The server's messages, as the game's handlers apply them.
void KamasUpdate(World& world, int64_t total) { SetKamas(world.inventory, total); }   // fef.bdmz
void Characteristics(World& world, int64_t total) { if (world.Kamas() != total) SetKamas(world.inventory, total); } // esg.zht
bool Content(World& world, int64_t total) {
    const Obj message = Make("irl"); Set<int64_t>(message, 0x30, total);
    return Hook<HandlerFn>(ContentRva)(world.content, message, nullptr);
}
bool StorageUpdate(World& world, int64_t total) {
    const Obj message = Make("ita"); Set<int64_t>(message, 0x18, total);
    return Hook<HandlerFn>(StorageRva)(world.storage, message, nullptr);
}
// StorageInventoryContentEvent (faj.bcaf, not hooked): a bank, chest or Havre-Sac chest opens.
void OpenStorage(World& world, int64_t total) {
    Obj storage = Peek<Obj>(world.inventories, 0x100);
    if (!storage) { storage = Make("Inventory"); Set<Obj>(world.inventories, 0x100, storage); }
    Set<int64_t>(storage, 0x30, total);
}
// fai.bbzj: faj.bcan(1, the message's second id, its first id, ...); fai.bbzi: -1, -1.
void TradeStart(World& world, int64_t first, int64_t second, int32_t kind = 1) {
    Hook<TradeFn>(TradeRva)(world.exchange, kind, first, second, 0, 0, 0, 0, nullptr);
}
void TradeEnd(World& world) { Set<bool>(world.character, 0x1D8, false); } // ExchangeLeaveEvent: esg.zfg(false)
bool Harvest(World& world, int32_t item, int32_t quantity, int32_t bonus = 0) {
    return Hook<HarvestFn>(HarvestRva)(world.harvest, item, quantity, bonus, nullptr);
}
struct CraftSpec { int32_t result = 2, which = 2, bits = 0, quantity = 1, uid = 9001; bool description = true; std::string klass = "kco+kcn+kcm"; };
bool Craft(World& world, const CraftSpec& spec) {
    const Obj message = Make("kco");
    Set<int32_t>(message, 0x18, spec.result); Set<int32_t>(message, 0x28, spec.which);
    if (spec.description) {
        const Obj description = Make(spec.klass), object = Make("lef");
        Set<int32_t>(description, 0x18, spec.bits); Set<Obj>(description, 0x30, object);
        Set<int32_t>(object, 0x28, spec.quantity); Set<int32_t>(object, 0x2C, spec.uid); Set<int32_t>(object, 0x30, 1514);
        Set<Obj>(message, 0x20, description);
    }
    return Hook<HandlerFn>(CraftRva)(world.craft, message, nullptr);
}
void Party(World& world, int64_t crafter, int64_t customer) {
    Set<int64_t>(world.crafter, 0x10, crafter); Set<int64_t>(world.customer, 0x10, customer);
}
bool Quest(World& world, int32_t quest, const std::string& klass = "ibc") {
    const Obj message = Make(klass); Set<int32_t>(message, 0x18, quest);
    return Hook<HandlerFn>(QuestRva)(world.quests, message, nullptr);
}
bool Achievement(World& world, int32_t id, int64_t by) {
    const Obj message = Make("mdi"), achievement = Make("mdl");
    Set<Obj>(message, 0x28, achievement); Set<int32_t>(achievement, 0x1C, id); Set<int64_t>(achievement, 0x20, by);
    return Hook<HandlerFn>(AchievementRva)(world.achievements, message, nullptr);
}

// The list of this PC's characters as another game client writes it.
const std::wstring listName = L"Local\\DofusMiniatures-stats-characters-tests-" + std::to_wstring(GetCurrentProcessId());
struct SharedBlock { volatile LONG64 ids[64]; volatile LONG next; };
HANDLE otherMapping = nullptr;
SharedBlock* otherList = nullptr;
SharedBlock& OtherList() {
    if (!otherList) {
        otherMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedBlock), listName.c_str());
        Check(otherMapping != nullptr, "Test list unavailable");
        otherList = static_cast<SharedBlock*>(MapViewOfFile(otherMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedBlock)));
        Check(otherList != nullptr, "Test list unavailable");
    }
    return *otherList;
}
void OtherClient(int64_t id) {
    auto& list = OtherList();
    const auto slot = static_cast<unsigned long>(InterlockedIncrement(&list.next) - 1) % 64;
    InterlockedExchange64(&list.ids[slot], id);
}
bool Listed(int64_t id) {
    const auto& list = OtherList();
    return std::any_of(std::begin(list.ids), std::end(list.ids), [&](const volatile LONG64& slot) { return slot == id; });
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char* image, const char* space, const char* name) const {
    const auto found = fake::classes.find(name);
    fake::Check(found != fake::classes.end() && found->second->image == image && found->second->space == space,
                "Economy class requested from the wrong image or namespace");
    return found->second;
}
Klass Api::NestedClass(Klass declaring, const char* name) const {
    const auto& nested = static_cast<fake::Class*>(declaring)->nested;
    const auto found = nested.find(name);
    if (found == nested.end()) throw std::runtime_error("Nested class unavailable");
    return found->second;
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    auto& type = *static_cast<fake::Class*>(klass);
    const auto key = type.name + "." + name;
    static const std::map<std::string, std::pair<int, uintptr_t>> expected{
        {"PlayerInventory.SetKamas", {1, 0x2AF6F20}}, {"fef.bdmu", {1, 0x1B07A50}}, {"euk.zyv", {1, 0x190B360}},
        {"faj.bcan", {7, 0x1A05F60}}, {"fcp.bctq", {3, 0x1A87B60}}, {"ezt.bbvb", {1, 0x19D18D0}},
        {"fde.bcys", {1, 0x1AA7380}}, {"fcv.bcvg", {1, 0x1A90D70}}};
    if (fake::fails.contains(key)) throw std::runtime_error("Verified method unavailable");
    const auto found = expected.find(key);
    fake::Check(found != expected.end() && found->second == std::pair(argc, rva), "Economy native outside the pinned contract");
    // The real lookup ends in NativeAt, which refuses an RVA without a 16-byte guard.
    fake::Check(std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& guard) { return guard.rva == rva; }),
                "Economy native without a signature guard");
    fake::guarded.insert(rva);
    fake::methods.push_back(std::make_unique<fake::MethodInfo>(fake::MethodInfo{&type, name, argc, rva}));
    return fake::methods.back().get();
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const {
    ++fake::lookups;
    const auto found = fake::services.find(name);
    return found == fake::services.end() ? nullptr : found->second;
}
void Log(const std::string& line) { fake::logs.push_back(line); }
// MinHook's rules: one hook per target, prepared at the method's own address.
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& info = *static_cast<const fake::MethodInfo*>(method);
    fake::Check(info.rva == rva, "A hook prepared away from its verified method");
    if (fake::fails.contains("hook:" + fake::Hex(rva))) throw std::runtime_error("Cannot prepare hook: injected");
    if (fake::detours.contains(rva)) throw std::runtime_error("Cannot prepare hook: MH_ERROR_ALREADY_CREATED");
    fake::detours[rva] = replacement;
    *original = fake::Original(rva);
}
void AddNativeHook(uintptr_t, void*, void**) { throw std::runtime_error("The economy tracker hooks managed methods only"); }
namespace stats {
void Add(int64_t character, Counter counter, int64_t amount) { fake::added.push_back({character, counter, amount}); }
void Raise(int64_t, Record, int64_t) { throw std::runtime_error("No record in the economy"); }
void AddTo(int64_t, Table, int, int64_t) { throw std::runtime_error("No table in the economy"); }
void Name(int64_t, const std::string&) {}
bool Counting() { return fake::counting; }
}

namespace {
using namespace fake;
using stats::Counter;
void Tick(int times = 1) {
    for (int i = 0; i < times; ++i) {
        now += 200;
        try { TickStatsEconomy(); }
        catch (...) { throw std::runtime_error("An exception escaped TickStatsEconomy towards Module::Pump"); }
    }
}
void Wait(uint64_t ms) { Tick(static_cast<int>(ms / 200)); }
int64_t Total(int64_t character, Counter counter) {
    int64_t sum = 0;
    for (const auto& add : added) if (add.character == character && add.counter == counter) sum += add.amount;
    return sum;
}
size_t Lines(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
bool Logged(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
// A new scenario: nothing pending or remembered, statistics on, fresh logs.
void Fresh() {
    RestoreStatsEconomy();
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    logs.clear(); added.clear(); calls.clear(); fails.clear(); throwIn.clear();
    counting = true; handled = true; lookups = 0;
    module->ready = true; module->servicesReady = true; module->stopped = false; module->stopRequested = false;
    now += 60000;
}

void InstallContract() {
    // A moved character field: nothing is prepared at all.
    fails = {"esg.dpzj"};
    InstallStatsEconomy();
    Check(Logged("STATS_ECONOMY_DISABLED stage=character") && detours.empty(), "Without the character, no hook");
    // A moved craft field and a hook MinHook refuses: those parts stay off alone.
    fails = {"lef.fade", "hook:190B360"};
    InstallStatsEconomy();
    Check(Logged("STATS_ECONOMY_DISABLED stage=kamas") && Logged("STATS_ECONOMY_DISABLED stage=craft"), "Each failed part says so");
    Check(detours.size() == 6 && detours.contains(SetKamasRva) && detours.contains(ContentRva) && !detours.contains(StorageRva) &&
          !detours.contains(CraftRva) && detours.contains(TradeRva) && detours.contains(HarvestRva) &&
          detours.contains(QuestRva) && detours.contains(AchievementRva), "The other parts are installed");
    // The two kamas hooks prepared before the refusal only run the game's code.
    fails.clear();
    World world(A, 1000);
    Tick();
    Check(Content(world, 1000) && world.Kamas() == 1000, "The content handler runs, its result forwarded");
    KamasUpdate(world, 1800);
    Wait(4000);
    Wait(12000);
    KamasUpdate(world, 2600);
    Wait(4000);
    Check(world.Kamas() == 2600 && calls[SetKamasRva] == 3 && added.empty() && !Lines("STATS_KAMAS"),
          "Without its storage side the kamas part counts nothing, and takes no baseline");
    // A game restarted with the module (a new process): what failed installs then.
    Fresh();
    detours.erase(SetKamasRva); detours.erase(ContentRva);
    layouts.clear(); guarded.clear();
    InstallStatsEconomy();
    Check(detours.size() == 8, "Every part installed");
    Check(layouts == std::set<std::string>{"Inventory.<kamas>k__BackingField", "PlayerInventory.m_playedCharacterService", "fef.dthz",
        "feh.<dtjh>k__BackingField", "feh.dtji", "euk.dqpb", "eul.dqpe", "eul.dqpg", "ezt.dsdy", "ezt.dsef", "ezt.dseg", "bgw.dhrz",
        "kco.ettz", "kco.etuc", "kco.etud", "kco+kcn+kcm.ettm", "kco+kcn+kcm.ettv", "lef.fade", "lef.fadg"},
        "Every field the retried parts read is checked against its pinned offset");
    Check(guarded == std::set<uintptr_t>{SetKamasRva, ContentRva, StorageRva, CraftRva}, "Each retried hook behind its signature guard");
    const auto before = detours;
    InstallStatsEconomy();
    Check(detours == before && !Lines("STATS_ECONOMY_DISABLED"), "A second installation changes nothing");
}
// The character's kamas: every change, whatever message brings it, once.
void KamasChanges() {
    Fresh();
    World world(A);
    Tick();
    Check(Content(world, 5000) && Logged("STATS_KAMAS_BASELINE source=content"), "The kamas of a loaded character are its baseline");
    Check(added.empty(), "The baseline is never counted");
    KamasUpdate(world, 5600);
    Wait(1000);
    Check(added.empty(), "Not before a transfer could be told apart");
    Wait(2000);
    Check(Total(A, Counter::KamasEarned) == 600 && Logged("STATS_KAMAS kind=earned amount=600"), "More kamas: earned");
    KamasUpdate(world, 5100);
    Characteristics(world, 5100); // the characteristics then agree: no change
    Wait(3000);
    Check(Total(A, Counter::KamasSpent) == 500, "Fewer kamas: spent, as a positive amount");
    Characteristics(world, 5350); // the characteristics first (a fight's loot)
    KamasUpdate(world, 5350);     // then the update: no change left
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 850 && added.size() == 3, "Whichever message comes first, once");
    Content(world, 5900); // in game, a server transfer back: its content is a baseline again
    Wait(3000);
    Check(added.size() == 3 && Lines("STATS_KAMAS_BASELINE source=content") == 2 && !Lines("STATS_KAMAS_UNTRACKED"),
          "The content's kamas are never a change");
    KamasUpdate(world, 6000);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 950 && added.size() == 4, "Counted from the content's kamas");
    Check(calls[SetKamasRva] == 7, "The game's SetKamas always runs");
}
void FirstValueNeverCounted() {
    Fresh();
    World world(A, 9000); // the module started in game: no content will come
    Tick();
    KamasUpdate(world, 9500);
    Wait(3000);
    Check(added.empty() && Logged("STATS_KAMAS_SKIPPED reason=owner"), "Before a baseline, nothing counts");
    Wait(7000);
    Check(Logged("STATS_KAMAS_BASELINE source=start"), "The same character and inventory for 10 s: a baseline");
    KamasUpdate(world, 9700);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 200 && added.size() == 1, "Then every change counts");
}
void CharacterSwitch() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 1000);
    KamasUpdate(world, 1200);
    now += 20000;                 // the character selection: no tick
    world.Become(B);
    Characteristics(world, 7000); // B's characteristics before its content: the inventory still holds A's kamas
    Content(world, 7000);
    KamasUpdate(world, 7100);     // a reward right at login, before the ticks resume: after B's baseline
    Tick();
    KamasUpdate(world, 7400);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 200, "A's change before the switch stays A's");
    Check(Total(B, Counter::KamasEarned) == 400 && added.size() == 3, "B's own changes only, never A's kamas against B's");
}
void SessionGap() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 1000);
    Tick(3);
    now += 600000;                // merchant mode, logged out: the sales are made offline
    world.Become(A);
    Characteristics(world, 4000); // back in game: the characteristics before the content
    Content(world, 4000);
    Tick();
    KamasUpdate(world, 4100);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 100 && added.size() == 1, "What changed off the game is never counted");
    Check(Logged("STATS_KAMAS_SKIPPED reason=session"), "A change between sessions is logged as skipped");
    // A gap without any content: a new baseline after 10 s.
    now += 30000;
    KamasUpdate(world, 4500);
    Tick();
    KamasUpdate(world, 4900);
    Wait(3000);
    Check(added.size() == 1, "After a gap, nothing counts before a new baseline");
    Wait(8000);
    KamasUpdate(world, 5000);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 200 && added.size() == 2, "Then it counts again");
}
void NewInventory() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 3000);
    // A reconnection: the game builds a new, empty player inventory (feh.bdra).
    const Obj fresh = Make("PlayerInventory");
    Set<Obj>(fresh, 0x60, world.character);
    Set<Obj>(world.inventories, 0xF8, fresh);
    world.inventory = fresh;
    Tick();
    Characteristics(world, 3000); // 0 -> 3000 on the new inventory
    Wait(3000);
    Check(added.empty(), "A new inventory's first kamas are never earned");
    Content(world, 3000);
    KamasUpdate(world, 3400);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 400 && added.size() == 1, "Its content is its baseline");
}
void Storage() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 10000);
    OpenStorage(world, 50000);
    KamasUpdate(world, 9990);                                       // the bank's fee
    KamasUpdate(world, 8990); StorageUpdate(world, 51000);          // a deposit
    Tick();
    StorageUpdate(world, 50500); Tick(2); KamasUpdate(world, 9490); // a withdrawal, the storage first
    StorageUpdate(world, 50700); StorageUpdate(world, 51000); KamasUpdate(world, 8990); // a deposit told in two storage updates
    Tick();
    KamasUpdate(world, 9290); StorageUpdate(world, 50700); Tick(); KamasUpdate(world, 9090); StorageUpdate(world, 50900); // two of each
    StorageUpdate(world, 50400); KamasUpdate(world, 9290); KamasUpdate(world, 9590); // a withdrawal told in two kamas updates
    Wait(3000);
    Check(Total(A, Counter::KamasSpent) == 10 && Total(A, Counter::KamasEarned) == 0, "Only the fee is spent: moves are transfers");
    Check(Lines("STATS_KAMAS kind=transfer") == 7 && Logged("STATS_ECONOMY_SEEN kind=storage"), "Each move is logged as a transfer");
    StorageUpdate(world, 51900); // credited at the bank (a marketplace sale): the bank's side only
    Wait(3000);
    Check(added.size() == 1, "A change of the storage alone is never counted");
    KamasUpdate(world, 8890);    // 700 spent...
    now += 4000;                 // ...a 4 s frame without any tick...
    StorageUpdate(world, 52600); // ...then an unrelated +700 at the bank: both still waiting at the next tick
    Wait(3000);
    Check(Total(A, Counter::KamasSpent) == 710, "Too far apart: not a transfer");
    Set<Obj>(world.inventories, 0x100, nullptr); // no storage opened since the login
    StorageUpdate(world, 900);
    KamasUpdate(world, 7990);
    Wait(3000);
    Check(Total(A, Counter::KamasSpent) == 1610, "A storage the game creates empty is never a side of a transfer");
    Check(calls[StorageRva] == 10, "The game's storage handler always runs");
}
void Bounds() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 1000);
    KamasUpdate(world, -5);   // a value no character can hold...
    KamasUpdate(world, 1200); // ...and the change from it
    Wait(3000);
    Check(added.empty() && Logged("STATS_KAMAS_SKIPPED reason=bounds"), "An implausible value is never counted, nor a change from it");
    KamasUpdate(world, 1300);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 100, "Counted again from the next value");
    OpenStorage(world, 2000000000000); // above any bank
    StorageUpdate(world, 1999999999500);
    KamasUpdate(world, 1800);
    Wait(3000);
    Check(Logged("STATS_KAMAS_SKIPPED reason=storage-bounds") && Total(A, Counter::KamasEarned) == 600,
          "An implausible storage is never a side of a transfer");
}
void OwnTrades() {
    Fresh();
    World world(A);
    OtherClient(B); // B is played with the mod in another client
    Tick();
    Content(world, 20000);
    Check(Listed(A), "This client lists its character for the others");
    TradeStart(world, B, A);
    Check(Logged("STATS_KAMAS_TRADE own=1") && Logged("STATS_ECONOMY_SEEN kind=own-trade"), "A trade with B is recognized");
    Tick();
    KamasUpdate(world, 25000); // B gives 5000
    TradeEnd(world);
    Tick(2);
    KamasUpdate(world, 24000); // right after the end: still the trade
    Wait(4000);
    KamasUpdate(world, 23000); // later: a real expense
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 0 && Total(A, Counter::KamasSpent) == 1000, "A trade between one's own characters is a transfer");
    Check(Lines("STATS_KAMAS kind=trade") == 2, "Logged as such");
    TradeStart(world, A, B); // the ids the other way round
    Tick();
    KamasUpdate(world, 23500);
    TradeEnd(world);
    Wait(6000);
    Check(added.size() == 1, "Either id may be the partner");
    TradeStart(world, C, A); // a character of another player
    Tick();
    KamasUpdate(world, 20500);
    TradeEnd(world);
    Wait(3000);
    TradeStart(world, -1, -1); // fai.bbzi: no ids
    Tick();
    KamasUpdate(world, 21000);
    TradeEnd(world);
    Wait(3000);
    TradeStart(world, B, A, 0); // not a trade between players
    Tick();
    KamasUpdate(world, 21100);
    TradeEnd(world);
    Wait(3000);
    Check(Total(A, Counter::KamasSpent) == 4000 && Total(A, Counter::KamasEarned) == 600 && added.size() == 4,
          "Another player's character, an unknown partner or another exchange: real kamas");
    Check(calls[TradeRva] == 5, "The game's trade start always runs");
    // An exchange still open after 30 minutes is forgotten.
    TradeStart(world, B, A);
    Tick();
    now += 1800000;
    Tick();
    KamasUpdate(world, 21300);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 600, "Inside the long exchange, after the gap: no baseline yet");
    Wait(10000);
    KamasUpdate(world, 21500);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 800, "Forgotten: the kamas count again");
}
// A trade's kamas may come until TradeGraceMs after the tick that saw its
// exchange end: they stay the trade's when they are committed on the tick
// that forgets the trade, or on the first tick after a long frame.
void OwnTradeGraceEnd() {
    Fresh();
    World world(A);
    OtherClient(B);
    Tick();
    Content(world, 20000);
    TradeStart(world, B, A);
    Tick();
    TradeEnd(world);
    Tick();                    // the end of the exchange, seen at this tick
    Tick(14);
    now += 150;
    KamasUpdate(world, 25000); // B gives 5000, 2.95 s after the end
    Wait(3000);                // committed on the tick that forgets the trade
    Check(added.empty() && Lines("STATS_KAMAS kind=trade") == 1, "Kamas at the end of a trade's grace stay the trade's");
    TradeStart(world, A, B);
    Tick();
    TradeEnd(world);
    Tick();
    Tick(14);
    now += 150;
    KamasUpdate(world, 21000); // A gives 4000, 2.95 s after the end...
    now += 6000;               // ...then a 6 s frame without any tick
    Tick();
    Check(added.empty() && Lines("STATS_KAMAS kind=trade") == 2, "However late the next tick, they stay the trade's");
    Content(world, 21000);     // a new baseline after the long frame
    TradeStart(world, B, A);
    Tick();
    TradeEnd(world);
    Tick();
    Tick(15);
    now += 50;
    KamasUpdate(world, 21700); // 3.05 s after the end: no longer the trade's
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 700 && added.size() == 1 && Lines("STATS_KAMAS kind=trade") == 2,
          "After the grace, kamas count again");
}
// The list of this PC's characters cannot be opened: its name is held by
// another kind of object, or by a mapping this client may not write (as one
// made by a client started as administrator). Nothing is listed or
// recognized, and everything else keeps counting.
void ListUnavailable() {
    const std::wstring held = listName + L"-held", readOnly = listName + L"-read-only";
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, held.c_str());
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READONLY, 0, sizeof(SharedBlock), readOnly.c_str());
    Check(mutex != nullptr && mapping != nullptr, "Test name holders unavailable");
    for (const auto& name : {held, readOnly}) {
        Fresh();
        UseStatsEconomyCharacterList(name.c_str());
        World world(A);
        Tick();                  // this client lists its character: the list cannot be opened
        Content(world, 1000);
        TradeStart(world, B, A); // B, played in another client: not recognized
        Tick();
        KamasUpdate(world, 1500);
        TradeEnd(world);
        Wait(3000);
        Wait(60000);             // listed again every minute
        Harvest(world, 289, 1);
        Check(Lines("STATS_ECONOMY_LIST_UNAVAILABLE") == 1 && !Lines("STATS_ECONOMY_ERROR") && !Lines("STATS_KAMAS_TRADE"),
              "An unavailable list is said once, and no trade is recognized");
        Check(Total(A, Counter::KamasEarned) == 500 && Total(A, Counter::Harvests) == 1 && added.size() == 2,
              "Without the list, the trade's kamas are real ones and everything else counts");
    }
    CloseHandle(mapping);
    CloseHandle(mutex);
    Fresh();
    UseStatsEconomyCharacterList(listName.c_str());
}
void CountingOff() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 1000);
    KamasUpdate(world, 1500);
    counting = false;
    Harvest(world, 289, 3);       // before the next tick
    Check(added.empty(), "Statistics off: the hooks stop at once");
    lookups = 0;
    Tick();
    Check(Logged("STATS_KAMAS_DROPPED count=1"), "Turning statistics off drops what was pending");
    KamasUpdate(world, 1700);
    Content(world, 1700);
    OpenStorage(world, 10);
    StorageUpdate(world, 20);
    TradeStart(world, B, A);
    TradeEnd(world);
    Harvest(world, 289, 3);
    Craft(world, {});
    Quest(world, 1234);
    Achievement(world, 55, A);
    Wait(5000);
    Check(added.empty() && lookups == 0, "Off: nothing counted, nothing read");
    Check(calls[SetKamasRva] == 4 && calls[HarvestRva] == 2 && calls[CraftRva] == 1 && calls[QuestRva] == 1 && calls[AchievementRva] == 1,
          "Off: the game's code still runs");
    counting = true;
    Tick();
    KamasUpdate(world, 1900);
    Wait(3000);
    Check(added.empty(), "On again: the kamas need a new baseline");
    Wait(8000);
    KamasUpdate(world, 2000);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 100 && added.size() == 1, "Counted from the new baseline");
}
void Harvests() {
    Fresh();
    World world(A);
    Tick();
    Check(Harvest(world, 289, 5), "The game's result is forwarded");
    handled = false;
    Check(!Harvest(world, 289, 3, 2), "Even false");
    handled = true;
    Harvest(world, 0, 3);
    Harvest(world, 289, 0);
    Check(Total(A, Counter::Harvests) == 2 && added.size() == 2, "One per harvest, bonus or not; never without an item");
    Check(Logged("STATS_ECONOMY_SEEN kind=harvest"), "The first harvest is logged once");
    world.Leave(); // no character (selection screen)
    Harvest(world, 289, 4);
    Check(added.size() == 2 && calls[HarvestRva] == 5, "Without a character nothing counts, the game's code runs");
}
void Crafts() {
    Fresh();
    World world(A);
    Tick();
    Check(Craft(world, {}), "The game's result is forwarded");
    Craft(world, {.quantity = 3});
    Party(world, A, C); // this character crafts for C
    Craft(world, {.quantity = 2});
    Party(world, C, A); // C crafts for this character
    Craft(world, {.quantity = 4});
    Party(world, -1, -1); // after an exchange's end (ezt.bbvf)
    Craft(world, {});
    Craft(world, {.result = 1}); // shown as a success by the game too
    Check(Total(A, Counter::Crafts) == 8 && added.size() == 5, "The quantity crafted alone or as the crafter, never as the customer");
    for (const CraftSpec spec : {CraftSpec{.result = 0}, CraftSpec{.result = 4}, CraftSpec{.which = 3}, CraftSpec{.bits = 1},
                                  CraftSpec{.uid = 0}, CraftSpec{.quantity = 0}, CraftSpec{.quantity = 100001},
                                  CraftSpec{.description = false}, CraftSpec{.klass = "Other"}})
        Craft(world, spec);
    Check(added.size() == 5 && calls[CraftRva] == 15, "Impossible, failed, smithmagic or malformed results are not crafts");
}
void QuestsAndAchievements() {
    Fresh();
    World world(A);
    Tick();
    Check(Quest(world, 1234) && Quest(world, 1234), "The game's result is forwarded");
    Quest(world, 0);
    Quest(world, 77, "Other");
    Check(Total(A, Counter::Quests) == 2, "One per quest completed, a repeated quest each time");
    Check(Achievement(world, 55, A), "The game's result is forwarded");
    Achievement(world, 56, B); // achieved by another character of the account
    Achievement(world, 0, A);
    Check(Total(A, Counter::Achievements) == 1 && added.size() == 3, "Only this character's achievements");
}
void GameExceptions() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 100);
    for (const char* handler : {"harvest", "craft", "quest", "achievement", "content", "storage", "trade"}) {
        throwIn = handler;
        bool passed = false;
        try {
            const std::string name = handler;
            if (name == "harvest") Harvest(world, 289, 2);
            else if (name == "craft") Craft(world, {});
            else if (name == "quest") Quest(world, 12);
            else if (name == "achievement") Achievement(world, 13, A);
            else if (name == "content") Content(world, 300);
            else if (name == "storage") { OpenStorage(world, 0); StorageUpdate(world, 50); }
            else TradeStart(world, B, A);
        } catch (const std::runtime_error& error) { passed = std::string(error.what()) == "Injected managed exception"; }
        Check(passed, "The game's exception passes through the hook unchanged");
    }
    throwIn.clear();
    TradeEnd(world);
    Wait(3000);
    Check(added.empty(), "Nothing counted for a handler that failed");
    KamasUpdate(world, 900);
    Harvest(world, 289, 2);
    Wait(3000);
    Check(Total(A, Counter::KamasEarned) == 600 && Total(A, Counter::Harvests) == 1, "The tracker keeps counting after them");
    Check(Logged("STATS_KAMAS_UNTRACKED amount=200"), "The content's change without a baseline is logged");
}
void OtherThread() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 100);
    std::thread([&] { Harvest(world, 289, 2); KamasUpdate(world, 300); Quest(world, 9); }).join();
    Wait(3000);
    Check(added.empty() && calls[HarvestRva] == 1 && world.Kamas() == 300, "Off the game thread, the hooks only run the game's code");
    Check(Lines("STATS_ECONOMY_THREAD other=1") == 1, "Said once in the log");
}
void ModuleStates() {
    Fresh();
    World world(A);
    Tick();
    Content(world, 100);
    module->stopRequested = true;
    lookups = 0;
    Harvest(world, 289, 2);
    KamasUpdate(world, 300);
    Tick(20);
    Check(added.empty() && lookups == 0, "A stopping module counts and reads nothing");
    module->stopRequested = false;
    module->ready = false;
    Harvest(world, 289, 2);
    Tick(5);
    Check(added.empty() && lookups == 0, "Nor a module not ready");
    module->ready = true;
    RestoreStatsEconomy();
    Harvest(world, 289, 2);
    Check(added.empty(), "Restored: the hooks only run the game's code until the next tick");
    Tick();
    Harvest(world, 289, 2);
    Check(Total(A, Counter::Harvests) == 1, "The next tick resumes");
}
void FixedCodeLogs() {
    earlier.insert(earlier.end(), logs.begin(), logs.end());
    const std::regex format(R"(^(STATS_KAMAS kind=(earned|spent|transfer|trade) amount=-?\d+|STATS_KAMAS_BASELINE source=(content|start))"
        R"(|STATS_KAMAS_SKIPPED reason=(owner|session|bounds|storage-bounds)|STATS_KAMAS_UNTRACKED amount=-?\d+|STATS_KAMAS_TRADE own=1)"
        R"(|STATS_KAMAS_DROPPED count=\d+|STATS_ECONOMY_SEEN kind=(harvest|craft|quest|achievement|storage|own-trade)|STATS_ECONOMY_THREAD other=1)"
        R"(|STATS_ECONOMY_LIST_UNAVAILABLE)"
        R"(|STATS_ECONOMY_DISABLED stage=(character|kamas|trade|harvest|craft|quest|achievement)|STATS_ECONOMY_ERROR stage=\w[\w-]*)$)");
    Check(earlier.size() > 50, "The scenarios produced logs");
    for (const auto& line : earlier) {
        Check(std::regex_match(line, format), "Every log line is a fixed code with numbers");
        for (const auto id : {A, B, C})
            Check(line.find(std::to_string(id)) == std::string::npos, "No character id in the log");
        Check(line.find("Injected") == std::string::npos, "No exception text in the log");
    }
    Check(std::none_of(earlier.begin(), earlier.end(), [](const std::string& line) { return line.rfind("STATS_ECONOMY_ERROR", 0) == 0; }),
          "No error in the scenarios");
}
}

int main() {
    using namespace fake;
    try {
        Initialize();
        Module owner{"offline-economy"};
        module = &owner;
        owner.ready = true; owner.servicesReady = true;
        UseStatsEconomyClock(&Clock);
        // Never the game clients' own list: a running client must not see these characters.
        UseStatsEconomyCharacterList(listName.c_str());
        InstallContract();
        KamasChanges();
        FirstValueNeverCounted();
        CharacterSwitch();
        SessionGap();
        NewInventory();
        Storage();
        Bounds();
        OwnTrades();
        OwnTradeGraceEnd();
        ListUnavailable();
        CountingOff();
        Harvests();
        Crafts();
        QuestsAndAchievements();
        GameExceptions();
        OtherThread();
        ModuleStates();
        FixedCodeLogs();
        RestoreStatsEconomy();
        UseStatsEconomyClock(nullptr);
        UseStatsEconomyCharacterList(nullptr);
        module = nullptr;
        Check(handles.empty(), "The economy tracker leaked GC references");
        std::cout << "PASS: kamas earned and spent from every SetKamas, content and 10 s baselines, sessions and character switches, "
                     "new inventories, bank/chest transfers paired either way and in parts, trades between this PC's characters "
                     "to the end of their grace, an unavailable character list, "
                     "counting off reads nothing; harvests, crafts (alone, crafter, never customer, no smithmagic), quests, "
                     "this character's achievements; game exceptions passed through, game thread only, fixed-code logs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
