#pragma once
#include "engine/platform/native/save_store.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace voxy::platform {
// One disk owner and at most one accepted operation, including its unconsumed
// completion. Only immutable owned bytes cross the simulation/worker boundary.
// All store calls and destruction happen on this worker. No gameplay callbacks.
class NativeSaveWorker {
public:
    enum class Operation { Open, Publish };
    struct Result {
        Operation operation{};
        StoreIssue issue{};
        StoredGeneration stored{}; // Open: complete payload; Publish: committed generation only.
    };
    // An optional observer must outlive the worker and synchronize any state
    // shared with its caller. Observer callbacks run only on the worker thread.
    explicit NativeSaveWorker(SaveIoObserver* observer=nullptr);
    ~NativeSaveWorker();
    NativeSaveWorker(const NativeSaveWorker&)=delete;
    NativeSaveWorker& operator=(const NativeSaveWorker&)=delete;
    [[nodiscard]] bool open(std::filesystem::path,game::construction::WorldNamespace);
    [[nodiscard]] bool publish(uint64_t expectedGeneration,std::vector<std::byte> payload);
    [[nodiscard]] std::optional<Result> poll();
    // Drains an already accepted write, releases the lock on the worker, joins.
    // Call at shutdown, not in a live frame. Repeated sequential calls are safe;
    // callers must not invoke shutdown concurrently or race with destruction.
    void shutdown();
private:
    struct Work {
        Operation operation{};
        std::filesystem::path directory;
        game::construction::WorldNamespace world{};
        uint64_t generation=0;
        std::vector<std::byte> payload;
    };
    bool enqueue(Work);
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_=false,occupied_=false;
    std::optional<Work> pending_;
    std::optional<Result> completed_;
    SaveIoObserver* observer_;
    std::thread thread_;
};
} // namespace voxy::platform
