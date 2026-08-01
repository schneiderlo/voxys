#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace voxy::game {

inline constexpr uint32_t kWreckwaterContentManifestSchemaVersion = 1u;
inline constexpr size_t kWreckwaterContentManifestDigestBytes = 32u;
inline constexpr size_t kWreckwaterContentManifestMaximumEntries = 128u;
inline constexpr size_t kWreckwaterContentManifestMaximumPathBytes = 127u;
inline constexpr uint64_t kWreckwaterContentManifestMaximumEntryBytes =
    512ull * 1024ull * 1024ull;
inline constexpr uint64_t kWreckwaterContentManifestMaximumTotalBytes =
    1024ull * 1024ull * 1024ull;

enum class WreckwaterContentEntryKind : uint32_t {
    SchemaVersion = 1u,
    GameplayRule = 2u,
    AuthoritativeAsset = 3u,
    PresentationAsset = 4u,
};

enum class WreckwaterContentValueType : uint32_t {
    Unsigned32 = 1u,
    Unsigned64 = 2u,
    Float32 = 3u,
    Bytes = 4u,
};

enum class WreckwaterContentManifestError : uint32_t {
    None = 0u,
    InvalidConfiguration,
    AlreadyInitialized,
    NotInitialized,
    AlreadyFinalized,
    InvalidDefinition,
    EntryCapacity,
    InvalidPath,
    DuplicatePath,
    UnknownEntry,
    DefinitionMismatch,
    DuplicateEntry,
    MissingEntry,
    EntryPayloadCapacity,
    TotalPayloadCapacity,
    NonCanonicalFloat,
    ZeroCompatibilityHash,
};

[[nodiscard]] const char* wreckwaterContentManifestErrorName(
    WreckwaterContentManifestError error) noexcept;

struct WreckwaterContentEntryDefinition {
    WreckwaterContentEntryKind kind =
        WreckwaterContentEntryKind::GameplayRule;
    WreckwaterContentValueType valueType =
        WreckwaterContentValueType::Unsigned32;
    std::string_view logicalPath{};
};

struct WreckwaterContentManifestLimits {
    size_t maximumEntries =
        kWreckwaterContentManifestMaximumEntries;
    size_t maximumPathBytes =
        kWreckwaterContentManifestMaximumPathBytes;
    uint64_t maximumEntryPayloadBytes =
        kWreckwaterContentManifestMaximumEntryBytes;
    uint64_t maximumTotalPayloadBytes =
        kWreckwaterContentManifestMaximumTotalBytes;
};

struct WreckwaterContentDigest {
    std::array<
        std::byte, kWreckwaterContentManifestDigestBytes> bytes{};

    [[nodiscard]] bool operator==(
        const WreckwaterContentDigest&) const = default;
};

struct WreckwaterContentManifestResult {
    WreckwaterContentManifestError error =
        WreckwaterContentManifestError::None;
    WreckwaterContentDigest digest{};
    // The replay schema currently has a 64-bit contentHash field. This is the
    // big-endian numeric value of digest bytes [0, 8). It is a truncated
    // compatibility fingerprint, not authentication.
    uint64_t replayContentHash = 0u;
    uint32_t entryCount = 0u;
    uint64_t totalPayloadBytes = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterContentManifestError::None;
    }
};

struct WreckwaterContentManifestStorageState {
    const void* entries = nullptr;
    size_t capacity = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterContentManifestStorageState&) const = default;
};

// A manifest is defined before values are supplied. That definition is the
// allowlist: additions with an unlisted logical path, kind, or value type are
// rejected. Definition and addition order do not affect the final digest.
//
// initialize(), add*(), and finalize() allocate no memory. Asset bytes are
// consumed into a SHA-256 leaf when addBytes() returns; their span is not kept.
class WreckwaterContentManifestBuilder {
public:
    WreckwaterContentManifestBuilder() = default;

    WreckwaterContentManifestBuilder(
        const WreckwaterContentManifestBuilder&) = delete;
    WreckwaterContentManifestBuilder& operator=(
        const WreckwaterContentManifestBuilder&) = delete;
    WreckwaterContentManifestBuilder(
        WreckwaterContentManifestBuilder&&) = delete;
    WreckwaterContentManifestBuilder& operator=(
        WreckwaterContentManifestBuilder&&) = delete;

    [[nodiscard]] WreckwaterContentManifestError initialize(
        std::span<const WreckwaterContentEntryDefinition> definition,
        const WreckwaterContentManifestLimits& limits = {}) noexcept;

    [[nodiscard]] WreckwaterContentManifestError addUnsigned32(
        WreckwaterContentEntryKind kind,
        std::string_view logicalPath,
        uint32_t value) noexcept;
    [[nodiscard]] WreckwaterContentManifestError addUnsigned64(
        WreckwaterContentEntryKind kind,
        std::string_view logicalPath,
        uint64_t value) noexcept;
    [[nodiscard]] WreckwaterContentManifestError addFloat32(
        WreckwaterContentEntryKind kind,
        std::string_view logicalPath,
        float value) noexcept;
    [[nodiscard]] WreckwaterContentManifestError addBytes(
        WreckwaterContentEntryKind kind,
        std::string_view logicalPath,
        std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] WreckwaterContentManifestResult finalize() noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool finalized() const noexcept {
        return finalized_;
    }
    [[nodiscard]] size_t definitionCount() const noexcept {
        return definitionCount_;
    }
    [[nodiscard]] size_t suppliedCount() const noexcept {
        return suppliedCount_;
    }
    [[nodiscard]] uint64_t totalPayloadBytes() const noexcept {
        return totalPayloadBytes_;
    }
    [[nodiscard]] const WreckwaterContentManifestResult& result()
        const noexcept {
        return result_;
    }
    [[nodiscard]] WreckwaterContentManifestStorageState storageState()
        const noexcept;

private:
    struct Entry {
        WreckwaterContentEntryKind kind =
            WreckwaterContentEntryKind::GameplayRule;
        WreckwaterContentValueType valueType =
            WreckwaterContentValueType::Unsigned32;
        std::array<
            char, kWreckwaterContentManifestMaximumPathBytes> path{};
        uint16_t pathBytes = 0u;
        uint64_t payloadBytes = 0u;
        WreckwaterContentDigest leafDigest{};
        bool supplied = false;
    };

    [[nodiscard]] WreckwaterContentManifestError addPayload(
        WreckwaterContentEntryKind kind,
        WreckwaterContentValueType valueType,
        std::string_view logicalPath,
        std::span<const std::byte> payload) noexcept;
    [[nodiscard]] static bool samePath(
        const Entry& entry, std::string_view path) noexcept;
    [[nodiscard]] static bool pathLess(
        const Entry& lhs, const Entry& rhs) noexcept;

    WreckwaterContentManifestLimits limits_{};
    std::array<
        Entry, kWreckwaterContentManifestMaximumEntries> entries_{};
    WreckwaterContentManifestResult result_{};
    size_t definitionCount_ = 0u;
    size_t suppliedCount_ = 0u;
    uint64_t totalPayloadBytes_ = 0u;
    bool initialized_ = false;
    bool finalized_ = false;
};

[[nodiscard]] std::array<
    char, kWreckwaterContentManifestDigestBytes * 2u + 1u>
wreckwaterContentDigestHex(
    const WreckwaterContentDigest& digest) noexcept;

} // namespace voxy::game
