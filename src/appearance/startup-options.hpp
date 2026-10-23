#pragma once
#include "core/runtime.hpp"

struct InitialCreatureOptions { bool combat = false, roleplay = false; };
bool ReadInitialCreatureOptions(managed::Obj combat, managed::Obj roleplay,
    managed::Obj effects, InitialCreatureOptions& result);
