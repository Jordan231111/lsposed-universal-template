#include "il2cpp_fixture.h"
#include <iostream>
#include <limits>

using il2cpp::Runtime;
static void check(bool condition, const char* reason) {
    if (!condition) { std::cerr << reason << '\n'; std::exit(1); }
}
template<class T> static T require(il2cpp::Result<T> result) {
    if (!result) { std::cerr << result.error << '\n'; std::exit(1); }
    return std::move(result.value);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    fixture::Fake f;
    Runtime runtime;
    if (test == "missing_api") {
        f.missing_export = "il2cpp_runtime_invoke";
        check(!runtime.bind(f.environment()), "Missing API must fail binding");
        runtime.confirm_initialized();
        check(!runtime.ready() && !f.metadata_calls, "A failed bind cannot initialize or probe metadata");
        return 0;
    }
    require(runtime.bind(f.environment()));
    if (test == "readiness") {
        check(!runtime.find_class("Game", "Example", "Player"), "Mapping/export binding is not readiness");
        check(!f.metadata_calls, "No metadata calls before initialization evidence");
        runtime.invalidate();
        runtime.confirm_initialized();
        check(!runtime.ready(), "Delayed initialization evidence cannot resurrect an invalidated lifetime");
        return 0;
    }
    runtime.confirm_initialized();
    if (test == "late_assembly") {
        f.registered = false;
        check(!runtime.find_class("Game", "Example", "Player"), "Early class must be missing");
        f.registered = true;
        check(static_cast<bool>(runtime.find_class("Game.dll", "Example", "Player")), "Class miss must be retried after assembly registration");
        return 0;
    }
    auto player = require(runtime.find_class("Game", "Example", "Player"));
    if (test == "assembly_nested") {
        f.klass(0, "Example", "Player", 0x12);
        check(!runtime.find_class(nullptr, "Example", "Player"), "Cross-assembly ambiguity must be rejected");
        check(require(runtime.find_class("Game.dll", "Example", "Player")).handle == f.player, "Assembly identity must disambiguate");
        auto* nested = f.klass(1, "", "Nested", 0x12); f.player->nested.push_back(nested);
        check(require(runtime.find_class("Game", "Example", "Player/Nested")).handle == nested, "Nested type lookup");
        check(require(runtime.find_class("Game", "Example", "Player+Nested")).handle == nested, "Reflection nested spelling");
    } else if (test == "threads") {
        const int before = f.attaches;
        {
            auto outer = runtime.attach(); auto inner = runtime.attach();
            check(bool(outer) && bool(inner), "Nested thread scopes");
        }
        check(f.attaches == before + 1 && f.attaches == f.detaches, "Only owned attachments may detach");
        f.attached = true;
        { auto borrowed = runtime.attach(); check(bool(borrowed), "Borrow existing managed thread"); }
        check(f.attached && f.attaches == f.detaches, "Borrowed engine thread must remain attached");
        f.attached = false; f.fail_attach = true;
        check(!runtime.find_class("Game", "Example", "Player"), "Attachment failure must stop metadata access");
    } else if (test == "signatures") {
        auto* exact = f.method(f.player, "Value", f.int64, {&f.int64->type}, false, f.code[2]);
        f.method(f.player, "Value", f.int32, {&f.int32->type}, false, f.code[3]);
        auto result = require(runtime.find_method(player, "Value", "System.Int64", {"System.Int64"}, false));
        check(result.handle == exact->opaque.data(), "Complete overload signature must match");
        exact->abstract = true;
        check(!runtime.find_method(player, "Value", "System.Int64", {"System.Int64"}, false), "Abstract metadata is not a callable implementation");
        exact->abstract = false;
        check(!runtime.find_method(player, "Value", "System.Int64", {"System.Int64"}, true), "Static/instance mismatch");
        f.int64->type.kind = 0x0d;
        check(!runtime.find_method(player, "Value", "System.Int64", {"System.Int64"}, false), "Same type name with changed primitive ABI must fail");
        f.int64->type.kind = 0x0a;
        f.method(f.player, "Value", f.int64, {&f.int64->type}, false, f.code[2]);
        check(!runtime.find_method(player, "Value", "System.Int64", {"System.Int64"}, false), "Duplicate complete signatures must fail");
    } else if (test == "inheritance") {
        auto* base = f.klass(1, "Example", "Base", 0x12); f.player->parent = base;
        auto* inherited = f.method(base, "Value", f.int32, {}, false, f.code[2]);
        f.method(f.player, "Value", f.int64, {&f.int64->type}, false, f.code[3]);
        check(require(runtime.find_method(player, "Value", "System.Int32", {}, false)).handle == inherited->opaque.data(), "Unrelated overload cannot hide a base contract");
        check(!runtime.find_method(player, "Value", "System.Int32", {}, false, false), "Declared-only lookup");
    } else if (test == "primitive_fields") {
        auto field = require(runtime.find_field(player, "Count", "System.Int64", false));
        check(require(runtime.read<std::int64_t>(field, f.instance)) == 9007199254740993LL, "Int64 must preserve values above 2^53");
        check(!runtime.read<std::int32_t>(field, f.instance), "Width/type mismatch must reject before writing a buffer");
        require(runtime.write(field, f.instance, std::numeric_limits<std::int64_t>::max()));
        check(require(runtime.read<std::int64_t>(field, f.instance)) == std::numeric_limits<std::int64_t>::max(), "Exact Int64 write");
        check(!runtime.write(field, f.error, std::int64_t{0}), "Wrong receiver type");
        check(!runtime.find_field(player, "Count", "System.Int32", false), "Changed field type");
        check(f.writes == 1, "Rejected writes must have no effects");
    } else if (test == "reference_fields") {
        auto field = require(runtime.find_field(player, "Reference", "System.String", false));
        auto text = require(runtime.string("sample"));
        require(runtime.write_object(field, f.instance, text.get()));
        auto read = require(runtime.read_object(field, f.instance));
        check(require(runtime.string_value(read)) == u"sample", "Managed reference write barrier path");
        check(!runtime.write_object(field, f.instance, f.instance), "Incompatible reference type");
        require(runtime.write_object(field, f.instance, nullptr));
        check(!require(runtime.read_object(field, f.instance)).get(), "Null reference assignment");
        check(f.writes == 2, "Rejected references must not reach setter API");
    } else if (test == "static_initializer") {
        auto* raw = f.field(f.player, "StaticCount", f.int32, 0x10, 4);
        std::int32_t number = 42; std::memcpy(raw->data.data(), &number, 4);
        auto field = require(runtime.find_field(player, "StaticCount", "System.Int32", true));
        check(require(runtime.read<std::int32_t>(field)) == 42 && f.player->initialized, "Static read must initialize through caught managed invocation");
        f.player->initializer_throws = true;
        check(!runtime.read<std::int32_t>(field), "Type initializer exception must be reported");
        check(f.handles.empty(), "Initializer result and exception handles must be released");
    } else if (test == "invocation") {
        auto text = require(runtime.string("hello"));
        auto* target = f.method(f.player, "ReferenceLength", f.int32, {&f.string->type}, true, f.code[2]);
        target->call = [&](void*, void** args, void**) {
            check(args[0] == text.get(), "Reference arguments must contain object, not &object");
            return f.box(f.int32, std::int32_t{5});
        };
        auto method = require(runtime.find_method(player, "ReferenceLength", "System.Int32", {"System.String"}, true));
        auto result = runtime.invoke(method, nullptr, {text.get()});
        check(bool(result), "Managed reference invocation");
        check(require(runtime.unbox<std::int32_t>(result.value)) == 5, "Exact primitive unboxing");
        check(!runtime.unbox<std::int64_t>(result.value), "Reject wrong boxed primitive width");
        check(!runtime.invoke(method, nullptr), "Argument count mismatch");
        check(!runtime.invoke(method, f.instance, {text.get()}), "Static receiver mismatch");
        target->call = [&](void*, void**, void** exception) -> fixture::Object* { *exception = f.error; return nullptr; };
        auto thrown = runtime.invoke(method, nullptr, {text.get()});
        check(!thrown && thrown.exception.get() == f.error, "Managed exceptions remain inspectable rooted results");
    } else if (test == "roots") {
        {
            auto first = runtime.retain(f.instance); auto second = std::move(first);
            check(!first.retained() && second.get() == f.instance, "Root move ownership");
            auto third = runtime.retain(f.error); third = std::move(second);
            check(f.handles.size() == 1 && third.get() == f.instance, "Move assignment must free the previous root");
        }
        check(f.handles.empty() && f.freed == 2, "Roots release exactly once");
        f.fail_root = true;
        check(!runtime.string("failure"), "Root allocation failure must be reported");
    } else if (test == "lifetime") {
        auto root = runtime.retain(f.instance);
        auto field = require(runtime.find_field(player, "Count", "System.Int64", false));
        runtime.invalidate(); runtime.confirm_initialized();
        check(!runtime.ready() && !root.get(), "Expired roots and readiness evidence remain invalid");
        require(runtime.bind(f.environment())); runtime.confirm_initialized();
        check(!runtime.read<std::int64_t>(field, f.instance), "Old field cannot cross a rebind lifetime");
        root.reset(); check(!f.freed, "Do not call a retired runtime to free its old handles");
    } else if (test == "calibration" || test == "calibration_failures") {
        auto* target = f.method(f.player, "Value", f.int32, {}, false, f.code[2]);
        auto method = require(runtime.find_method(player, "Value", "System.Int32", {}, false));
        auto layout = require(runtime.calibrate_method_pointer());
        check(layout.slots == std::vector<std::size_t>({3 * sizeof(void*), 7 * sizeof(void*)}), "Discover moved MethodInfo slots");
        auto code = require(runtime.code_pointer(method, layout));
        check(code.address == f.code[2], "Calibrated native pointer");
        check(!Runtime::unique_hook_targets({code, code}), "Reject aliasing hook targets");
        if (test == "calibration_failures") {
            target->opaque[7] = f.code[3];
            check(!runtime.code_pointer(method, layout), "Disagreeing candidate slots must fail");
            target->opaque[7] = target->opaque[3];
            f.reject_memory = true;
            check(!runtime.code_pointer(method, layout), "Unreadable metadata must fail");
            check(!runtime.calibrate_method_pointer(), "Unreadable calibration must not guess a layout");
        } else {
            runtime.invalidate(); require(runtime.bind(f.environment())); runtime.confirm_initialized();
            auto fresh = require(runtime.find_class("Game", "Example", "Player"));
            auto new_method = require(runtime.find_method(fresh, "Value", "System.Int32", {}, false));
            check(!runtime.code_pointer(new_method, layout), "Calibration cannot cross runtime lifetimes");
        }
    } else if (test == "native_abi") {
        auto* target = f.method(f.player, "Value", f.int32, {}, false, f.code[2]);
        auto method = require(runtime.find_method(player, "Value", "System.Int32", {}, false));
        auto layout = require(runtime.calibrate_method_pointer());
        target->inflated = true;
        check(!runtime.code_pointer(method, layout), "Generic shared native code needs an adapter");
        target->inflated = false; f.player->value = true;
        check(!runtime.code_pointer(method, layout), "Value-type receiver needs an ABI adapter");
    } else return 2;
    check(f.names_allocated == f.names_freed, "All type-name strings must be freed");
    std::cout << test << ": passed\n";
}
