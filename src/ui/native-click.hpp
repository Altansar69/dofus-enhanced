#pragma once
#include "core/runtime.hpp"

// Click detection for the game's own DofusButtonCustom components (toolbar
// buttons, window close buttons, panel actions). Their ExtendedClickable stops
// pointer propagation, so a wrapper element never sees the click: the hook on
// ExtendedClickable.ProcessUpEvent observes the native click decision (active
// manipulator, pointer still over the target) and forwards the clicked button
// to the registered handlers after the game's own handling ran. No managed
// delegate is created; buttons that no handler owns are left untouched.
namespace nativeClick {
void Install();
// Handlers return true for a button they own. Registered once at installation.
void AddHandler(bool (*handler)(managed::Obj button));
}
