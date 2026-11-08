#include "il2cpp_runtime.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace il2cpp {
namespace {
constexpr std::uint32_t Static = 0x10, Readonly = 0x20, Literal = 0x40, Abstract = 0x400;
template<class T> Result<T> failure(const std::string& message) { return {T{}, message}; }
std::string image_key(const char* value) {
    std::string name = value ? value : "";
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".dll") name.resize(name.size() - 4);
    return name;
}
bool alive(const std::shared_ptr<State>& state, std::uint64_t generation) {
    return state && (generation & 1) && state->lifecycle.load(std::memory_order_acquire) == generation;
}
bool reference_type(const Api& api, const void* type) {
    if (!type || api.type_is_byref(type)) return false;
    const int kind = api.type_get_type(type);
    if (kind != 0x0e && kind != 0x12 && kind != 0x14 && kind != 0x15 && kind != 0x1c && kind != 0x1d)
        return false;
    void* klass = api.class_from_type(type);
    return klass && !api.class_is_valuetype(klass);
}
bool native_argument(const Api& api, const void* type) {
    if (!type) return false;
    if (api.type_is_byref(type)) return true;
    const int kind = api.type_get_type(type);
    return (kind >= 0x01 && kind <= 0x0d) || kind == 0x18 || kind == 0x19 || reference_type(api, type);
}
} // namespace

ThreadScope::ThreadScope(std::shared_ptr<State> state) : state_(std::move(state)) {
    if (!state_) return;
    generation_ = state_->lifecycle.load(std::memory_order_acquire);
    if (!alive(state_, generation_)) return;
    if (state_->api.thread_current()) { attached_ = true; return; }
    void* domain = state_->api.domain_get();
    if (domain) owned_ = state_->api.thread_attach(domain);
    attached_ = owned_ != nullptr;
}
ThreadScope::~ThreadScope() {
    if (owned_ && alive(state_, generation_)) state_->api.thread_detach(owned_);
}

Root::Root(std::shared_ptr<State> state, void* object) : state_(std::move(state)) {
    if (!state_ || !object) return;
    generation_ = state_->lifecycle.load(std::memory_order_acquire);
    ThreadScope thread(state_);
    if (thread) handle_ = state_->api.gchandle_new(object, false);
}
Root::~Root() { reset(); }
Root::Root(Root&& other) noexcept { *this = std::move(other); }
Root& Root::operator=(Root&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        handle_ = std::exchange(other.handle_, 0);
        generation_ = other.generation_;
    }
    return *this;
}
void* Root::get() const {
    if (!handle_ || !alive(state_, generation_)) return nullptr;
    ThreadScope thread(state_);
    return thread ? state_->api.gchandle_get_target(handle_) : nullptr;
}
void Root::reset() {
    if (handle_ && alive(state_, generation_)) {
        ThreadScope thread(state_);
        if (thread) state_->api.gchandle_free(handle_);
    }
    handle_ = 0;
    state_.reset();
}

