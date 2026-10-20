#pragma once
#include "runtime.hpp"
void InstallSpousePartyMenu();
void TickSpousePartyMenu();
void RestoreSpousePartyMenu();
bool HandleSpousePartyMenuCallback(managed::Obj callback, managed::Obj view);
bool ReadPartySpouse(managed::Obj member, std::string& distance, bool& online, const char** reason = nullptr);
bool TeleportPartySpouse(managed::Obj member);
