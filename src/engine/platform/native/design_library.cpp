#include "engine/platform/native/design_library.hpp"
#include "core/sha256.hpp"
#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voxy::platform {
using namespace game::expedition;
namespace {
constexpr game::construction::WorldNamespace libraryIdentity{{'v','o','x','y','s','-','d','e','s','i','g','n','s','-','0','1'}};
constexpr std::string_view extension = ".voxy-design.json";
bool validRoot(const std::filesystem::path& path) {
    if (!path.is_absolute() || path == path.root_path()) return false;
    for (const auto& part : path) if (part == ".." || part.native().find(typename std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) return false;
    return true;
}
bool validFilename(std::string_view name) noexcept {
    if (name.empty() || name.size() > 255 || !name.ends_with(extension)) return false;
    for (char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || byte == 127 || c == '/' || c == '\\') return false;
    }
    return true;
}
std::string explanation(const StoreIssue& issue) {
    if (issue.publicationMayHaveHappened || issue.error == StoreError::RecoveryRequired)
        return "Save outcome is uncertain. Restart the game to recover saved designs.";
    switch (issue.error) {
    case StoreError::Busy: return "The design library is open in another process.";
    case StoreError::NoSpace: return "Storage is full. Previous saved designs are safe.";
    case StoreError::Permission: return "The Designs folder is not writable.";
    case StoreError::InvalidPath: return "Use an absolute Designs folder without links or parent traversal.";
    case StoreError::InvalidData: return "The saved library or design file is damaged.";
    case StoreError::UnsupportedSchema: return "This library needs a different game version.";
    case StoreError::Capacity: return "Design storage capacity was reached.";
    case StoreError::Conflict: return "The saved library changed. Restart the game before saving designs.";
    case StoreError::UnsupportedPlatform: return "Native design storage is unavailable on this platform.";
    default: return "The design operation failed. Previous saved designs are unchanged.";
    }
}
#if defined(__linux__)
struct Descriptor {
    int value = -1;
    explicit Descriptor(int fd = -1) : value(fd) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    void reset(int fd = -1) { if (value >= 0) ::close(value); value = fd; }
    int release() { return std::exchange(value, -1); }
};
bool ioFailure(StoreIssue& issue, int code, bool uncertain = false) {
    auto error = StoreError::Io;
    if (code == ENOSPC || code == EDQUOT) error = StoreError::NoSpace;
    if (code == EACCES || code == EPERM || code == EROFS) error = StoreError::Permission;
    issue = {error, code, uncertain}; return false;
}
bool flush(int fd, StoreIssue& issue) {
    while (::fsync(fd) != 0) if (errno != EINTR) return ioFailure(issue, errno);
    return true;
}
int openDirectory(const std::filesystem::path& path, StoreIssue& issue) {
    Descriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.value < 0) { ioFailure(issue, errno); return -1; }
    for (const auto& part : path.relative_path()) {
        if (part.empty() || part == ".") continue;
        Descriptor next(::openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (next.value < 0) { ioFailure(issue, errno); return -1; }
        current.reset(next.release());
    }
    return current.release();
}
int childDirectory(int root, const char* name, StoreIssue& issue) {
    if (::mkdirat(root, name, 0700) != 0 && errno != EEXIST) { ioFailure(issue, errno); return -1; }
    Descriptor child(::openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (child.value < 0) { ioFailure(issue, errno); return -1; }
    if (!flush(root, issue)) return -1;
    return child.release();
}
bool listImports(int directory, std::vector<std::string>& output, StoreIssue& issue, std::string& error) {
    // Opening "." yields an independent directory cursor, unlike dup().
    Descriptor descriptor(::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (descriptor.value < 0) return ioFailure(issue, errno);
    DIR* entries = ::fdopendir(descriptor.value);
    if (!entries) return ioFailure(issue, errno);
    (void)descriptor.release();
    struct CloseDirectory { void operator()(DIR* value) const noexcept { (void)::closedir(value); } };
    std::unique_ptr<DIR, CloseDirectory> owner(entries);
    std::vector<std::string> files;
    size_t scanned = 0;
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(entries);
        if (!entry) { if (errno) return ioFailure(issue, errno); break; }
        if (++scanned > 1024) { issue.error = StoreError::Capacity; error = "Imports has too many entries (maximum 1024)."; return false; }
        const std::string name = entry->d_name;
        if (!validFilename(name)) continue;
        struct stat state{};
        if (::fstatat(directory, name.c_str(), &state, AT_SYMLINK_NOFOLLOW) != 0) return ioFailure(issue, errno);
        if (!S_ISREG(state.st_mode) || state.st_nlink != 1 || state.st_size <= 0
            || static_cast<uint64_t>(state.st_size) > kMaximumDesignFileBytes) continue;
        if (files.size() == NativeDesignLibrary::maximumImportFiles) {
            issue.error = StoreError::Capacity; error = "Keep at most 64 design files in Imports."; return false;
        }
        files.push_back(name);
    }
    std::sort(files.begin(), files.end()); output = std::move(files); return true;
}
bool readImport(int directory, const std::string& name, std::string& text, StoreIssue& issue) {
    Descriptor file(::openat(directory, name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (file.value < 0) return ioFailure(issue, errno);
    struct stat state{};
    if (::fstat(file.value, &state) != 0) return ioFailure(issue, errno);
    if (!S_ISREG(state.st_mode) || state.st_nlink != 1 || state.st_size <= 0
        || static_cast<uint64_t>(state.st_size) > kMaximumDesignFileBytes) { issue.error = StoreError::InvalidData; return false; }
    text.resize(static_cast<size_t>(state.st_size));
    size_t offset = 0;
    while (offset < text.size()) {
        const auto count = ::read(file.value, text.data() + offset, text.size() - offset);
        if (count < 0) { if (errno == EINTR) continue; return ioFailure(issue, errno); }
        if (count == 0) { issue.error = StoreError::InvalidData; return false; }
        offset += static_cast<size_t>(count);
    }
    char extra = 0;
    ssize_t count = -1;
    do { count = ::read(file.value, &extra, 1); } while (count < 0 && errno == EINTR);
    if (count < 0) return ioFailure(issue, errno);
    if (count != 0) { issue.error = StoreError::InvalidData; return false; }
    return true;
}
bool writeExport(int directory, const std::string& name, std::string_view text, StoreIssue& issue, std::string& error) {
    const std::string stage = "." + name + ".pending";
    Descriptor file(::openat(directory, stage.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (file.value < 0) {
        if (errno == EEXIST) error = "An unfinished export already exists. Check the Exports folder.";
        return ioFailure(issue, errno);
    }
    bool linked = false;
    const auto fail = [&] {
        // This operation exclusively created the staging name. A published
        // target is a complete flushed file and is never removed on failure.
        (void)::unlinkat(directory, stage.c_str(), 0);
        if (linked) issue.publicationMayHaveHappened = true;
        return false;
    };
    size_t offset = 0;
    while (offset < text.size()) {
        const auto count = ::write(file.value, text.data() + offset, text.size() - offset);
        if (count < 0) { if (errno == EINTR) continue; ioFailure(issue, errno); return fail(); }
        if (count == 0) { ioFailure(issue, EIO); return fail(); }
        offset += static_cast<size_t>(count);
    }
    if (!flush(file.value, issue)) return fail();
    if (::close(file.release()) != 0) { ioFailure(issue, errno); return fail(); }
    // linkat is an atomic no-overwrite publication on the same filesystem.
    if (::linkat(directory, stage.c_str(), directory, name.c_str(), 0) != 0) {
        if (errno == EEXIST) error = "This exact export already exists. Check the Exports folder.";
        ioFailure(issue, errno); return fail();
    }
    linked = true;
    if (::unlinkat(directory, stage.c_str(), 0) != 0) { ioFailure(issue, errno); return fail(); }
    if (!flush(directory, issue)) return fail();
    return true;
}
#endif
} // namespace

struct NativeDesignLibrary::Impl {
    enum class State { New, Opening, Ready, Failed, Closed };
    enum class DiskOperation { Open, Publish, ListImports, ReadImport, Export };
    struct Work {
        DiskOperation kind{};
        uint64_t generation = 0;
        std::vector<std::byte> bytes;
        std::string filename, text;
    };
    struct Result {
        DiskOperation kind{};
        StoreIssue issue;
        StoredGeneration stored;
        std::vector<std::string> files;
        std::string text, error, filename;
    };
    std::filesystem::path root, lastPath;
    Validate validate;
    SaveIoObserver* observer;
    State state = State::New;
    Operation pending = Operation::None, completedOperation = Operation::None;
    bool succeeded = false;
    uint64_t generation = 0, selected = 0, candidateSelected = 0;
    DesignLibraryImage image;
    std::optional<DesignLibraryImage> candidate;
    std::vector<Row> rows;
    std::vector<std::string> imports;
    std::string message = "Open Saved designs.";
    StoreIssue issue;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, occupied = false;
    std::optional<Work> work;
    std::optional<Result> result;
    std::thread worker;

    Impl(std::filesystem::path path, Validate callback, SaveIoObserver* hook)
        : root(std::move(path)), validate(std::move(callback)), observer(hook) {}
    bool refuse(std::string why) { message = std::move(why); return false; }
    bool available() {
        if (state == State::Closed) return refuse("The design library is closed.");
        if (state == State::Failed) return refuse("Restart the game to recover saved designs.");
        if (state != State::Ready || pending != Operation::None) return refuse("Wait for the current design operation.");
        return true;
    }
    bool enqueue(Work next) {
        std::lock_guard lock(mutex);
        if (stopping || occupied) return false;
        work = std::move(next); occupied = true; wake.notify_one(); return true;
    }
    std::optional<Result> take() {
        std::lock_guard lock(mutex);
        if (!result || stopping) return {};
        auto value = std::move(result); result.reset(); occupied = false; return value;
    }
    void run() {
        std::unique_ptr<NativeSaveStore> store;
#if defined(__linux__)
        Descriptor importsDirectory, exportsDirectory;
#endif
        for (;;) {
            Work next;
            {
                std::unique_lock lock(mutex); wake.wait(lock, [&] { return stopping || work.has_value(); });
                if (!work) break;
                next = std::move(*work); work.reset();
            }
            Result value; value.kind = next.kind; value.filename = next.filename;
            try {
                if (next.kind == DiskOperation::Open) {
                    store = NativeSaveStore::open(root / "Library", libraryIdentity, value.issue, observer);
                    if (store && !store->load(value.stored, value.issue)) store.reset();
#if defined(__linux__)
                    if (store) {
                        Descriptor directory(openDirectory(root, value.issue));
                        if (directory.value >= 0) importsDirectory.reset(childDirectory(directory.value, "Imports", value.issue));
                        if (!value.issue) exportsDirectory.reset(childDirectory(directory.value, "Exports", value.issue));
                        if (value.issue) store.reset();
                    }
#endif
                } else if (!store) value.issue.error = StoreError::InvalidPath;
                else if (next.kind == DiskOperation::Publish) {
                    if (store->publish(next.generation, next.bytes, value.issue)) value.stored.generation = next.generation + 1;
                }
#if defined(__linux__)
                else if (next.kind == DiskOperation::ListImports) {
                    (void)listImports(importsDirectory.value, value.files, value.issue, value.error);
                } else if (next.kind == DiskOperation::ReadImport) {
                    (void)readImport(importsDirectory.value, next.filename, value.text, value.issue);
                } else if (next.kind == DiskOperation::Export) {
                    (void)writeExport(exportsDirectory.value, next.filename, next.text, value.issue, value.error);
                }
#else
                else value.issue.error = StoreError::UnsupportedPlatform;
#endif
            } catch (const std::bad_alloc&) { value.issue.error = StoreError::Capacity; }
            catch (const std::filesystem::filesystem_error& error) { value.issue = {StoreError::Io, error.code().value()}; }
            catch (...) { value.issue = {StoreError::Io, EIO}; }
            { std::lock_guard lock(mutex); result = std::move(value); }
        }
        // Store, directory descriptors and lifetime lock die on this thread.
    }
    void close() {
        if (state == State::Closed) return;
        { std::lock_guard lock(mutex); stopping = true; wake.notify_one(); }
        if (worker.joinable()) worker.join();
        candidate.reset(); pending = Operation::None; state = State::Closed;
        message = "The design library is closed.";
    }
    void refreshRows() {
        std::vector<Row> next; next.reserve(image.rows.size());
        for (const auto& row : image.rows) next.push_back({row.current.id, row.current.revision, row.current.name, row.backup.has_value()});
        std::sort(next.begin(), next.end(), [](const Row& a, const Row& b) { return a.name < b.name || (a.name == b.name && a.id < b.id); });
        rows = std::move(next);
    }
    const SavedDesignRow* find(uint64_t id, uint64_t expected) {
        const auto row = std::find_if(image.rows.begin(), image.rows.end(), [&](const auto& value) { return value.current.id == id; });
        if (row == image.rows.end()) { refuse("Choose a saved design first."); return nullptr; }
        if (row->current.revision != expected) { refuse("This design changed. Choose it again before saving."); return nullptr; }
        return &*row;
    }
    bool checked(std::span<const std::byte> bytes) {
        if (!validDesignBlueprintEnvelope(bytes) || !validate || !validate(bytes))
            return refuse("This design is damaged, incompatible or needs unavailable parts.");
        return true;
    }
    bool unique(std::string_view name, uint64_t except = 0) {
        if (!validDesignName(name)) return refuse("Enter a short name on one line (96 UTF-8 bytes maximum).");
        for (const auto& row : image.rows) if (row.current.id != except && row.current.name == name)
            return refuse("Choose a different design name.");
        return true;
    }
    bool publish(DesignLibraryImage next, Operation operation, uint64_t select) {
        if (generation == std::numeric_limits<uint64_t>::max()) return refuse("Design library history is full.");
        std::vector<std::byte> bytes;
        if (!encodeDesignLibrary(next, bytes, message)) return false;
        candidate = std::move(next); candidateSelected = select;
        if (!enqueue({DiskOperation::Publish, generation, std::move(bytes), {}, {}})) {
            candidate.reset(); return refuse("Wait for the current design operation.");
        }
        pending = operation; issue = {}; succeeded = false; message = "Saving design..."; return true;
    }
    bool add(std::string name, std::vector<std::byte> bytes, Operation operation) {
        if (!unique(name) || !checked(bytes)) return false;
        if (image.rows.size() == kMaximumLibraryDesigns || image.nextId == std::numeric_limits<uint64_t>::max())
            return refuse("The library is full (32 designs). Export or remove a design first.");
        auto next = image; const auto id = next.nextId++;
        next.rows.push_back({{id, 1, std::move(name), std::move(bytes)}, {}});
        return publish(std::move(next), operation, id);
    }
    bool replace(uint64_t id, uint64_t expected, std::string name, std::vector<std::byte> bytes, Operation operation) {
        if (!find(id, expected) || !unique(name, id) || !checked(bytes)) return false;
        if (expected == std::numeric_limits<uint64_t>::max()) return refuse("This design history is full.");
        auto next = image;
        auto row = std::find_if(next.rows.begin(), next.rows.end(), [&](const auto& value) { return value.current.id == id; });
        if (row == next.rows.end()) return refuse("This design changed. Choose it again before saving.");
        row->backup = row->current; row->current = {id, expected + 1, std::move(name), std::move(bytes)};
        return publish(std::move(next), operation, id);
    }
};

std::filesystem::path NativeDesignLibrary::defaultRoot(StoreIssue& issue) {
    const auto saves = NativeSaveStore::defaultRoot(issue);
    return issue ? std::filesystem::path{} : saves.parent_path() / "Designs";
}
NativeDesignLibrary::NativeDesignLibrary(std::filesystem::path root, Validate validate, SaveIoObserver* observer)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(validate), observer)) {}
NativeDesignLibrary::~NativeDesignLibrary() { close(); }
bool NativeDesignLibrary::open() {
    auto& i = *impl_;
    if (i.state != Impl::State::New) return i.refuse("The library is already open or closed.");
    if (!validRoot(i.root) || !i.validate) { i.issue.error = StoreError::InvalidPath; i.state = Impl::State::Failed; return i.refuse(explanation(i.issue)); }
    i.worker = std::thread([&i] { i.run(); });
    i.state = Impl::State::Opening; i.pending = Operation::Open; i.message = "Opening saved designs...";
    return i.enqueue({Impl::DiskOperation::Open, 0, {}, {}, {}});
}
bool NativeDesignLibrary::poll() {
    auto& i = *impl_; auto result = i.take(); if (!result) return false;
    const auto operation = i.pending;
    i.completedOperation = operation; i.succeeded = false; i.issue = result->issue;
    if (result->issue) {
        i.candidate.reset(); i.pending = Operation::None;
        const bool libraryFailure = operation == Operation::Open || (result->kind == Impl::DiskOperation::Publish
            && (result->issue.publicationMayHaveHappened || result->issue.error == StoreError::Conflict
                || result->issue.error == StoreError::RecoveryRequired));
        if (libraryFailure) i.state = Impl::State::Failed;
        i.message = result->error.empty() ? explanation(result->issue) : std::move(result->error);
        return true;
    }
    try {
        if (result->kind == Impl::DiskOperation::Open) {
            DesignLibraryImage next;
            if (result->stored.generation && !decodeDesignLibrary(result->stored.payload, next, i.message)) {
                i.pending = Operation::None; i.state = Impl::State::Failed; i.issue.error = StoreError::InvalidData; return true;
            }
            i.image = std::move(next); i.generation = result->stored.generation; i.refreshRows();
            i.state = Impl::State::Ready; i.message = "Choose a saved design, or name this boat.";
        } else if (result->kind == Impl::DiskOperation::Publish) {
            if (!i.candidate || result->stored.generation != i.generation + 1) {
                i.state = Impl::State::Failed; i.issue.error = StoreError::RecoveryRequired;
                i.pending = Operation::None; i.message = explanation(i.issue); return true;
            }
            i.image = std::move(*i.candidate); i.candidate.reset(); i.generation = result->stored.generation;
            i.selected = i.candidateSelected; i.refreshRows();
            i.message = operation == Operation::Remove ? "Saved design and backup removed." : operation == Operation::Restore
                ? "Previous saved version restored." : operation == Operation::Import ? "Design imported. Choose Load to use it." : "Design saved.";
        } else if (result->kind == Impl::DiskOperation::ListImports) {
            i.imports = std::move(result->files); i.message = i.imports.empty() ? "Put .voxy-design.json files in Imports." : "Choose a file to import.";
        } else if (result->kind == Impl::DiskOperation::ReadImport) {
            DesignFile file;
            if (!parseDesignFile(result->text, file, i.message)) { i.pending = Operation::None; return true; }
            // Validation runs here on the application thread. A read is not a
            // successful import: only the following durable publish can be one.
            if (i.add(std::move(file.name), std::move(file.blueprint), Operation::Import)) return false;
            i.pending = Operation::None; return true;
        } else if (result->kind == Impl::DiskOperation::Export) {
            i.lastPath = i.root / "Exports" / result->filename; i.message = "Design file exported to Exports.";
        }
        i.pending = Operation::None; i.succeeded = true; return true;
    } catch (const std::bad_alloc&) {
        // If a committed collection cannot be presented, require reconciliation;
        // never retry the prior generation or pretend the write was rolled back.
        i.issue = {StoreError::Capacity, 0, result->kind == Impl::DiskOperation::Publish};
        if (operation == Operation::Open || result->kind == Impl::DiskOperation::Publish) i.state = Impl::State::Failed;
        i.pending = Operation::None; i.candidate.reset(); i.message = explanation(i.issue); return true;
    }
}
void NativeDesignLibrary::close() { if (impl_) impl_->close(); }
bool NativeDesignLibrary::ready() const noexcept { return impl_->state == Impl::State::Ready && !busy(); }
bool NativeDesignLibrary::busy() const noexcept { return impl_->pending != Operation::None; }
bool NativeDesignLibrary::failed() const noexcept { return impl_->state == Impl::State::Failed; }
bool NativeDesignLibrary::isClosed() const noexcept { return impl_->state == Impl::State::Closed; }
const std::string& NativeDesignLibrary::message() const noexcept { return impl_->message; }
const StoreIssue& NativeDesignLibrary::storageIssue() const noexcept { return impl_->issue; }
NativeDesignLibrary::Operation NativeDesignLibrary::lastCompletedOperation() const noexcept { return impl_->completedOperation; }
bool NativeDesignLibrary::lastSucceeded() const noexcept { return impl_->succeeded; }
uint64_t NativeDesignLibrary::generation() const noexcept { return impl_->generation; }
uint64_t NativeDesignLibrary::selectedId() const noexcept { return impl_->selected; }
const std::vector<NativeDesignLibrary::Row>& NativeDesignLibrary::rows() const noexcept { return impl_->rows; }
const std::vector<std::string>& NativeDesignLibrary::imports() const noexcept { return impl_->imports; }
const std::filesystem::path& NativeDesignLibrary::root() const noexcept { return impl_->root; }
std::filesystem::path NativeDesignLibrary::importsPath() const { return impl_->root / "Imports"; }
std::filesystem::path NativeDesignLibrary::exportsPath() const { return impl_->root / "Exports"; }
const std::filesystem::path& NativeDesignLibrary::lastPath() const noexcept { return impl_->lastPath; }
bool NativeDesignLibrary::read(uint64_t id, uint64_t revision, std::vector<std::byte>& output) {
    auto& i = *impl_; if (!i.available()) return false;
    const auto* row = i.find(id, revision); if (!row || !i.checked(row->current.blueprint)) return false;
    output = row->current.blueprint; i.message = "Check the cost, then Launch to build it."; return true;
}
bool NativeDesignLibrary::saveNew(std::string name, std::vector<std::byte> bytes) {
    return impl_->available() && impl_->add(std::move(name), std::move(bytes), Operation::SaveNew);
}
bool NativeDesignLibrary::update(uint64_t id, uint64_t revision, std::string name, std::vector<std::byte> bytes) {
    return impl_->available() && impl_->replace(id, revision, std::move(name), std::move(bytes), Operation::Update);
}
bool NativeDesignLibrary::duplicate(uint64_t id, uint64_t revision, std::string name) {
    auto& i = *impl_; if (!i.available()) return false;
    const auto* row = i.find(id, revision); return row && i.add(std::move(name), row->current.blueprint, Operation::Duplicate);
}
bool NativeDesignLibrary::rename(uint64_t id, uint64_t revision, std::string name) {
    auto& i = *impl_; if (!i.available()) return false;
    const auto* row = i.find(id, revision); return row && i.replace(id, revision, std::move(name), row->current.blueprint, Operation::Rename);
}
bool NativeDesignLibrary::restore(uint64_t id, uint64_t revision) {
    auto& i = *impl_; if (!i.available()) return false;
    const auto* row = i.find(id, revision); if (!row) return false;
    if (!row->backup) return i.refuse("This design has no previous saved version.");
    return i.replace(id, revision, row->backup->name, row->backup->blueprint, Operation::Restore);
}
bool NativeDesignLibrary::remove(uint64_t id, uint64_t revision) {
    auto& i = *impl_; if (!i.available() || !i.find(id, revision)) return false;
    auto next = i.image; std::erase_if(next.rows, [&](const auto& row) { return row.current.id == id; });
    return i.publish(std::move(next), Operation::Remove, 0);
}
bool NativeDesignLibrary::refreshImports() {
    auto& i = *impl_; if (!i.available()) return false;
    if (!i.enqueue({Impl::DiskOperation::ListImports, 0, {}, {}, {}})) return i.refuse("Wait for the current design operation.");
    i.pending = Operation::RefreshImports; i.succeeded = false; i.message = "Reading Imports..."; return true;
}
bool NativeDesignLibrary::importFile(std::string filename) {
    auto& i = *impl_; if (!i.available()) return false;
    if (!validFilename(filename) || std::find(i.imports.begin(), i.imports.end(), filename) == i.imports.end())
        return i.refuse("Refresh Imports and choose a listed design file.");
    if (!i.enqueue({Impl::DiskOperation::ReadImport, 0, {}, std::move(filename), {}})) return i.refuse("Wait for the current design operation.");
    i.pending = Operation::Import; i.succeeded = false; i.message = "Reading design file..."; return true;
}
bool NativeDesignLibrary::exportFile(std::string name, std::vector<std::byte> bytes) {
    auto& i = *impl_; if (!i.available() || !i.checked(bytes)) return false;
    std::string text;
    if (!encodeDesignFile({name, std::move(bytes)}, text, i.message)) return false;
    std::string stem;
    for (char c : name) {
        if (stem.size() == 48) break;
        stem.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? c : '_');
    }
    const auto digest = core::sha256(std::as_bytes(std::span(text)));
    const std::string filename = stem + "-" + core::sha256Hex(digest) + std::string(extension);
    if (!i.enqueue({Impl::DiskOperation::Export, 0, {}, filename, std::move(text)})) return i.refuse("Wait for the current design operation.");
    i.pending = Operation::Export; i.succeeded = false; i.message = "Exporting design file..."; return true;
}
} // namespace voxy::platform
