#pragma once
#include "core/contracts.hpp"
#include "core/runtime.hpp"
#include <string>

// Resolution of the game's methods by their managed declaration (class, name,
// arity, signature) instead of their pinned address. Today it only runs as a
// shadow check on the supported client: nothing the module hooks or calls
// depends on it. It measures, in the real game, whether a declaration alone
// finds the address the guards pin, before any later revision relies on it.
namespace compat {
enum class Outcome { ByName, BySignature, Missing, Ambiguous };
struct Resolution {
    managed::Method method = nullptr;
    Outcome outcome = Outcome::Missing;
};
// Return(Parameters) in run-time class names, as contracts.hpp writes it.
std::string Signature(managed::Method method);
// The one method of the declared class with this name and arity; several
// overloads are told apart by their signature. Never throws.
Resolution Resolve(const MethodContract& contract);
struct Shadow {
    unsigned total = 0, generic = 0, byName = 0, bySignature = 0, missing = 0, ambiguous = 0;
    unsigned wrongAddress = 0, signatureDiffers = 0;
    std::string samples; // the first differences, for module.log
};
Shadow ShadowCheck();
// ShadowCheck behind a structured-exception guard: false when it faulted.
bool TryShadowCheck(Shadow& result);
std::string Describe(const Shadow& shadow);
}
