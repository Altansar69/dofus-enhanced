#include "core/compat.hpp"
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>

// Resolution by declaration against a stand-in runtime: classes, methods and
// types are plain test structures. No game file or process is read.
namespace {
struct TestMethod;
struct TestClass {
    const char* name;
    std::vector<TestMethod*> methods;
    std::vector<TestClass*> nested;
};
struct TestMethod {
    void* address; // first, as the runtime's method pointer
    const char* name;
    TestClass* returned;
    std::vector<TestClass*> params;
};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
TestClass voidType{"Void"}, int32Type{"Int32"}, int64Type{"Int64"}, listType{"List`1"}, readOnlyType{"IReadOnlyList`1"}, fmn{"fmn"};
unsigned char code[8]{};
TestMethod setKamas{code + 0, "SetKamas", &voidType, {&int64Type}};
TestMethod initList{code + 1, "Initialize", &voidType, {&listType, &fmn}};
TestMethod initReadOnly{code + 2, "Initialize", &voidType, {&readOnlyType, &fmn}};
TestMethod twinA{code + 3, "Twin", &int32Type, {&int32Type}}, twinB{code + 4, "Twin", &int32Type, {&int32Type}};
TestMethod run{code + 5, "Run", &voidType, {}};
TestClass state{"State", {&run}};
TestClass inventory{"Inventory", {&setKamas, &initList, &initReadOnly, &twinA, &twinB}, {&state}};
const void* assemblies[] = {&inventory};
MethodContract Contract(const char* type, const char* name, int argc, const char* signature) {
    return {0, "Core.dll", "Game", type, name, argc, signature, false};
}
void Install() {
    using namespace managed;
    api.domain_get = []() -> void* { return &inventory; };
    api.domain_get_assemblies = [](void*, size_t* count) -> const void** { *count = 1; return assemblies; };
    api.assembly_get_image = [](const void*) -> void* { return &inventory; };
    api.image_get_name = [](void*) -> const char* { return "Core.dll"; };
    api.class_from_name = [](void*, const char* space, const char* name) -> Klass {
        return !std::strcmp(space, "Game") && !std::strcmp(name, "Inventory") ? &inventory : nullptr;
    };
    api.class_get_name = [](Klass klass) { return static_cast<TestClass*>(klass)->name; };
    api.class_get_nested_types = [](Klass klass, void** iterator) -> Klass {
        auto& nested = static_cast<TestClass*>(klass)->nested;
        const auto index = reinterpret_cast<uintptr_t>(*iterator);
        if (index >= nested.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1);
        return nested[index];
    };
    api.class_get_methods = [](Klass klass, void** iterator) -> Method {
        auto& methods = static_cast<TestClass*>(klass)->methods;
        const auto index = reinterpret_cast<uintptr_t>(*iterator);
        if (index >= methods.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1);
        return methods[index];
    };
    api.method_get_name = [](Method method) { return static_cast<const TestMethod*>(method)->name; };
    api.method_get_param_count = [](Method method) { return static_cast<uint32_t>(static_cast<const TestMethod*>(method)->params.size()); };
    api.method_get_param = [](Method method, uint32_t index) -> const void* { return static_cast<const TestMethod*>(method)->params[index]; };
    api.method_get_return_type = [](Method method) -> const void* { return static_cast<const TestMethod*>(method)->returned; };
    api.class_from_type = [](const void* type) -> Klass { return const_cast<void*>(type); };
}
void TestResolution() {
    using compat::Outcome;
    Require(compat::Signature(&initList) == "Void(List`1,fmn)" && compat::Signature(&run) == "Void()",
            "A signature is Return(Parameters) in run-time class names");
    auto found = compat::Resolve(Contract("Inventory", "SetKamas", 1, "Void(Int64)"));
    Require(found.method == &setKamas && found.outcome == Outcome::ByName, "A single name and arity resolves by name");
    // By name alone the signature is not a condition: the caller compares it.
    found = compat::Resolve(Contract("Inventory", "SetKamas", 1, "Void(Int32)"));
    Require(found.method == &setKamas && found.outcome == Outcome::ByName, "A lone candidate is returned whatever its signature");
    found = compat::Resolve(Contract("Inventory", "Initialize", 2, "Void(IReadOnlyList`1,fmn)"));
    Require(found.method == &initReadOnly && found.outcome == Outcome::BySignature, "Overloads are told apart by signature");
    found = compat::Resolve(Contract("Inventory", "Initialize", 2, "Void(Dictionary`2,fmn)"));
    Require(!found.method && found.outcome == Outcome::Ambiguous, "Overloads without the declared signature stay unresolved");
    found = compat::Resolve(Contract("Inventory", "Twin", 1, "Int32(Int32)"));
    Require(!found.method && found.outcome == Outcome::Ambiguous, "Two methods of one signature stay unresolved");
    found = compat::Resolve(Contract("Inventory", "SetKamas", 2, "Void(Int64)"));
    Require(!found.method && found.outcome == Outcome::Missing, "Another arity is another method");
    found = compat::Resolve(Contract("Bank", "SetKamas", 1, "Void(Int64)"));
    Require(!found.method && found.outcome == Outcome::Missing, "An absent class resolves nothing");
    found = compat::Resolve(Contract("Inventory+State", "Run", 0, "Void()"));
    Require(found.method == &run && found.outcome == Outcome::ByName, "A nested type resolves under its declaring type");
    found = compat::Resolve(Contract("Inventory+Other", "Run", 0, "Void()"));
    Require(!found.method && found.outcome == Outcome::Missing, "An absent nested type resolves nothing");
}
void TestShadow() {
    // The generated table names the game's classes: none exists in this runtime.
    compat::Shadow shadow;
    Require(compat::TryShadowCheck(shadow), "The shadow check must complete on a runtime without the game's classes");
    Require(shadow.total == std::size(MethodContracts) && shadow.total > 500, "Every generated declaration is visited");
    Require(shadow.missing + shadow.generic == shadow.total && !shadow.byName && !shadow.bySignature && !shadow.wrongAddress,
            "Absent classes are counted as missing, never as resolved");
    Require(compat::Describe(shadow).find("total=" + std::to_string(shadow.total)) == 0 &&
            compat::Describe(shadow).size() < 1400, "The log line is bounded");
    // A fault inside the runtime is reported, not propagated to the game.
    const auto classes = managed::api.class_from_name;
    managed::api.class_from_name = nullptr;
    compat::Shadow faulted;
    Require(!compat::TryShadowCheck(faulted), "A fault in the runtime must be contained");
    managed::api.class_from_name = classes;
}
}

int main() {
    try {
        Install();
        TestResolution();
        TestShadow();
        std::cout << "PASS: methods resolve by declaration; the shadow check is bounded and contained.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
