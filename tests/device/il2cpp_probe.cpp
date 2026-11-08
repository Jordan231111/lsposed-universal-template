// External read-only QA library. Never linked into the module APK.
#include "il2cpp_runtime.h"
#include "native_utils.h"
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <limits>
#include <stdexcept>

namespace {
template<class T> T require(il2cpp::Result<T> result) {
    if (!result) throw std::runtime_error(result.error);
    return std::move(result.value);
}
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) >= 32) out += c;
    }
    return out + '"';
}

struct ProbeEnvironment {
    const std::uintptr_t* addresses;
    std::size_t count;
    std::vector<native_utils::MemoryRange> ranges = native_utils::process_ranges();
    static std::uintptr_t lookup(void* context, const char* requested) {
        auto& self = *static_cast<ProbeEnvironment*>(context);
        static const char* names[] = {
#define PROBE_NAME(ret, name, args) "il2cpp_" #name,
            TEMPLATE_IL2CPP_APIS(PROBE_NAME)
#undef PROBE_NAME
        };
        if (self.count != std::size(names)) return 0;
        for (std::size_t i = 0; i < self.count; ++i) if (std::strcmp(names[i], requested) == 0) return self.addresses[i];
        return 0;
    }
    static bool executable(void* context, std::uintptr_t address) {
        auto& self = *static_cast<ProbeEnvironment*>(context);
#if defined(__arm__)
        address &= ~std::uintptr_t{1};
#endif
        return std::any_of(self.ranges.begin(), self.ranges.end(), [&](const auto& range) {
            return range.readable && range.executable && range.contains(address, 4);
        });
    }
    static bool read(void* context, std::uintptr_t address, std::uintptr_t* out) {
        auto& self = *static_cast<ProbeEnvironment*>(context);
        auto contains = [&] { return std::any_of(self.ranges.begin(), self.ranges.end(), [&](const auto& range) {
            return range.readable && range.contains(address, sizeof(*out));
        }); };
        if (!contains()) self.ranges = native_utils::process_ranges();
        if (!contains()) return false;
        std::memcpy(out, reinterpret_cast<void*>(address), sizeof(*out));
        return true;
    }
};
void stage(const char* name) { __android_log_print(ANDROID_LOG_INFO, "TemplateAudit", "%s", name); }
}

extern "C" __attribute__((visibility("default")))
const char* template_runtime_audit(
        const std::uintptr_t* addresses, std::size_t count, bool initialized) {
    static std::string report;
    try {
        il2cpp::Runtime runtime;
        ProbeEnvironment environment{addresses, count};
        stage("bind");
        require(runtime.bind({&environment, ProbeEnvironment::lookup, ProbeEnvironment::executable, ProbeEnvironment::read}));
        if (!initialized) {
            report = "{\"bound\":true,\"metadata_accessed\":false}";
            return report.c_str();
        }
        runtime.confirm_initialized(); // Caller schedules this only in a verified initialized VM.
        stage("attach");
        auto thread = runtime.attach();
        if (!thread) throw std::runtime_error("Probe thread cannot attach");
        const auto* image = runtime.api().get_corlib();
        const char* corlib = image ? runtime.api().image_get_name(image) : nullptr;
        if (!corlib) throw std::runtime_error("No core library");
        auto math = require(runtime.find_class(corlib, "System", "Math"));
        stage("resolve-abs");
        auto abs = require(runtime.find_method(math, "Abs", "System.Int32", {"System.Int32"}, true));
        std::int32_t negative = -7;
        stage("invoke-abs");
        auto result = runtime.invoke(abs, nullptr, {&negative});
        if (!result || require(runtime.unbox<std::int32_t>(result.value)) != 7)
            throw std::runtime_error("Primitive invocation failed");
        auto strings = require(runtime.find_class(corlib, "System", "String"));
        stage("concat");
        auto concat = require(runtime.find_method(strings, "Concat", "System.String",
                {"System.String", "System.String"}, true));
        auto left = require(runtime.string("template-"));
        auto right = require(runtime.string("audit"));
        auto text = runtime.invoke(concat, nullptr, {left.get(), right.get()});
        if (!text || require(runtime.string_value(text.value)) != u"template-audit")
            throw std::runtime_error("Managed reference invocation failed");
        auto integers = require(runtime.find_class(corlib, "System", "Int64"));
        stage("exact-int64");
        auto parse = require(runtime.find_method(integers, "Parse", "System.Int64", {"System.String"}, true));
        auto maximum_text = require(runtime.string("9223372036854775807"));
        auto maximum = runtime.invoke(parse, nullptr, {maximum_text.get()});
        if (!maximum) throw std::runtime_error("Int64 invocation failed");
        const auto exact = require(runtime.unbox<std::int64_t>(maximum.value));
        if (exact != std::numeric_limits<std::int64_t>::max()) throw std::runtime_error("Int64 field value differs");
        auto reflection = require(runtime.find_class(corlib, "System.Reflection", "Module"));
        auto readonly = require(runtime.find_field(reflection, "FilterTypeName", "System.Reflection.TypeFilter", true));
        if (runtime.write_object(readonly, nullptr, nullptr)) throw std::runtime_error("Readonly field was writable");
        stage("exception");
        auto invalid = require(runtime.string("not-an-integer"));
        auto exception = runtime.invoke(parse, nullptr, {invalid.get()});
        if (exception || !exception.exception.get()) throw std::runtime_error("Managed exception was not captured");
        auto layout = require(runtime.calibrate_method_pointer());
        stage("calibrated");
        auto code = require(runtime.code_pointer(abs, layout));
        if (!code.address) throw std::runtime_error("No calibrated method pointer");
        std::ostringstream out;
        out << "{\"bound\":true,\"primitive_invoke\":true,\"reference_invoke\":true,"
            << "\"exception_caught\":true,\"readonly_preserved\":true,\"int64\":" << quoted(std::to_string(exact))
            << ",\"calibrated_slots\":" << layout.slots.size() << ",\"corlib\":" << quoted(corlib) << '}';
        report = out.str();
    } catch (const std::exception& error) {
        report = "{\"error\":" + quoted(error.what()) + '}';
    }
    return report.c_str();
}
