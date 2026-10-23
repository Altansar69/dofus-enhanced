#include "core/runtime.hpp"
#include "core/signatures.hpp"
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstring>

// A stand-in for the ABI observed in the saved Unity 6000.3 binary. This test
// executes Ref itself, but never loads a game DLL or opens a game process.
namespace {
uintptr_t nextHandle = 0x123456780020ULL;
std::unordered_map<uintptr_t, managed::Obj> handles;
bool invalidFree = false;
bool allocationFails = false;
uintptr_t __declspec(noinline) NewHandle(managed::Obj obj, bool) {
    if (allocationFails) return 0;
    auto handle = nextHandle; nextHandle += 8;
    handles.emplace(handle, obj);
    return handle;
}
managed::Obj __declspec(noinline) GetTarget(uintptr_t handle) {
    auto it = handles.find(handle);
    return it == handles.end() ? nullptr : it->second;
}
void __declspec(noinline) FreeHandle(uintptr_t handle) {
    if (!handles.erase(handle)) invalidFree = true;
}
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TestClass {
    const char* name;
    std::vector<TestClass*> children;
};
TestClass walkState{"xm", {}}, otherState{"xm", {}}, jumpState{"xt", {}}, pushState{"xw", {}};
TestClass walk{"xq", {&walkState}}, other{"other", {&otherState}};
TestClass jump{"xu", {&jumpState}}, push{"xy", {&pushState}}, thrown{"ye", {}};
TestClass intermediate{"intermediate", {&walkState}}, deep{"deep", {&intermediate}};
const void* assemblies[] = {&walk};
void* Domain() { return &walk; }
const void** Assemblies(void*, size_t* count) { *count = 1; return assemblies; }
void* Image(const void*) { return &walk; }
const char* ImageName(void*) { return "Core.dll"; }
managed::Klass TopLevel(void*, const char* space, const char* name) {
    if (*space) return nullptr;
    for (auto value : {&walk, &other, &jump, &push, &thrown, &deep})
        if (std::strcmp(value->name, name) == 0) return value;
    return nullptr;
}
const char* ClassName(managed::Klass value) { return static_cast<TestClass*>(value)->name; }
managed::Klass NextNested(managed::Klass value, void** iterator) {
    auto& children = static_cast<TestClass*>(value)->children;
    const auto index = reinterpret_cast<uintptr_t>(*iterator);
    if (index >= children.size()) return nullptr;
    *iterator = reinterpret_cast<void*>(index + 1);
    return children[index];
}
void TestNestedClasses() {
    using namespace managed;
    api.domain_get = Domain;
    api.domain_get_assemblies = Assemblies;
    api.assembly_get_image = Image;
    api.image_get_name = ImageName;
    api.class_from_name = TopLevel;
    api.class_get_name = ClassName;
    api.class_get_nested_types = NextNested;
    Require(api.NestedClass(api.Class("Core.dll", "", "xq"), "xm") == &walkState,
            "Private walk state must resolve under xq");
    Require(api.NestedClass(&other, "xm") == &otherState,
            "Equal nested names under different parents must not alias");
    Require(api.NestedClass(&jump, "xt") == &jumpState && api.NestedClass(&push, "xw") == &pushState,
            "Nested movement states must resolve under their declaring types");
    Require(api.Class("Core.dll", "", "ye") == &thrown,
            "The top-level throw state must keep ordinary class lookup");
    bool topLevelRejected = false, wrongParentRejected = false, descendantRejected = false;
    try { api.Class("Core.dll", "", "xm"); }
    catch (const std::runtime_error&) { topLevelRejected = true; }
    try { api.NestedClass(&jump, "xm"); }
    catch (const std::runtime_error& error) {
        wrongParentRejected = std::string(error.what()) == "Nested class unavailable: xu+xm";
    }
    try { api.NestedClass(&deep, "xm"); }
    catch (const std::runtime_error&) { descendantRejected = true; }
    Require(topLevelRejected && wrongParentRejected && descendantRejected,
            "Nested resolution must not change top-level semantics, cross parents or search descendants");
}
struct LookupMethod { void* address; const char* name; uint32_t argc; };
std::vector<LookupMethod> lookupMethods;
void TestVerifiedOverloads() {
    using namespace managed;
    auto bytes = Signatures[0].bytes;
    const uintptr_t rva = Signatures[0].rva;
    gameBase = reinterpret_cast<uintptr_t>(bytes.data()) - rva;
    lookupMethods = {{bytes.data() + 1, ".ctor", 1}, {bytes.data(), "Other", 1},
                     {bytes.data(), ".ctor", 2}, {bytes.data(), ".ctor", 1}};
    api.class_get_parent = [](Klass klass) -> Klass { return klass == &walk ? &other : nullptr; };
    api.class_get_methods = [](Klass klass, void** iterator) -> Method {
        if (klass != &other) return nullptr;
        const auto index = reinterpret_cast<uintptr_t>(*iterator);
        if (index >= lookupMethods.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1);
        return &lookupMethods[index];
    };
    api.method_get_name = [](Method method) { return static_cast<const LookupMethod*>(method)->name; };
    api.method_get_param_count = [](Method method) { return static_cast<const LookupMethod*>(method)->argc; };
    Require(api.FindVerified(&walk, ".ctor", 1, rva) == &lookupMethods.back(),
            "Verified lookup must select exact RVA/name/arity including inherited overloads");
    bytes[0] ^= 1;
    bool badSignature = false, absent = false, invalid = false;
    try { api.FindVerified(&walk, ".ctor", 1, rva); } catch (const std::exception&) { badSignature = true; }
    bytes[0] ^= 1;
    try { api.FindVerified(&walk, ".ctor", 0, rva); } catch (const std::exception&) { absent = true; }
    try { api.FindVerified(nullptr, ".ctor", 1, rva); } catch (const std::exception&) { invalid = true; }
    Require(badSignature && absent && invalid, "Unverified overloads must fail closed");
    gameBase = 0;
}
struct BindingBox { void* header[2]{}; void* entries[19]{}; } bindingBox;
int fieldOwner=0, referenceTarget=0, bindingField=0, referenceField=0, nullField=0;
unsigned boxedFieldReads=0;
void TestManagedFields() {
    using namespace managed;
    // WidgetManagerBinding is an inline 152-byte value inherited from gob<T>.
    // Simulate the unsafe copy as a checked failure, never corrupt the test stack.
    api.object_get_class=[](Obj owner)->Klass { Require(owner==&fieldOwner,"Unexpected field owner");return &walk; };
    api.class_get_parent=[](Klass k)->Klass {return k==&walk?&other:nullptr;};
    api.class_get_field_from_name=[](Klass k,const char* name)->void* {
        if(k!=&other)return nullptr;
        if(std::strcmp(name,"dzyi")==0)return &bindingField;
        if(std::strcmp(name,"reference")==0)return &referenceField;
        if(std::strcmp(name,"nullReference")==0)return &nullField;
        return nullptr;
    };
    api.field_get_value=[](Obj,void*,void*) {
        throw std::runtime_error("Unsafe field read: 152-byte inline WidgetManagerBinding cannot fit in an 8-byte object pointer");
    };
    api.field_get_value_object=[](void* field,Obj owner)->Obj {
        Require(owner==&fieldOwner,"Boxed field lookup must preserve the owner");++boxedFieldReads;
        if(field==&bindingField)return &bindingBox;
        if(field==&referenceField)return &referenceTarget;
        if(field==&nullField)return nullptr;
        throw std::runtime_error("Unexpected field metadata");
    };
    api.object_unbox=[](Obj box)->void* {Require(box==&bindingBox,"Expected complete binding box");return bindingBox.entries;};
    bindingBox.entries[9]=&referenceTarget;
    Ref binding(api.Field(&fieldOwner,"dzyi"));
    Require(binding.get()==&bindingBox && static_cast<void**>(api.object_unbox(binding.get()))[9]==&referenceTarget,
            "Inline binding must be boxed intact before its value receiver is unboxed");
    Require(api.Field(&fieldOwner,"reference")==&referenceTarget,"Reference field identity must not change");
    Require(api.Field(&fieldOwner,"nullReference")==nullptr&&api.Field(nullptr,"dzyi")==nullptr,"Null fields/owners remain null");
    bool missing=false;
    try {api.Field(&fieldOwner,"missing");} catch(const std::exception& e) {missing=std::string(e.what())=="Field unavailable: missing";}
    Require(missing&&boxedFieldReads==3,"Missing fields must fail without any copy or box");
}
}

