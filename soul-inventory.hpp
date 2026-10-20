#pragma once
#include "runtime.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace soul {
struct StoneStack {
    // Native item identifiers are int32 keys; retain all 32 bits without sign extension.
    uint64_t uid = 0;
    int genericId = 0;
    int quantity = 0;
    bool equippable = false;
};
struct InventorySnapshot {
    bool complete = false;
    int equippedGenericId = 0;
    std::vector<StoneStack> bag;
    // Own the actual consumable-slot item for the native icon/tooltip. On this
    // client soul stones use position31 (type83 -> supertype70), independently
    // of the weapon in position1. Policy uses standard-only equippedGenericId.
    std::shared_ptr<managed::Ref> equippedItem;
    std::string equippedName;
    // The mod's equipment request awaits an inventory confirmation.
    bool equipmentPending = false;
};
InventorySnapshot ReadInventory(managed::Obj service);
// Callers validate the combat or selected group and exact power first. The helper
// shares this action for its manual button and bounded automatic placement attempt.
// True means the native request was sent;
// the inventory update remains the authority on successful equipment.
bool EquipExactStone(managed::Obj service, uint64_t uid, int genericId);
void RestoreInventoryBridge();
}
