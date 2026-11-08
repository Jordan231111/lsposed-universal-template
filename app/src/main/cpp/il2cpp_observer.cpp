#include "il2cpp_observer.h"
#include "library_observer.h"
#include <shadowhook.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace il2cpp {
namespace {
enum class Phase { stopped, library, invocation, ready, unavailable, retired };
class Observer {
public:
    ~Observer() { stop(); }
    Runtime runtime;
    native_runtime::LibraryObserver library;
    std::atomic<Phase> phase{Phase::stopped};
    std::atomic<bool> stopping{false}, observed{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::string error, name;
    std::vector<native_utils::MemoryRange> ranges;
    std::mutex ranges_mutex;
    void* symbols{};
    void* invocation_stub{};
    void* shutdown_stub{};
    bool started{};

    bool executable(std::uintptr_t address) {
#if defined(__arm__)
        const auto code = address & ~std::uintptr_t{1};
        const std::size_t width = (address & 1) ? 2 : 4;
        if (code % width) return false;
#else
        const auto code = address;
        const std::size_t width = 4;
        if (code & 3) return false;
#endif
        std::lock_guard lock(ranges_mutex);
        auto present = [&] {
            return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) {
                return range.readable && range.executable && range.contains(code, width);
            });
        };
        if (present()) return true;
        ranges = native_utils::process_ranges();
        return present();
    }
    bool read_pointer(std::uintptr_t address, std::uintptr_t* out) {
        if (!out || address % alignof(std::uintptr_t)) return false;
        std::lock_guard lock(ranges_mutex);
        auto present = [&] {
            return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) {
                return range.readable && range.contains(address, sizeof(*out));
            });
        };
        if (!present()) ranges = native_utils::process_ranges();
        if (!present()) return false;
        std::memcpy(out, reinterpret_cast<void*>(address), sizeof(*out));
        return true;
    }
    void fail(std::string reason) {
        { std::lock_guard lock(mutex); error = std::move(reason); }
        auto current = phase.load(std::memory_order_acquire);
        while (current != Phase::retired && current != Phase::stopped &&
                !phase.compare_exchange_weak(current, Phase::unavailable,
                        std::memory_order_release, std::memory_order_acquire)) {}
    }
    void retire() {
        runtime.invalidate();
        phase.store(Phase::retired, std::memory_order_release);
        changed.notify_one();
    }
    void witness() {
        if (!observed.exchange(true, std::memory_order_acq_rel)) changed.notify_one();
    }
    void loaded(const native_utils::ModuleInfo& image);
    void stop() {
        stopping.store(true, std::memory_order_release);
        changed.notify_one();
        library.stop();
        runtime.invalidate();
        bool removed = true;
        for (void** stub : {&invocation_stub, &shutdown_stub}) {
            if (*stub && shadowhook_unhook(*stub) != 0) removed = false;
            else *stub = nullptr;
        }
        if (symbols) { shadowhook_dlclose(symbols); symbols = nullptr; }
        started = false;
        if (!removed) fail("An observer hook could not be removed; do not restart this observer");
        else phase.store(Phase::stopped, std::memory_order_release);
    }
};

// Process lifetime: library unload/reinitialization retires this instance. Explicit stop/start
// is required before another lifetime; old Class/Method/Field/Root objects stay invalid.
Observer& observer() { static Observer instance; return instance; }
std::mutex control;

void* invocation_proxy(const void* method, void* receiver, void** arguments, void** exception) {
    void* result;
    {
        SHADOWHOOK_STACK_SCOPE();
        result = SHADOWHOOK_CALL_PREV(invocation_proxy, method, receiver, arguments, exception);
    }
    observer().witness();
    return result;
}
void shutdown_proxy() {
    SHADOWHOOK_STACK_SCOPE();
    observer().retire();
    SHADOWHOOK_CALL_PREV(shutdown_proxy);
}

