#pragma once
#include "runtime.hpp"

namespace soul {
// Native item slot plus a hover-only native item tooltip. Owns only its slot
// and tooltip; missing tooltip services never disable the reminder panel.
void InstallItemView();
managed::Obj CreateItemView(managed::Obj parent);
bool UpdateItemView(managed::Obj item);
void ReleaseItemView();
managed::Obj CreateSpellView(managed::Obj parent);
bool UpdateSpellView(managed::Obj spell, bool enabled);
void ReleaseSpellView();
void SetSpellClick(void (*click)(managed::Obj));
}
