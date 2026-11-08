// External Android executable exercising the shipped native helpers and loader observer.
#include "native_utils.h"
#include "library_observer.h"
#include <shadowhook.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <future>
#include <iostream>
#include <thread>
#include <time.h>

namespace {
std::atomic<int> calls{0};
int add_proxy(int left, int right) {
    SHADOWHOOK_STACK_SCOPE();
    ++calls;
    return SHADOWHOOK_CALL_PREV(add_proxy, left, right) + 1;
}
long long cpu_time() {
    timespec value{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value);
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const char* path = argv[1];
    const int delay = std::stoi(argv[2]);
    if (delay < 0 || delay > 300) return 2;
    const int initialized = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (initialized != 0) {
        std::cerr << "ShadowHook initialization: " << initialized << " "
                  << shadowhook_to_errmsg(initialized) << '\n';
        return 3;
    }
    native_runtime::LibraryObserver observer;
    std::atomic<bool> retired{false};
    std::promise<bool> loaded;
    auto complete = loaded.get_future();
    std::string error;
    const char* slash = std::strrchr(path, '/');
    if (!observer.start(slash ? slash + 1 : path, [&](const auto& image) {
        bool success = image.valid && !image.ranges.empty();
        auto address = native_utils::resolve_export(image.base, "template_fixture_add");
        success &= address != 0 && native_utils::resolve_export(image.base, "missing_fixture_export") == 0;
        if (address) {
            auto add = reinterpret_cast<int(*)(int, int)>(address);
            success &= add(2, 3) == 5;
            void* original = nullptr;
            auto* stub = shadowhook_hook_sym_addr_2(reinterpret_cast<void*>(address),
                    reinterpret_cast<void*>(add_proxy), &original, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
            success &= stub != nullptr;
            if (stub) {
                for (int i = 0; i < 100; ++i) success &= add(i, 3) == i + 4;
                success &= calls.load() == 100;
                success &= shadowhook_unhook(stub) == 0;
                success &= add(2, 3) == 5;
            }
        }
        loaded.set_value(success);
    }, [&] { retired.store(true); }, error)) {
        std::cerr << error << '\n'; return 4;
    }
    auto before = cpu_time();
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    auto waiting_cpu = cpu_time() - before;
    void* library = dlopen(path, RTLD_NOW);
    if (!library) { std::cerr << dlerror() << '\n'; observer.stop(); return 5; }
    bool success = complete.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    if (success) success = complete.get();
    success &= native_utils::resolve_export(0x1234, "invalid_base") == 0;
    dlclose(library);
    success &= retired.load() && observer.retired() && !observer.callback_failed();
    observer.stop();
    std::cout << "{\"passed\":" << (success ? "true" : "false")
              << ",\"delay_seconds\":" << delay << ",\"waiting_cpu_ns\":" << waiting_cpu
              << ",\"shared_proxy_calls\":" << calls.load()
              << ",\"retirement_observed\":" << (retired.load() ? "true" : "false") << "}\n";
    return success ? 0 : 6;
}