Result<bool> Runtime::bind(Environment environment) {
    if (ready()) return failure<bool>("Invalidate the previous runtime before rebinding");
    if (!environment.export_address || !environment.executable)
        return failure<bool>("Export resolver and executable-address validator are required");
    Api candidate;
#define TEMPLATE_IL2CPP_BIND(ret, name, args) do { \
    auto address = environment.export_address(environment.context, "il2cpp_" #name); \
    if (!address || !environment.executable(environment.context, address)) \
        return failure<bool>("Missing or non-executable API: il2cpp_" #name); \
    candidate.name = reinterpret_cast<decltype(candidate.name)>(address); \
} while (false);
    TEMPLATE_IL2CPP_APIS(TEMPLATE_IL2CPP_BIND)
#undef TEMPLATE_IL2CPP_BIND
    invalidate();
    bound_lifecycle_ = state_->lifecycle.load(std::memory_order_acquire);
    state_->api = candidate;
    state_->environment = environment;
    return {true, {}};
}
void Runtime::confirm_initialized() {
    auto expected = bound_lifecycle_;
    if (state_->api.runtime_invoke && expected)
        state_->lifecycle.compare_exchange_strong(expected, bound_lifecycle_ | 1,
                std::memory_order_release, std::memory_order_relaxed);
}
void Runtime::invalidate() {
    auto current = state_->lifecycle.load(std::memory_order_acquire);
    while (!state_->lifecycle.compare_exchange_weak(current, (current | 1) + 1,
            std::memory_order_acq_rel, std::memory_order_acquire)) {}
}
bool Runtime::ready() const { return (state_->lifecycle.load(std::memory_order_acquire) & 1) != 0; }
bool Runtime::valid(Class value) const {
    return value.handle && value.state == state_ && alive(state_, value.generation);
}
bool Runtime::type_matches(const void* type, const char* expected) const {
    if (!type || !expected) return false;
    char* actual = api().type_get_name(type);
    const bool equal = actual && std::strcmp(actual, expected) == 0;
    if (actual) api().free(actual);
    if (!equal) return false;
    const std::size_t length = std::strlen(expected);
    const bool byref = length != 0 && expected[length - 1] == '&';
    if (api().type_is_byref(type) != byref) return false;
    const std::string base(expected, length - (byref ? 1 : 0));
    const char* primitives[] = {"System.Void", "System.Boolean", "System.Char", "System.SByte",
        "System.Byte", "System.Int16", "System.UInt16", "System.Int32", "System.UInt32",
        "System.Int64", "System.UInt64", "System.Single", "System.Double"};
    for (int i = 0; i < 13; ++i)
        if (base == primitives[i]) return api().type_get_type(type) == i + 1;
    if (base == "System.IntPtr") return api().type_get_type(type) == 0x18;
    if (base == "System.UIntPtr") return api().type_get_type(type) == 0x19;
    return true;
}

Result<Class> Runtime::find_class(const char* assembly, const char* namesp, const char* nested_name) const {
    auto thread = attach();
    if (!thread) return failure<Class>("Runtime initialization has not been observed, or thread attach failed");
    if (!nested_name || !*nested_name) return failure<Class>("Class name is empty");
    std::vector<std::string> path;
    std::string name(nested_name);
    for (std::size_t begin = 0; begin <= name.size();) {
        auto end = name.find_first_of("/+", begin);
        auto part = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty()) return failure<Class>("Empty nested class name");
        path.push_back(std::move(part));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::size_t count = 0;
    const void** assemblies = api().domain_get_assemblies(api().domain_get(), &count);
    if ((!assemblies && count) || count > 100000) return failure<Class>("Invalid assembly enumeration");
    void* found = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const void* image = assemblies[i] ? api().assembly_get_image(assemblies[i]) : nullptr;
        if (!image || (assembly && image_key(api().image_get_name(image)) != image_key(assembly))) continue;
        void* current = api().class_from_name(image, namesp ? namesp : "", path.front().c_str());
        for (std::size_t part = 1; current && part < path.size(); ++part) {
            void* iterator = nullptr;
            void* nested = nullptr;
            while (void* candidate = api().class_get_nested_types(current, &iterator)) {
                const char* candidate_name = api().class_get_name(candidate);
                if (!candidate_name || path[part] != candidate_name) continue;
                if (nested && nested != candidate) return failure<Class>("Ambiguous nested class");
                nested = candidate;
            }
            current = nested;
        }
        if (!current) continue;
        if (found && found != current) return failure<Class>("Ambiguous class; specify its assembly");
        found = current;
    }
    // A miss is never cached: assemblies/classes may become available later.
    if (!found) return failure<Class>("Class not currently available: " + name);
    return {{found, state_, state_->lifecycle.load()}, {}};
}

Result<Method> Runtime::find_method(Class owner, const char* name, const char* returns,
        std::initializer_list<const char*> parameters, bool is_static, bool include_bases) const {
    auto thread = attach();
    if (!thread || !valid(owner)) return failure<Method>("Invalid or expired declaring class");
    if (!name || !returns) return failure<Method>("Method name and exact return type are required");
    void* current = owner.handle;
    for (int depth = 0; current && depth < 128; ++depth) {
        void* iterator = nullptr;
        const void* found = nullptr;
        while (const void* method = api().class_get_methods(current, &iterator)) {
            const char* actual = api().method_get_name(method);
            std::uint32_t implementation_flags = 0;
            if (!actual || std::strcmp(actual, name) ||
                bool(api().method_get_flags(method, &implementation_flags) & Static) != is_static ||
                api().method_get_param_count(method) != parameters.size() ||
                !type_matches(api().method_get_return_type(method), returns)) continue;
            std::uint32_t index = 0;
            bool matches = true;
            for (const char* parameter : parameters)
                if (!type_matches(api().method_get_param(method, index++), parameter)) { matches = false; break; }
            if (!matches) continue;
            if (found && found != method) return failure<Method>("Ambiguous complete method signature");
            found = method;
        }
        if (found) {
            std::uint32_t implementation_flags = 0;
            if (api().method_get_flags(found, &implementation_flags) & Abstract)
                return failure<Method>("Abstract method requires a concrete implementation");
            if (api().method_is_generic(found) && !api().method_is_inflated(found))
                return failure<Method>("Open generic method requires a constructed reflection method");
            return {{found, {api().method_get_class(found), state_, owner.generation}, is_static}, {}};
        }
        if (!include_bases) break;
        current = api().class_get_parent(current);
    }
    return failure<Method>(std::string("Method signature not available: ") + name);
}

