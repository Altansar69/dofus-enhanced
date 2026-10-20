#include "instant-combat.hpp"
#include "module.hpp"
#include <vector>

using namespace managed;
namespace {
using MoveFn = bool(*)(Obj, Obj, Obj, uint64_t, uint64_t, uint64_t, Method);
MoveFn originalMove;
constexpr size_t maxQueued = 1024;
thread_local unsigned drainingDepth = 0;
struct Batch {
    std::vector<std::function<void()>> actions;
    Batch* parent;
    Batch* root;
    size_t checkpoint;
    bool committed = false;
    static thread_local Batch* current;
    Batch() : parent(current), root(parent ? parent->root : this), checkpoint(root->actions.size()) { current = this; }
    ~Batch() {
        if (!committed) root->actions.resize(checkpoint);
        current = parent;
    }
};
thread_local Batch* Batch::current = nullptr;

bool MoveHook(Obj entity, Obj route, Obj behavior, uint64_t first, uint64_t second,
              uint64_t third, Method method) {
    Batch batch;
    // rfw itself dispatches rjn, then followers, then OnMoveStart. Keep that
    // entire transaction before running a movement's terminal operations.
    const bool started = originalMove(entity, route, behavior, first, second, third, method);
    if (!started) return false;
    batch.committed = true;
    if (batch.parent) return true;
    Batch::current = nullptr;
    if (drainingDepth >= 32) return true;
    struct Depth { Depth() { ++drainingDepth; } ~Depth() { --drainingDepth; } } depth;
    for (auto& action : batch.actions) {
        try { action(); }
        catch (const std::exception& error) {
            Log(std::string("MOVEMENT_DISPATCH_ERROR ") + error.what());
            if (module) module->stopRequested.store(true);
            break;
        } catch (...) {
            if (module) module->stopRequested.store(true);
            throw;
        }
    }
    return true;
}
}

bool CanDeferInstantMovement() {
    auto batch = Batch::current;
    return batch && batch->root->actions.size() < maxQueued && drainingDepth < 32;
}

bool DeferInstantMovement(std::function<void()> finish) {
    if (!CanDeferInstantMovement()) return false;
    Batch::current->root->actions.push_back(std::move(finish));
    return true;
}

void InstallMovementDispatch() {
    auto entity = api.Class("Core.dll", "Core.Rendering.Entity.CoreAnimatedEntity", "AnimatedEntity");
    api.Native(api.Find(entity, "rfw", 5), 0x2616140);
    AddNativeHook(0x2616140, reinterpret_cast<void*>(MoveHook), reinterpret_cast<void**>(&originalMove));
}
