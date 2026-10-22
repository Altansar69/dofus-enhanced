#pragma once
#include "common.hpp"
#include <initializer_list>
#include <stdexcept>
#include <cstdint>

namespace managed {
using Obj = void*;
using Klass = void*;
using Method = const void*;
// Unity 6000.3 uses pointer-sized GC handles, not the uint32_t IDs of older APIs.
using GCHandle = uintptr_t;
static_assert(sizeof(GCHandle) == 8, "This module targets Windows x64 only");
struct Api {
    void* (*domain_get)();
    void* (*thread_attach)(void*);
    void (*thread_detach)(void*);
    const void** (*domain_get_assemblies)(void*, size_t*);
    void* (*assembly_get_image)(const void*);
    const char* (*image_get_name)(void*);
    Klass (*class_from_name)(void*, const char*, const char*);
    Klass (*class_get_parent)(Klass);
    const char* (*class_get_name)(Klass);
    Klass (*class_get_nested_types)(Klass, void**);
    Method (*class_get_method_from_name)(Klass, const char*, int);
    Method (*class_get_methods)(Klass, void**);
    const void* (*class_get_type)(Klass);
    Klass (*class_from_type)(const void*);
    bool (*class_is_assignable_from)(Klass, Klass);
    bool (*class_is_valuetype)(Klass);
    const void* (*method_get_param)(Method, uint32_t);
    const char* (*method_get_name)(Method);
    uint32_t (*method_get_param_count)(Method);
    const void* (*method_get_return_type)(Method);
    Obj (*type_get_object)(const void*);
    Klass (*object_get_class)(Obj);
    Obj (*object_new)(Klass);
    void* (*object_unbox)(Obj);
    Obj (*runtime_invoke)(Method, Obj, void**, Obj*);
    Obj (*string_new)(const char*);
    int32_t (*string_length)(Obj);
    const wchar_t* (*string_chars)(Obj);
    GCHandle (*gchandle_new)(Obj, bool);
    Obj (*gchandle_get_target)(GCHandle);
    void (*gchandle_free)(GCHandle);
    void (*format_exception)(Obj, char*, int);
    void* (*class_get_field_from_name)(Klass, const char*);
    size_t (*field_get_offset)(void*);
    void (*field_get_value)(Obj, void*, void*);
    Obj (*field_get_value_object)(void*, Obj);
    void (*field_static_get_value)(void*, void*);
    void (*gc_wbarrier_set_field)(Obj, void**, Obj);
    void Load(HMODULE game);
    void WaitForInitialization() const;
    Klass Class(const char* image, const char* space, const char* name) const;
    Klass NestedClass(Klass declaringClass, const char* name) const;
    Method Find(Klass klass, const char* name, int argc) const;
    Method FindVerified(Klass klass, const char* name, int argc, uintptr_t expectedRva) const;
    Obj Invoke(Method method, Obj self, std::initializer_list<void*> args = {}) const;
    Obj Call(Obj self, const char* name, std::initializer_list<void*> args = {}) const;
    Obj New(Klass klass, std::initializer_list<void*> args = {}) const;
    Obj Field(Obj self, const char* name) const;
    // Field metadata of a class or one of its parents, resolved once.
    void* FieldInfo(Klass klass, const char* name) const;
    Obj StaticField(Klass klass, const char* name) const;
    void* FieldAddress(Obj self, const char* name) const;
    std::string String(Obj self) const;
    template<class T> T Value(Obj box) const {
        if (!box) throw std::runtime_error("Expected a boxed value");
        return *static_cast<T*>(object_unbox(box));
    }
    void* Native(Method method, uintptr_t expectedRva) const;
    void* NativeAt(uintptr_t expectedRva) const;
    void SelfTest() const;
};
extern Api api;
extern uintptr_t gameBase;
struct Ref {
    GCHandle handle = 0;
    Ref() = default;
    explicit Ref(Obj obj) { reset(obj); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : handle(other.handle) { other.handle = 0; }
    Ref& operator=(Ref&& other) noexcept { if (this != &other) { reset(); handle = other.handle; other.handle = 0; } return *this; }
    ~Ref() { reset(); }
    Obj get() const { return handle ? api.gchandle_get_target(handle) : nullptr; }
    void reset(Obj obj = nullptr) {
        auto next = obj ? api.gchandle_new(obj, false) : 0;
        if (obj && !next) throw std::runtime_error("Cannot create managed GC reference");
        if (handle) api.gchandle_free(handle);
        handle = next;
    }
};
template<class T> T Read(Obj obj, size_t offset) {
    if (!obj) throw std::runtime_error("Null managed field owner");
    return *reinterpret_cast<T*>(static_cast<char*>(obj) + offset);
}
}

void Log(const std::string& text);