void Observer::loaded(const native_utils::ModuleInfo& image) {
    try {
        if (stopping.load() || library.retired()) return;
        if (!image.valid) { fail("Ambiguous library identity; use an exact runtime path or adapter"); return; }
        symbols = shadowhook_dlopen(image.path.c_str());
        if (!symbols) { fail("Loaded IL2CPP image is unavailable to the symbol resolver"); return; }
        {
            std::lock_guard lock(ranges_mutex);
            ranges = native_utils::process_ranges();
        }
        Environment environment{
            this,
            [](void* context, const char* name) -> std::uintptr_t {
                auto& self = *static_cast<Observer*>(context);
                return reinterpret_cast<std::uintptr_t>(shadowhook_dlsym_dynsym(self.symbols, name));
            },
            [](void* context, std::uintptr_t address) { return static_cast<Observer*>(context)->executable(address); },
            [](void* context, std::uintptr_t address, std::uintptr_t* out) {
                return static_cast<Observer*>(context)->read_pointer(address, out);
            }
        };
        auto bound = runtime.bind(environment);
        if (!bound) { fail(bound.error); return; }
        if (stopping.load() || library.retired()) return;
        void* shutdown = shadowhook_dlsym_dynsym(symbols, "il2cpp_shutdown");
        if (!shutdown || !executable(reinterpret_cast<std::uintptr_t>(shutdown))) {
            fail("Missing lifecycle API: il2cpp_shutdown"); return;
        }
        void* original = nullptr;
        shutdown_stub = shadowhook_hook_sym_addr_2(shutdown, reinterpret_cast<void*>(shutdown_proxy),
                &original, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
        if (!shutdown_stub) { fail("Cannot observe IL2CPP shutdown"); return; }
        auto expected = Phase::library;
        if (!phase.compare_exchange_strong(expected, Phase::invocation)) return;
        invocation_stub = shadowhook_hook_sym_addr_2(reinterpret_cast<void*>(runtime.api().runtime_invoke),
                reinterpret_cast<void*>(invocation_proxy), &original, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
        if (!invocation_stub) { fail("Cannot observe managed invocation readiness"); return; }
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] {
                return observed.load() || stopping.load() || library.retired() || phase.load() == Phase::retired;
            });
        }
        if (stopping.load() || library.retired() || phase.load() == Phase::retired) return;
        runtime.confirm_initialized();
        const bool metadata_valid = [&] {
            auto thread = runtime.attach();
            const void* corlib = thread ? runtime.api().get_corlib() : nullptr;
            const char* corlib_name = corlib ? runtime.api().image_get_name(corlib) : nullptr;
            return corlib && static_cast<bool>(runtime.find_class(corlib_name, "System", "Object"));
        }();
        if (!metadata_valid) {
            runtime.invalidate();
            fail("Managed invocation was observed, but public core metadata could not be validated");
            return;
        }
        if (shadowhook_unhook(invocation_stub) != 0) {
            runtime.invalidate();
            fail("Readiness probe could not be removed");
            return;
        }
        invocation_stub = nullptr;
        if (library.retired() || stopping.load() || phase.load() == Phase::retired) return;
        expected = Phase::invocation;
        phase.compare_exchange_strong(expected, Phase::ready, std::memory_order_release);
    } catch (...) {
        runtime.invalidate();
        fail("Native exception during IL2CPP observer setup");
    }
}
} // namespace

bool start_observer(const std::string& name, std::string& error) {
    std::lock_guard lock(control);
    auto& self = observer();
    if (self.started) {
        const auto phase = self.phase.load(std::memory_order_acquire);
        if (self.name == name && (phase == Phase::library || phase == Phase::invocation || phase == Phase::ready)) return true;
        error = "Stop the current observer before changing its library or restarting a retired/failed runtime";
        return false;
    }
    if (self.invocation_stub || self.shutdown_stub) {
        error = "Old observer hooks remain; restart the target process";
        return false;
    }
    self.error.clear();
    self.name = name;
    self.observed.store(false);
    self.stopping.store(false);
    self.phase.store(Phase::library);
    self.started = self.library.start(name,
            [&self](const auto& image) { self.loaded(image); }, [&self] { self.retire(); }, error);
    if (!self.started) self.fail(error);
    return self.started;
}
void stop_observer() {
    std::lock_guard lock(control);
    observer().stop();
}
Runtime* observed_runtime() {
    auto& self = observer();
    return self.phase.load(std::memory_order_acquire) == Phase::ready && self.runtime.ready() ? &self.runtime : nullptr;
}
std::string observer_status() {
    auto& self = observer();
    switch (self.phase.load(std::memory_order_acquire)) {
        case Phase::stopped: return "disabled/stopped";
        case Phase::library: return "waiting for configured library";
        case Phase::invocation: return "waiting for a managed invocation";
        case Phase::ready: return "public metadata ready; transient readiness probe removed";
        case Phase::retired: return "runtime retired; old bindings invalidated";
        case Phase::unavailable: {
            std::lock_guard lock(self.mutex);
            return "unavailable: " + self.error;
        }
    }
    return "unavailable";
}
} // namespace il2cpp
