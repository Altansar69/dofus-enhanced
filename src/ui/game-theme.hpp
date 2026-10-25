#pragma once
// The game's interface themes (1.18.1): « Couleur du curseur » offers its
// « Défaut » (the theme chosen in the game's options, Thème) and each theme
// on its own. Core.Services.Contracts.UI.ThemeEnum (pinned snapshot): None 0,
// then the eleven themes 1..11, which the game's selector lists in the order
// of Themes (OptionGeneralUi.CreateThemeCategory). The game names them through
// its translations (ui.option.theme.*), which the snapshot does not hold: the
// names are the enum's, in French.
#include <cstdint>

namespace gameTheme {
inline constexpr int Standard = 1, Count = 11;
struct Theme { int id; const char* name; };
inline constexpr Theme Themes[Count] = {{1, "Standard"}, {3, "Bonta"}, {4, "Brakmar"}, {2, "Tribute"}, {5, "Gold and steel"},
    {7, "Belladone"}, {8, "Unicorn"}, {6, "Emerald mine"}, {9, "Sufokia"}, {10, "Pandala"}, {11, "Wabbit"}};
inline constexpr bool Valid(int id) { return id >= 1 && id <= Count; }
// A theme's name, null for an unknown id.
inline constexpr const char* Name(int id) {
    for (const auto& theme : Themes)
        if (theme.id == id) return theme.name;
    return nullptr;
}
}

// The theme chosen in the game's options (DofusOptionsContainer.currentTheme,
// a shared option, readable before the login), 1..11; 0 when unknown (no
// options yet, a value outside 1..11, any error). Main thread; never throws.
int ReadGameTheme();
