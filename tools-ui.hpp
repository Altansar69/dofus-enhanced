#pragma once
#include "runtime.hpp"
#include <vector>
namespace toolsUi {
// Registered once at installation; visibility is changed only by an explicit UI action.
// extraLabel/extraToggle: an optional extra row in the panel's ⋯ menu (label
// read when the menu opens, action when clicked), e.g. the notebook's layout.
void Register(const char* key, const char* title, int icon, bool (*visible)(), bool (*toggle)(), const char* (*extraLabel)() = nullptr, bool (*extraToggle)() = nullptr);
bool ToggleExtra(const char* key);
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