Result<Field> Runtime::find_field(Class owner, const char* name, const char* type, bool is_static) const {
    auto thread = attach();
    if (!thread || !valid(owner)) return failure<Field>("Invalid or expired field owner");
    if (!name || !type) return failure<Field>("Field name and exact type are required");
    void* current = owner.handle;
    for (int depth = 0; current && depth < 128; ++depth, current = api().class_get_parent(current)) {
        void* field = api().class_get_field_from_name(current, name);
        if (!field) continue;
        const int flags = api().field_get_flags(field);
        if (bool(flags & Static) != is_static || !type_matches(api().field_get_type(field), type))
            return failure<Field>("Field signature changed");
        return {{field, {api().field_get_parent(field), state_, owner.generation},
                 api().field_get_type(field), is_static, !(flags & (Readonly | Literal))}, {}};
    }
    return failure<Field>(std::string("Field not available: ") + name);
}

Invocation Runtime::invoke(Method method, void* receiver, std::initializer_list<void*> arguments) const {
    auto thread = attach();
    if (!thread || !method.handle || !valid(method.owner)) return {{}, {}, "Invalid or expired method"};
    if (api().method_get_param_count(method.handle) != arguments.size())
        return {{}, {}, "Managed invocation argument count mismatch"};
    void* target = receiver;
    if (method.is_static) {
        if (receiver) return {{}, {}, "Static method must have a null receiver"};
    } else {
        if (!receiver || !api().class_is_assignable_from(method.owner.handle, api().object_get_class(receiver)))
            return {{}, {}, "Receiver is not an instance of the declaring class"};
        if (api().class_is_valuetype(method.owner.handle)) target = api().object_unbox(receiver);
    }
    void* exception = nullptr;
    void* result = api().runtime_invoke(method.handle, target,
            arguments.size() ? const_cast<void**>(arguments.begin()) : nullptr, &exception);
    Invocation out{retain(result), retain(exception), {}};
    if ((result && !out.value.retained()) || (exception && !out.exception.retained()))
        out.error = "Could not retain managed invocation result";
    return out;
}

Result<bool> Runtime::initialize_class(Class owner) const {
    auto thread = attach();
    if (!thread || !valid(owner)) return failure<bool>("Invalid or expired class initializer target");
    // Invoke RuntimeHelpers so type-initializer exceptions return through runtime_invoke's
    // exception out-parameter. Calling il2cpp_runtime_class_init directly can unwind into us.
    Root reflection_type = retain(api().type_get_object(api().class_get_type(owner.handle)));
    void* type = reflection_type.get();
    if (!type) return failure<bool>("Reflection type unavailable");
    auto get_handle = find_method({api().object_get_class(type), state_, owner.generation},
            "get_TypeHandle", "System.RuntimeTypeHandle", {}, false);
    if (!get_handle) return failure<bool>(get_handle.error);
    auto handle = invoke(get_handle.value, type);
    if (!handle || !handle.value.get()) return failure<bool>("Could not obtain RuntimeTypeHandle");
    const void* corlib = api().get_corlib();
    void* helpers = corlib ? api().class_from_name(corlib, "System.Runtime.CompilerServices", "RuntimeHelpers") : nullptr;
    auto run = find_method({helpers, state_, owner.generation}, "RunClassConstructor", "System.Void",
            {"System.RuntimeTypeHandle"}, true);
    if (!run) return failure<bool>(run.error);
    void* data = api().object_unbox(handle.value.get());
    if (!data) return failure<bool>("RuntimeTypeHandle is not boxed");
    auto result = invoke(run.value, nullptr, {data});
    if (!result) return failure<bool>("Managed type initializer failed");
    return {true, {}};
}

