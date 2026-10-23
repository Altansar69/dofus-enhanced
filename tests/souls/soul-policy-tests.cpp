#include "souls/soul-policy.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Stack { uint64_t uid; int genericId, quantity; bool equippable; };
struct Inventory { bool complete; int equippedGenericId; std::vector<Stack> bag; };
}
int main() {
    try {
        for (const auto& [level, power] : std::vector<std::pair<int,int>>{{1,50},{50,50},{51,100},
            {100,100},{101,150},{150,150},{151,190},{190,190},{191,1000},{1000,1000}})
            Check(soul::RequiredStone(level) && soul::RequiredStone(level)->power == power, "Incorrect exact bracket");
        for (int level : {-1,0,1001}) Check(!soul::RequiredStone(level), "Unknown/special level must not guess a stone");
        const auto& required = *soul::RequiredStone(51);
        Inventory inventory{true, 9688, {{7,9686,5,true},{8,9688,9,true},{9,7010,3,true}}};
        auto selected = soul::SelectStone(required, inventory);
        Check(!selected.equipped && selected.bagCount == 0 && selected.uid == 0, "150 cannot substitute for exact100");
        inventory.bag = {{5,9687,3,false},{6,9687,4,true},{7,9687,2,true},{8,9687,0,true}};
        selected = soul::SelectStone(required, inventory);
        Check(selected.bagCount == 9 && selected.uid == 6, "Count bags independently of eligibility; select one usable stack");
        inventory.equippedGenericId = 9687;
        Check(soul::SelectStone(required,inventory).equipped, "Exact equipped type matches");
        inventory.complete = false;
        selected = soul::SelectStone(required, inventory);
        Check(!selected.equipped && !selected.uid && !selected.bagCount, "Incomplete inventory cannot offer equip");
        for (int id : {7010,9718,18241,23324,29100,0}) Check(!soul::StoneById(id), "Special/full stones excluded");
        soul::AutoEquipAttempts attempts;
        Check(!attempts.Allowed(0,9687) && !attempts.Allowed(1,7010), "Unknown fight and nonstandard stone cannot auto equip");
        Check(attempts.Allowed(1,9687), "New fight permits exact tier");
        attempts.Mark(1,9687);
        Check(!attempts.Allowed(1,9687) && attempts.Allowed(1,9688), "Each tier can be handled only once per fight");
        attempts.Mark(1,9688);
        Check(!attempts.Allowed(1,9687) && !attempts.Allowed(1,9688), "Earlier tiers remain remembered");
        Check(attempts.Allowed(2,9687), "Next fight resets handled tiers");
        soul::PendingEquip pending;
        pending.Start(12,9687,100);
        Check(pending.Active(12,9687,false,101), "Pending server confirmation must block repeated clicks");
        Check(!pending.Active(12,9687,true,200), "Observed equipment completes request");
        pending.Start(12,9687,100);
        Check(!pending.Active(13,9687,false,200), "New fight invalidates pending action");
        pending.Start(12,9687,100);
        Check(!pending.Active(12,9688,false,200), "Group-level change invalidates old target");
        pending.Start(12,9687,100);
        Check(!pending.Active(12,9687,false,3100), "Timeout allows another explicit click, never retries");
        std::cout << "PASS: exact soul brackets, stock, equipped mismatch, pending confirmation and stale sessions\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
