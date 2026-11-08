#pragma once
#include "il2cpp_runtime.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

// A deliberately different opaque metadata/object layout. Only the public API knows it.
// The same production Runtime is linked into host sanitizer and Android tests.
namespace fixture {
struct Klass; struct Method; struct Field; struct Object;
struct Type { std::string name; int kind; bool byref{}; Klass* klass{}; };
struct Image { std::string name; std::vector<Klass*> classes; };
struct Klass {
    std::string namesp, name;
    Type type;
    Klass* parent{};
    bool value{}, initialized{}, initializer_throws{};
    std::vector<Klass*> nested;
    std::vector<Method*> methods;
    std::vector<Field*> fields;
};
struct Object {
    Klass* klass{};
    alignas(std::max_align_t) std::array<std::byte, 32> data{};
    std::u16string text;
    std::map<Field*, std::array<std::byte, 16>> fields;
};
struct Method {
    std::array<std::uintptr_t, 16> opaque{};
    std::string name;
    Klass* owner{};
    Type* result{};
    std::vector<Type*> params;
    bool is_static{}, generic{}, inflated{}, abstract{};
    std::function<Object*(void*, void**, void**)> call;
};
struct Field {
    std::string name;
    Klass* owner{};
    Type* type{};
    int flags{};
    std::size_t width{};
    std::array<std::byte, 16> data{};
};

class Fake {
public:
    inline static Fake* current{};
    il2cpp::Api api;
    std::vector<std::unique_ptr<Klass>> classes;
    std::vector<std::unique_ptr<Method>> methods;
    std::vector<std::unique_ptr<Field>> fields;
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<Image> images{{"mscorlib.dll", {}}, {"Game.dll", {}}};
    std::vector<const void*> visible;
    std::map<std::uintptr_t, Object*> handles;
    std::uintptr_t next_handle{100};
    std::vector<std::uintptr_t> code{0x1000, 0x2000, 0x3000, 0x4000};
    std::vector<std::size_t> slots{3, 7};
    std::string missing_export;
    bool registered{true}, attached{}, fail_attach{}, fail_root{}, reject_memory{};
    int metadata_calls{}, attaches{}, detaches{}, freed{}, writes{}, names_allocated{}, names_freed{};
    Klass *object{}, *void_class{}, *int32{}, *int64{}, *single{}, *boolean{}, *intptr{}, *string{},
          *type_class{}, *runtime_type{}, *handle_class{}, *helpers{}, *module{}, *delegate{}, *player{};
    Object *error{}, *instance{}, *delegate1{}, *delegate2{};
    Field *count{}, *reference{};

