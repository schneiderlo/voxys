#include "game/assets/cooked_part_directory.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#if defined(__unix__) || defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voxy::game::assets {
namespace {
#if defined(__unix__) || defined(__EMSCRIPTEN__)
struct Descriptor {
    int value = -1;
    explicit Descriptor(int descriptor) : value(descriptor) {}
    ~Descriptor() { if (value >= 0) static_cast<void>(::close(value)); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
};

bool leafName(std::string_view name) {
    return !name.empty() && name.size() <= 96 && name.front() != '.'
        && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        });
}

std::optional<std::vector<uint8_t>> readLeaf(
    int root, std::string_view filename, size_t maximum, std::string& error) {
    error.clear();
    if (!leafName(filename) || maximum == 0 || maximum > 64u * 1024u * 1024u) {
        error = "invalid package leaf name/read capacity";
        return std::nullopt;
    }
    const std::string name(filename);
    const Descriptor file(::openat(root, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (file.value < 0) { error = "cannot open package leaf without following links"; return std::nullopt; }
    struct stat info{};
    if (::fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0
        || static_cast<uint64_t>(info.st_size) > maximum) {
        error = "package leaf is not a nonempty bounded regular file";
        return std::nullopt;
    }
    const auto size = static_cast<size_t>(info.st_size);
    std::vector<uint8_t> bytes(size);
    size_t readBytes = 0;
    while (readBytes < size) {
        const auto result = ::read(file.value, bytes.data() + readBytes, size - readBytes);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { error = "package leaf changed or failed during read"; return std::nullopt; }
        readBytes += static_cast<size_t>(result);
    }
    uint8_t extra = 0;
    ssize_t more = 0;
    do { more = ::read(file.value, &extra, 1); } while (more < 0 && errno == EINTR);
    struct stat after{};
    if (more != 0 || ::fstat(file.value, &after) != 0 || after.st_size != info.st_size) {
        error = "package leaf changed size or failed at EOF";
        return std::nullopt;
    }
    return bytes;
}
#endif
} // namespace

std::optional<CookedPartByteProvider> openCookedPartDirectory(
    const std::filesystem::path& root, std::string& error) {
    error.clear();
#if defined(__unix__) || defined(__EMSCRIPTEN__)
    const auto text = root.native();
    if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos) {
        error = "invalid package root";
        return std::nullopt;
    }
    constexpr int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK;
    auto current = std::make_shared<Descriptor>(-1);
    current->value = ::open(root.is_absolute() ? "/" : ".", flags);
    if (current->value < 0) { error = "cannot open package base"; return std::nullopt; }
    for (const auto& segment : root) {
        const auto name = segment.native();
        if (name == "/" || name == "." || name.empty()) continue;
        if (name == ".." || name.size() > 255) { error = "package root traversal/component capacity"; return std::nullopt; }
        // Allocate the owner before opening, so allocation failure cannot leak
        // a descriptor returned during argument evaluation.
        auto next = std::make_shared<Descriptor>(-1);
        next->value = ::openat(current->value, name.c_str(), flags);
        if (next->value < 0) { error = "package root component is missing, linked or not a directory"; return std::nullopt; }
        current = std::move(next);
    }
#if defined(__EMSCRIPTEN__)
    std::error_code pathError;
    const auto absolute = std::filesystem::absolute(root, pathError).lexically_normal();
    struct stat admitted{};
    if (pathError || ::fstat(current->value, &admitted) != 0) { error = "cannot identify packaged MEMFS root"; return std::nullopt; }
    return CookedPartByteProvider([current, absolute, admitted](std::string_view name, size_t maximum, std::string& reason) {
        struct stat present{};
        if (::lstat(absolute.c_str(), &present) != 0 || !S_ISDIR(present.st_mode)
            || present.st_dev != admitted.st_dev || present.st_ino != admitted.st_ino) {
            reason = "packaged MEMFS root changed";
            return std::optional<std::vector<uint8_t>>{};
        }
        return readLeaf(current->value, name, maximum, reason);
    });
#else
    return CookedPartByteProvider([current](std::string_view name, size_t maximum, std::string& reason) {
        return readLeaf(current->value, name, maximum, reason);
    });
#endif
#else
    static_cast<void>(root);
    error = "this platform has no verified cooked-part directory adapter";
    return std::nullopt;
#endif
}
} // namespace voxy::game::assets
