# Virtual File System Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

The Virtual File System (VFS) provides a unified abstraction for file access across native and WebAssembly platforms. This layer decouples application code from platform-specific file I/O, enabling seamless asset loading whether files reside on disk (native), in Emscripten's virtual filesystem (WASM), or embedded in memory.

---

## 1. Motivation

### 1.1 Current State

The codebase currently uses direct file I/O:

| Pattern | Usage |
|---------|-------|
| `std::ifstream` | Shader loading, config parsing, binary asset loading |
| `std::filesystem::path` | Path manipulation across all subsystems |
| `std::filesystem::exists` | File existence checks |
| `std::fopen` | Log file creation |

### 1.2 Challenges

| Challenge | Description |
|-----------|-------------|
| **WASM Compatibility** | Emscripten's virtual FS requires special handling; `std::filesystem::exists` may not work correctly |
| **Path Resolution** | Native builds use relative paths from CWD; WASM uses preloaded virtual paths |
| **Asset Streaming** | Future tile streaming requires async I/O not well-supported by sync `std::ifstream` |
| **Embedded Assets** | Some assets may need to be embedded in binary; no current support |
| **Hot Reloading** | Development requires watching files for changes; no unified mechanism |

### 1.3 Goals

1. **Unified API** — Single interface for all file operations across platforms
2. **Backend Abstraction** — Support filesystem, virtual FS, embedded, and network backends
3. **Async Support** — First-class async file operations for streaming
4. **Path Normalization** — Consistent path handling across platforms
5. **Minimal Overhead** — Zero-cost abstraction when possible

---

## 2. Architecture

### 2.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      Application Layer                          │
│  (Heightmap, Textures, Shaders, Config, Compression)           │
├─────────────────────────────────────────────────────────────────┤
│                          VFS API                                │
│                     vfs::FileSystem                             │
├─────────────────────────────────────────────────────────────────┤
│                    Mount Point Router                           │
│         /shaders → ...   /assets → ...   /config → ...         │
├───────────────┬─────────────────────────┬───────────────────────┤
│  Native FS    │    Emscripten VFS       │   Embedded Backend    │
│   Backend     │       Backend           │      (Future)         │
└───────────────┴─────────────────────────┴───────────────────────┘
```

### 2.2 Design Principles

| Principle | Description |
|-----------|-------------|
| **Layered** | Clear separation between API, routing, and backends |
| **Extensible** | New backends can be added without changing API |
| **RAII** | File handles manage their own lifecycle |
| **Error Handling** | Use `Result<T, Error>` for all fallible operations |
| **Thread Safety** | All operations are thread-safe for future async support |

### 2.3 Sync vs Async: A Fundamental Distinction

> [!CAUTION]
> **On WASM, synchronous I/O only works for preloaded data.** The browser cannot pause
> JavaScript execution to fetch from the network. This is a platform constraint, not a VFS limitation.

**Backend Capability Model:**

| Backend | Sync Read | Async Read | Notes |
|---------|-----------|------------|-------|
| **NativeBackend** | ✓ | ✓ | Full sync support on desktop |
| **EmscriptenMemFSBackend** | ✓ | ✓ | Sync for preloaded MEMFS files |
| **EmscriptenFetchBackend** | ✗ | ✓ | Async-only, for streaming |
| **ArchiveBackend** | ✓ | ✓ | Sync after archive is loaded |
| **HttpBackend** | ✗ (cache only) | ✓ | Sync fails unless cached; async required |

**API Design Consequence:**

```cpp
// Sync open() may fail on async-only backends
Result<File> open(const Path& path);  
// Returns ErrorCode::NotCached if data isn't available synchronously

// Async open() always works (if file exists)
AsyncRequest openAsync(const Path& path, AsyncCallback callback);
```

### 2.4 Mount Point Categories

Explicitly separate read-only assets from read-write user data:

```cpp
enum class MountCategory {
    Asset,      // Read-only game assets (streamed, cached, archived)
    UserData,   // Read-write user files (save games, config, screenshots)
};
```

| Category | Typical Backends | Sync Guarantee | Write Support |
|----------|------------------|----------------|---------------|
| **Asset** | Archive, HTTP, Embedded | No (may require async) | No |
| **UserData** | Native, IndexedDB | Yes | Yes |

**Platform-Specific UserData Paths:**

| Platform | UserData Location |
|----------|-------------------|
| Windows | `%APPDATA%/voxy/` |
| macOS | `~/Library/Application Support/voxy/` |
| Linux | `~/.local/share/voxy/` |
| WASM | IndexedDB (`/userdata/` virtual mount) |

```cpp
void initVfs() {
    auto& vfs = vfs::FileSystem::instance();
    
    // Assets: read-only, may be async
    vfs.mount("/assets", MountCategory::Asset, 
              std::make_unique<ArchiveBackend>("data.pak"));
    vfs.mount("/dlc", MountCategory::Asset, 
              std::make_unique<HttpBackend>("https://cdn.example.com/dlc/"));
    
    // UserData: read-write, always sync
    vfs.mount("/saves", MountCategory::UserData, 
              std::make_unique<NativeBackend>(getUserDataPath() / "saves"));
    vfs.mount("/config", MountCategory::UserData, 
              std::make_unique<NativeBackend>(getUserDataPath() / "config"));
}
```

---

## 3. Core Types

### 3.1 Path

> [!NOTE]
> **Path automatically normalizes separators.** Game code never needs to worry about
> Windows `\` vs Unix `/`. All paths are stored internally with `/` separators.

```cpp
namespace vfs {

// Normalized, platform-independent path
class Path {
public:
    Path() = default;
    explicit Path(std::string_view path);              // Normalizes \ → /
    explicit Path(const std::filesystem::path& path);  // Normalizes \ → /
    explicit Path(const char* path) : Path(std::string_view{path}) {}
    
    // Path operations
    [[nodiscard]] Path parent() const;
    [[nodiscard]] Path join(std::string_view component) const;
    [[nodiscard]] Path operator/(std::string_view component) const;
    
    // Queries
    [[nodiscard]] std::string_view filename() const;
    [[nodiscard]] std::string_view extension() const;
    [[nodiscard]] std::string_view stem() const;
    [[nodiscard]] bool isAbsolute() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool hasScheme() const;  // e.g., "assets://"
    
    // Conversion
    [[nodiscard]] const std::string& string() const;   // Always uses '/'
    [[nodiscard]] std::filesystem::path toStdPath() const;  // Native separators
    
    // Comparison
    bool operator==(const Path& other) const;
    auto operator<=>(const Path& other) const;
    
private:
    std::string normalized_;  // Always uses '/' separator, never '\'
    
    // Normalize on construction
    static std::string normalize(std::string_view input) {
        std::string result;
        result.reserve(input.size());
        for (char c : input) {
            result.push_back(c == '\\' ? '/' : c);
        }
        // Remove redundant slashes, handle . and ..
        return result;
    }
};

} // namespace vfs
```

### 3.2 File Handle

```cpp
namespace vfs {

// Read-only file handle (RAII)
class File {
public:
    ~File();
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    
    // Non-copyable
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    // Properties
    [[nodiscard]] size_t size() const;
    [[nodiscard]] const Path& path() const;
    [[nodiscard]] bool isOpen() const;
    
    // Synchronous read operations
    [[nodiscard]] Result<size_t> read(std::span<uint8_t> buffer);
    [[nodiscard]] Result<BufferPtr> readAll();           // Zero-copy when possible
    [[nodiscard]] Result<std::string> readString();      // Always copies to string
    
