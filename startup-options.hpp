#pragma once
#include "runtime.hpp"

struct InitialCreatureOptions { bool combat = false, roleplay = false; };
bool ReadInitialCreatureOptions(managed::Obj combat, managed::Obj roleplay,
    managed::Obj effects, InitialCreatureOptions& result);
