#pragma once
#include "runtime.hpp"
namespace toolsUi {
// Registered once at installation; visibility is changed only by an explicit UI action.
void Register(const char* key, const char* title, int icon, bool (*visible)(), bool (*toggle)());
void AttachHeader(const char* key, managed::Obj window);
void DetachHeader(const char* key);
managed::Obj ScrollBody(managed::Obj window, managed::Obj root);
void Install();
void Tick();
void Restore();
bool CleanupPending();
}
