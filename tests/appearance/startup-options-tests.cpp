#include "appearance/startup-options.hpp"
#include <iostream>
#include <string_view>

namespace {
struct Option { bool loaded = false, value = false; };
int valueReads = 0;
bool box;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
namespace managed {
Api api{};
Obj Api::Call(Obj source, const char* name, std::initializer_list<void*>) const {
    auto& option = *static_cast<Option*>(source);
    if (std::string_view(name) == "IsLoaded") box = option.loaded;
    else if (std::string_view(name) == "get_property") {
        ++valueReads;
        if (!option.loaded) throw std::runtime_error("OptionNotLoadedException");
        box = option.value;
    } else throw std::runtime_error("Unexpected option mutation or method");
    return &box;
}
}
int main() {
    using namespace managed;
    try {
        api.object_unbox = [](Obj object) -> void* { return object; };
        Option combat, roleplay, effects;
        InitialCreatureOptions state{true, true};
        for (int attempt = 0; attempt != 10; ++attempt)
            Check(!ReadInitialCreatureOptions(&combat, &roleplay, &effects, state),
                "Unbound account should wait, even after three discovery attempts");
        Check(valueReads == 0 && state.combat && state.roleplay, "Waiting touched unloaded values");
        combat = {true, false};
        Check(!ReadInitialCreatureOptions(&combat, &roleplay, &effects, state), "Partially loaded roleplay read");
        roleplay = {true, true};
        Check(!ReadInitialCreatureOptions(&combat, &roleplay, &effects, state), "Effects source must be ready too");
        Check(valueReads == 0, "Partial readiness must not read any saved value");
        effects.loaded = true;
        Check(ReadInitialCreatureOptions(&combat, &roleplay, &effects, state) &&
            !state.combat && state.roleplay && valueReads == 2, "Late binding did not preserve actual account preferences");
        Check(!ReadInitialCreatureOptions(nullptr, &roleplay, &effects, state), "Missing source must wait");
        combat = {false, true};
        Check(!ReadInitialCreatureOptions(&combat, &roleplay, &effects, state) &&
            valueReads == 2, "Unbound replacement source was read");
        combat.loaded = true;
        Check(ReadInitialCreatureOptions(&combat, &roleplay, &effects, state) && state.combat,
            "Retry after account binding failed");
        std::cout << "PASS: delayed/partial/missing account options wait without reads; retry preserves actual values\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
