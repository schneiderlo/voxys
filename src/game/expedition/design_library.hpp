#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::game::expedition {

inline constexpr size_t kMaximumLibraryDesigns = 32;
inline constexpr size_t kMaximumDesignNameBytes = 96;
inline constexpr size_t kMaximumDesignBlueprintBytes = 128 * 1024;
inline constexpr size_t kMaximumDesignFileBytes = 2 * kMaximumDesignBlueprintBytes + 1024;
inline constexpr size_t kMaximumDesignLibraryBytes = 9 * 1024 * 1024;

// Library IDs/revisions identify user documents only. They are never world,
// physical-part or entitlement IDs. The binary blueprint contains design intent.
struct SavedDesign {
    uint64_t id = 0;
    uint64_t revision = 0;
    std::string name;
    std::vector<std::byte> blueprint;
    bool operator==(const SavedDesign&) const = default;
};
struct SavedDesignRow {
    SavedDesign current;
    std::optional<SavedDesign> backup;
    bool operator==(const SavedDesignRow&) const = default;
};
struct DesignLibraryImage {
    uint64_t nextId = 1;
    std::vector<SavedDesignRow> rows; // Canonical ascending library ID.
    bool operator==(const DesignLibraryImage&) const = default;
};
struct DesignFile {
    std::string name;
    std::vector<std::byte> blueprint;
    bool operator==(const DesignFile&) const = default;
};

[[nodiscard]] bool validDesignName(std::string_view) noexcept;
// Checks the bounded SVBP v1 envelope, size/counts and checksum, but cannot
// establish installed-content/assembly validity. The host must call its existing
// blueprint validator before saving, importing or loading a chosen document.
[[nodiscard]] bool validDesignBlueprintEnvelope(std::span<const std::byte>) noexcept;
[[nodiscard]] std::string designBlueprintHex(std::span<const std::byte>);
[[nodiscard]] bool parseDesignBlueprintHex(std::string_view, std::vector<std::byte>&);

// Exact browser exchange object: {version:1,name,blueprint:lowercaseHex}.
// Failed parsers/encoders preserve the caller's output.
[[nodiscard]] bool parseDesignFile(std::string_view, DesignFile&, std::string& error);
[[nodiscard]] bool encodeDesignFile(const DesignFile&, std::string&, std::string& error);
// SVBL v1 is a native library collection, separate from SVBP/SVCE. The
// generation store atomically publishes the entire current/backup collection.
[[nodiscard]] bool encodeDesignLibrary(const DesignLibraryImage&, std::vector<std::byte>&, std::string& error);
[[nodiscard]] bool decodeDesignLibrary(std::span<const std::byte>, DesignLibraryImage&, std::string& error);

} // namespace voxy::game::expedition
