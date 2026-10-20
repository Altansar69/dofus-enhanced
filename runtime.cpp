#include "runtime.hpp"
#include "signatures.hpp"
#include <array>
#include <cstring>

namespace managed {
Api api{};
uintptr_t gameBase = 0;
void Api::Load(HMODULE game) {
    gameBase = reinterpret_cast<uintptr_t>(game);
#define LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(game, "il2cpp_" #name)); if (!name) throw std::runtime_error("Missing IL2CPP export: " #name)
    LOAD(domain_get); LOAD(thread_attach); LOAD(thread_detach); LOAD(domain_get_assemblies);
    LOAD(assembly_get_image); LOAD(image_get_name); LOAD(class_from_name); LOAD(class_get_parent);
    LOAD(class_get_name); LOAD(class_get_method_from_name); LOAD(class_get_type); LOAD(class_from_type);
    LOAD(class_get_methods); LOAD(method_get_name); LOAD(method_get_param_count);
    LOAD(class_get_nested_types);
    LOAD(class_is_assignable_from); LOAD(class_is_valuetype); LOAD(method_get_param); LOAD(method_get_return_type); LOAD(type_get_object);
    LOAD(object_get_class); LOAD(object_new); LOAD(object_unbox); LOAD(runtime_invoke); LOAD(string_new);
    LOAD(string_length); LOAD(string_chars); LOAD(gchandle_new); LOAD(gchandle_get_target); LOAD(gchandle_free);
    LOAD(format_exception); LOAD(class_get_field_from_name); LOAD(field_get_offset); LOAD(field_get_value);
    LOAD(field_get_value_object);
    LOAD(field_static_get_value);
    LOAD(gc_wbarrier_set_field);
#undef LOAD
}
Klass Api::Class(const char* imageName, const char* space, const char* name) const {
    size_t count = 0;
    auto list = domain_get_assemblies(domain_get(), &count);
    if (!list || count > 4096) throw std::runtime_error("Invalid assembly list");
    for (size_t i = 0; i < count; ++i) {
        auto image = assembly_get_image(list[i]);
        if (std::string(image_get_name(image)) == imageName) {
            auto klass = class_from_name(image, space, name);
            if (klass) return klass;
            break;
        }
    }
    throw std::runtime_error(std::string("Class unavailable: ") + imageName + ":" + space + "." + name);
}
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    if (!declaringClass || !name || !*name) throw std::runtime_error("Invalid nested class lookup");
    void* iterator = nullptr;
    for (size_t count = 0; count < 4096; ++count) {
        const auto nested = class_get_nested_types(declaringClass, &iterator);
        if (!nested) {
            throw std::runtime_error(std::string("Nested class unavailable: ") +
                                     class_get_name(declaringClass) + "+" + name);
        }
        const auto nestedName = class_get_name(nested);
        if (nestedName && std::strcmp(nestedName, name) == 0) return nested;
    }
    throw std::runtime_error("Nested class enumeration exceeds supported limit");
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    for (auto current = klass; current; current = class_get_parent(current)) {
        auto method = class_get_method_from_name(current, name, argc);
        if (method) return method;
    }
    throw std::runtime_error(std::string("Method unavailable: ") + class_get_name(klass) + "." + name);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t expectedRva) const {
    if (!klass || !name || !*name || argc < 0) throw std::runtime_error("Invalid verified method lookup");
    unsigned parents = 0;
    for (auto current = klass; current; current = class_get_parent(current)) {
        if (++parents > 64) throw std::runtime_error("Invalid method inheritance depth");
        void* iterator = nullptr;
        bool ended = false;
        for (unsigned count = 0; count < 16384; ++count) {
            auto method = class_get_methods(current, &iterator);
            if (!method) { ended = true; break; }
            const char* candidate = method_get_name(method);
            if (!candidate || std::strcmp(candidate, name) || method_get_param_count(method) != static_cast<uint32_t>(argc)) continue;
            if (reinterpret_cast<uintptr_t>(*static_cast<void* const*>(method)) != gameBase + expectedRva) continue;
            Native(method, expectedRva);
            return method;
        }
        if (!ended) throw std::runtime_error("Method enumeration exceeds supported limit");
    }
    throw std::runtime_error(std::string("Verified method unavailable: ") + class_get_name(klass) + "." + name);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    if (!method) throw std::runtime_error("Null method");
    Obj exception = nullptr;
    auto result = runtime_invoke(method, self, const_cast<void**>(args.begin()), &exception);
    if (exception) {
        std::array<char, 2048> message{};
        format_exception(exception, message.data(), static_cast<int>(message.size()));
        throw std::runtime_error(message.data());
    }
    return result;
}
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    if (!self) throw std::runtime_error(std::string("Null receiver: ") + name);
    auto klass = object_get_class(self);
    return Invoke(Find(klass, name, static_cast<int>(args.size())), class_is_valuetype(klass) ? object_unbox(self) : self, args);
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    Ref obj(object_new(klass));
    if (!obj.get()) throw std::runtime_error("Object allocation failed");
    Invoke(Find(klass, ".ctor", static_cast<int>(args.size())), class_is_valuetype(klass) ? object_unbox(obj.get()) : obj.get(), args);
    return obj.get();
}
Obj Api::Field(Obj self, const char* name) const {
    if (!self) return nullptr;
    for (auto c = object_get_class(self); c; c = class_get_parent(c)) {
        if (auto field = class_get_field_from_name(c, name)) {
            // Native boxing handles both references and inline value types.
            // field_get_value copies the entire field: an Obj-sized destination
            // corrupts the stack for structs such as WidgetManagerBinding.
            return field_get_value_object(field, self);
        }
    }
    throw std::runtime_error(std::string("Field unavailable: ") + name);
}
Obj Api::StaticField(Klass klass, const char* name) const {
    auto field = class_get_field_from_name(klass, name);
    if (!field) throw std::runtime_error(std::string("Static field unavailable: ") + name);
    Obj value = nullptr;
    field_static_get_value(field, &value);
    return value;
}
void* Api::FieldAddress(Obj self, const char* name) const {
    if (!self) throw std::runtime_error("Null field owner");
    for (auto c = object_get_class(self); c; c = class_get_parent(c)) {
        if (auto field = class_get_field_from_name(c, name))
            return static_cast<char*>(self) + field_get_offset(field);
    }
    throw std::runtime_error(std::string("Field unavailable: ") + name);
}
std::string Api::String(Obj self) const {
    if (!self) return {};
    int count = string_length(self);
    if (count < 0 || count > 16384) return {};
    auto chars = string_chars(self);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, chars, count, nullptr, 0, nullptr, nullptr);
    std::string result(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, count, result.data(), bytes, nullptr, nullptr);
    return result;
}
void* Api::Native(Method method, uintptr_t expectedRva) const {
    auto pointer = *static_cast<void* const*>(method);
    if (reinterpret_cast<uintptr_t>(pointer) != gameBase + expectedRva)
        throw std::runtime_error("Native method address does not match supported snapshot");
    return NativeAt(expectedRva);
}
void* Api::NativeAt(uintptr_t expectedRva) const {
    auto pointer = reinterpret_cast<void*>(gameBase + expectedRva);
    bool checked = false;
    for (const auto& signature : Signatures) {
        if (signature.rva != expectedRva) continue;
        if (memcmp(pointer, signature.bytes.data(), signature.bytes.size()) != 0)
            throw std::runtime_error("Loaded method bytes differ from snapshot, or another module already hooks this method");
        checked = true;
        break;
    }
    if (!checked) throw std::runtime_error("Missing native signature guard");
    return pointer;
}
void Api::WaitForInitialization() const {
    // Exact-version Runtime::Init counter and final lock release, verified in the
    // saved binary. domain_get is a lazy allocator and cannot serve as a probe.
    NativeAt(0x59D540);
    NativeAt(0x59E63E);
    const auto count = reinterpret_cast<volatile const LONG*>(gameBase + 0x71678C0);
    const auto lock = reinterpret_cast<volatile const LONG*>(gameBase + 0x7167870);
    const auto deadline = GetTickCount64() + 180000;
    while (*count <= 0 || *lock != 0) {
        if (GetTickCount64() >= deadline) throw std::runtime_error("IL2CPP initialization did not complete within three minutes");
        Sleep(100);
    }
}
void Api::SelfTest() const {
    auto klass = Class("Core.dll", "", "exf");
    auto reflected = type_get_object(class_get_type(klass));
    if (!reflected) throw std::runtime_error("Reflection type is unavailable");
    Ref reference(reflected);
    if (reference.get() != reflected) throw std::runtime_error("GC handle roundtrip failed; no hooks enabled");
    Ref moved(std::move(reference));
    if (reference.get() || moved.get() != reflected) throw std::runtime_error("GC handle ownership check failed");
    Ref name(Call(moved.get(), "get_Name"));
    if (String(name.get()) != "exf") throw std::runtime_error("Managed reflection invocation failed; no hooks enabled");
    // Exercise the native inline-value field API before installing any hooks.
    // Bounds/Vector3 are private math values here, never game objects or UI.
    struct Vector3 { float x, y, z; };
    Vector3 center{1,2,3}, size{4,6,8};
    const auto vectorClass = Class("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
    Ref bounds(New(Class("UnityEngine.CoreModule.dll", "UnityEngine", "Bounds"), {&center,&size}));
    Ref boxedCenter(Field(bounds.get(), "m_Center")), boxedExtents(Field(bounds.get(), "m_Extents"));
    if (!boxedCenter.get() || !boxedExtents.get() || object_get_class(boxedCenter.get()) != vectorClass ||
        object_get_class(boxedExtents.get()) != vectorClass)
        throw std::runtime_error("Managed struct field boxing failed; no hooks enabled");
    const auto c = Value<Vector3>(boxedCenter.get()), e = Value<Vector3>(boxedExtents.get());
    if (c.x != 1 || c.y != 2 || c.z != 3 || e.x != 2 || e.y != 3 || e.z != 4)
        throw std::runtime_error("Managed struct field contents invalid; no hooks enabled");
}
}
