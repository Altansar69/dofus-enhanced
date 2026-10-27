#pragma once
#include "core/runtime.hpp"
// « Reconnexion après une déconnexion » (settings.reloginGuard, 1.20.2).
// After an inactivity disconnection, « Jouer » asks the Ankama launcher for
// the account's details (eog.RequestUserInfos). When the launcher answers with
// an error, the game builds its reply (cvg) without details (cvg.dhzn null:
// JsonUtility.FromJson of no text) and its own reply handler (pw.phz) reads
// them without a check: a NullReferenceException stops the login before it
// asks the launcher for its connection token, and the loading never ends.
// The guard gives that reply an empty details object (cvf, every field null),
// the case the handler already handles: it logs its own warning and goes on
// with the token request, as the game does when details are incomplete. No
// account data is made up, read or logged; a reply with details is untouched.
void InstallReloginGuard(); // worker thread, before the hooks are enabled; never throws
namespace reloginGuard {
// The reply as the handler is about to read it: true when the guard filled
// its missing details (exposed for the offline tests).
bool Repair(managed::Obj reply);
}
