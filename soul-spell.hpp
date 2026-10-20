#pragma once
#include "runtime.hpp"
namespace soul {
void InstallSpell();
// Root the returned wrapper until Render has bound it to the native slot.
managed::Obj CaptureSpell(bool& enabled);
void CastCaptureSpell(managed::Obj displayedSpell);
}