    Fake() {
        current = this;
        object = klass(0, "System", "Object", 0x1c);
        void_class = klass(0, "System", "Void", 1, true);
        boolean = klass(0, "System", "Boolean", 2, true);
        int32 = klass(0, "System", "Int32", 8, true);
        int64 = klass(0, "System", "Int64", 10, true);
        single = klass(0, "System", "Single", 12, true);
        intptr = klass(0, "System", "IntPtr", 0x18, true);
        string = klass(0, "System", "String", 0x0e);
        type_class = klass(0, "System", "Type", 0x12);
        runtime_type = klass(0, "System", "RuntimeType", 0x12);
        runtime_type->parent = type_class;
        handle_class = klass(0, "System", "RuntimeTypeHandle", 0x11, true);
        helpers = klass(0, "System.Runtime.CompilerServices", "RuntimeHelpers", 0x12);
        module = klass(0, "System.Reflection", "Module", 0x12);
        delegate = klass(0, "System.Reflection", "TypeFilter", 0x12);
        player = klass(1, "Example", "Player", 0x12);
        instance = make(player);
        error = make(object);
        method(type_class, "get_TypeHandle", handle_class, {}, false, 0x3000)->call = [this](void* receiver, void**, void**) {
            auto* box = make(handle_class);
            box->data = static_cast<Object*>(receiver)->data;
            return box;
        };
        method(helpers, "RunClassConstructor", void_class, {&handle_class->type}, true, 0x4000)->call =
                [this](void*, void** args, void** exception) -> Object* {
            Klass* target = *static_cast<Klass**>(args[0]);
            if (target->initializer_throws) *exception = error;
            else target->initialized = true;
            return nullptr;
        };
        auto* a = method(module, "FilterA", boolean, {}, true, code[0]);
        auto* b = method(module, "FilterB", boolean, {}, true, code[1]);
        delegate1 = make(delegate); delegate2 = make(delegate);
        auto* ptr = field(delegate, "method_ptr", intptr, 0, sizeof(std::uintptr_t));
        auto* meta = field(delegate, "method", intptr, 0, sizeof(std::uintptr_t));
        set(delegate1, ptr, code[0]); set(delegate2, ptr, code[1]);
        set(delegate1, meta, reinterpret_cast<std::uintptr_t>(a->opaque.data()));
        set(delegate2, meta, reinterpret_cast<std::uintptr_t>(b->opaque.data()));
        auto* first = field(module, "FilterTypeName", delegate, 0x10 | 0x20, sizeof(void*));
        auto* second = field(module, "FilterTypeNameIgnoreCase", delegate, 0x10 | 0x20, sizeof(void*));
        std::memcpy(first->data.data(), &delegate1, sizeof(void*));
        std::memcpy(second->data.data(), &delegate2, sizeof(void*));
        count = field(player, "Count", int64, 0, 8);
        reference = field(player, "Reference", string, 0, sizeof(void*));
        set(instance, count, std::int64_t{9007199254740993LL});
        bind_api();
    }
    Klass* klass(int image, std::string ns, std::string name, int kind, bool value = false) {
        auto item = std::make_unique<Klass>();
        item->namesp = std::move(ns); item->name = std::move(name);
        item->type = {(item->namesp.empty() ? "" : item->namesp + ".") + item->name, kind, false, item.get()};
        item->value = value; item->parent = object;
        auto* result = item.get(); classes.push_back(std::move(item));
        images.at(image).classes.push_back(result);
        return result;
    }
    Object* make(Klass* klass) {
        auto item = std::make_unique<Object>(); item->klass = klass;
        auto* result = item.get(); objects.push_back(std::move(item)); return result;
    }
    template<class T> Object* box(Klass* klass, T value) {
        auto* result = make(klass); std::memcpy(result->data.data(), &value, sizeof(value)); return result;
    }
    Method* method(Klass* owner, std::string name, Klass* result, std::vector<Type*> params,
                   bool is_static, std::uintptr_t pc) {
        auto item = std::make_unique<Method>();
        item->name = std::move(name); item->owner = owner; item->result = &result->type;
        item->params = std::move(params); item->is_static = is_static;
        for (auto slot : slots) item->opaque[slot] = pc;
        auto* value = item.get(); methods.push_back(std::move(item)); owner->methods.push_back(value); return value;
    }
    Field* field(Klass* owner, std::string name, Klass* type, int flags, std::size_t width) {
        auto item = std::make_unique<Field>(); item->name = std::move(name); item->owner = owner;
        item->type = &type->type; item->flags = flags; item->width = width;
        auto* value = item.get(); fields.push_back(std::move(item)); owner->fields.push_back(value); return value;
    }
    template<class T> void set(Object* object, Field* field, T value) {
        std::memcpy(object->fields[field].data(), &value, sizeof(value));
    }
    Method* metadata(const void* value) {
        for (auto& method : methods) if (method->opaque.data() == value) return method.get();
        std::abort();
    }
    template<class T> static T* iterate(std::vector<T*>& values, void** iterator) {
        auto index = reinterpret_cast<std::uintptr_t>(*iterator);
        if (index >= values.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1); return values[index];
    }
    bool assignable(Klass* parent, Klass* child) {
        for (; child; child = child->parent) if (parent == child) return true;
        return false;
    }
    std::uintptr_t lookup(const char* name) {
        if (missing_export == name) return 0;
#define FIXTURE_LOOKUP(ret, member, args) if (std::strcmp(name, "il2cpp_" #member) == 0) return reinterpret_cast<std::uintptr_t>(api.member);
        TEMPLATE_IL2CPP_APIS(FIXTURE_LOOKUP)
#undef FIXTURE_LOOKUP
        return 0;
    }
    il2cpp::Environment environment() {
        return {this,
            [](void* data, const char* name) { return static_cast<Fake*>(data)->lookup(name); },
            [](void* data, std::uintptr_t address) {
                auto& self = *static_cast<Fake*>(data);
                if (!address) return false;
                if (std::find(self.code.begin(), self.code.end(), address) != self.code.end()) return true;
#define FIXTURE_EXEC(ret, name, args) if (address == reinterpret_cast<std::uintptr_t>(self.api.name)) return true;
                TEMPLATE_IL2CPP_APIS(FIXTURE_EXEC)
#undef FIXTURE_EXEC
                return false;
            },
            [](void* data, std::uintptr_t address, std::uintptr_t* out) {
                auto& self = *static_cast<Fake*>(data);
                if (self.reject_memory) return false;
                for (const auto& method : self.methods) {
                    auto begin = reinterpret_cast<std::uintptr_t>(method->opaque.data());
                    if (address >= begin && address - begin <= sizeof(method->opaque) - sizeof(*out)) {
                        std::memcpy(out, reinterpret_cast<void*>(address), sizeof(*out)); return true;
                    }
                }
                return false;
            }
        };
    }
    void bind_api() {
        api.domain_get = []() -> void* { ++current->metadata_calls; return current; };
        api.domain_get_assemblies = [](void*, std::size_t* count) -> const void** {
            auto& f = *current; f.visible.clear();
            for (std::size_t i = 0; i < f.images.size(); ++i) if (!i || f.registered) f.visible.push_back(&f.images[i]);
            *count = f.visible.size(); return f.visible.data();
        };
        api.assembly_get_image = [](const void* value) { return value; };
        api.image_get_name = [](const void* value) { return static_cast<const Image*>(value)->name.c_str(); };
        api.get_corlib = []() -> const void* { return &current->images[0]; };
        api.thread_current = []() -> void* { return current->attached ? current : nullptr; };
        api.thread_attach = [](void*) -> void* {
            if (current->fail_attach) return nullptr;
            ++current->attaches; current->attached = true; return current;
        };
        api.thread_detach = [](void*) { ++current->detaches; current->attached = false; };
        api.class_from_name = [](const void* image, const char* ns, const char* name) -> void* {
            for (auto* c : static_cast<const Image*>(image)->classes)
                if (c->namesp == ns && c->name == name) return c;
            return nullptr;
        };
        api.class_get_parent = [](void* value) -> void* { return static_cast<Klass*>(value)->parent; };
        api.class_get_nested_types = [](void* value, void** it) -> void* { return iterate(static_cast<Klass*>(value)->nested, it); };
        api.class_get_name = [](void* value) { return static_cast<Klass*>(value)->name.c_str(); };
        api.class_get_type = [](void* value) -> const void* { return &static_cast<Klass*>(value)->type; };
        api.class_get_methods = [](void* value, void** it) -> const void* {
            auto* result = iterate(static_cast<Klass*>(value)->methods, it);
            return result ? result->opaque.data() : nullptr;
        };
        api.class_get_field_from_name = [](void* value, const char* name) -> void* {
            for (auto* field : static_cast<Klass*>(value)->fields) if (field->name == name) return field;
            return nullptr;
        };
        api.class_is_valuetype = [](void* value) { return static_cast<Klass*>(value)->value; };
        api.class_is_assignable_from = [](void* parent, void* child) { return current->assignable(static_cast<Klass*>(parent), static_cast<Klass*>(child)); };
        api.class_from_type = [](const void* type) -> void* { return static_cast<const Type*>(type)->klass; };
        api.method_get_name = [](const void* value) { return current->metadata(value)->name.c_str(); };
        api.method_get_return_type = [](const void* value) -> const void* { return current->metadata(value)->result; };
        api.method_get_param_count = [](const void* value) { return static_cast<std::uint32_t>(current->metadata(value)->params.size()); };
        api.method_get_param = [](const void* value, std::uint32_t index) -> const void* { return current->metadata(value)->params.at(index); };
        api.method_get_flags = [](const void* value, std::uint32_t* implementation) -> std::uint32_t {
            assert(implementation); *implementation = 0;
            auto* method = current->metadata(value);
            return (method->is_static ? 0x10 : 0) | (method->abstract ? 0x400 : 0);
        };
        api.method_is_generic = [](const void* value) { return current->metadata(value)->generic; };
        api.method_is_inflated = [](const void* value) { return current->metadata(value)->inflated; };
        api.method_get_class = [](const void* value) -> void* { return current->metadata(value)->owner; };
        api.field_get_type = [](void* value) -> const void* { return static_cast<Field*>(value)->type; };
        api.field_get_flags = [](void* value) { return static_cast<Field*>(value)->flags; };
        api.field_get_parent = [](void* value) -> void* { return static_cast<Field*>(value)->owner; };
        api.field_get_value = [](void* receiver, void* handle, void* out) {
            auto* field = static_cast<Field*>(handle);
            std::memcpy(out, static_cast<Object*>(receiver)->fields[field].data(), field->width);
        };
        api.field_get_value_object = [](void* handle, void* receiver) -> void* {
            auto* field = static_cast<Field*>(handle);
            auto* bytes = receiver ? static_cast<Object*>(receiver)->fields[field].data() : field->data.data();
            if (!field->type->klass->value) { Object* out{}; std::memcpy(&out, bytes, sizeof(out)); return out; }
            auto* out = current->make(field->type->klass); std::memcpy(out->data.data(), bytes, field->width); return out;
        };
        api.field_set_value = [](void* receiver, void* handle, void* data) {
            ++current->writes; auto* field = static_cast<Field*>(handle);
            std::memcpy(static_cast<Object*>(receiver)->fields[field].data(), data, field->width);
        };
        api.field_static_get_value = [](void* handle, void* out) {
            auto* field = static_cast<Field*>(handle); assert(field->owner->initialized);
            std::memcpy(out, field->data.data(), field->width);
        };
        api.field_static_set_value = [](void* handle, void* data) {
            ++current->writes; auto* field = static_cast<Field*>(handle); assert(field->owner->initialized);
            std::memcpy(field->data.data(), data, field->width);
        };
        api.field_set_value_object = [](void* receiver, void* handle, void* value) {
            ++current->writes; current->set(static_cast<Object*>(receiver), static_cast<Field*>(handle), value);
        };
        api.type_get_name = [](const void* value) { ++current->names_allocated; return ::strdup(static_cast<const Type*>(value)->name.c_str()); };
        api.type_get_type = [](const void* value) { return static_cast<const Type*>(value)->kind; };
        api.type_is_byref = [](const void* value) { return static_cast<const Type*>(value)->byref; };
        api.type_get_object = [](const void* value) -> void* {
            return current->box(current->runtime_type, static_cast<const Type*>(value)->klass);
        };
        api.free = [](void* value) { ++current->names_freed; std::free(value); };
        api.object_get_class = [](void* value) -> void* { return static_cast<Object*>(value)->klass; };
        api.object_unbox = [](void* value) -> void* { return static_cast<Object*>(value)->data.data(); };
        api.string_new = [](const char* text) -> void* {
            auto* out = current->make(current->string);
            while (*text) out->text.push_back(static_cast<unsigned char>(*text++));
            return out;
        };
        api.string_length = [](void* value) { return static_cast<std::int32_t>(static_cast<Object*>(value)->text.size()); };
        api.string_chars = [](void* value) -> const char16_t* { return static_cast<Object*>(value)->text.data(); };
        api.runtime_invoke = [](const void* value, void* receiver, void** args, void** error) -> void* {
            auto* method = current->metadata(value); return method->call ? method->call(receiver, args, error) : nullptr;
        };
        api.gchandle_new = [](void* value, bool) -> std::uintptr_t {
            if (current->fail_root) return 0;
            auto handle = ++current->next_handle; current->handles[handle] = static_cast<Object*>(value); return handle;
        };
        api.gchandle_get_target = [](std::uintptr_t handle) -> void* { return current->handles.at(handle); };
        api.gchandle_free = [](std::uintptr_t handle) { assert(current->handles.erase(handle) == 1); ++current->freed; };
    }
};
} // namespace fixture
