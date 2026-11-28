#include "core/compat.hpp"
#include <cstring>
#include <vector>

namespace compat {
using namespace managed;
namespace {
const char* TypeName(const void* type) {
    const auto klass = type ? api.class_from_type(type) : nullptr;
    const auto name = klass ? api.class_get_name(klass) : nullptr;
    return name ? name : "?";
}
// "Outer+Inner" under its namespace; null when any part is absent.
Klass DeclaredClass(const MethodContract& contract) {
    try {
        std::string chain = contract.type;
        auto cut = chain.find('+');
        auto klass = api.Class(contract.image, contract.space, chain.substr(0, cut).c_str());
        while (cut != std::string::npos) {
            const auto next = chain.find('+', cut + 1);
            klass = api.NestedClass(klass, chain.substr(cut + 1, next == std::string::npos ? next : next - cut - 1).c_str());
            cut = next;
        }
        return klass;
    } catch (const std::exception&) { return nullptr; }
}
void Sample(Shadow& shadow, const MethodContract& contract, const std::string& what) {
    if (shadow.samples.size() > 900) return;
    shadow.samples += std::string(" [") + contract.type + "." + contract.name + "/" + std::to_string(contract.argc) + " " + what + "]";
}
}
std::string Signature(Method method) {
    std::string text = TypeName(api.method_get_return_type(method));
    text += '(';
    const auto count = api.method_get_param_count(method);
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        if (i) text += ',';
        text += TypeName(api.method_get_param(method, i));
    }
    return text + ')';
}
Resolution Resolve(const MethodContract& contract) {
    const auto klass = DeclaredClass(contract);
    if (!klass) return {};
    std::vector<Method> named;
    void* iterator = nullptr;
    for (unsigned count = 0; count < 16384; ++count) {
        const auto method = api.class_get_methods(klass, &iterator);
        if (!method) break;
        const auto name = api.method_get_name(method);
        if (name && !std::strcmp(name, contract.name) &&
            api.method_get_param_count(method) == static_cast<uint32_t>(contract.argc)) named.push_back(method);
    }
    if (named.empty()) return {};
    if (named.size() == 1) return {named.front(), Outcome::ByName};
    Method match = nullptr;
    for (auto method : named) {
        if (Signature(method) != contract.signature) continue;
        if (match) return {nullptr, Outcome::Ambiguous};
        match = method;
    }
    return match ? Resolution{match, Outcome::BySignature} : Resolution{nullptr, Outcome::Ambiguous};
}
Shadow ShadowCheck() {
    Shadow shadow;
    for (const auto& contract : MethodContracts) {
        ++shadow.total;
        // A generic definition has no code of its own: its instances do.
        if (contract.generic) { ++shadow.generic; continue; }
        const auto resolution = Resolve(contract);
        if (resolution.outcome == Outcome::Missing) { ++shadow.missing; Sample(shadow, contract, "missing"); continue; }
        if (resolution.outcome == Outcome::Ambiguous) { ++shadow.ambiguous; Sample(shadow, contract, "ambiguous"); continue; }
        ++(resolution.outcome == Outcome::ByName ? shadow.byName : shadow.bySignature);
        if (reinterpret_cast<uintptr_t>(*static_cast<void* const*>(resolution.method)) != gameBase + contract.rva) {
            ++shadow.wrongAddress; Sample(shadow, contract, "address"); continue;
        }
        const auto signature = Signature(resolution.method);
        if (signature != contract.signature) { ++shadow.signatureDiffers; Sample(shadow, contract, "signature " + signature); }
    }
    return shadow;
}
namespace {
// No C++ exception can leave this, so the structured guard below never unwinds one.
void Run(Shadow* result) noexcept {
    try { *result = ShadowCheck(); }
    catch (...) { result->samples += " [exception]"; }
}
}
bool TryShadowCheck(Shadow& result) {
    auto target = &result;
    __try { Run(target); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string Describe(const Shadow& shadow) {
    return "total=" + std::to_string(shadow.total) + " byName=" + std::to_string(shadow.byName) +
        " bySignature=" + std::to_string(shadow.bySignature) + " generic=" + std::to_string(shadow.generic) +
        " missing=" + std::to_string(shadow.missing) + " ambiguous=" + std::to_string(shadow.ambiguous) +
        " wrongAddress=" + std::to_string(shadow.wrongAddress) + " signatureDiffers=" + std::to_string(shadow.signatureDiffers) +
        shadow.samples;
}
}
