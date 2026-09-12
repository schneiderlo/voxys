#pragma once

#include "engine/platform/native/save_store.hpp"
#include "game/expedition/design_library.hpp"
#include <functional>
#include <memory>

namespace voxy::platform {

// Main-thread host of the native named-design library. Its worker owns all
// descriptors and serializes blocking disk I/O. No game callbacks cross threads.
class NativeDesignLibrary {
public:
    using Validate = std::function<bool(std::span<const std::byte>)>;
    enum class Operation { None, Open, SaveNew, Update, Duplicate, Rename, Restore,
                           Remove, RefreshImports, Import, Export };
    struct Row {
        uint64_t id = 0, revision = 0;
        std::string name;
        bool backupAvailable = false;
    };
    static constexpr size_t maximumImportFiles = 64;
    // Defaults to .../voxys/Designs, beside .../voxys/saves. Never inside a world.
    [[nodiscard]] static std::filesystem::path defaultRoot(game::expedition::StoreIssue&);
    explicit NativeDesignLibrary(std::filesystem::path designsRoot, Validate,
                                 SaveIoObserver* observer = nullptr);
    ~NativeDesignLibrary();
    NativeDesignLibrary(const NativeDesignLibrary&) = delete;
    NativeDesignLibrary& operator=(const NativeDesignLibrary&) = delete;

    // True means accepted, not durable. poll returns true when an operation
    // finishes (success or failure). Committed rows change only after disk ack.
    [[nodiscard]] bool open();
    [[nodiscard]] bool poll();
    // Shutdown only: drain accepted disk work, release lock, join. Do not use
    // close to dismiss the menu; keep the host alive while the application runs.
    void close();
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool isClosed() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept;
    [[nodiscard]] const game::expedition::StoreIssue& storageIssue() const noexcept;
    [[nodiscard]] Operation lastCompletedOperation() const noexcept;
    [[nodiscard]] bool lastSucceeded() const noexcept;
    [[nodiscard]] uint64_t generation() const noexcept;
    [[nodiscard]] uint64_t selectedId() const noexcept;
    [[nodiscard]] const std::vector<Row>& rows() const noexcept;
    [[nodiscard]] const std::vector<std::string>& imports() const noexcept;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path importsPath() const;
    [[nodiscard]] std::filesystem::path exportsPath() const;
    [[nodiscard]] const std::filesystem::path& lastPath() const noexcept;

    [[nodiscard]] bool read(uint64_t id, uint64_t expectedRevision, std::vector<std::byte>& output);
    [[nodiscard]] bool saveNew(std::string name, std::vector<std::byte> blueprint);
    [[nodiscard]] bool update(uint64_t id, uint64_t expectedRevision, std::string name, std::vector<std::byte> blueprint);
    [[nodiscard]] bool duplicate(uint64_t id, uint64_t expectedRevision, std::string name);
    [[nodiscard]] bool rename(uint64_t id, uint64_t expectedRevision, std::string name);
    [[nodiscard]] bool restore(uint64_t id, uint64_t expectedRevision);
    // Confirmation belongs to the menu. This removes current and backup together.
    [[nodiscard]] bool remove(uint64_t id, uint64_t expectedRevision);
    [[nodiscard]] bool refreshImports();
    // A filename from Imports only; no arbitrary path or automatic draft load.
    [[nodiscard]] bool importFile(std::string filename);
    // A fresh deterministic content/name-derived filename in Exports. An
    // existing file is refused, never silently overwritten. Success means flush.
    [[nodiscard]] bool exportFile(std::string name, std::vector<std::byte> blueprint);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voxy::platform
