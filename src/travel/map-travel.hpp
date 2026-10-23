#pragma once
#include "core/runtime.hpp"
void InstallMapTravel();
void TickMapTravel();
void RestoreMapTravel();
bool HandleMapTravelCallback(managed::Obj callback, managed::Obj view);