    // Seek operations
    [[nodiscard]] Result<void> seek(size_t position);
    [[nodiscard]] size_t tell() const;
    
private:
    friend class FileSystem;
    explicit File(std::unique_ptr<FileImpl> impl);
    std::unique_ptr<FileImpl> impl_;
};

} // namespace vfs
```

### 3.3 Buffer (Zero-Copy File Data)

> [!IMPORTANT]
> **Returning `std::vector<uint8_t>` forces heap allocation + copy for every read.**
> For 128MB+ heightmaps, this is unacceptable. Use `Buffer` for zero-copy `mmap` on native.

```cpp
namespace vfs {

// Ref-counted buffer that can hold data from multiple sources
class Buffer {
public:
    // Storage strategy (opaque to user)
    enum class StorageType {
        Owned,      // std::vector<uint8_t> - we own the memory
        Mapped,     // mmap'd file - OS owns memory, released on destruction
        Static,     // Points to static/embedded data - never freed
        External,   // SharedArrayBuffer or other external - ref-counted
    };
    
    ~Buffer();
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    
    // Non-copyable (use shared_ptr for sharing)
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    // Access data (valid for lifetime of Buffer)
    [[nodiscard]] std::span<const uint8_t> data() const;
    [[nodiscard]] const uint8_t* ptr() const { return data().data(); }
    [[nodiscard]] size_t size() const { return data().size(); }
    [[nodiscard]] bool empty() const { return size() == 0; }
    
    // Storage info (for debugging/optimization)
    [[nodiscard]] StorageType storageType() const;
    [[nodiscard]] bool isMemoryMapped() const { return storageType() == StorageType::Mapped; }
    
    // Factory methods (used by backends)
    static BufferPtr fromVector(std::vector<uint8_t> data);
    static BufferPtr fromMmap(void* mappedAddr, size_t size, int fd);
    static BufferPtr fromStatic(std::span<const uint8_t> data);
    static BufferPtr fromExternal(std::span<const uint8_t> data, 
                                   std::function<void()> release);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Shared ownership for passing around
using BufferPtr = std::shared_ptr<Buffer>;

} // namespace vfs
```

**Platform-Specific Implementations:**

| Platform | Large File Strategy | Small File Strategy |
|----------|---------------------|---------------------|
| **Native** | `mmap()` / `MapViewOfFile` | Read into vector |
| **WASM** | Reference preloaded memory | Read into vector |
| **Archive** | Decompress to vector | Decompress to vector |
| **HTTP** | Stream to vector | Fetch to vector |

**Threshold for mmap:**

```cpp
constexpr size_t MMAP_THRESHOLD = 1024 * 1024;  // 1 MB

BufferPtr NativeBackend::readFile(const Path& path) {
    size_t fileSize = getFileSize(path);
    
    if (fileSize >= MMAP_THRESHOLD) {
        // Use mmap for large files (zero-copy)
        int fd = open(path.c_str(), O_RDONLY);
        void* mapped = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
        return Buffer::fromMmap(mapped, fileSize, fd);
    } else {
        // Read small files into vector
        std::vector<uint8_t> data(fileSize);
        // ... read file ...
        return Buffer::fromVector(std::move(data));
    }
}
```

### 3.4 Error Types

```cpp
#include <tl/expected.hpp>  // WASM and C++20 compatible

namespace vfs {

enum class ErrorCode {
    NotFound,           // File or directory not found
    PermissionDenied,   // Access denied
    InvalidPath,        // Malformed path
    NotMounted,         // No mount point for path
    IoError,            // Read/write failure
    NotSupported,       // Operation not supported by backend
    Corrupted,          // File data corrupted
    OutOfMemory,        // Memory allocation failed
    NetworkError,       // HTTP fetch failed
    Timeout,            // Operation timed out
    Cancelled,          // Operation cancelled by user or backpressure
    QueueFull,          // Too many in-flight requests
    NotCached,          // Sync read failed; data not cached (use async)
};

struct Error {
    ErrorCode code;
    std::string message;
    Path path;
    int httpStatus = 0;     // HTTP status code (for network errors)
    
