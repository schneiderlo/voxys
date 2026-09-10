#pragma once
#include "game/expedition/save_generation.hpp"
#include <filesystem>
#include <memory>

namespace voxy::platform {
using game::expedition::StoredGeneration;
using game::expedition::StoreIssue;
// Observation/fault seam for isolated real-filesystem tests. Production uses null.
// after() may terminate a child process to test actual descriptor/lock cleanup.
enum class SaveIo:uint8_t { OpenStage,WriteChunk,FlushFile,CloseFile,Rename,FlushDirectory };
struct SaveIoObserver {
    virtual ~SaveIoObserver()=default;
    [[nodiscard]] virtual int before(SaveIo)noexcept{return 0;}
    virtual void after(SaveIo)noexcept{}
};
// Synchronous blocking I/O; call on the save worker, never the simulation thread.
// One world per absolute slot directory, one owner for this object's lifetime.
// Calls on the same object must be serialized by that worker. Linux local
// filesystems are implemented; other platforms explicitly refuse for now.
// The caller validates the payload's game/physical semantics independently.
class NativeSaveStore {
public:
    [[nodiscard]] static std::filesystem::path defaultRoot(StoreIssue&);
    [[nodiscard]] static std::unique_ptr<NativeSaveStore> open(const std::filesystem::path& slotDirectory,
        game::construction::WorldNamespace,StoreIssue&,SaveIoObserver* =nullptr);
    ~NativeSaveStore();
    NativeSaveStore(const NativeSaveStore&)=delete;
    NativeSaveStore& operator=(const NativeSaveStore&)=delete;
    [[nodiscard]] bool load(StoredGeneration&,StoreIssue&);
    // Requires load/reconciliation after an ambiguous write. On success both
    // replicas have the same next generation and payload, flushed before ack.
    [[nodiscard]] bool publish(uint64_t expectedGeneration,std::span<const std::byte>,StoreIssue&);
private:
    struct Impl;
    explicit NativeSaveStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace voxy::platform
