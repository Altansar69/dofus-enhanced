#pragma once
#include "core/runtime.hpp"
#include "appearance/mode-policy.hpp"
#include <functional>

// Shared eligibility checks. These inspect only the active fight's own managed
// registries; an ID match without object identity is insufficient.
// « Déplacements instantanés » (walk, push, throw, jump) and « Sans attentes
// visuelles » (skipped-animation waiters, visual stagger, script pauses, the
// waits before a movement or an appearance change): each its own toggle, in
// combat only, while the module runs.
bool InstantMovementEnabled();
bool InstantWaitsEnabled();
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
// The recorded provenance of a gpy step: Death for the dwp.uia call site,
// Ordinary for any other recorded constructor, Unknown when none was recorded.
AnimationStepKind ClassifyAnimationStep(managed::Obj step);
void PumpCombatCleanup(bool inCombat);
void ClearCombatCleanup();
