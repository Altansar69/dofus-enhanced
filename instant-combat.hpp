#pragma once
#include "runtime.hpp"
#include <functional>

// Shared eligibility checks. These inspect only the active fight's own managed
// registries; an ID match without object identity is insufficient.
bool InstantCombatEnabled();
bool IsCurrentFightEntity(managed::Obj entity);

void InstallInstantMovement();
void InstallInstantPushMovement();
void InstallInstantThrowMovement();
void InstallInstantJumpMovement();
void InstallInstantMovementWaits();
void InstallMovementDispatch();
// Queue only within the known native rfw wrapper. Unknown callers keep their
// ordinary movement; no timer or frame delay is introduced.
bool DeferInstantMovement(std::function<void()> finish);
bool CanDeferInstantMovement();
void InstallInstantWaits();
void RememberSkippedAnimation(managed::Obj step);
void PumpInstantWaits(bool inCombat);
void ClearInstantWaits();
void InstallScriptWaits();
void PumpScriptWaits(bool inCombat);
void ClearScriptWaits();
void InstallCombatCleanup();
bool MustKeepNativeAnimation(managed::Obj step);
void PumpCombatCleanup(bool inCombat);
void ClearCombatCleanup();
