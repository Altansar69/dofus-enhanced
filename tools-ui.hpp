#pragma once
#include "runtime.hpp"
#include <vector>
namespace toolsUi {
// Registered once at installation; visibility is changed only by an explicit UI action.
void Register(const char* key, const char* title, int icon, bool (*visible)(), bool (*toggle)());
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
