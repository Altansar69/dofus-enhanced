#pragma once
#include "core/runtime.hpp"
void InstallSoulGroupMenu();
void TickSoulGroupMenu();
void RestoreSoulGroupMenu();
bool HandleSoulGroupMenuCallback(managed::Obj callback, managed::Obj view);
