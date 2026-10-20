#include "startup-options.hpp"

bool ReadInitialCreatureOptions(managed::Obj combat, managed::Obj roleplay,
    managed::Obj effects, InitialCreatureOptions& result) {
    using namespace managed;
    // Account-bound option objects exist before their values can be read.
    // Wait for all sources used by startup, without writing or caching defaults.
    for (auto option : {combat, roleplay, effects})
        if (!option || !api.Value<bool>(api.Call(option, "IsLoaded"))) return false;
    const bool originalCombat = api.Value<bool>(api.Call(combat, "get_property"));
    const bool originalRoleplay = api.Value<bool>(api.Call(roleplay, "get_property"));
    result = {originalCombat, originalRoleplay};
    return true;
}
