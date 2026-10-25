#include "ui/game-theme.hpp"
#include "core/module.hpp"

// As the game's own reads (eqr.yvl, OptionGeneralUi.CreateThemeCategory):
// the options service's DofusOptionsContainer, its currentTheme (a
// SharedOptionJson<Int32>), its value (OptionJson.get_property, guarded).
int ReadGameTheme() {
    using namespace managed;
    try {
        if (!module || !module->cached.get()) return 0;
        Ref container(api.Call(module->cached.get(), "get_dofusOptionsContainer"));
        if (!container.get()) return 0;
        Ref option(api.Field(container.get(), "currentTheme"));
        if (!option.get()) return 0;
        const int theme = api.Value<int>(api.Call(option.get(), "get_property"));
        return gameTheme::Valid(theme) ? theme : 0;
    } catch (...) {
        return 0;
    }
}
