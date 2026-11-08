#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace il2cpp {

// Public exported API contracts only. Engine metadata/managed objects remain opaque.
// On ARM, a pointer-sized GC-handle slot also preserves older zero-extended uint32 handles.
#define TEMPLATE_IL2CPP_APIS(X) \
    X(void*, domain_get, ()) \
    X(const void**, domain_get_assemblies, (void*, std::size_t*)) \
    X(const void*, assembly_get_image, (const void*)) \
    X(const char*, image_get_name, (const void*)) \
    X(const void*, get_corlib, ()) \
    X(void*, thread_current, ()) \
    X(void*, thread_attach, (void*)) \
    X(void, thread_detach, (void*)) \
    X(void*, class_from_name, (const void*, const char*, const char*)) \
    X(void*, class_get_parent, (void*)) \
    X(void*, class_get_nested_types, (void*, void**)) \
    X(const char*, class_get_name, (void*)) \
    X(const void*, class_get_type, (void*)) \
    X(const void*, class_get_methods, (void*, void**)) \
    X(void*, class_get_field_from_name, (void*, const char*)) \
    X(bool, class_is_valuetype, (void*)) \
    X(bool, class_is_assignable_from, (void*, void*)) \
    X(void*, class_from_type, (const void*)) \
    X(const char*, method_get_name, (const void*)) \
    X(const void*, method_get_return_type, (const void*)) \
    X(std::uint32_t, method_get_param_count, (const void*)) \
    X(const void*, method_get_param, (const void*, std::uint32_t)) \
    X(std::uint32_t, method_get_flags, (const void*, std::uint32_t*)) \
    X(bool, method_is_generic, (const void*)) \
    X(bool, method_is_inflated, (const void*)) \
    X(void*, method_get_class, (const void*)) \
    X(const void*, field_get_type, (void*)) \
    X(int, field_get_flags, (void*)) \
    X(void*, field_get_parent, (void*)) \
    X(void, field_get_value, (void*, void*, void*)) \
    X(void*, field_get_value_object, (void*, void*)) \
    X(void, field_set_value, (void*, void*, void*)) \
    X(void, field_static_get_value, (void*, void*)) \
    X(void, field_static_set_value, (void*, void*)) \
    X(void, field_set_value_object, (void*, void*, void*)) \
    X(char*, type_get_name, (const void*)) \
    X(int, type_get_type, (const void*)) \
    X(bool, type_is_byref, (const void*)) \
    X(void*, type_get_object, (const void*)) \
    X(void, free, (void*)) \
    X(void*, object_get_class, (void*)) \
    X(void*, object_unbox, (void*)) \
    X(void*, string_new, (const char*)) \
    X(std::int32_t, string_length, (void*)) \
    X(const char16_t*, string_chars, (void*)) \
    X(void*, runtime_invoke, (const void*, void*, void**, void**)) \
    X(std::uintptr_t, gchandle_new, (void*, bool)) \
    X(void*, gchandle_get_target, (std::uintptr_t)) \
    X(void, gchandle_free, (std::uintptr_t))

struct Api {
#define TEMPLATE_IL2CPP_DECLARE(ret, name, args) ret (*name) args = nullptr;
    TEMPLATE_IL2CPP_APIS(TEMPLATE_IL2CPP_DECLARE)
#undef TEMPLATE_IL2CPP_DECLARE
};

