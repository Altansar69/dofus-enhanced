#include "soul-inventory.hpp"
#include "soul-helper.hpp"
#include "soul-combat.hpp"
#include "soul-capture.hpp"
#include "soul-panel.hpp"
#include "soul-layout.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

// Real soul-inventory.cpp linked against a managed fixture, with no game,
// loader, process, UI, account, registry, settings or network access.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> refs;
    std::map<std::string, int64_t> numbers;
    std::vector<Obj> items;
    std::string text;
    uint64_t scalar = 0;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<GCHandle, Obj> handles;
std::map<std::string, Obj> services;
GCHandle nextHandle = 0xF12345670000ULL;
std::string failMethod;
std::vector<std::string> logs;
std::function<void()> onConstruct;
std::function<void()> onDispose;
std::function<void()> onEquip;
unsigned requests = 0, constructors = 0, criteriaChecks = 0, disposals = 0;
Obj requested = nullptr;
soul::CombatSnapshot combat{soul::CombatState::Ready, 1, 51};
soul::CaptureState capture = soul::CaptureState::Missing;
soul::PanelView shown;
Ref presentedItem;
void (*click)() = nullptr;
int renders = 0, hides = 0, primaryReads = 0;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Object& Get(Obj object) { Check(object != nullptr, "Null fake object"); return *static_cast<Object*>(object); }
Class* Type(const std::string& name) {
    auto& result = classes[name];
    if (!result) result = std::make_unique<Class>(Class{name});
    return result.get();
}
Obj Make(const std::string& name) {
    auto object = std::make_unique<Object>();
    object->klass = Type(name);
    const auto result = object.get();
    objects.push_back(std::move(object));
    return result;
}
Obj Box(uint64_t value) { auto object = Make("Box"); Get(object).scalar = value; return object; }
void InitializeApi() {
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.object_unbox = [](Obj object) -> void* { return &Get(object).scalar; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        auto handle = ++nextHandle; handles.emplace(handle, object); return handle;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Invalid or truncated GC handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Double GC release"); };
}
struct Fixture {
    Module state{L"offline-soul-inventory"};
    Obj service, inventory, contents;
    Fixture() {
        soul::RestoreInventoryBridge();
        module = &state;
        requests = constructors = criteriaChecks = disposals = 0;
        requested = nullptr;
        failMethod.clear(); logs.clear(); services.clear(); onConstruct = {}; onDispose = {}; onEquip = {};
        state.servicesReady = true;
        state.compositor.reset(Make("Compositor"));
        for (const auto name : {"fmz", "fmo", "fmn", "fnm", "fng", "fou", "frf", "fjw", "fkq"})
            services[name] = Make(name == std::string("fmo") ? "feh" : name);
        service = services.at("fmo");
        inventory = Make("Inventory"); contents = Make("Dictionary");
        Get(service).refs["inventory"] = inventory;
        Get(inventory).refs["content"] = contents;
    }
    ~Fixture() { soul::RestoreInventoryBridge(); module = nullptr; }
    Obj Item(uint32_t uid, int id = 9687, int quantity = 1, short position = 63,
             uint32_t type = 83, bool allowed = true, bool hasCriteria = true) {
        auto item = Make("mc");
        Get(item).numbers = {{"uid", uid}, {"id", id}, {"quantity", quantity}, {"position", position}, {"type", type}};
        auto name = Make("String"); Get(name).text = "Objet natif " + std::to_string(id);
        Get(item).refs["name"] = name;
        if (hasCriteria) {
            auto criteria = Make("mq"); Get(criteria).scalar = allowed; Get(item).refs["criteria"] = criteria;
        }
        auto mask = Make("ItemMasks"); Get(mask).refs["item"] = item;
        Get(contents).items.push_back(mask);
        if (position == 1) Get(service).refs["equipped"] = item;
        return item;
    }
};
}
namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char* image, const char*, const char* name) const {
    fake::Check(std::string(image) == "Core.dll", "Unexpected native inventory image");
    return fake::Type(name);
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc});
    const auto result = method.get(); fake::methods.push_back(std::move(method)); return result;
}
void* Api::Native(Method method, uintptr_t rva) const {
    using namespace fake;
    const auto& m = *static_cast<const MethodInfo*>(method);
    static const std::map<std::string, std::pair<int, uintptr_t>> expected{
        {"feh.bdoq", {0, 0x11EDC80}},
        {"Inventory.get_content", {0, 0x68D2B0}}, {"Inventory.GetItem", {1, 0x2AECC70}},
        {"mc.mym", {0, 0x2AAC140}}, {"mc.myn", {0, 0xF20D40}}, {"mc.myo", {0, 0x861E70}},
        {"mc.nag", {0, 0x2AACF80}}, {"mc.mzb", {0, 0x2AAC720}}, {"mc.nan", {1, 0x2AAD790}},
        {"mc.nac", {0, 0x2AACE80}},
        {"mq.onc", {0, 0x2BFDAB0}}, {"EquipmentBehavior..ctor", {8, 0x2B62A60}},
        {"EquipmentBehavior.EquipItem", {1, 0x2B60FE0}}
    };
    Check(expected.at(m.klass->name + "." + m.name) == std::pair(m.argc, rva), "Native inventory ABI changed");
    return reinterpret_cast<void*>(rva);
}
Obj Api::Field(Obj object, const char* name) const { return fake::Get(object).refs.at(name); }
std::string Api::String(Obj object) const { return object ? fake::Get(object).text : std::string{}; }
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fake;
    Check(static_cast<fake::Class*>(klass)->name == "EquipmentBehavior" && args.size() == 8,
          "Only the verified native equipment constructor is allowed");
    const std::array<const char*, 8> names{"fmz", "fmo", "fmn", "fnm", "fng", "fou", "frf", "fjw"};
    size_t i = 0;
    for (auto arg : args) Check(arg == services.at(names[i++]), "Equipment dependency/order mismatch");
    ++constructors;
    if (onConstruct) onConstruct();
    return Make("EquipmentBehavior");
}
Obj Api::Call(Obj object, const char* rawName, std::initializer_list<void*> args) const {
    using namespace fake;
    const std::string name = rawName;
    if (name == failMethod) throw std::runtime_error("Fixture failure: " + name);
    auto& value = Get(object);
    const std::vector<void*> arguments(args);
    if (name == "bdoq") { ++primaryReads; return value.refs.at("inventory"); }
    if (name == "bdom") throw std::runtime_error("Native notification flag is not an equipment lock");
    if (name == "bdrm") throw std::runtime_error("Equipment display must not read the derived equipment view");
    if (name == "get_content") return value.refs.at("content");
    if (name == "get_Count") return Box(value.items.size());
    if (name == "get_Values") return object;
    if (name == "GetEnumerator") {
        auto iterator = Make("Enumerator"); Get(iterator).refs["dictionary"] = object;
        Get(iterator).numbers["index"] = -1; return iterator;
    }
    if (name == "MoveNext") return Box(++value.numbers["index"] < static_cast<int64_t>(Get(value.refs.at("dictionary")).items.size()));
    if (name == "get_Current") return Get(value.refs.at("dictionary")).items.at(static_cast<size_t>(value.numbers.at("index")));
    if (name == "Dispose") { ++disposals; if (onDispose) onDispose(); return nullptr; }
    if (name == "GetItem") {
        Check(args.size() == 1, "Item lookup requires UID");
        const auto uid = static_cast<uint32_t>(*static_cast<int32_t*>(arguments[0]));
        for (auto mask : Get(value.refs.at("content")).items) {
            const auto item = Get(mask).refs.at("item");
            if (Get(item).numbers.at("uid") == uid) return item;
        }
        return nullptr;
    }
    if (name == "mym") return Box(value.numbers.at("uid"));
    if (name == "myn") return Box(value.numbers.at("id"));
    if (name == "myo") return Box(value.numbers.at("quantity"));
    if (name == "nag") return Box(value.numbers.at("position"));
    if (name == "mzb") return Box(value.numbers.at("type"));
    if (name == "nac") return value.refs.at("name");
    if (name == "nan") {
        Check(args.size() == 1 && arguments[0] == services.at("fkq"), "Item criterion must use current provider");
        return value.refs["criteria"];
    }
    if (name == "onc") { ++criteriaChecks; return Box(value.scalar); }
    if (name == "EquipItem") {
        Check(value.klass->name == "EquipmentBehavior" && args.size() == 1, "Unexpected equip path");
        ++requests; requested = arguments[0]; Get(services.at("fmo")).numbers["pending"] = 1;
        if (onEquip) onEquip();
        return nullptr;
    }
    throw std::runtime_error("Unexpected native inventory method: " + value.klass->name + "." + name);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Module::Change(Settings next,bool) { settings=next; }
managed::Obj Module::FindService(const char* name) const {
    const auto found = fake::services.find(name); return found == fake::services.end() ? nullptr : found->second;
}
void Log(const std::string& text) { fake::logs.push_back(text); }
namespace soul {
void InstallCombat() {}
void ResetCombat() {}
CombatSnapshot ReadCombat(managed::Obj,managed::Obj) { return fake::combat; }
void InstallCapture() {}
void ResetCapture() {}
CaptureState ReadCapture(managed::Obj,managed::Obj) { return fake::capture; }
managed::Obj GetUiRoot() { return module->compositor.get(); }
void InstallLayout() {}
void ResetLayout() {}
LayoutPosition ReadLayout(managed::Obj) { return {120,240,7,true}; }
bool SaveLayoutPosition(managed::Obj,int,int,uint64_t) { throw std::runtime_error("No user drag occurred in inventory integration"); }
void Panel::Install(void (*equip)(),void (*)(int,int,uint64_t),void (*)()) { fake::click = equip; }
void Panel::Render(managed::Obj,const PanelView& view,int x,int y,uint64_t revision) {
    fake::Check(x==120&&y==240&&revision==7,"Controller must retain native layout contract");
    fake::shown=view; fake::presentedItem.reset(view.equippedItem); ++fake::renders;
}
void Panel::Hide() { fake::shown={}; fake::presentedItem.reset(); ++fake::hides; }
bool PanelCleanupPending() { return false; }
}

namespace fake {
void CatalogueAndStock() {
    Fixture f;
    f.Item(1, 9686, 7); f.Item(2, 9687, 4); f.Item(3, 9688, 2); f.Item(4, 9689, 3); f.Item(5, 9690, 6);
    f.Item(6, 9687, 1, 31);
    f.Item(7, 7010, 8, 63, 85); f.Item(8, 9718, 1); f.Item(9, 18241, 2); f.Item(10, 23324); f.Item(11, 29100);
    f.Item(12, 9687, 5, 63, 85); f.Item(13, 9687, 2, 2); f.Item(14, 9687, 0); f.Item(0, 9687);
    const auto snapshot = soul::ReadInventory(f.service);
    Check(snapshot.complete && snapshot.equippedGenericId == 9687 && snapshot.bag.size() == 5,
          "Stock must contain only five regular empty stone types in backpack");
    const std::array<int, 5> quantities{7, 4, 2, 3, 6};
    for (size_t i = 0; i < 5; ++i) Check(snapshot.bag[i].genericId == 9686 + static_cast<int>(i) &&
        snapshot.bag[i].quantity == quantities[i] && snapshot.bag[i].equippable, "Wrong stock or eligibility");
    Check(requests == 0 && constructors == 0 && disposals == 1, "Inventory reading must never equip or construct action object");
}
void EligibilityAndFreshClick() {
    for (int id = 9686; id <= 9690; ++id) {
        Fixture f;
        const auto item = f.Item(0xF2345678U, id, 3);
        f.Item(7, id == 9686 ? 9687 : 9686, 1, 31);
        const auto read = soul::ReadInventory(f.service);
        Check(read.complete && read.bag[0].uid == 0xF2345678ULL, "Upper-half UID lost native bits");
        Check(soul::EquipExactStone(f.service, 0xF2345678ULL, id) && requested == item && requests == 1 && constructors == 1,
              "Explicit exact stone click did not follow native equipment action");
        Check(!soul::EquipExactStone(f.service, 0xF2345678ULL, id) && requests == 1, "Pending native request must not repeat");
        Check(Get(item).numbers.at("position") == 63 && soul::ReadInventory(f.service).equippedGenericId != id,
              "Dispatched request must not invent successful equipment");
    }
    Fixture f;
    f.Item(1, 9687, 3, 63, 83, false);
    f.Item(2, 9687, 2, 63, 83, true, false);
    auto read = soul::ReadInventory(f.service);
    Check(read.complete && !read.bag[0].equippable && read.bag[1].equippable, "Native equipment criteria not respected");
    Check(!soul::EquipExactStone(f.service, 1, 9687) && requests == 0, "Failed criterion must block equip");
    Check(soul::EquipExactStone(f.service, 2, 9687), "Absent native criterion means no additional constraint");
}
void RejectStaleOrInvalidRequests() {
    for (int scenario = 0; scenario < 12; ++scenario) {
        Fixture f;
        auto item = f.Item(12);
        Check(soul::ReadInventory(f.service).complete, "Initial snapshot missing");
        uint64_t uid = 12;
        int id = 9687;
        if (scenario == 0) Get(item).numbers["quantity"] = 0;
        if (scenario == 1) Get(item).numbers["position"] = 31;
        if (scenario == 2) Get(item).numbers["id"] = 9688;
        if (scenario == 3) Get(item).numbers["type"] = 85;
        if (scenario == 4) Get(Get(item).refs.at("criteria")).scalar = false;
        if (scenario == 5) services["fmo"] = Make("feh");
        if (scenario == 6) Get(f.contents).items.clear();
        if (scenario == 7) uid = 0x10000000CULL;
        if (scenario == 8) id = 9718;
        if (scenario == 9) f.Item(15, 9687, 1, 31);
        if (scenario == 10) f.state.stopped = true;
        if (scenario == 11) onConstruct = [&] { f.state.compositor.reset(Make("OtherSession")); };
        Check(!soul::EquipExactStone(f.service, uid, id) && requests == 0, "Stale/invalid item dispatched equipment");
    }
}
void PrimaryEquipmentUpdates() {
    Fixture f;
    auto stone = f.Item(0xF2345678U, 9687, 1);
    auto spare = f.Item(19, 9687, 3);
    auto weapon = f.Item(22, 1234, 1, 1, 6);
    Get(Get(weapon).refs.at("name")).text = "Épée équipée";
    auto before = soul::ReadInventory(f.service);
    Check(before.complete && before.equippedGenericId == 0 && !before.equippedItem && before.equippedName.empty(),
          "Weapon position1 must not become the soul-stone preview when consumable31 is empty");
    Check(soul::EquipExactStone(f.service, 0xF2345678ULL, 9687), "Expected explicit equipment request");
    // The authoritative objects change in place, while bdrm's derived view still
    // contains the old weapon. No synthetic fixture assignment updates that view.
    Get(stone).numbers["position"] = 31;
    Get(f.service).numbers["pending"] = 0;
    auto equipped = soul::ReadInventory(f.service);
    Check(equipped.complete && equipped.equippedGenericId == 9687 && equipped.equippedItem &&
          equipped.equippedItem->get() == stone && equipped.equippedName == "Objet natif 9687" &&
          equipped.bag.size() == 1 && equipped.bag[0].uid == 19 && Get(f.service).refs["equipped"] == weapon,
          "Bag-to-consumable31 transition must follow primary inventory while weapon1 remains equipped");
    Check(Get(weapon).numbers["position"]==1,"Equipping the stone never changes the weapon slot");
    auto copy = equipped;
    Check(copy.equippedItem == equipped.equippedItem,"Snapshot copies share one owned managed stone reference");
    Check(!soul::EquipExactStone(f.service, 19, 9687) && requests == 1,
          "A stale equipment view must not allow a duplicate same-stone request");
    Get(stone).numbers["position"] = 63;
    Get(spare).numbers["position"] = 31;
    Get(spare).numbers["id"] = 9688;
    Get(Get(spare).refs.at("name")).text = "Grande pierre d’âme";
    Get(f.service).refs["equipped"] = nullptr;
    auto replaced = soul::ReadInventory(f.service);
    Check(replaced.complete && replaced.equippedGenericId == 9688 && replaced.equippedItem->get() == spare &&
          replaced.equippedName == "Grande pierre d’âme",
          "Same inventory object and empty derived slot must still refresh the equipped identity");
    Get(spare).numbers["id"] = 9718;
    auto special = soul::ReadInventory(f.service);
    Check(special.complete && special.equippedGenericId == 0 && special.equippedItem->get() == spare,
          "A special stone must remain visible without qualifying as the required standard stone");
    Get(spare).numbers["position"] = 63;
    auto empty = soul::ReadInventory(f.service);
    Check(empty.complete && empty.equippedGenericId == 0 && !empty.equippedItem && empty.equippedName.empty(),
          "Unequipping must clear old item, name and standard-stone state");
}
void RejectAmbiguousOrChangingEquipment() {
    for (int scenario = 0; scenario < 11; ++scenario) {
        Fixture f;
        auto item = f.Item(42, 9687, 1, 31);
        if (scenario == 0) f.Item(43, 1234, 1, 31, 6);
        if (scenario == 1) Get(item).numbers["quantity"] = 0;
        if (scenario == 2) Get(item).numbers["uid"] = 0;
        if (scenario == 3) onDispose = [&] { Get(item).numbers["position"] = 63; };
        if (scenario == 4) onDispose = [&] { Get(item).numbers["uid"] = 51; };
        if (scenario == 5) onDispose = [&] { Get(item).numbers["id"] = 9688; };
        if (scenario == 6) onDispose = [&] { services["fmo"] = Make("feh"); };
        if (scenario == 7) onDispose = [&] {
            auto replacement = Make("mc");
            Get(replacement).numbers = Get(item).numbers;
            Get(Get(f.contents).items.front()).refs["item"] = replacement;
        };
        if (scenario == 8) onDispose = [&] { f.state.compositor.reset(Make("OtherSession")); };
        if (scenario == 9) onDispose = [&] { f.Item(43); };
        if (scenario == 10) onDispose = [&] { Get(f.inventory).refs["content"] = Make("Dictionary"); };
        const auto read = soul::ReadInventory(f.service);
        Check(!read.complete && !read.equippedItem && read.equippedName.empty() && read.equippedGenericId == 0,
              "Ambiguous or mutated equipment must not present a partial successful state");
    }
}
void FailuresAndLifetime() {
    for (const auto failure : {"get_content", "MoveNext", "get_Current", "nan", "onc", "Dispose"}) {
        Fixture f; f.Item(1); failMethod = failure;
        const auto read = soul::ReadInventory(f.service);
        Check(!read.complete && read.bag.empty() && read.equippedGenericId == 0 && requests == 0,
              "Partial/failed inventory must never appear complete");
        soul::RestoreInventoryBridge();
        Check(handles.size() == 1, "Failure leaked inventory GC ownership");
    }
    {
        Fixture f; f.Item(1); f.Item(1);
        Check(!soul::ReadInventory(f.service).complete, "Duplicate item UID must not inflate stock");
    }
    {
        Fixture f; f.Item(1); services.erase("fkq");
        Check(!soul::ReadInventory(f.service).complete && !soul::EquipExactStone(f.service, 1, 9687),
              "Missing criterion provider must fail closed");
    }
    {
        Fixture f; f.Item(1); failMethod = "EquipItem";
        Check(!soul::EquipExactStone(f.service, 1, 9687) && requests == 0, "Native dispatch exception falsely succeeded");
        soul::RestoreInventoryBridge();
        Check(handles.size() == 1, "Equipment instance refs survived restoration");
    }
}
void ActualControllerInventoryEquipTransition() {
    Fixture f; f.state.ready=true; f.state.settings.soulReminder=true;
    const auto weapon=f.Item(101,1234,1,1,6);
    const auto wrong=f.Item(102,9688,1,31);
    const auto correct=f.Item(103,9687,1,63);
    f.Item(104,9687,2,63);
    Get(Get(weapon).refs.at("name")).text="Arme à ne pas afficher";
    Get(Get(wrong).refs.at("name")).text="Grande pierre d’âme parfaite";
    Get(Get(correct).refs.at("name")).text="Moyenne pierre d’âme parfaite";
    combat={soul::CombatState::Ready,42,51};capture=soul::CaptureState::Missing;
    InstallSoulHelper();TickSoulHelper();
    Check(shown.showEquipment&&shown.equippedItem==wrong&&!shown.equippedCorrect&&shown.equipEnabled&&
        shown.equipped.find("Grande pierre")!=std::string::npos&&shown.stock.find("3")!=std::string::npos,
        "Actual controller must display the wrong slot31 stone and exact stock, never weapon1");
    click();click();
    Check(requests==1&&requested==correct,"Explicit click must route exact bag stone through real native bridge once");
    TickSoulHelper();Check(!shown.equippedCorrect&&!shown.equipEnabled&&shown.equippedItem==wrong,
        "Dispatch alone cannot claim equipped success");
    // Server/native inventory modifies the primary mc instances. The unrelated
    // weapon remains equipped, and the old derived equipment view remains stale.
    Get(wrong).numbers["position"]=63;Get(correct).numbers["position"]=31;Get(f.service).numbers["pending"]=0;
    Sleep(230);TickSoulHelper();
    Check(shown.equippedCorrect&&!shown.equipEnabled&&shown.equippedItem==correct&&
        shown.equipped.find("Moyenne pierre")!=std::string::npos&&shown.stock.find("2")!=std::string::npos,
        "Production helper/reader must refresh icon, name, correct tier and reduced bag stock after native confirmation");
    Check(Get(weapon).numbers["position"]==1&&requests==1,"Confirmation must not move weapon or send another action");
    const int before=primaryReads;combat.state=soul::CombatState::Fighting;TickSoulHelper();
    Check(!shown.showEquipment&&!shown.showEquip&&!shown.equippedItem&&shown.equipped.empty()&&
        shown.stock.empty()&&shown.requirement.empty()&&shown.statusTone==soul::StatusTone::Warning&&primaryReads>before,
        "Combat start rechecks equipment while keeping only the compact status");
    const int fightingReads=primaryReads;
    capture=soul::CaptureState::Active;TickSoulHelper();Check(shown.statusTone==soul::StatusTone::Good&&primaryReads==fightingReads,
        "Capture effect uses throttled equipment snapshot");click();Check(requests==1,"Fighting cannot equip");
    combat.state=soul::CombatState::Ready;TickSoulHelper();
    Check(shown.showEquipment&&shown.equippedItem==correct&&shown.equippedCorrect&&primaryReads>before,
        "A placement reentry must freshly bind the actual slot31 item");
    const int beforeHide=renders;combat.state=soul::CombatState::Hidden;TickSoulHelper();click();
    Check(renders==beforeHide&&!presentedItem.get()&&requests==1,"Hidden/PvP path removes item and rejects click");
    RestoreSoulHelper();
}
void ConsumableNotificationDoesNotLockEquipment() {
    Fixture f; f.state.ready=true; f.state.settings.soulReminder=true;
    f.Item(201,1234,1,1,6);
    const auto stock=f.Item(202,9686,58);
    // Native feh+0xF4 is left set after the position31/63 notification path.
    // It is not an in-flight request lock, even with no stone currently worn.
    Get(f.service).numbers["pending"]=1;
    combat={soul::CombatState::Ready,43,50};
    InstallSoulHelper();TickSoulHelper();
    Check(shown.stock=="En sac : 58" && shown.requirement=="Requise : 50 · Monstre : niv. 50" &&
        !shown.equippedItem && shown.equipEnabled && shown.showEquip && shown.status.empty(),
        "Native consumable notification flag must not hide Equip with 58 matching stones in bag");
    click();click();
    Check(requests==1 && requested==stock,"Explicit equipment must dispatch once despite notification flag");
    TickSoulHelper();
    Check(!shown.equippedCorrect && !shown.equipEnabled,"Request alone must not claim successful equipment");
    // A stack of58 splits: server equips a NEW UID and leaves57 in the bag.
    Get(stock).numbers["quantity"]=57;
    const auto worn=f.Item(203,9686,1,31);
    Sleep(230);TickSoulHelper();
    Check(shown.equippedCorrect && shown.equippedItem==worn && shown.stock=="En sac : 57" &&
        !shown.showEquip && shown.status.empty(),"Confirmation must recognize the split stack's new equipped UID");
    Check(Get(f.service).numbers["pending"]==1,"Mod must never clear native notification state");
    // Native unequip still does not clear that flag. A later explicit action works.
    Get(worn).numbers["position"]=63;
    const auto available=soul::ReadInventory(f.service);
    Check(available.complete && available.bag.front().equippable &&
        soul::EquipExactStone(f.service,202,9686) && requests==2,
        "Confirmed request must release its own gate for subsequent equipment");
    RestoreSoulHelper();
}
void OwnRequestReentryAndTimeout() {
    Fixture f;
    f.Item(301,9686,58);
    f.Item(302,9687,4);
    onEquip=[&] {
        const auto read=soul::ReadInventory(f.service);
        Check(read.complete && read.equipmentPending && !read.bag[0].equippable && !read.bag[1].equippable &&
            !soul::EquipExactStone(f.service,301,9686) && !soul::EquipExactStone(f.service,302,9687),
            "Reentrant actions must respect the mod's pending request across both tiers");
    };
    Check(soul::EquipExactStone(f.service,301,9686) && requests==1,"Initial request was not dispatched once");
    onEquip={};
    Sleep(3050);
    const auto retry=soul::ReadInventory(f.service);
    Check(retry.complete && !retry.equipmentPending && retry.bag[0].equippable && !retry.equippedGenericId && requests==1,
        "Unconfirmed request must expire without claiming success or automatically retrying");
    Check(Get(f.service).numbers["pending"]==1 && soul::EquipExactStone(f.service,301,9686) && requests==2,
        "A later explicit click must work even if native notification remains set");
}
}
int main() {
    try {
        fake::InitializeApi();
        fake::CatalogueAndStock();
        fake::EligibilityAndFreshClick();
        fake::RejectStaleOrInvalidRequests();
        fake::PrimaryEquipmentUpdates();
        fake::RejectAmbiguousOrChangingEquipment();
        fake::FailuresAndLifetime();
        fake::ActualControllerInventoryEquipTransition();
        fake::ConsumableNotificationDoesNotLockEquipment();
        fake::OwnRequestReentryAndTimeout();
        fake::Check(fake::handles.empty(), "Managed references survived test fixture");
        std::cout << "SoulInventoryTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SoulInventoryTests failed: " << error.what() << '\n';
        return 1;
    }
}

namespace soul { void InstallSpell() {} managed::Obj CaptureSpell(bool& enabled) { enabled=false; return nullptr; } }
