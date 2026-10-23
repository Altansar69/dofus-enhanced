#pragma once
#include "core/runtime.hpp"
#include <string>
#include <vector>
namespace toolsUi {
// Optional layouts of a panel, listed as the game's radio rows in its ⋯ menu
// (the current one marked; a click on another one applies it), e.g. the
// notebook's full and compact versions.
struct Layout {std::string label,help;};
struct Layouts {std::vector<Layout> choices;int (*current)()=nullptr;bool (*choose)(int)=nullptr;};
// Registered once at installation; visibility is changed only by an explicit UI action.
void Register(const char* key, const char* title, int icon, bool (*visible)(), bool (*toggle)(), Layouts layouts = {});
bool ChooseLayout(const char* key, int layout);
struct Shortcut {std::string key,title;int icon=0;};
std::vector<Shortcut> Shortcuts();
bool Visible(const char* key);
bool Toggle(const char* key);
void AttachHeader(const char* key, managed::Obj window);
void DetachHeader(const char* key);
// A scrolling body inside a mod window. A resizable window keeps no size caps:
// the scroll view fills whatever height the user gives the window.
managed::Obj ScrollBody(managed::Obj window, managed::Obj root, bool resizable = false);
void Install();
void Tick();
void Restore();
bool CleanupPending();
}
