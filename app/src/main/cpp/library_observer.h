#pragma once
#include "native_utils.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct dl_phdr_info;

namespace native_runtime {
// One library lifetime. Discovery runs on a worker, never inside the linker's callback.
// A background-created process can wait indefinitely without polling or a startup deadline.
class LibraryObserver {
public:
    using Loaded = std::function<void(const native_utils::ModuleInfo&)>;
    using Retired = std::function<void()>; // Must only invalidate/signal; no linker or engine calls.
    ~LibraryObserver();
    LibraryObserver() = default;
    LibraryObserver(const LibraryObserver&) = delete;
    LibraryObserver& operator=(const LibraryObserver&) = delete;
    bool start(std::string library, Loaded loaded, Retired retired, std::string& error);
    void stop(); // Call from the owner thread, not from either callback.
    bool retired() const { return retired_.load(std::memory_order_acquire); }
    bool callback_failed() const { return callback_failed_.load(std::memory_order_acquire); }
private:
    static void loaded_callback(dl_phdr_info*, std::size_t, void*);
    static void retired_callback(dl_phdr_info*, std::size_t, void*);
    bool matches(const char*) const;
    void run();
    std::string library_;
    Loaded loaded_;
    Retired on_retired_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_{false}, notified_{false}, registered_{false};
    std::atomic<bool> retired_{false};
    std::atomic<bool> callback_failed_{false};
    std::thread worker_;
};
} // namespace native_runtime
