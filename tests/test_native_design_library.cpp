#include "engine/platform/native/design_library.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <cerrno>

namespace {
using namespace voxy::platform;
using namespace voxy::game::expedition;
std::vector<std::byte> blueprint(uint8_t marker = 1) {
    std::vector<std::byte> bytes(75, std::byte{0});
    bytes[0] = std::byte{'S'}; bytes[1] = std::byte{'V'}; bytes[2] = std::byte{'B'}; bytes[3] = std::byte{'P'};
    bytes[4] = std::byte{1}; bytes[8] = std::byte{1}; bytes[16] = std::byte{1}; bytes[20] = static_cast<std::byte>(marker);
    const auto hash = voxy::core::sha256(bytes); bytes.insert(bytes.end(), hash.bytes.begin(), hash.bytes.end()); return bytes;
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        const auto* root = std::getenv("VOXY_STORE_TEST_ROOT");
        auto name = (std::filesystem::path(root ? root : "/tmp") / "voxys-designs-XXXXXX").string();
        const auto* result = ::mkdtemp(name.data()); if (!result) throw std::runtime_error("mkdtemp"); path = result;
    }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void complete(NativeDesignLibrary& library) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (library.busy() && std::chrono::steady_clock::now() < until) {
        (void)library.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(library.busy()) << library.message();
}
void opened(NativeDesignLibrary& library) { ASSERT_TRUE(library.open()); complete(library); ASSERT_TRUE(library.ready()) << library.message(); }
void success(NativeDesignLibrary& library) { complete(library); ASSERT_TRUE(library.lastSucceeded()) << library.message(); }
bool validate(std::span<const std::byte> bytes) { return validDesignBlueprintEnvelope(bytes) && bytes[20] != std::byte{99}; }
void file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary); output.write(text.data(), static_cast<std::streamsize>(text.size())); ASSERT_TRUE(output.good());
}
struct Fault : SaveIoObserver {
    std::atomic<int> error{0}; SaveIo boundary = SaveIo::WriteChunk;
    int before(SaveIo operation) noexcept override { return operation == boundary ? error.load() : 0; }
};
TEST(NativeDesignLibrary, NameCopyUpdateBackupRestoreAndRemovalSurviveReopen) {
    Directory directory; const auto root = directory.path / "Designs";
    NativeDesignLibrary library(root, validate); opened(library);
    EXPECT_EQ(library.generation(), 0u); EXPECT_TRUE(library.rows().empty());
    ASSERT_TRUE(library.saveNew("Tug \xc3\xa9", blueprint())); EXPECT_TRUE(library.rows().empty()); success(library);
    ASSERT_EQ(library.rows().size(), 1u); const auto id = library.selectedId(); EXPECT_EQ(library.rows()[0].revision, 1u);
    ASSERT_TRUE(library.duplicate(id, 1, "Spare")); success(library); const auto copy = library.selectedId(); EXPECT_NE(copy, id);
    ASSERT_TRUE(library.rename(copy, 1, "Second tug")); success(library);
    ASSERT_TRUE(library.update(id, 1, "Changed tug", blueprint(2))); success(library);
    ASSERT_TRUE(library.restore(id, 2)); success(library);
    std::vector<std::byte> output; ASSERT_TRUE(library.read(id, 3, output)); EXPECT_EQ(output, blueprint());
    EXPECT_FALSE(library.update(id, 2, "Stale", blueprint(3))); EXPECT_FALSE(library.busy());
    EXPECT_FALSE(library.saveNew("Tug \xc3\xa9", blueprint()));
    const auto generation = library.generation(); library.close();
    NativeDesignLibrary reopened(root, validate); opened(reopened); EXPECT_EQ(reopened.generation(), generation);
    ASSERT_EQ(reopened.rows().size(), 2u); ASSERT_TRUE(reopened.read(id, 3, output)); EXPECT_EQ(output, blueprint());
    ASSERT_TRUE(reopened.remove(copy, 2)); success(reopened); EXPECT_EQ(reopened.rows().size(), 1u);
    EXPECT_FALSE(reopened.read(copy, 2, output)); EXPECT_EQ(output, blueprint());
    EXPECT_TRUE(reopened.rows()[0].backupAvailable);
}
TEST(NativeDesignLibrary, HostValidationAndExplicitRevisionRefusalsPreserveCollection) {
    Directory directory; NativeDesignLibrary library(directory.path / "Designs", validate); opened(library);
    EXPECT_FALSE(library.saveNew("bad\nname", blueprint())); EXPECT_FALSE(library.saveNew("Missing content", blueprint(99)));
    auto corrupted = blueprint(); corrupted[25] ^= std::byte{1}; EXPECT_FALSE(library.saveNew("Damaged", corrupted));
    EXPECT_EQ(library.generation(), 0u); EXPECT_TRUE(library.rows().empty());
    ASSERT_TRUE(library.saveNew("Good", blueprint())); success(library);
    EXPECT_FALSE(library.duplicate(1, 2, "Wrong revision")); EXPECT_FALSE(library.rename(1, 2, "Wrong revision"));
    EXPECT_FALSE(library.remove(1, 2)); EXPECT_FALSE(library.restore(1, 1));
    EXPECT_EQ(library.generation(), 1u); EXPECT_EQ(library.rows()[0].name, "Good");
    library.close(); EXPECT_TRUE(library.isClosed()); EXPECT_FALSE(library.saveNew("Closed", blueprint())); EXPECT_FALSE(library.poll());
}
struct Barrier : SaveIoObserver {
    std::mutex mutex; std::condition_variable changed; bool waiting = false, released = false;
    std::thread::id worker;
    int before(SaveIo operation) noexcept override {
        if (operation == SaveIo::FlushFile) {
            std::unique_lock lock(mutex); worker = std::this_thread::get_id(); waiting = true; changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }
        return 0;
    }
    bool wait() { std::unique_lock lock(mutex); return changed.wait_for(lock, std::chrono::seconds(5), [&] { return waiting; }); }
    void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
    ~Barrier() { release(); }
};
TEST(NativeDesignLibrary, DurableAcknowledgmentOwnsBytesAndBlocksConcurrentOperations) {
    Directory directory; Barrier barrier; const auto main = std::this_thread::get_id();
    bool validatedOnMain = true;
    NativeDesignLibrary library(directory.path / "Designs", [&](auto bytes) { validatedOnMain &= std::this_thread::get_id() == main; return validate(bytes); }, &barrier);
    opened(library); auto bytes = blueprint(); ASSERT_TRUE(library.saveNew("Queued", bytes)); bytes[20] = std::byte{99};
    const bool waiting = barrier.wait(); const bool invisible = library.rows().empty() && library.generation() == 0 && !library.poll();
    const bool refused = !library.saveNew("Concurrent", blueprint()) && !library.refreshImports();
    barrier.release(); ASSERT_TRUE(waiting); EXPECT_TRUE(invisible); EXPECT_TRUE(refused); EXPECT_TRUE(validatedOnMain);
    EXPECT_NE(barrier.worker, main); success(library);
    std::vector<std::byte> output; ASSERT_TRUE(library.read(1, 1, output)); EXPECT_EQ(output, blueprint());
    EXPECT_EQ(library.lastCompletedOperation(), NativeDesignLibrary::Operation::SaveNew);
}
TEST(NativeDesignLibrary, OrdinaryDiskFailureKeepsCurrentAndBackupTogetherThenRetries) {
    Directory directory; Fault fault; NativeDesignLibrary library(directory.path / "Designs", validate, &fault); opened(library);
    ASSERT_TRUE(library.saveNew("First", blueprint())); success(library);
    ASSERT_TRUE(library.update(1, 1, "Second", blueprint(2))); success(library);
    fault.error = ENOSPC; ASSERT_TRUE(library.update(1, 2, "Must not save", blueprint(3))); complete(library);
    EXPECT_FALSE(library.lastSucceeded()); EXPECT_TRUE(library.ready()); EXPECT_FALSE(library.failed());
    EXPECT_EQ(library.storageIssue().error, StoreError::NoSpace); EXPECT_EQ(library.generation(), 2u); EXPECT_EQ(library.rows()[0].name, "Second");
    fault.error = 0; ASSERT_TRUE(library.restore(1, 2)); success(library);
    std::vector<std::byte> output; ASSERT_TRUE(library.read(1, 3, output)); EXPECT_EQ(output, blueprint()); EXPECT_EQ(library.rows()[0].name, "First");
}
TEST(NativeDesignLibrary, UncertainPublicationRequiresReopenWithoutFalseSuccess) {
    Directory directory; Fault fault; fault.boundary = SaveIo::FlushDirectory;
    const auto root = directory.path / "Designs"; NativeDesignLibrary library(root, validate, &fault); opened(library);
    ASSERT_TRUE(library.saveNew("Old", blueprint())); success(library);
    fault.error = EIO; ASSERT_TRUE(library.update(1, 1, "New", blueprint(2))); complete(library);
    EXPECT_TRUE(library.failed()); EXPECT_FALSE(library.lastSucceeded()); EXPECT_TRUE(library.storageIssue().publicationMayHaveHappened);
    EXPECT_EQ(library.generation(), 1u); EXPECT_EQ(library.rows()[0].name, "Old");
    EXPECT_FALSE(library.saveNew("Wrong retry", blueprint())); library.close();
    NativeDesignLibrary reopened(root, validate); opened(reopened); EXPECT_EQ(reopened.generation(), 2u); EXPECT_EQ(reopened.rows()[0].name, "New");
}
TEST(NativeDesignLibrary, ExclusiveLibraryOwnerAndShutdownDrainRemainIndependentOfWorlds) {
    Directory directory; const auto root = directory.path / "Designs";
    NativeDesignLibrary first(root, validate); opened(first);
    NativeDesignLibrary second(root, validate); ASSERT_TRUE(second.open()); complete(second);
    EXPECT_TRUE(second.failed()); EXPECT_EQ(second.storageIssue().error, StoreError::Busy); second.close();
    ASSERT_TRUE(first.saveNew("Drain", blueprint())); first.close(); // Accepted write drains without a UI acknowledgment.
    NativeDesignLibrary reopened(root, validate); opened(reopened); ASSERT_EQ(reopened.rows().size(), 1u); EXPECT_EQ(reopened.rows()[0].name, "Drain");
    EXPECT_FALSE(std::filesystem::exists(directory.path / "saves"));
    NativeDesignLibrary invalid("relative/Designs", validate); EXPECT_FALSE(invalid.open()); EXPECT_TRUE(invalid.failed());
}
TEST(NativeDesignLibrary, ActualExportImportFilesAreBoundedValidatedAndNeverOverwrite) {
    Directory directory; NativeDesignLibrary library(directory.path / "Designs", validate); opened(library);
    ASSERT_TRUE(library.exportFile("Transfer \xc3\xa9", blueprint(2))); success(library);
    const auto exported = library.lastPath(); ASSERT_TRUE(std::filesystem::exists(exported)); EXPECT_EQ(exported.parent_path(), library.exportsPath());
    const auto size = std::filesystem::file_size(exported);
    ASSERT_TRUE(library.exportFile("Transfer \xc3\xa9", blueprint(2))); complete(library); EXPECT_FALSE(library.lastSucceeded());
    EXPECT_EQ(std::filesystem::file_size(exported), size); EXPECT_TRUE(library.ready());
    std::filesystem::copy_file(exported, library.importsPath() / "transfer.voxy-design.json");
    file(library.importsPath() / "not-a-design.txt", "ignored");
    file(library.importsPath() / "corrupt.voxy-design.json", "{}");
    std::filesystem::create_symlink(exported, library.importsPath() / "linked.voxy-design.json");
    ASSERT_TRUE(library.refreshImports()); success(library); ASSERT_EQ(library.imports().size(), 2u);
    EXPECT_FALSE(library.importFile("../transfer.voxy-design.json")); EXPECT_FALSE(library.importFile("linked.voxy-design.json"));
    ASSERT_TRUE(library.importFile("corrupt.voxy-design.json")); complete(library); EXPECT_FALSE(library.lastSucceeded()); EXPECT_TRUE(library.rows().empty());
    ASSERT_TRUE(library.importFile("transfer.voxy-design.json")); success(library);
    ASSERT_EQ(library.rows().size(), 1u); EXPECT_EQ(library.rows()[0].name, "Transfer \xc3\xa9");
    EXPECT_EQ(library.lastCompletedOperation(), NativeDesignLibrary::Operation::Import);
    std::vector<std::byte> output; ASSERT_TRUE(library.read(1, 1, output)); EXPECT_EQ(output, blueprint(2));
    ASSERT_TRUE(library.importFile("transfer.voxy-design.json")); complete(library); EXPECT_FALSE(library.lastSucceeded()); EXPECT_EQ(library.rows().size(), 1u);
}
TEST(NativeDesignLibrary, ImportValidationOccursOnMainThreadAndInvalidContentCannotPublish) {
    Directory directory; const auto main = std::this_thread::get_id(); bool correctThread = true;
    NativeDesignLibrary library(directory.path / "Designs", [&](auto bytes) { correctThread &= std::this_thread::get_id() == main; return validate(bytes); }); opened(library);
    std::string text, error; ASSERT_TRUE(encodeDesignFile({"Unavailable", blueprint(99)}, text, error));
    file(library.importsPath() / "missing.voxy-design.json", text);
    ASSERT_TRUE(library.refreshImports()); success(library); ASSERT_TRUE(library.importFile("missing.voxy-design.json")); complete(library);
    EXPECT_TRUE(correctThread); EXPECT_FALSE(library.lastSucceeded()); EXPECT_EQ(library.generation(), 0u); EXPECT_TRUE(library.rows().empty());
    for (int i = 0; i < 64; ++i) file(library.importsPath() / ("file-" + std::to_string(i) + ".voxy-design.json"), "{}");
    ASSERT_TRUE(library.refreshImports()); complete(library); EXPECT_FALSE(library.lastSucceeded());
    EXPECT_EQ(library.storageIssue().error, StoreError::Capacity); EXPECT_EQ(library.imports().size(), 1u);
}
} // namespace