template<class T> struct Result {
    T value{};
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

// A custom protected runtime can supply a reviewed API resolver without altering this layer.
// There is deliberately no private API-table decoder or guessed RVA fallback here.
struct Environment {
    void* context{};
    std::uintptr_t (*export_address)(void*, const char*){};
    bool (*executable)(void*, std::uintptr_t){};
    bool (*read_pointer)(void*, std::uintptr_t, std::uintptr_t*){};
};

struct State {
    Api api;
    Environment environment;
    // Low bit = initialized; upper bits = lifetime. One atomic prevents a delayed readiness
    // notification from resurrecting a runtime already invalidated by shutdown/unload.
    std::atomic<std::uint64_t> lifecycle{2};
};

struct Class { void* handle{}; std::shared_ptr<State> state; std::uint64_t generation{}; };
struct Method { const void* handle{}; Class owner; bool is_static{}; };
struct Field { void* handle{}; Class owner; const void* type{}; bool is_static{}; bool writable{}; };
struct Code { std::uintptr_t address{}; Method method; };
struct PointerLayout {
    std::vector<std::size_t> slots;
    std::shared_ptr<State> state;
    std::uint64_t generation{};
};

class ThreadScope {
public:
    explicit ThreadScope(std::shared_ptr<State> state);
    ~ThreadScope();
    ThreadScope(const ThreadScope&) = delete;
    ThreadScope& operator=(const ThreadScope&) = delete;
    explicit operator bool() const { return attached_; }
private:
    friend class Runtime;
    std::shared_ptr<State> state_;
    void* owned_{};
    std::uint64_t generation_{};
    bool attached_{};
};

// A root preserves memory, not a Unity object's scene/lifecycle validity.
class Root {
public:
    Root() = default;
    Root(std::shared_ptr<State> state, void* object);
    ~Root();
    Root(const Root&) = delete;
    Root& operator=(const Root&) = delete;
    Root(Root&& other) noexcept;
    Root& operator=(Root&& other) noexcept;
    void* get() const;
    bool retained() const { return handle_ != 0; }
    void reset();
private:
    friend class Runtime;
    std::shared_ptr<State> state_;
    std::uintptr_t handle_{};
    std::uint64_t generation_{};
};

struct Invocation {
    Root value;
    Root exception;
    std::string error;
    explicit operator bool() const { return error.empty() && !exception.retained(); }
};

template<class T> constexpr int primitive_kind() {
    if constexpr (std::is_same_v<T, bool>) return 0x02;
    else if constexpr (std::is_same_v<T, char16_t>) return 0x03;
    else if constexpr (std::is_same_v<T, std::int8_t>) return 0x04;
    else if constexpr (std::is_same_v<T, std::uint8_t>) return 0x05;
    else if constexpr (std::is_same_v<T, std::int16_t>) return 0x06;
    else if constexpr (std::is_same_v<T, std::uint16_t>) return 0x07;
    else if constexpr (std::is_same_v<T, std::int32_t>) return 0x08;
    else if constexpr (std::is_same_v<T, std::uint32_t>) return 0x09;
    else if constexpr (std::is_same_v<T, std::int64_t>) return 0x0a;
    else if constexpr (std::is_same_v<T, std::uint64_t>) return 0x0b;
    else if constexpr (std::is_same_v<T, float>) return 0x0c;
    else if constexpr (std::is_same_v<T, double>) return 0x0d;
    else return 0;
}

class Runtime {
public:
    Runtime() = default;
    ~Runtime() { invalidate(); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    // Bind resolves exports only. It MUST NOT call metadata APIs as a readiness probe.
    Result<bool> bind(Environment environment);
    // Called only after an observed successful managed invocation, or by an owned runtime
    // integration that has established initialization. Not inferred from library mapping.
    void confirm_initialized();
    void invalidate();
    bool ready() const;
    const Api& api() const { return state_->api; }
    ThreadScope attach() const { return ThreadScope(state_); }
    Root retain(void* object) const { return Root(state_, object); }

    Result<Class> find_class(const char* assembly, const char* namesp, const char* nested_name) const;
    Result<Method> find_method(Class owner, const char* name, const char* returns,
            std::initializer_list<const char*> parameters, bool is_static,
            bool include_bases = true) const;
    Result<Field> find_field(Class owner, const char* name, const char* type, bool is_static) const;
    Result<Class> class_of(const Root& object) const;
    Result<Root> string(const char* utf8) const;
    Result<std::u16string> string_value(const Root& string) const;
    // Invocations are ABI-independent. Values/byrefs point to storage; reference arguments
    // contain the OBJECT itself, not &object. The caller must obey the target's thread affinity.
    Invocation invoke(Method method, void* receiver, std::initializer_list<void*> arguments = {}) const;
    Result<bool> initialize_class(Class owner) const;
    Result<Root> read_object(Field field, void* receiver = nullptr) const;
    Result<bool> write_object(Field field, void* receiver, void* value) const;

    template<class T> Result<T> read(Field field, void* receiver = nullptr) const {
        static_assert(primitive_kind<T>() != 0, "Use read_object for structs, enums and references");
        T out{};
        auto result = field_value(field, receiver, &out, primitive_kind<T>(), false);
        return {out, result.error};
    }
    template<class T> Result<bool> write(Field field, void* receiver, T value) const {
        static_assert(primitive_kind<T>() != 0, "Use write_object for references");
        return field_value(field, receiver, &value, primitive_kind<T>(), true);
    }
    template<class T> Result<T> unbox(const Root& object) const {
        static_assert(primitive_kind<T>() != 0, "Only exact primitive unboxing is supported");
        T out{};
        auto result = unbox_value(object, &out, sizeof(out), primitive_kind<T>());
        return {out, result.error};
    }

    // Native hooking is separate from managed invocation: no assumed MethodInfo layout.
    // Calibrate once on the installation thread; retain the returned immutable offset set.
    Result<PointerLayout> calibrate_method_pointer() const;
    Result<Code> code_pointer(Method method, const PointerLayout& layout) const;
    static Result<bool> unique_hook_targets(const std::vector<Code>& methods);

private:
    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::uint64_t bound_lifecycle_{};
    bool valid(Class value) const;
    bool type_matches(const void* type, const char* expected) const;
    Result<bool> check_field(Field field, void* receiver, bool write) const;
    Result<bool> field_value(Field field, void* receiver, void* storage, int kind, bool write) const;
    Result<bool> unbox_value(const Root& object, void* storage, std::size_t size, int kind) const;
};

} // namespace il2cpp