Result<bool> Runtime::check_field(Field field, void* receiver, bool write) const {
    if (!field.handle || !valid(field.owner)) return failure<bool>("Invalid or expired field");
    if (write && !field.writable) return failure<bool>("Readonly/literal field cannot be written");
    if (field.is_static) {
        if (receiver) return failure<bool>("Static field must have a null receiver");
        return initialize_class(field.owner);
    }
    if (!receiver || !api().class_is_assignable_from(field.owner.handle, api().object_get_class(receiver)))
        return failure<bool>("Field receiver has the wrong declaring type");
    if (api().class_is_valuetype(field.owner.handle))
        return failure<bool>("Use a managed accessor for fields of boxed value types");
    return {true, {}};
}

Result<bool> Runtime::field_value(Field field, void* receiver, void* storage, int kind, bool write) const {
    auto thread = attach();
    if (!thread) return failure<bool>("Runtime/thread is not ready");
    auto checked = check_field(field, receiver, write);
    if (!checked) return checked;
    if (!storage || !field.type || api().type_is_byref(field.type) || api().type_get_type(field.type) != kind)
        return failure<bool>("Primitive field ABI mismatch");
    if (field.is_static) {
        if (write) api().field_static_set_value(field.handle, storage);
        else api().field_static_get_value(field.handle, storage);
    } else {
        if (write) api().field_set_value(receiver, field.handle, storage);
        else api().field_get_value(receiver, field.handle, storage);
    }
    return {true, {}};
}

Result<Root> Runtime::read_object(Field field, void* receiver) const {
    auto thread = attach();
    if (!thread) return failure<Root>("Runtime/thread is not ready");
    auto checked = check_field(field, receiver, false);
    if (!checked) return failure<Root>(checked.error);
    void* object = api().field_get_value_object(field.handle, receiver);
    Root root = retain(object);
    if (object && !root.retained()) return failure<Root>("Could not retain field value");
    return {std::move(root), {}};
}

Result<bool> Runtime::write_object(Field field, void* receiver, void* value) const {
    auto thread = attach();
    if (!thread) return failure<bool>("Runtime/thread is not ready");
    auto checked = check_field(field, receiver, true);
    if (!checked) return checked;
    if (field.is_static) return failure<bool>("Use a managed setter for static reference fields");
    if (!reference_type(api(), field.type)) return failure<bool>("Field is not a managed reference");
    void* klass = api().class_from_type(field.type);
    if (value && !api().class_is_assignable_from(klass, api().object_get_class(value)))
        return failure<bool>("Reference assignment type mismatch");
    // This API performs the runtime's write barrier; never memcpy a managed reference.
    api().field_set_value_object(receiver, field.handle, value);
    return {true, {}};
}

Result<Class> Runtime::class_of(const Root& object) const {
    auto thread = attach();
    if (!thread || object.state_ != state_ || !object.get()) return failure<Class>("Object belongs to no active runtime");
    return {{api().object_get_class(object.get()), state_, state_->lifecycle.load()}, {}};
}

Result<Root> Runtime::string(const char* utf8) const {
    auto thread = attach();
    if (!thread || !utf8) return failure<Root>("Runtime/thread or UTF-8 input unavailable");
    Root result = retain(api().string_new(utf8));
    if (!result.get()) return failure<Root>("Managed string allocation failed");
    return {std::move(result), {}};
}

Result<std::u16string> Runtime::string_value(const Root& string) const {
    auto thread = attach();
    auto klass = class_of(string);
    if (!thread || !klass || !type_matches(api().class_get_type(klass.value.handle), "System.String"))
        return failure<std::u16string>("Object is not a managed String");
    const auto length = api().string_length(string.get());
    const auto* chars = api().string_chars(string.get());
    if (length < 0 || length > 16 * 1024 * 1024 || (length && !chars))
        return failure<std::u16string>("Managed String bounds are invalid");
    return {length ? std::u16string(chars, static_cast<std::size_t>(length)) : std::u16string{}, {}};
}

Result<bool> Runtime::unbox_value(const Root& object, void* storage, std::size_t size, int kind) const {
    auto thread = attach();
    void* value = object.get();
    if (!thread || object.state_ != state_ || !value || !storage) return failure<bool>("No boxed value available");
    void* klass = api().object_get_class(value);
    const void* type = klass ? api().class_get_type(klass) : nullptr;
    if (!type || !api().class_is_valuetype(klass) || api().type_get_type(type) != kind)
        return failure<bool>("Boxed primitive ABI mismatch");
    void* data = api().object_unbox(value);
    if (!data) return failure<bool>("Value is not boxed");
    std::memcpy(storage, data, size);
    return {true, {}};
}