    [[nodiscard]] std::string format() const;
};

// Using tl::expected for C++20 and WASM compatibility
template<typename T>
using Result = tl::expected<T, Error>;

using VoidResult = Result<void>;

} // namespace vfs
```

---

## 4. FileSystem API

### 4.1 Priority-Based Mount Resolution

> [!IMPORTANT]
> **Mounts are stored as a priority list, not a map.** This allows multiple backends
> at the same path (e.g., base game + mod + patch) with higher priority checked first.

```
Mount Resolution for "assets/texture.png":
┌──────────────────────────────────────────────────────────────────┐
│  Priority 100: ModBackend("/assets")                             │
│    → "texture.png" exists? YES → Return                          │
├──────────────────────────────────────────────────────────────────┤
│  Priority 50:  PatchBackend("/assets")                           │
│    → "texture.png" exists? NO  → Continue                        │
├──────────────────────────────────────────────────────────────────┤
│  Priority 0:   BaseGameBackend("/assets")                        │
│    → "texture.png" exists? YES → Return                          │
└──────────────────────────────────────────────────────────────────┘
```

### 4.2 Path Schemes

Use URI-style schemes to simplify path handling:

| Scheme | Description | Example | Resolution |
|--------|-------------|---------|------------|
| `assets://` | Read-only game assets | `assets://textures/grass.png` | First matching backend at `/assets` |
| `user://` | Read-write user data | `user://settings.cfg` | Writable backend at `/user` |
| `res://` | Read-only resources | `res://shaders/terrain.wgsl` | First matching backend at `/shaders` |
| (none) | Absolute virtual path | `/assets/textures/grass.png` | Standard mount resolution |

### 4.3 Core Interface

```cpp
namespace vfs {

// Read priority for async operations
enum class Priority {
    Low,        // Background prefetch
    Normal,     // Standard loading
    High,       // User-initiated action
    Critical,   // Required for frame (blocks render)
};

class FileSystem {
public:
    // Singleton access (or dependency injection)
    static FileSystem& instance();
    
    // Initialization
    void init(const InitConfig& config = {});
    void shutdown();
    
    // ─────────────────────────────────────────────────────────────
    // Mount Management (priority: higher = checked first)
    // ─────────────────────────────────────────────────────────────
    
    void mount(std::string_view mountPoint, 
               std::shared_ptr<Backend> backend,
               int priority = 0);
    
    void unmount(std::shared_ptr<Backend> backend);  // Unmount specific backend
    void unmountAll(std::string_view mountPoint);    // Unmount all at path
    
    // ─────────────────────────────────────────────────────────────
    // Synchronous Operations (may fail with NotCached on async backends)
    // ─────────────────────────────────────────────────────────────
    
    [[nodiscard]] bool exists(const Path& path);
    [[nodiscard]] Result<size_t> fileSize(const Path& path);
    
    [[nodiscard]] Result<BufferPtr> readFile(const Path& path);
    [[nodiscard]] Result<std::string> readTextFile(const Path& path);
    
    // ─────────────────────────────────────────────────────────────
    // Asynchronous Operations (always available)
    // ─────────────────────────────────────────────────────────────
    
    using ReadCallback = std::function<void(Result<BufferPtr>)>;
    using TextCallback = std::function<void(Result<std::string>)>;
    
    AsyncRequest readFileAsync(const Path& path, 
                                ReadCallback callback,
                                Priority priority = Priority::Normal);
    
    AsyncRequest readTextFileAsync(const Path& path, 
                                    TextCallback callback);
    
    // Batch async (with priority queue)
    void readFilesAsync(std::span<const Path> paths,
                        std::function<void(const Path&, Result<BufferPtr>)> callback,
                        Priority priority = Priority::Normal);
    
    // ─────────────────────────────────────────────────────────────
    // Write Operations (only to writable backends)
    // ─────────────────────────────────────────────────────────────
    
    [[nodiscard]] VoidResult writeFile(const Path& path, 
                                        std::span<const uint8_t> data);
    [[nodiscard]] VoidResult writeTextFile(const Path& path, 
                                            std::string_view text);
    [[nodiscard]] VoidResult createDirectory(const Path& path);
    [[nodiscard]] VoidResult deleteFile(const Path& path);

    // ─────────────────────────────────────────────────────────────
    // Persistence Control
    // ─────────────────────────────────────────────────────────────
    
    // Force sync to persistent storage (Required for WASM IndexedDB)
    // Must be called after writing critical user data (saves, config)
    [[nodiscard]] VoidResult flush();
    
    // ─────────────────────────────────────────────────────────────
    // Directory Operations
    // ─────────────────────────────────────────────────────────────
    
    [[nodiscard]] Result<std::vector<Path>> listDirectory(const Path& path);
    [[nodiscard]] Result<bool> isDirectory(const Path& path);
    
    // ─────────────────────────────────────────────────────────────
    // Query & Debug
    // ─────────────────────────────────────────────────────────────
    
    [[nodiscard]] Path getUserDataPath() const;    // Platform-specific user dir
    [[nodiscard]] std::vector<MountInfo> getMounts() const;  // Debug: list all mounts
    
private:
    struct MountEntry {
        std::string mountPoint;
        std::shared_ptr<Backend> backend;
        int priority;
    };
    
    // Sorted by: (1) mountPoint length descending, (2) priority descending
    std::vector<MountEntry> mounts_;
    
    // Find backend for path (highest priority first)
    Backend* findBackend(const Path& path, Path& relativePath) const;
    Backend* findWritableBackend(const Path& path, Path& relativePath) const;
};

} // namespace vfs
```

### 4.4 Mount Resolution Algorithm

```cpp
Backend* FileSystem::findBackend(const Path& path, Path& relativePath) const {
    std::string_view pathStr = path.string();
    
    // Mounts sorted by mountPoint length desc, then priority desc
    for (const auto& entry : mounts_) {
        if (pathStr.starts_with(entry.mountPoint)) {
            // Extract relative path
            relativePath = Path{pathStr.substr(entry.mountPoint.size())};
            
            // Check if file exists in this backend
            auto exists = entry.backend->exists(relativePath);
            if (exists && *exists) {
                return entry.backend.get();
            }
            // Continue to next backend (lower priority or different mount)
        }
    }
    return nullptr;  // Not found
}
```

### 4.5 Configuration

```cpp
namespace vfs {

struct InitConfig {
    bool enableHotReload = false;  // Enable file watching (native only)
    bool enableLogging = true;     // Log VFS operations
    size_t asyncQueueSize = 64;    // Max pending async requests
};

// Debug info for mounted backends
struct MountInfo {
    std::string mountPoint;
    std::string backendName;
    int priority;
    BackendCaps capabilities;
};

} // namespace vfs
```

### 4.6 Usage Example

```cpp
void initVfs() {
    auto& vfs = vfs::FileSystem::instance();
    vfs.init({});
    
    // Base game assets (priority 0)
    vfs.mount("/assets", std::make_shared<ArchiveBackend>("base.pak"), 0);
    
    // Official patch (priority 10, overrides base)
    vfs.mount("/assets", std::make_shared<ArchiveBackend>("patch1.pak"), 10);
    
    // User mod (priority 100, overrides everything)
    if (std::filesystem::exists("mods/texture_pack.pak")) {
        vfs.mount("/assets", std::make_shared<ArchiveBackend>("mods/texture_pack.pak"), 100);
    }
    
    // DLC from network (async only)
    vfs.mount("/dlc", std::make_shared<HttpBackend>("https://cdn.example.com/dlc/"), 0);
    
    // User data (writable)
    vfs.mount("/user", std::make_shared<NativeBackend>(getUserDataPath()), 0);
}

// Reading with fallback:
// If mod has "grass.png" → returns mod version
// Else if patch has it → returns patch version  
// Else → returns base game version
auto buffer = vfs.readFile("assets://textures/grass.png");
```

---

## 5. Backends

### 5.1 Backend Interface

```cpp
namespace vfs {

// Backend capability flags
enum class BackendCaps : uint32_t {
    None         = 0,
    SyncRead     = 1 << 0,   // Supports sync open()/read()
    AsyncRead    = 1 << 1,   // Supports async openAsync()/readAsync()
    Write        = 1 << 2,   // Supports write operations
    Directory    = 1 << 3,   // Supports directory listing
    Watch        = 1 << 4,   // Supports file watching (hot-reload)
};

inline BackendCaps operator|(BackendCaps a, BackendCaps b) {
    return static_cast<BackendCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

class Backend {
public:
    virtual ~Backend() = default;
    
    // Capability query (MUST implement)
    [[nodiscard]] virtual BackendCaps capabilities() const = 0;
    
    // Sync operations (may fail with ErrorCode::NotCached if !SyncRead)
    [[nodiscard]] virtual Result<std::unique_ptr<FileImpl>> 
        open(const Path& path) = 0;
        
    [[nodiscard]] virtual Result<bool> exists(const Path& path) = 0;
    [[nodiscard]] virtual Result<size_t> fileSize(const Path& path) = 0;
    [[nodiscard]] virtual Result<std::vector<Path>> 
        listDirectory(const Path& path) = 0;
    [[nodiscard]] virtual Result<bool> isDirectory(const Path& path) = 0;
    
    // Async operations (implement if AsyncRead capability)
    virtual void openAsync(const Path& path, 
                           std::function<void(Result<std::unique_ptr<FileImpl>>)> callback) {
        // Default: dispatch sync open on caller thread
        callback(open(path));
    }
    
    // Write operations (implement if Write capability)
    [[nodiscard]] virtual VoidResult writeFile(const Path& path,
                                                std::span<const uint8_t> data);
    [[nodiscard]] virtual VoidResult createDirectory(const Path& path);
    
    // Flush modifications to permanent storage (e.g. syncfs for Emscripten)
    [[nodiscard]] virtual VoidResult flush() { return {}; }
    
    // Backend identification
    [[nodiscard]] virtual std::string_view name() const = 0;
    
    // Convenience
    [[nodiscard]] bool hasCap(BackendCaps cap) const {
        return (static_cast<uint32_t>(capabilities()) & static_cast<uint32_t>(cap)) != 0;
    }
    [[nodiscard]] bool isWritable() const { return hasCap(BackendCaps::Write); }
    [[nodiscard]] bool supportsSyncRead() const { return hasCap(BackendCaps::SyncRead); }
    [[nodiscard]] bool supportsAsyncRead() const { return hasCap(BackendCaps::AsyncRead); }
};

} // namespace vfs
```

**Backend Capability Summary:**

| Backend | SyncRead | AsyncRead | Write | Directory | Watch |
|---------|----------|-----------|-------|-----------|-------|
| NativeBackend | ✓ | ✓ | ✓ | ✓ | ✓ |
| EmscriptenMemFSBackend | ✓ | ✓ | ✗ | ✓ | ✗ |
| EmscriptenFetchBackend | ✗ | ✓ | ✗ | ✗ | ✗ |
| ArchiveBackend | ✓ | ✓ | ✗ | ✓ | ✗ |
| HttpBackend | ✗* | ✓ | ✗ | ✗ | ✗ |
| EmbeddedBackend | ✓ | ✓ | ✗ | ✓ | ✗ |

*\* HttpBackend: Sync returns cached data only, otherwise `ErrorCode::NotCached`*

### 5.2 Native Filesystem Backend

```cpp
namespace vfs {

class NativeBackend : public Backend {
public:
    explicit NativeBackend(const std::filesystem::path& root);
    
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override;
    Result<bool> exists(const Path& path) override;
    Result<size_t> fileSize(const Path& path) override;
    Result<std::vector<Path>> listDirectory(const Path& path) override;
    Result<bool> isDirectory(const Path& path) override;
    
    VoidResult writeFile(const Path& path, 
                          std::span<const uint8_t> data) override;
    VoidResult createDirectory(const Path& path) override;
    
    std::string_view name() const override { return "NativeFS"; }
    bool isWritable() const override { return true; }
    
private:
    std::filesystem::path root_;
};

} // namespace vfs
```

### 5.3 Emscripten MEMFS Backend

For preloaded assets bundled in the initial WASM download:

```cpp
namespace vfs {

// Sync access to Emscripten MEMFS (preloaded assets)
class EmscriptenMemFSBackend : public Backend {
public:
    explicit EmscriptenMemFSBackend(std::string_view virtualRoot = "/");
    
    BackendCaps capabilities() const override {
        return BackendCaps::SyncRead | BackendCaps::AsyncRead | BackendCaps::Directory;
    }
    
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override;
    Result<bool> exists(const Path& path) override;
    Result<size_t> fileSize(const Path& path) override;
    Result<std::vector<Path>> listDirectory(const Path& path) override;
    Result<bool> isDirectory(const Path& path) override;
    
    std::string_view name() const override { return "EmscriptenMemFS"; }
    
private:
    std::string virtualRoot_;
};

} // namespace vfs
```

### 5.3.1 Emscripten Fetch Backend

For streaming assets that don't fit in the initial download (DLC, large terrain tiles):

```cpp
namespace vfs {

// Async-only access to remote assets via Emscripten Fetch API
class EmscriptenFetchBackend : public Backend {
public:
    explicit EmscriptenFetchBackend(std::string_view baseUrl);
    
    BackendCaps capabilities() const override {
        return BackendCaps::AsyncRead;  // No sync, no directory listing
    }
    
    // Sync operations always fail with NotCached
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override {
        return tl::unexpected(Error{ErrorCode::NotCached, 
            "EmscriptenFetchBackend is async-only; use openAsync()"});
    }
    
    Result<bool> exists(const Path& path) override {
        // HEAD request or cache check
        return isCached(path);
    }
    
    Result<size_t> fileSize(const Path& path) override {
        return tl::unexpected(Error{ErrorCode::NotSupported, 
            "File size requires async fetch"});
    }
    
    Result<std::vector<Path>> listDirectory(const Path& path) override {
        return tl::unexpected(Error{ErrorCode::NotSupported, 
            "Directory listing not supported over HTTP"});
    }
    
    Result<bool> isDirectory(const Path& path) override {
        return false;  // No directories over HTTP
    }
    
    // Async fetch (the only way to get data)
    void openAsync(const Path& path, 
                   std::function<void(Result<std::unique_ptr<FileImpl>>)> callback) override;
    
    // Cache management
    [[nodiscard]] bool isCached(const Path& path) const;
    void prefetch(std::span<const Path> paths);
    void clearCache();
    
    std::string_view name() const override { return "EmscriptenFetch"; }
    
private:
    std::string baseUrl_;
    std::unordered_map<std::string, BufferPtr> cache_;
};

} // namespace vfs
```

### 5.4 Archive Backend (Phase 1 — Required)

> [!IMPORTANT]
> **ArchiveBackend is a Phase 1 requirement, not a future enhancement.**
> Shipping with loose files causes significant performance issues and complicates updates.

**Rationale:**

| Problem | Impact | Solution |
|---------|--------|----------|
| **OS Overhead** | Opening 1000+ files = 1000+ syscalls | Single `.pak` open, seek inside |
| **Download Size** | HTTP overhead per file | One large compressed archive |
| **Update/DLC** | Redownload entire `assets.data` | Patch individual `.pak` files |
| **Directory Listing** | `readdir()` is slow on many files | In-memory central directory |

**Format Choice:**

Use **ZIP format** (via `miniz` — single-header, public domain, WASM-compatible):

| Feature | miniz | libzip | Custom .pak |
|---------|-------|--------|-------------|
| Dependency | Header-only | Library | None |
| WASM support | ✓ | Needs porting | ✓ |
| Compression | Deflate | Deflate+more | Optional |
| Tooling | Standard ZIP tools | Standard | Custom |
| **Recommendation** | **Use this** | Overkill | Maintenance burden |

```cpp
namespace vfs {

struct ArchiveEntry {
    uint64_t offset;        // Offset into archive file
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint32_t crc32;
    bool isCompressed;      // true = deflate, false = store
};

class ArchiveBackend : public Backend {
public:
    // Load from file (native) or preloaded memory (WASM)
    explicit ArchiveBackend(const Path& archivePath);
    explicit ArchiveBackend(std::span<const uint8_t> archiveData);  // Zero-copy
    
    ~ArchiveBackend();
    
    // Backend interface
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override;
    Result<bool> exists(const Path& path) override;
    Result<size_t> fileSize(const Path& path) override;
    Result<std::vector<Path>> listDirectory(const Path& path) override;
    Result<bool> isDirectory(const Path& path) override;
    
    std::string_view name() const override { return "ArchiveBackend"; }
    bool isWritable() const override { return false; }
    
    // Archive-specific
    [[nodiscard]] size_t entryCount() const;
    [[nodiscard]] bool hasEntry(const Path& path) const;
    
private:
    // Central directory (parsed once on load)
    std::unordered_map<std::string, ArchiveEntry> entries_;
    
    // Archive data source (one of these is active)
    std::unique_ptr<File> archiveFile_;           // Native: file handle
    std::span<const uint8_t> archiveMemory_;       // WASM: preloaded memory
    bool ownsData_ = false;
    
    // Parse ZIP central directory on construction
    VoidResult parseDirectory();
    
    // Read and decompress entry
    Result<std::vector<uint8_t>> readEntry(const ArchiveEntry& entry);
};

} // namespace vfs
```

**ArchiveFileImpl (seeking within archive):**

```cpp
namespace vfs {

// File handle that reads from a region within an archive
class ArchiveFileImpl : public FileImpl {
public:
    ArchiveFileImpl(ArchiveBackend* backend, 
                    const ArchiveEntry& entry,
                    std::vector<uint8_t> decompressedData);
    
    size_t size() const override { return data_.size(); }
    
    Result<size_t> read(std::span<uint8_t> buffer) override {
        size_t toRead = std::min(buffer.size(), data_.size() - offset_);
        std::memcpy(buffer.data(), data_.data() + offset_, toRead);
        offset_ += toRead;
        return toRead;
    }
    
    Result<void> seek(size_t position) override {
        if (position > data_.size()) {
            return tl::unexpected(Error{ErrorCode::IoError, "Seek past end"});
        }
        offset_ = position;
        return {};
    }
    
    size_t tell() const override { return offset_; }
    
private:
    std::vector<uint8_t> data_;  // Decompressed content
    size_t offset_ = 0;
};

} // namespace vfs
```

**Archive Creation Tool:**

```bash
# Use standard zip tools to create archives
cd assets/
zip -r -0 ../assets.pak .   # -0 = store (no compression, faster)
zip -r -9 ../assets.pak .   # -9 = max compression (smaller)

# Or create pak_tool for custom options
# Or create pak_tool for custom options
pak_tool create assets.pak assets/ --compress=zstd --level=19
```

> [!CAUTION]
> **Compression Strategy:**
> Ensure the archive uses **per-file compression** (like standard .zip).
> **Do NOT use solid compression** (like .tar.gz or .7z) where the entire archive is one compressed block.
> Random access to a single file in a solid archive requires decompressing the entire archive, which is a performance disaster.
> The `pak_tool` must compress each file individually.

### 5.5 Embedded Asset Backend (Future)

```cpp
namespace vfs {

// Assets embedded in the binary via CMake/resource compiler
class EmbeddedBackend : public Backend {
public:
    // Register embedded asset (called by generated code)
    static void registerAsset(std::string_view path, 
                               std::span<const uint8_t> data);
    
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override;
    Result<bool> exists(const Path& path) override;
    Result<size_t> fileSize(const Path& path) override;
    Result<std::vector<Path>> listDirectory(const Path& path) override;
    Result<bool> isDirectory(const Path& path) override;
    
    std::string_view name() const override { return "EmbeddedAssets"; }
    bool isWritable() const override { return false; }
};

} // namespace vfs
```

---

## 6. Platform-Specific Initialization

### 6.1 Native Initialization

```cpp
void initVfsNative() {
    vfs::InitConfig config;
    
    // Detect data path from executable location or environment
    if (auto envPath = std::getenv("VOXY_DATA_PATH")) {
        config.dataPath = vfs::Path{envPath};
    } else {
        config.dataPath = vfs::Path{"."};  // Current working directory
    }
    
    config.shaderPath = vfs::Path{"shaders"};
    config.assetPath = vfs::Path{"assets"};
    config.enableHotReload = true;  // Debug builds
    
    auto& vfs = vfs::FileSystem::instance();
    vfs.init(config);
    
    // Mount native filesystem backends
    auto dataBackend = std::make_unique<vfs::NativeBackend>(
        config.dataPath.toStdPath()
    );
    vfs.mount("/", std::move(dataBackend));
}
```

### 6.2 WebAssembly Initialization

```cpp
void initVfsWasm() {
    vfs::InitConfig config;
    
    // Emscripten preloads assets to virtual filesystem
    config.dataPath = vfs::Path{"/"};
    config.shaderPath = vfs::Path{"/shaders"};
    config.assetPath = vfs::Path{"/assets"};
    config.enableHotReload = false;
    
    auto& vfs = vfs::FileSystem::instance();
    vfs.init(config);
    
    // Mount Emscripten virtual filesystem
    auto wasmBackend = std::make_unique<vfs::EmscriptenBackend>("/");
    vfs.mount("/", std::move(wasmBackend));
}
```

### 6.3 Unified Entry Point

```cpp
void initVfs() {
#if defined(VOXY_WASM)
    initVfsWasm();
#else
    initVfsNative();
#endif
}
```

### 6.4 The Bootstrap Phase (WASM Paradox)

> [!WARNING]
> **The Bootstrap Paradox:**
> You cannot mount the `ArchiveBackend` ("data.pak") immediately on WASM because the file does not exist yet.
> It must be downloaded first.

**Solution: Two-Phase Initialization**

1.  **Phase 1 (Bootstrap):**
    *   Mount minimal VFS (just `/userdata` and maybe a temporary `/bootstrap`).
    *   Show a loading screen / usage bar.
    *   Download `data.pak` (via `emscripten_fetch` or JS helper) to a temporary location or IDBFS.
2.  **Phase 2 (Full Mount):**
    *   Once `data.pak` is ready, mount it to `/assets` using `ArchiveBackend`.
    *   Signal application start.

```cpp
// Sketch of WASM Bootstrap
void bootstrapApplication() {
    // 1. Show loader
    drawLoadingScreen(0.0f);
    
    // 2. Fetch data.pak
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = [](emscripten_fetch_t *fetch) {
        // 3. Initialize VFS with downloaded data
        auto& vfs = vfs::FileSystem::instance();
        vfs.mount("/assets", 
                  std::make_shared<ArchiveBackend>(
                      std::span{reinterpret_cast<uint8_t*>(fetch->data), fetch->numBytes}
                  ));
                  
        // 4. Start Game
        enterGameLoop();
        
        emscripten_fetch_close(fetch);
    };
    
    emscripten_fetch(&attr, "data.pak");
}
```

---

## 7. Usage Examples

### 7.1 Shader Loading

**Before (current):**
```cpp
bool loadShader(const std::filesystem::path& shaderPath) {
    std::ifstream file(shaderPath);
    if (!file) {
        LOG_ERROR("Failed to open shader: {}", shaderPath.string());
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    // ... use source
}
```

**After (with VFS):**
```cpp
bool loadShader(const vfs::Path& shaderPath) {
    auto result = vfs::FileSystem::instance().readTextFile(shaderPath);
    if (!result) {
        LOG_ERROR("Failed to open shader: {}", result.error().format());
        return false;
    }
    std::string source = std::move(*result);
    // ... use source
}
```

### 7.2 Binary Asset Loading

**Before:**
```cpp
std::vector<uint8_t> loadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    
    auto size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}
```

**After:**
```cpp
Result<std::vector<uint8_t>> loadBinaryFile(const vfs::Path& path) {
    return vfs::FileSystem::instance().readFile(path);
}
```

### 7.3 Checking File Existence

**Before:**
```cpp
if (!std::filesystem::exists(path)) {
    // May not work correctly on WASM
}
```

**After:**
```cpp
auto exists = vfs::FileSystem::instance().exists(path);
if (!exists || !*exists) {
    // Works correctly on both platforms
}
```

---

## 8. Migration Strategy

### 8.1 Phase 1: Core VFS Implementation

1. Implement `vfs::Path` class
2. Implement `vfs::FileSystem` API with mount points
3. Implement `NativeBackend` for native development
4. Implement `EmscriptenBackend` for WASM preloaded assets
5. **Implement `ArchiveBackend`** for production asset loading
6. Add unit tests for all components

> [!IMPORTANT]
> **ArchiveBackend is Phase 1**, not a future enhancement. Without it:
> - Native builds suffer from OS file handle overhead (1000+ open/close calls)
> - WASM builds can't support DLC/patching without full re-download
> - Directory listing is slow with many loose files

### 8.2 Phase 2: Shader Loading Migration

1. Update shader loading to use VFS
2. Update `mip_pipeline.cpp` and render paths
3. Verify shaders load on both platforms
4. Test with both loose files (dev) and archive (release)

### 8.3 Phase 3: Config and Asset Migration

1. Migrate `config.cpp` to use VFS
2. Migrate heightmap loading
3. Migrate texture loading
4. Migrate compression utilities
5. Create `assets.pak` build step

### 8.4 Phase 4: Full Integration

1. Remove direct `std::ifstream` usage from application code
2. Update all `std::filesystem::exists` calls
3. Add hot-reload support for development (falls back to NativeBackend)
4. Performance testing: loose files vs archive
5. Integrate archive creation into CMake/Bazel build

---

## 9. Directory Structure

```
src/
├── engine/
│   ├── vfs/
│   │   ├── vfs.hpp              # Public API header (includes all below)
│   │   ├── path.hpp             # vfs::Path
│   │   ├── path.cpp
│   │   ├── file.hpp             # vfs::File
│   │   ├── file.cpp
│   │   ├── filesystem.hpp       # vfs::FileSystem
│   │   ├── filesystem.cpp
│   │   ├── error.hpp            # vfs::Error, vfs::Result
│   │   ├── backend.hpp          # vfs::Backend interface
│   │   ├── buffer.hpp           # vfs::Buffer
│   │   ├── buffer.cpp
│   │   ├── backends/
│   │   │   ├── native.hpp       # Phase 1
│   │   │   ├── native.cpp
│   │   │   ├── emscripten_memfs.hpp   # Phase 1
│   │   │   ├── emscripten_memfs.cpp
│   │   │   ├── emscripten_fetch.hpp   # Phase 1 (async-only)
│   │   │   ├── emscripten_fetch.cpp
│   │   │   ├── archive.hpp      # Phase 1 (miniz-based)
│   │   │   ├── archive.cpp
│   │   │   ├── http.hpp         # Phase 2+
│   │   │   ├── http.cpp
│   │   │   └── embedded.hpp     # Future
│   │   ├── BUILD                # Bazel build file
│   │   └── CMakeLists.txt       # CMake build file
│   └── platform/                # Existing platform abstraction
│       └── ...

third_party/
├── miniz/                   # Single-header ZIP library
│   ├── miniz.h              # https://github.com/richgel999/miniz
│   └── BUILD                # Bazel cc_library
├── tl_expected/             # Result<T, E> implementation
│   ├── expected.hpp
│   └── BUILD
```

> [!NOTE]
> **Include path:** `#include "engine/vfs/file.hpp"` provides clear context that VFS
> is a foundational engine subsystem, not application-level code.

---

## 10. Testing

### 10.1 Unit Tests

| Test Suite | Coverage |
|------------|----------|
| `PathTest` | Normalization, joining, parent, extension extraction |
| `NativeBackendTest` | File operations on local filesystem |
| `FileSystemTest` | Mount points, path resolution, convenience methods |

### 10.2 Integration Tests

| Test | Description |
|------|-------------|
| `ShaderLoadTest` | Load all shaders via VFS |
| `HeightmapLoadTest` | Load heightmap formats via VFS |
| `CrossPlatformTest` | Verify identical behavior on native vs WASM |

### 10.3 Platform-Specific Tests

| Platform | Test |
|----------|------|
| Native | Hot-reload detection, write operations |
| WASM | Preloaded asset access, virtual path resolution |

---

## 11. Performance Considerations

### 11.1 Overhead Analysis

| Operation | Expected Overhead |
|-----------|-------------------|
| Path normalization | Negligible (string operations) |
| Mount point lookup | O(log n) where n = mount points |
| File open | Backend-specific (native ~same as `fopen`) |
| File read | Zero-copy possible with `std::span` |

### 11.2 Optimization Strategies

1. **Path Caching** — Cache normalized paths to avoid repeated parsing
2. **Mount Point Caching** — Cache mount point resolution for known paths
3. **Memory Mapping** — Use `mmap` in native backend for large files
4. **Async Prefetch** — Prefetch commonly-used assets on background thread

---

## 12. WASM Memory Limits & Backpressure

WebAssembly has constrained memory and network characteristics that require careful management.

### 12.1 Memory Constraints

| Constraint | Value | Notes |
|------------|-------|-------|
| **Max Linear Memory** | 2 GB | Emscripten default max |
| **Initial Memory** | 256 MB | Configured in build |
| **Stack Size** | 1 MB | Limited recursion depth |
| **Asset Budget** | ~512 MB | Leave headroom for runtime |
| **Single File Max** | 128 MB | Prevent OOM on large files |

### 12.2 Chunked Read API

Large files must be read in chunks to avoid memory pressure:

```cpp
namespace vfs {

struct ChunkedReadConfig {
    size_t chunkSize = 1 * 1024 * 1024;    // 1 MB per chunk
    size_t maxMemoryUsage = 64 * 1024 * 1024;  // 64 MB max buffered
    bool allowPartialResults = false;       // Return partial on OOM
};

class ChunkedReader {
public:
    explicit ChunkedReader(const Path& path, ChunkedReadConfig config = {});
    
    // Read next chunk (returns empty span when done)
    [[nodiscard]] Result<std::span<const uint8_t>> readChunk();
    
    // Stream directly to callback (avoids buffering)
    using ChunkCallback = std::function<VoidResult(std::span<const uint8_t>)>;
    [[nodiscard]] VoidResult streamTo(ChunkCallback callback);
    
    // Progress tracking
    [[nodiscard]] size_t bytesRead() const;
    [[nodiscard]] size_t totalSize() const;
    [[nodiscard]] float progress() const;  // 0.0 - 1.0
    
    // Control
    void cancel();
    [[nodiscard]] bool isCancelled() const;
    
    // Buffer management (for advanced use)
    void setExternalBuffer(std::span<uint8_t> buffer);  // Reuse buffer across reads
    
private:
    Path path_;
    ChunkedReadConfig config_;
    size_t offset_ = 0;
    
    // Internal buffer - reused across readChunk() calls to prevent fragmentation
    std::vector<uint8_t> buffer_;
    bool ownsBuffer_ = true;  // false if setExternalBuffer() was called
    
    bool cancelled_ = false;
};

} // namespace vfs
```

**Buffer Reuse Pattern:**

```cpp
// Anti-fragmentation: reuse a single buffer for multiple chunked reads
std::vector<uint8_t> reusableBuffer(1024 * 1024);  // 1 MB, allocated once

void loadMultipleFiles(const std::vector<Path>& paths) {
    for (const auto& path : paths) {
        ChunkedReader reader(path);
        reader.setExternalBuffer(reusableBuffer);  // Reuse buffer
        
        while (auto chunk = reader.readChunk()) {
            processChunk(*chunk);
        }
        // buffer not reallocated between files
    }
}
```

### 12.3 In-Flight Request Limits

Concurrent async operations are limited to prevent memory exhaustion:

```cpp
namespace vfs {

struct AsyncConfig {
    // Maximum concurrent fetch/read operations
    size_t maxInFlightRequests = 4;
    
    // Maximum total bytes being fetched concurrently
    size_t maxInFlightBytes = 32 * 1024 * 1024;  // 32 MB
    
    // Queue behavior when limits exceeded
    enum class QueuePolicy {
        Block,      // Wait for slot (sync callers block, async callers queue)
        Reject,     // Return ErrorCode::QueueFull immediately
        DropOldest, // Cancel oldest request to make room
    };
    QueuePolicy policy = QueuePolicy::Block;
    
    // Request timeout (0 = no timeout)
    std::chrono::milliseconds timeout{30000};  // 30 seconds
};

} // namespace vfs
```

### 12.4 Backpressure Mechanism

The VFS automatically applies backpressure when approaching memory limits:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Backpressure State Machine                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   [NORMAL]  ─── memory > 70% ───►  [THROTTLED]                  │
│      ▲                                  │                       │
│      │                                  ▼                       │
│      └────── memory < 50% ────────  [Process queue slowly]      │
│                                         │                       │
│   [NORMAL]  ◄── memory < 30% ───────────┘                       │
│                                                                 │
│   [THROTTLED] ─── memory > 85% ───►  [PAUSED]                   │
│                                          │                      │
│                                          ▼                      │
│                                    [Reject new requests]        │
│                                    [Wait for memory free]       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Memory Thresholds:**

| State | Memory Usage | Behavior |
|-------|--------------|----------|
| **NORMAL** | < 70% | Full speed, no limits |
| **THROTTLED** | 70-85% | Delay new requests, process queue slowly |
| **PAUSED** | > 85% | Reject new requests, wait for memory to free |

```cpp
namespace vfs {

class MemoryPressureMonitor {
public:
    enum class PressureLevel {
        Normal,     // < 70% - full speed
        Throttled,  // 70-85% - slow down
        Paused,     // > 85% - reject new requests
    };
    
    [[nodiscard]] static PressureLevel currentLevel();
    [[nodiscard]] static size_t availableMemory();
    [[nodiscard]] static size_t usedMemory();
    [[nodiscard]] static float usageRatio();  // 0.0 - 1.0
    
    // Register callback for pressure changes
    using PressureCallback = std::function<void(PressureLevel)>;
    static void onPressureChange(PressureCallback callback);
    
    // Force garbage collection (WASM-specific)
    static void requestGC();
};

} // namespace vfs
```

### 12.5 Tile Streaming Considerations

For future terrain tile streaming, apply specialized limits:

| Parameter | Value | Rationale |
|-----------|-------|----------|
| **Tile Size** | 256×256 or 512×512 | Balance granularity vs overhead |
| **Max Cached Tiles** | 64-128 | ~128-512 MB at 16-bit |
| **Prefetch Distance** | 2-3 tiles | Smooth streaming |
| **Max Concurrent Loads** | 2 | Prevent network saturation |
| **Load Priority** | Distance-based | Closest tiles first |

```cpp
namespace vfs {

struct TileStreamConfig {
    size_t maxCachedTiles = 64;
    size_t maxConcurrentLoads = 2;
    size_t prefetchDistance = 2;  // Tiles ahead of camera
    size_t tileSizeBytes = 256 * 256 * 2;  // 128 KB per tile
    
    // LRU eviction when cache full
    bool enableLruEviction = true;
    
    // Priority function (higher = more urgent)
    using PriorityFunc = std::function<float(const Path& tilePath)>;
    PriorityFunc priorityFunc = nullptr;  // Default: FIFO
};

} // namespace vfs
```

---

## 13. Async File Operations

Async API for non-blocking file access, essential for WASM where blocking is prohibited.

### 13.1 Core Async API

```cpp
namespace vfs {

// Async request handle
class AsyncRequest {
public:
    // Status
    [[nodiscard]] bool isDone() const;
    [[nodiscard]] bool isCancelled() const;
    [[nodiscard]] bool hasError() const;
    
    // Progress (for streaming/chunked reads)
    [[nodiscard]] float progress() const;  // 0.0 - 1.0
    [[nodiscard]] size_t bytesLoaded() const;
    [[nodiscard]] size_t totalBytes() const;
    
    // Control
    void cancel();
    
    // Wait (blocks - native only, no-op on WASM)
    void wait();
    
private:
    std::shared_ptr<AsyncRequestImpl> impl_;
};

// Callback types
using AsyncCallback = std::function<void(Result<BufferPtr>)>;
using AsyncTextCallback = std::function<void(Result<std::string>)>;
using AsyncProgressCallback = std::function<void(size_t bytesLoaded, size_t totalBytes)>;

// Async file operations on FileSystem
class FileSystem {
public:
    // ... existing sync methods ...
    
    // Async read (callback on completion)
    [[nodiscard]] AsyncRequest readFileAsync(
        const Path& path, 
        AsyncCallback callback,
        AsyncProgressCallback progressCallback = nullptr);
    
    [[nodiscard]] AsyncRequest readTextFileAsync(
        const Path& path, 
        AsyncTextCallback callback);
    
    // Batch async reads (optimized for multiple files)
    [[nodiscard]] std::vector<AsyncRequest> readFilesAsync(
        std::span<const Path> paths,
        std::function<void(const Path&, Result<BufferPtr>)> callback);
    
    // Async queue management
    [[nodiscard]] size_t pendingRequests() const;
    [[nodiscard]] size_t inFlightBytes() const;
    void cancelAllPending();
};

} // namespace vfs
```

### 13.2 WASM-Specific Async (Emscripten)

```cpp
namespace vfs {

// Uses Emscripten's asyncify or fetch API
class EmscriptenAsyncBackend {
public:
    // Non-blocking fetch using JavaScript fetch API
    void fetchAsync(const Path& url, AsyncCallback callback);
    
    // Progress tracking via XHR
    void fetchWithProgress(const Path& url, 
                           AsyncCallback callback,
                           AsyncProgressCallback progress);
    
    // Abort controller for cancellation
    void abort(uint32_t requestId);
};

} // namespace vfs
```

---

## 14. HTTP Fetch Backend

Network backend for loading assets via HTTP/HTTPS.

### 14.1 Configuration

```cpp
namespace vfs {

struct HttpBackendConfig {
    std::string baseUrl;                      // e.g., "https://cdn.example.com/assets"
    std::chrono::milliseconds timeout{30000}; // Request timeout
    size_t maxRetries = 3;                    // Retry on transient failures
    std::chrono::milliseconds retryDelay{1000};
    
    // Headers for all requests
    std::vector<std::pair<std::string, std::string>> headers;
    
    // Caching Strategy
    // WARNING: On WASM, application memory is scarce (limited linear memory).
    // Large assets (terrain) should rely on the Browser HTTP Cache where possible.
    
    bool enableMemoryCache = true;            // Store in RAM (fastest, uses heap)
    size_t maxMemoryCacheSize = 64 * 1024 * 1024; // 64 MB Strict LRU limit
    float evictionThreshold = 0.8f;           // Evict until usage < 80% of max
    
    // Browser Cache Control
    // If true, requests allow the browser to serve from its disk cache.
    // If false, adds cache-busting headers (e.g., ?_t=<timestamp>).
    // 
    // Set to false ONLY when:
    //   - Development hot-reload (content changes frequently with same URL)
    //   - Content is known to be stale and must be re-fetched
    //   - Debugging cache-related issues
    bool useBrowserCache = true;
    
    // Backpressure
    size_t maxConcurrentRequests = 4;
    size_t maxQueuedRequests = 32;
};

} // namespace vfs
```

### 14.2 Backend Implementation

```cpp
namespace vfs {

class HttpBackend : public Backend {
public:
    explicit HttpBackend(HttpBackendConfig config);
    
    // Sync operations (block on WASM - use async preferred)
    Result<std::unique_ptr<FileImpl>> open(const Path& path) override;
    Result<bool> exists(const Path& path) override;
    Result<size_t> fileSize(const Path& path) override;
    
    // Directory listing (requires server support)
    Result<std::vector<Path>> listDirectory(const Path& path) override;
    Result<bool> isDirectory(const Path& path) override;
    
    // Async operations (preferred for network)
    AsyncRequest fetchAsync(const Path& path, AsyncCallback callback);
    
    // Cache management
    void clearCache();
    [[nodiscard]] size_t cacheSize() const;
    [[nodiscard]] bool isCached(const Path& path) const;
    void prefetch(std::span<const Path> paths);  // Background fetch
    
    // Observability
    struct CacheStats {
        size_t hits = 0;              // Served from memory cache
        size_t misses = 0;            // Required network fetch
        size_t evictions = 0;         // Entries removed by LRU
        size_t currentBytes = 0;      // Current cache usage
        size_t peakBytes = 0;         // High-water mark
        size_t dedupedRequests = 0;   // Requests that piggybacked on in-flight
    };
    [[nodiscard]] CacheStats cacheStats() const;
    void resetCacheStats();
    
    std::string_view name() const override { return "HttpBackend"; }
    bool isWritable() const override { return false; }
    
private:
    HttpBackendConfig config_;
    
    // LRU Cache Entry
    struct CacheEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point lastUsed;
        size_t size() const { return data.size(); }
    };
    
    // RAM Cache with LRU metadata
    std::unordered_map<std::string, CacheEntry> cache_;
    size_t currentCacheUsage_ = 0;
    CacheStats stats_;
    
    // Enforce LRU policy: evict until usage < evictionThreshold * maxMemoryCacheSize
    void pruneCache();
    void updateLru(const std::string& key);

    std::queue<AsyncRequest> pendingQueue_;
    std::atomic<size_t> inFlightCount_{0};
    std::atomic<size_t> inFlightBytes_{0};
};

} // namespace vfs
```

### 14.3 URL Resolution

```cpp
// Path to URL mapping
vfs::Path path{"/assets/terrain/tile_0_0.ldh"};

// With baseUrl = "https://cdn.example.com/v1"
// Resolves to: https://cdn.example.com/v1/assets/terrain/tile_0_0.ldh
```

### 14.4 Error Handling

| HTTP Status | ErrorCode | Behavior |
|-------------|-----------|----------|
| 200 | Success | Return data |
| 304 | Success | Return cached |
| 404 | `NotFound` | No retry |
| 403 | `PermissionDenied` | No retry |
| 500-599 | `NetworkError` | Retry with backoff |
| Timeout | `Timeout` | Retry |
| Network error | `NetworkError` | Retry |

### 14.5 WASM Caching Strategy

> [!CAUTION]
> **WASM Heap vs. Browser Cache:**
> WebAssembly has a strict heap limit (default 2GB, max 4GB). Caching streamed terrain tiles 
> indiscriminately in `std::unordered_map` will cause **Out of Memory (OOM)** crashes.

**Implementation Rules:**

1.  **LRU Eviction with Soft Threshold:** The `HttpBackend` MUST implement a Least Recently Used eviction policy. When `currentCacheUsage_ > maxMemoryCacheSize`, evict entries until usage drops below `evictionThreshold * maxMemoryCacheSize` (default 80%). This prevents thrashing when the cache is near capacity.
2.  **Browser Cache First:** For large static assets (terrain tiles, textures), configure the CDN/Server with aggressive `Cache-Control` headers (e.g., `max-age=31536000, immutable`). 
    *   Fetching a file that is in the *Browser's* disk cache is nearly as fast as RAM but costs **zero WASM heap** until loaded.
    *   Set `HttpBackendConfig::useBrowserCache = true`.
3.  **Deduplication:** Ensure multiple async requests for the same URL piggyback on a single fetch operation to avoid duplicate memory usage during transit.
4.  **Observability:** Expose `CacheStats` for debugging memory issues in production. Key metrics:
    *   `hits / (hits + misses)` — Cache hit rate (target: >90% for terrain)
    *   `evictions` — High eviction counts indicate `maxMemoryCacheSize` is too small
    *   `peakBytes` — Helps tune cache size based on real-world usage

---

## 15. File Watching (Hot Reload)

> [!NOTE]
> **FileWatcher is conditionally compiled.** It's only available when:
> - `VOXY_NATIVE` is defined (not on WASM)
> - `VOXY_TOOLS` is defined (development builds only)
>
> This keeps the shipping game client lean.

```cpp
#if defined(VOXY_NATIVE) && defined(VOXY_TOOLS)

namespace vfs {

enum class WatchEvent {
    Created,
    Modified,
    Deleted,
    Renamed,
};

using WatchCallback = std::function<void(const Path&, WatchEvent)>;

class FileWatcher {
public:
    // Watch a file or directory (native only)
    [[nodiscard]] VoidResult watch(const Path& path, WatchCallback callback);
    void unwatch(const Path& path);
    void unwatchAll();
    
    // Poll for changes (call each frame or periodically)
    void poll();
    
    // Check if watching is supported on this platform
    [[nodiscard]] static constexpr bool isSupported() { return true; }
    
private:
    // Platform-specific implementation
    #if defined(__linux__)
        int inotifyFd_ = -1;
    #elif defined(__APPLE__)
        // FSEvents / kqueue
    #elif defined(_WIN32)
        // ReadDirectoryChangesW
    #endif
    
    std::unordered_map<std::string, WatchCallback> watches_;
};

} // namespace vfs

#else  // !VOXY_NATIVE || !VOXY_TOOLS

namespace vfs {

// Stub implementation for non-native or shipping builds
class FileWatcher {
public:
    [[nodiscard]] VoidResult watch(const Path&, std::function<void(const Path&, int)>) {
        return tl::unexpected(Error{ErrorCode::NotSupported, "FileWatcher disabled"});
    }
    void unwatch(const Path&) {}
    void unwatchAll() {}
    void poll() {}
    [[nodiscard]] static constexpr bool isSupported() { return false; }
};

} // namespace vfs

#endif
```

---

## 16. Development vs Production Asset Loading

The VFS supports different mount configurations for development and production:

### 16.1 Development Mode

```cpp
void initVfsDevelopment() {
    auto& vfs = vfs::FileSystem::instance();
    
    // Mount loose files for hot-reload
    vfs.mount("/shaders", std::make_unique<NativeBackend>("shaders/"));
    vfs.mount("/assets", std::make_unique<NativeBackend>("assets/"));
    
    // Enable file watching
    vfs.enableHotReload(true);
}
```

### 16.2 Production Mode

```cpp
void initVfsProduction() {
    auto& vfs = vfs::FileSystem::instance();
    
    // Mount single archive for all assets
    vfs.mount("/", std::make_unique<ArchiveBackend>("data.pak"));
    
    // Optional: HTTP backend for DLC
    vfs.mount("/dlc", std::make_unique<HttpBackend>("https://cdn.example.com/dlc/"));
}
```

### 16.3 Build Integration

**CMake:**

```cmake
# CMakeLists.txt
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/data.pak
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
            zip -r -0 ${CMAKE_BINARY_DIR}/data.pak shaders assets
    DEPENDS ${SHADER_FILES} ${ASSET_FILES}
    COMMENT "Creating data.pak archive"
)

add_custom_target(data_pak DEPENDS ${CMAKE_BINARY_DIR}/data.pak)
add_dependencies(voxy_native data_pak)
```

**Bazel:**

```python
# BUILD.bazel
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

pkg_zip(
    name = "data_pak",
    srcs = [
        "//shaders:all_shaders",
        "//assets:all_assets",
    ],
    out = "data.pak",
    strip_prefix = ".",
)

# Or using genrule for more control:
genrule(
    name = "data_pak_genrule",
    srcs = glob(["shaders/**", "assets/**"]),
    outs = ["data.pak"],
    cmd = "cd $(RULEDIR) && zip -r -0 $@ shaders assets",
)

# VFS library (under src/engine/)
cc_library(
    name = "vfs",
    srcs = glob(["src/engine/vfs/**/*.cpp"]),
    hdrs = glob(["src/engine/vfs/**/*.hpp"]),
    deps = [
        "//third_party/miniz",
        "//third_party/tl_expected",
    ],
    defines = select({
        "//conditions:default": ["VOXY_NATIVE"],
        "//:wasm": [],
    }) + select({
        "//:debug": ["VOXY_TOOLS"],
        "//conditions:default": [],
    }),
)
```

**Bazel miniz dependency:**

```python
# third_party/miniz/BUILD.bazel
cc_library(
    name = "miniz",
    hdrs = ["miniz.h"],
    defines = ["MINIZ_NO_STDIO"],  # WASM-safe
    visibility = ["//visibility:public"],
)
```

