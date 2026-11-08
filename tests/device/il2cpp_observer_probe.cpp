// Owned synthetic runtime: verifies the real observer's readiness/shutdown protocol on Android.
#include "il2cpp_observer.h"
#include <shadowhook.h>
#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <thread>

template<class Predicate> bool until(Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (int error = shadowhook_init(SHADOWHOOK_MODE_SHARED, false)) {
        std::cerr << shadowhook_to_errmsg(error) << '\n'; return 3;
    }
    std::string error;
    if (!il2cpp::start_observer(argv[1], error)) { std::cerr << error; return 4; }
    bool passed = il2cpp::observed_runtime() == nullptr;
    std::this_thread::sleep_for(std::chrono::seconds(std::stoi(argv[2])));
    void* library = dlopen(argv[1], RTLD_NOW);
    if (!library) { std::cerr << dlerror(); il2cpp::stop_observer(); return 5; }
    auto invoke = reinterpret_cast<void*(*)(const void*, void*, void**, void**)>(dlsym(library, "il2cpp_runtime_invoke"));
    auto shutdown = reinterpret_cast<void(*)()>(dlsym(library, "il2cpp_shutdown"));
    for (int lifetime = 0; lifetime < 2 && passed; ++lifetime) {
        passed &= until([] { return il2cpp::observer_status() == "waiting for a managed invocation"; });
        // Let hook installation finish without making metadata calls from an unready runtime.
        passed &= il2cpp::observed_runtime() == nullptr;
        passed &= until([&] { invoke(nullptr, nullptr, nullptr, nullptr); return il2cpp::observed_runtime() != nullptr; });
        if (auto* runtime = il2cpp::observed_runtime()) {
            auto object = runtime->find_class("mscorlib.dll", "System", "Object");
            passed &= bool(object);
            shutdown();
            passed &= il2cpp::observed_runtime() == nullptr && !runtime->ready();
            passed &= !runtime->find_method(object.value, "ToString", "System.String", {}, false);
            passed &= !il2cpp::start_observer(argv[1], error);
        } else passed = false;
        il2cpp::stop_observer();
        if (lifetime == 0) passed &= il2cpp::start_observer(argv[1], error);
    }
    il2cpp::stop_observer();
    dlclose(library);
    std::cout << "{\"passed\":" << (passed ? "true" : "false")
              << ",\"readiness_required\":true,\"shutdown_and_restart_lifetimes\":2}\n";
    return passed ? 0 : 6;
}
