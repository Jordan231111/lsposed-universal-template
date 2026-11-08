#include "library_observer.h"
#include <link.h>
#include <shadowhook.h>
#include <cstring>

namespace native_runtime {
LibraryObserver::~LibraryObserver() { stop(); }
bool LibraryObserver::matches(const char* path) const {
    if (!path) return false;
    if (library_.find('/') != std::string::npos) return library_ == path;
    const char* basename = std::strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    return library_ == basename;
}
void LibraryObserver::loaded_callback(dl_phdr_info* info, std::size_t, void* data) {
    auto& self = *static_cast<LibraryObserver*>(data);
    if (!info || !self.matches(info->dlpi_name)) return;
    {
        std::lock_guard lock(self.mutex_);
        self.notified_ = true;
    }
    self.changed_.notify_one();
}
void LibraryObserver::retired_callback(dl_phdr_info* info, std::size_t, void* data) {
    auto& self = *static_cast<LibraryObserver*>(data);
    if (!info || !self.matches(info->dlpi_name)) return;
    self.retired_.store(true, std::memory_order_release);
    try { if (self.on_retired_) self.on_retired_(); }
    catch (...) { self.callback_failed_.store(true, std::memory_order_release); }
    self.changed_.notify_one();
}
bool LibraryObserver::start(std::string library, Loaded loaded, Retired retired, std::string& error) {
    if (registered_ || worker_.joinable()) { error = "Observer already started"; return false; }
    if (library.empty() || !loaded) {
        error = "A library identity and load callback are required";
        return false;
    }
    library_ = std::move(library);
    loaded_ = std::move(loaded);
    on_retired_ = std::move(retired);
    retired_.store(false);
    callback_failed_.store(false);
    stopping_ = false;
    notified_ = true; // Initial probe closes the already-loaded/registration race.
    int result = shadowhook_register_dl_init_callback(nullptr, loaded_callback, this);
    if (result) { error = "Cannot register library-load observer: " + std::to_string(result); return false; }
    result = shadowhook_register_dl_fini_callback(retired_callback, nullptr, this);
    if (result) {
        shadowhook_unregister_dl_init_callback(nullptr, loaded_callback, this);
        error = "Cannot register library-retirement observer: " + std::to_string(result);
        return false;
    }
    registered_ = true;
    try { worker_ = std::thread(&LibraryObserver::run, this); }
    catch (...) { stop(); error = "Cannot start library observer worker"; return false; }
    return true;
}
void LibraryObserver::run() {
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return stopping_ || notified_ || retired(); });
            if (stopping_ || retired()) return;
            notified_ = false;
        }
        auto image = native_utils::find_module_info(library_.c_str());
        if (!image.valid && !image.ambiguous) continue;
        try { if (!retired()) loaded_(image); }
        catch (...) {
            callback_failed_.store(true, std::memory_order_release);
            retired_.store(true, std::memory_order_release);
            try { if (on_retired_) on_retired_(); } catch (...) {}
        }
        return;
    }
}
void LibraryObserver::stop() {
    if (registered_) {
        shadowhook_unregister_dl_init_callback(nullptr, loaded_callback, this);
        shadowhook_unregister_dl_fini_callback(retired_callback, nullptr, this);
        registered_ = false;
    }
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_one();
    if (worker_.joinable()) worker_.join();
}
} // namespace native_runtime