Result<PointerLayout> Runtime::calibrate_method_pointer() const {
    using Slots = std::vector<std::size_t>;
    auto thread = attach();
    const auto& env = state_->environment;
    if (!thread || !env.read_pointer) return failure<PointerLayout>("Native pointer calibration unavailable");
    const void* corlib = api().get_corlib();
    Class module{corlib ? api().class_from_name(corlib, "System.Reflection", "Module") : nullptr,
                 state_, state_->lifecycle.load()};
    auto initialized = initialize_class(module);
    if (!initialized) return failure<PointerLayout>(initialized.error);
    Slots slots;
    std::uintptr_t previous_code = 0;
    for (const char* name : {"FilterTypeName", "FilterTypeNameIgnoreCase"}) {
        auto field = find_field(module, name, "System.Reflection.TypeFilter", true);
        if (!field) return failure<PointerLayout>(field.error);
        void* delegate = nullptr;
        api().field_static_get_value(field.value.handle, &delegate);
        Root root = retain(delegate);
        if (!root.get()) return failure<PointerLayout>("Reflection calibration delegate unavailable");
        Class owner{api().object_get_class(root.get()), state_, module.generation};
        auto code_field = find_field(owner, "method_ptr", "System.IntPtr", false);
        auto metadata_field = find_field(owner, "method", "System.IntPtr", false);
        if (!code_field || !metadata_field) return failure<PointerLayout>("Reflection delegate fields changed");
        std::uintptr_t code = 0, metadata = 0;
        api().field_get_value(root.get(), code_field.value.handle, &code);
        api().field_get_value(root.get(), metadata_field.value.handle, &metadata);
        if (!code || code == previous_code || !env.executable(env.context, code))
            return failure<PointerLayout>("Calibration requires independent executable delegate targets");
        Slots matches;
        // A bounded readable search, not a copied MethodInfo layout or version-specific offset.
        for (std::size_t offset = 0; offset < 128; offset += sizeof(std::uintptr_t)) {
            std::uintptr_t value = 0;
            if (metadata > UINTPTR_MAX - offset || !env.read_pointer(env.context, metadata + offset, &value)) break;
            if (value == code) matches.push_back(offset);
        }
        if (!previous_code) slots = std::move(matches);
        else slots.erase(std::remove_if(slots.begin(), slots.end(), [&](std::size_t offset) {
            return std::find(matches.begin(), matches.end(), offset) == matches.end();
        }), slots.end());
        if (slots.empty()) return failure<PointerLayout>("No agreed MethodInfo code-pointer layout");
        previous_code = code;
    }
    return {{std::move(slots), state_, module.generation}, {}};
}

Result<Code> Runtime::code_pointer(Method method, const PointerLayout& layout) const {
    auto thread = attach();
    const auto& env = state_->environment;
    if (!thread || !valid(method.owner) || !method.handle || !env.read_pointer || layout.slots.empty() ||
        layout.state != state_ || !alive(state_, layout.generation))
        return failure<Code>("Native address resolution is not ready");
    if (api().class_is_valuetype(method.owner.handle) || api().method_is_generic(method.handle) ||
        api().method_is_inflated(method.handle))
        return failure<Code>("Native hooks on value-type owners or generic sharing require an explicit ABI adapter");
    if (!native_argument(api(), api().method_get_return_type(method.handle)))
        return failure<Code>("Struct native returns require an ABI adapter; use managed invoke");
    for (std::uint32_t i = 0; i < api().method_get_param_count(method.handle); ++i)
        if (!native_argument(api(), api().method_get_param(method.handle, i)))
            return failure<Code>("Struct native arguments require an ABI adapter; use managed invoke");
    std::uintptr_t code = 0;
    for (std::size_t slot : layout.slots) {
        const auto metadata = reinterpret_cast<std::uintptr_t>(method.handle);
        std::uintptr_t candidate = 0;
        if (slot >= 128 || slot % sizeof(std::uintptr_t) || metadata > UINTPTR_MAX - slot ||
            !env.read_pointer(env.context, metadata + slot, &candidate) ||
            !env.executable(env.context, candidate) || (code && candidate != code))
            return failure<Code>("Unreadable, non-executable or conflicting method-pointer slots");
        code = candidate;
    }
    return {{code, method}, {}};
}
Result<bool> Runtime::unique_hook_targets(const std::vector<Code>& methods) {
    std::unordered_set<std::uintptr_t> addresses;
    for (const Code& method : methods)
        if (!method.address || !method.method.handle || !alive(method.method.owner.state, method.method.owner.generation) ||
            !addresses.insert(method.address).second)
            return failure<bool>("Selected native hooks alias the same code address");
    return {true, {}};
}

} // namespace il2cpp
