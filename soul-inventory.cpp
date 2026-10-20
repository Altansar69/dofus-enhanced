#include "soul-inventory.hpp"
#include "module.hpp"
#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <unordered_set>

namespace soul {
namespace {
using namespace managed;
// Pinned ItemsData: type83 -> supertype70 -> possiblePositions [31].
// CharacterInventoryPositionEnum.INVENTORY_POSITION_CONSUMABLE is31.
constexpr short SoulStonePosition = 31;
constexpr short BagPosition = 63;
constexpr int MaxInventoryEntries = 32768;
constexpr std::array<const char*, 9> DependencyNames{
    "fmz", "fmo", "fmn", "fnm", "fng", "fou", "frf", "fjw", "fkq"
};
struct Bridge {
    Module* owner = nullptr;
    Ref compositor;
    std::array<Ref, DependencyNames.size()> dependencies;
    Ref equipment;
    Klass itemClass = nullptr;
    Klass maskClass = nullptr;
    Klass equipmentClass = nullptr;
    int requestedGenericId = 0;
    uint64_t requestUntil = 0;
};
std::unique_ptr<Bridge> bridge;
bool RegularStone(int id) { return id >= 9686 && id <= 9690; }
bool Instance(Obj object, Klass klass) {
    return object && api.class_is_assignable_from(klass, api.object_get_class(object));
}
void Guard(Klass klass, const char* name, int argc, uintptr_t rva) {
    api.Native(api.Find(klass, name, argc), rva);
}
bool Available(Obj service) {
    return service && module && !module->stopped && module->servicesReady &&
           module->compositor.get() && module->FindService("fmo") == service;
}
Bridge& Bind(Obj service) {
    if (!Available(service)) throw std::runtime_error("Soul inventory session unavailable");
    bool same = bridge && bridge->owner == module &&
        bridge->compositor.get() == module->compositor.get();
    std::array<Ref, DependencyNames.size()> dependencies;
    for (size_t i = 0; i < dependencies.size(); ++i) {
        dependencies[i].reset(module->FindService(DependencyNames[i]));
        if (!dependencies[i].get()) throw std::runtime_error("Soul inventory dependency unavailable");
        same = same && bridge->dependencies[i].get() == dependencies[i].get();
    }
    if (dependencies[1].get() != service) throw std::runtime_error("Soul inventory service changed");
    if (same) return *bridge;
    auto next = std::make_unique<Bridge>();
    next->owner = module;
    next->compositor.reset(module->compositor.get());
    next->dependencies = std::move(dependencies);
    const auto serviceClass = api.Class("Core.dll", "", "feh");
    if (!Instance(service, serviceClass)) throw std::runtime_error("Unsupported inventory implementation");
    Guard(serviceClass, "bdoq", 0, 0x11EDC80);
    const auto inventory = api.Class("Core.dll", "Core.UILogic.Inventory", "Inventory");
    Guard(inventory, "get_content", 0, 0x68D2B0);
    Guard(inventory, "GetItem", 1, 0x2AECC70);
    next->itemClass = api.Class("Core.dll", "", "mc");
    next->maskClass = api.Class("Core.dll", "Core.UILogic.Inventory", "ItemMasks");
    Guard(next->itemClass, "mym", 0, 0x2AAC140);
    Guard(next->itemClass, "myn", 0, 0xF20D40);
    Guard(next->itemClass, "myo", 0, 0x861E70);
    Guard(next->itemClass, "nag", 0, 0x2AACF80);
    Guard(next->itemClass, "mzb", 0, 0x2AAC720);
    Guard(next->itemClass, "nan", 1, 0x2AAD790);
    Guard(next->itemClass, "nac", 0, 0x2AACE80);
    Guard(api.Class("Core.dll", "", "mq"), "onc", 0, 0x2BFDAB0);
    next->equipmentClass = api.Class("Core.dll", "Core.UILogic.Inventory.Behaviours", "EquipmentBehavior");
    Guard(next->equipmentClass, ".ctor", 8, 0x2B62A60);
    Guard(next->equipmentClass, "EquipItem", 1, 0x2B60FE0);
    bridge = std::move(next);
    return *bridge;
}
uint64_t ItemUid(Obj item) {
    return static_cast<uint32_t>(api.Value<int32_t>(api.Call(item, "mym")));
}
bool Stone(Bridge& state, Obj item, int* id = nullptr) {
    if (!Instance(item, state.itemClass)) return false;
    const auto genericId = api.Value<int>(api.Call(item, "myn"));
    if (!RegularStone(genericId) || api.Value<uint32_t>(api.Call(item, "mzb")) != 83) return false;
    if (id) *id = genericId;
    return true;
}
bool Criteria(Bridge& state, Obj item) {
    Ref criteria(api.Call(item, "nan", {state.dependencies[8].get()}));
    // nan is the native equipment criterion (including instance overrides).
    // ohr checks visibility and is deliberately not used here.
    return !criteria.get() || api.Value<bool>(api.Call(criteria.get(), "onc"));
}
bool Pending(Bridge& state, int equippedGenericId = 0) {
    // feh.bdom is a native equipment-chat notification flag, not a request
    // lock: fef.bdnb leaves it set for consumable31 and backpack63 updates.
    // Track only our own equipment request, and never modify that native flag.
    if (equippedGenericId == state.requestedGenericId || GetTickCount64() >= state.requestUntil) {
        state.requestedGenericId = 0;
        state.requestUntil = 0;
    }
    return state.requestUntil != 0;
}
void Error(const char* action, const std::exception& error) {
    Log(std::string("Soul inventory ") + action + ": " + error.what());
}
}

InventorySnapshot ReadInventory(Obj service) {
    InventorySnapshot snapshot;
    try {
        auto& state = Bind(service);
        Ref inventory(api.Call(service, "bdoq"));
        Ref contents(api.Call(inventory.get(), "get_content"));
        const auto expectedCount = api.Value<int>(api.Call(contents.get(), "get_Count"));
        if (expectedCount < 0 || expectedCount > MaxInventoryEntries)
            throw std::runtime_error("Unsupported inventory size");
        Ref values(api.Call(contents.get(), "get_Values"));
        Ref iterator(api.Call(values.get(), "GetEnumerator"));
        std::unordered_set<uint64_t> seen;
        uint64_t equippedUid = 0;
        int equippedId = 0;
        int count = 0;
        while (api.Value<bool>(api.Call(iterator.get(), "MoveNext"))) {
            if (++count > expectedCount) throw std::runtime_error("Inventory changed during enumeration");
            Ref mask(api.Call(iterator.get(), "get_Current"));
            if (!Instance(mask.get(), state.maskClass)) throw std::runtime_error("Invalid inventory entry");
            Ref item(api.Field(mask.get(), "item"));
            if (!Instance(item.get(), state.itemClass)) continue;
            const auto position = api.Value<short>(api.Call(item.get(), "nag"));
            if (position != SoulStonePosition && position != BagPosition) continue;
            const auto uid = ItemUid(item.get());
            const auto quantity = api.Value<int>(api.Call(item.get(), "myo"));
            if (position == SoulStonePosition) {
                if (!uid || quantity <= 0 || snapshot.equippedItem)
                    throw std::runtime_error("Invalid or ambiguous equipped consumable");
                if (!seen.insert(uid).second) throw std::runtime_error("Duplicate inventory identifier");
                // Inventory.content is updated before its derived EquipmentView.
                // Read the primary object's position rather than bdrm's view slot.
                snapshot.equippedItem = std::make_shared<Ref>(item.get());
                equippedUid = uid;
                equippedId = api.Value<int>(api.Call(item.get(), "myn"));
                if (Stone(state, item.get())) snapshot.equippedGenericId = equippedId;
                Ref name(api.Call(item.get(), "nac"));
                snapshot.equippedName = api.String(name.get());
                continue;
            }
            int genericId = 0;
            if (!Stone(state, item.get(), &genericId)) continue;
            if (!uid || quantity <= 0) continue;
            if (!seen.insert(uid).second) throw std::runtime_error("Duplicate inventory identifier");
            snapshot.bag.push_back({uid, genericId, quantity, Criteria(state, item.get())});
        }
        api.Call(iterator.get(), "Dispose");
        if (count != expectedCount || api.Value<int>(api.Call(contents.get(), "get_Count")) != expectedCount ||
            !Available(service) || module != state.owner || module->compositor.get() != state.compositor.get() ||
            api.Call(service, "bdoq") != inventory.get() || api.Call(inventory.get(), "get_content") != contents.get())
            throw std::runtime_error("Inventory changed during read");
        if (snapshot.equippedItem) {
            auto nativeUid = std::bit_cast<int32_t>(static_cast<uint32_t>(equippedUid));
            const auto equipped = snapshot.equippedItem->get();
            if (api.Call(inventory.get(), "GetItem", {&nativeUid}) != equipped ||
                ItemUid(equipped) != equippedUid || api.Value<int>(api.Call(equipped, "myn")) != equippedId ||
                api.Value<short>(api.Call(equipped, "nag")) != SoulStonePosition ||
                api.Value<int>(api.Call(equipped, "myo")) <= 0)
                throw std::runtime_error("Equipped consumable changed during read");
        }
        snapshot.equipmentPending = Pending(state, snapshot.equippedGenericId);
        if (snapshot.equipmentPending)
            for (auto& stack : snapshot.bag) stack.equippable = false;
        snapshot.complete = true;
    } catch (const std::exception& error) {
        Error("read failed", error);
        snapshot = {};
    }
    return snapshot;
}

bool EquipExactStone(Obj service, uint64_t uid, int genericId) {
    if (!RegularStone(genericId) || !uid || uid > std::numeric_limits<uint32_t>::max()) return false;
    try {
        // The same primary-inventory read governs both the display and this
        // duplicate-request guard, including when the derived view is stale.
        const auto current = ReadInventory(service);
        if (!current.complete || current.equipmentPending || current.equippedGenericId == genericId) return false;
        auto& state = Bind(service);
        if (Pending(state)) return false;
        Ref inventory(api.Call(service, "bdoq"));
        auto nativeUid = std::bit_cast<int32_t>(static_cast<uint32_t>(uid));
        Ref item(api.Call(inventory.get(), "GetItem", {&nativeUid}));
        int foundId = 0;
        if (!Stone(state, item.get(), &foundId) || foundId != genericId || ItemUid(item.get()) != uid ||
            api.Value<short>(api.Call(item.get(), "nag")) != BagPosition ||
            api.Value<int>(api.Call(item.get(), "myo")) <= 0 || !Criteria(state, item.get())) return false;
        if (!state.equipment.get()) {
            const auto& d = state.dependencies;
            state.equipment.reset(api.New(state.equipmentClass, {
                d[0].get(), d[1].get(), d[2].get(), d[3].get(),
                d[4].get(), d[5].get(), d[6].get(), d[7].get()}));
        }
        if (!Available(service) || module != state.owner ||
            module->compositor.get() != state.compositor.get() || Pending(state) ||
            api.Call(service, "bdoq") != inventory.get() ||
            api.Call(inventory.get(), "GetItem", {&nativeUid}) != item.get()) return false;
        // Gate before dispatch, which may synchronously reenter the panel/menu.
        // Confirmation may have a different UID when the server splits a stack.
        // A timeout permits another user click; it never retries automatically.
        state.requestedGenericId = genericId;
        state.requestUntil = GetTickCount64() + 3000;
        api.Call(state.equipment.get(), "EquipItem", {item.get()});
        return true;
    } catch (const std::exception& error) {
        Error("equip failed", error);
        return false;
    }
}
void RestoreInventoryBridge() { bridge.reset(); }
}
