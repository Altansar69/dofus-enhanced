#pragma once
#include <array>
#include <cstdint>

namespace soul {
struct StoneType { int genericId, power; const char* name; };
// Verified against this client's ItemsDataRoot: type83, action705, value=power.
inline constexpr std::array<StoneType, 5> Stones{{
    {9686, 50, "Petite pierre d’âme"},
    {9687, 100, "Moyenne pierre d’âme"},
    {9688, 150, "Grande pierre d’âme"},
    {9689, 190, "Énorme pierre d’âme"},
    {9690, 1000, "Gigantesque pierre d’âme"}
}};
inline const StoneType* RequiredStone(int highestLevel) {
    if (highestLevel <= 0) return nullptr;
    for (const auto& stone : Stones) if (highestLevel <= stone.power) return &stone;
    return nullptr;
}
inline const StoneType* StoneById(int genericId) {
    for (const auto& stone : Stones) if (genericId == stone.genericId) return &stone;
    return nullptr;
}
struct Selection {
    int64_t bagCount = 0;
    uint64_t uid = 0;
    bool equipped = false;
};
template<class Inventory> Selection SelectStone(const StoneType& required, const Inventory& inventory) {
    Selection result;
    if (!inventory.complete) return result;
    result.equipped = inventory.equippedGenericId == required.genericId;
    for (const auto& stack : inventory.bag) {
        if (stack.genericId != required.genericId || stack.quantity <= 0) continue;
        result.bagCount += stack.quantity;
        if (stack.equippable && stack.uid && (!result.uid || stack.uid < result.uid)) result.uid = stack.uid;
    }
    return result;
}
// Retain handled tiers across panel hiding, rejected requests and pending timeouts.
// A manual request also counts, so enabling automation cannot repeat it.
struct AutoEquipAttempts {
    uint64_t fight = 0;
    std::array<bool, Stones.size()> handled{};
    bool Allowed(uint64_t session, int genericId) {
        if (!session) return false;
        if (fight != session) { fight = session; handled.fill(false); }
        for (size_t i = 0; i < Stones.size(); ++i)
            if (Stones[i].genericId == genericId) return !handled[i];
        return false;
    }
    void Mark(uint64_t session, int genericId) {
        if (!Allowed(session, genericId)) return;
        for (size_t i = 0; i < Stones.size(); ++i)
            if (Stones[i].genericId == genericId) handled[i] = true;
    }
};
struct PendingEquip {
    uint64_t fight = 0, until = 0;
    int genericId = 0;
    bool Active(uint64_t session, int required, bool alreadyEquipped, uint64_t now) {
        if (fight != session || genericId != required || alreadyEquipped || now >= until) Clear();
        return until != 0;
    }
    void Start(uint64_t session, int required, uint64_t now) {
        fight = session; genericId = required; until = now + 3000;
    }
    void Clear() { fight = until = 0; genericId = 0; }
};
}