int main() {
    using namespace managed;
    // Cast exactly as export binding does, so old 32-bit declarations reproduce
    // the truncation without dereferencing an invalid address in this test.
    api.gchandle_new = reinterpret_cast<decltype(api.gchandle_new)>(NewHandle);
    api.gchandle_get_target = reinterpret_cast<decltype(api.gchandle_get_target)>(GetTarget);
    api.gchandle_free = reinterpret_cast<decltype(api.gchandle_free)>(FreeHandle);
    try {
        TestNestedClasses();
        TestVerifiedOverloads();
        TestManagedFields();
        int firstObject = 1, secondObject = 2;
        {
            Ref first(&firstObject);
            Require(first.get() == &firstObject, "GCHandle was truncated: reference lookup lost upper 32 bits");
            allocationFails = true;
            bool allocationRefused = false;
            try { first.reset(&secondObject); } catch (const std::exception&) { allocationRefused = true; }
            allocationFails = false;
            Require(allocationRefused && first.get() == &firstObject, "Failed allocation must preserve the old managed reference");
            Ref second(std::move(first));
            Require(!first.get() && second.get() == &firstObject, "Move constructor must transfer the full handle");
            second.reset(&secondObject);
            Require(second.get() == &secondObject && handles.size() == 1, "Reset must release and replace a 64-bit handle");
            Ref third(&firstObject);
            third = std::move(second);
            Require(!second.get() && third.get() == &secondObject && handles.size() == 1, "Move assignment must preserve 64-bit handle ownership");
            third = std::move(third);
            Require(third.get() == &secondObject, "Self-move must preserve ownership");
        }
        Require(handles.empty() && !invalidFree, "Destruction must release the exact handle once");
        std::cout << "PASS: explicit nested class resolution and high-address GC handle ownership.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
