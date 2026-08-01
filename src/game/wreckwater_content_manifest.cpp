#include "game/wreckwater_content_manifest.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace voxy::game {
namespace {

constexpr std::string_view kLeafDomain =
    "VOXY/WRECKWATER/CONTENT-LEAF/V1";
constexpr std::string_view kRootDomain =
    "VOXY/WRECKWATER/CONTENT-MANIFEST/V1";
constexpr std::string_view kEntryDomain =
    "VOXY/WRECKWATER/CONTENT-ENTRY/V1";
constexpr std::string_view kEndDomain =
    "VOXY/WRECKWATER/CONTENT-END/V1";

constexpr std::array<uint32_t, 64> kSha256RoundConstants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

class Sha256 {
public:
    void update(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty()) return;
        totalBytes_ += bytes.size();
        size_t offset = 0u;
        if (blockBytes_ != 0u) {
            const size_t copied = std::min(
                bytes.size(), block_.size() - blockBytes_);
            std::memcpy(
                block_.data() + blockBytes_,
                bytes.data(),
                copied);
            blockBytes_ += copied;
            offset += copied;
            if (blockBytes_ == block_.size()) {
                transform(block_);
                blockBytes_ = 0u;
            }
        }
        while (bytes.size() - offset >= block_.size()) {
            transform(bytes.subspan(offset, block_.size()));
            offset += block_.size();
        }
        const size_t remaining = bytes.size() - offset;
        if (remaining != 0u) {
            std::memcpy(
                block_.data(), bytes.data() + offset, remaining);
            blockBytes_ = remaining;
        }
    }

    void string(std::string_view text) noexcept {
        update(std::as_bytes(
            std::span<const char>(text.data(), text.size())));
    }

    void u32Little(uint32_t value) noexcept {
        std::array<std::byte, sizeof(uint32_t)> bytes{};
        for (uint32_t index = 0u; index < 4u; ++index) {
            bytes[index] = std::byte{
                static_cast<uint8_t>(value >> (index * 8u))};
        }
        update(bytes);
    }

    void u64Little(uint64_t value) noexcept {
        std::array<std::byte, sizeof(uint64_t)> bytes{};
        for (uint32_t index = 0u; index < 8u; ++index) {
            bytes[index] = std::byte{
                static_cast<uint8_t>(value >> (index * 8u))};
        }
        update(bytes);
    }

    [[nodiscard]] WreckwaterContentDigest finish() noexcept {
        const uint64_t bitLength = totalBytes_ * 8u;
        std::array<std::byte, 128> padding{};
        padding[0] = std::byte{0x80u};
        const size_t paddingBytes =
            blockBytes_ < 56u
            ? 56u - blockBytes_
            : 120u - blockBytes_;
        update(std::span<const std::byte>(
            padding.data(), paddingBytes));

        std::array<std::byte, 8> encodedLength{};
        for (uint32_t index = 0u; index < 8u; ++index) {
            encodedLength[index] = std::byte{
                static_cast<uint8_t>(
                    bitLength >> ((7u - index) * 8u))};
        }
        update(encodedLength);

        WreckwaterContentDigest result;
        for (size_t word = 0u; word < state_.size(); ++word) {
            for (uint32_t byte = 0u; byte < 4u; ++byte) {
                result.bytes[word * 4u + byte] = std::byte{
                    static_cast<uint8_t>(
                        state_[word] >> ((3u - byte) * 8u))};
            }
        }
        return result;
    }

private:
    static uint32_t loadBigEndian(
        std::span<const std::byte> bytes, size_t offset) noexcept {
        uint32_t value = 0u;
        for (uint32_t index = 0u; index < 4u; ++index) {
            value = (value << 8u)
                | std::to_integer<uint32_t>(
                    bytes[offset + index]);
        }
        return value;
    }

    void transform(std::span<const std::byte> block) noexcept {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0u; index < 16u; ++index) {
            words[index] = loadBigEndian(block, index * 4u);
        }
        for (size_t index = 16u; index < words.size(); ++index) {
            const uint32_t x = words[index - 15u];
            const uint32_t y = words[index - 2u];
            const uint32_t small0 =
                std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3u);
            const uint32_t small1 =
                std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10u);
            words[index] =
                words[index - 16u] + small0
                + words[index - 7u] + small1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];
        for (size_t index = 0u; index < words.size(); ++index) {
            const uint32_t big1 =
                std::rotr(e, 6) ^ std::rotr(e, 11)
                ^ std::rotr(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t first =
                h + big1 + choose
                + kSha256RoundConstants[index] + words[index];
            const uint32_t big0 =
                std::rotr(a, 2) ^ std::rotr(a, 13)
                ^ std::rotr(a, 22);
            const uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const uint32_t second = big0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    }};
    std::array<std::byte, 64> block_{};
    uint64_t totalBytes_ = 0u;
    size_t blockBytes_ = 0u;
};

template <typename Enum>
[[nodiscard]] constexpr uint32_t enumWord(Enum value) noexcept {
    static_assert(std::is_enum_v<Enum>);
    return static_cast<uint32_t>(value);
}

[[nodiscard]] bool validEntryKind(
    WreckwaterContentEntryKind kind) noexcept {
    switch (kind) {
        case WreckwaterContentEntryKind::SchemaVersion:
        case WreckwaterContentEntryKind::GameplayRule:
        case WreckwaterContentEntryKind::AuthoritativeAsset:
        case WreckwaterContentEntryKind::PresentationAsset:
            return true;
    }
    return false;
}

[[nodiscard]] bool validValueType(
    WreckwaterContentValueType type) noexcept {
    switch (type) {
        case WreckwaterContentValueType::Unsigned32:
        case WreckwaterContentValueType::Unsigned64:
        case WreckwaterContentValueType::Float32:
        case WreckwaterContentValueType::Bytes:
            return true;
    }
    return false;
}

[[nodiscard]] bool validKindAndType(
    WreckwaterContentEntryKind kind,
    WreckwaterContentValueType type) noexcept {
    if (!validEntryKind(kind) || !validValueType(type)) return false;
    switch (kind) {
        case WreckwaterContentEntryKind::SchemaVersion:
            return type == WreckwaterContentValueType::Unsigned32;
        case WreckwaterContentEntryKind::GameplayRule:
            return type == WreckwaterContentValueType::Unsigned32
                || type == WreckwaterContentValueType::Unsigned64
                || type == WreckwaterContentValueType::Float32;
        case WreckwaterContentEntryKind::AuthoritativeAsset:
        case WreckwaterContentEntryKind::PresentationAsset:
            return type == WreckwaterContentValueType::Bytes;
    }
    return false;
}

[[nodiscard]] bool validPathCharacter(char value) noexcept {
    return (value >= 'a' && value <= 'z')
        || (value >= '0' && value <= '9')
        || value == '-' || value == '_' || value == '.'
        || value == '/';
}

[[nodiscard]] bool canonicalPath(
    std::string_view path, size_t maximumBytes) noexcept {
    if (path.empty() || path.size() > maximumBytes
        || path.front() == '/' || path.back() == '/') {
        return false;
    }
    size_t segmentStart = 0u;
    for (size_t index = 0u; index < path.size(); ++index) {
        if (!validPathCharacter(path[index])) return false;
        if (path[index] != '/') continue;
        if (index == segmentStart) return false;
        const std::string_view segment =
            path.substr(segmentStart, index - segmentStart);
        if (segment == "." || segment == "..") return false;
        segmentStart = index + 1u;
    }
    const std::string_view finalSegment =
        path.substr(segmentStart);
    return finalSegment != "." && finalSegment != "..";
}

[[nodiscard]] bool validLimits(
    const WreckwaterContentManifestLimits& limits) noexcept {
    return limits.maximumEntries != 0u
        && limits.maximumEntries
            <= kWreckwaterContentManifestMaximumEntries
        && limits.maximumPathBytes != 0u
        && limits.maximumPathBytes
            <= kWreckwaterContentManifestMaximumPathBytes
        && limits.maximumEntryPayloadBytes != 0u
        && limits.maximumEntryPayloadBytes
            <= kWreckwaterContentManifestMaximumEntryBytes
        && limits.maximumTotalPayloadBytes != 0u
        && limits.maximumTotalPayloadBytes
            <= kWreckwaterContentManifestMaximumTotalBytes
        && limits.maximumEntryPayloadBytes
            <= limits.maximumTotalPayloadBytes;
}

[[nodiscard]] WreckwaterContentDigest leafDigest(
    WreckwaterContentValueType valueType,
    std::span<const std::byte> payload) noexcept {
    Sha256 hash;
    hash.string(kLeafDomain);
    hash.u32Little(enumWord(valueType));
    hash.u64Little(payload.size());
    hash.update(payload);
    return hash.finish();
}

[[nodiscard]] uint64_t replayHash(
    const WreckwaterContentDigest& digest) noexcept {
    uint64_t value = 0u;
    for (size_t index = 0u; index < sizeof(uint64_t); ++index) {
        value = (value << 8u)
            | std::to_integer<uint64_t>(digest.bytes[index]);
    }
    return value;
}

} // namespace

bool WreckwaterContentManifestBuilder::samePath(
    const Entry& entry, std::string_view path) noexcept {
    return size_t{entry.pathBytes} == path.size()
        && std::equal(
            path.begin(), path.end(), entry.path.begin());
}

bool WreckwaterContentManifestBuilder::pathLess(
    const Entry& lhs, const Entry& rhs) noexcept {
    const size_t shared = std::min(
        size_t{lhs.pathBytes}, size_t{rhs.pathBytes});
    for (size_t index = 0u; index < shared; ++index) {
        const auto left =
            static_cast<unsigned char>(lhs.path[index]);
        const auto right =
            static_cast<unsigned char>(rhs.path[index]);
        if (left != right) return left < right;
    }
    if (lhs.pathBytes != rhs.pathBytes) {
        return lhs.pathBytes < rhs.pathBytes;
    }
    return enumWord(lhs.kind) < enumWord(rhs.kind);
}

const char* wreckwaterContentManifestErrorName(
    WreckwaterContentManifestError error) noexcept {
    switch (error) {
        case WreckwaterContentManifestError::None:
            return "none";
        case WreckwaterContentManifestError::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterContentManifestError::AlreadyInitialized:
            return "already initialized";
        case WreckwaterContentManifestError::NotInitialized:
            return "not initialized";
        case WreckwaterContentManifestError::AlreadyFinalized:
            return "already finalized";
        case WreckwaterContentManifestError::InvalidDefinition:
            return "invalid definition";
        case WreckwaterContentManifestError::EntryCapacity:
            return "entry capacity";
        case WreckwaterContentManifestError::InvalidPath:
            return "invalid path";
        case WreckwaterContentManifestError::DuplicatePath:
            return "duplicate path";
        case WreckwaterContentManifestError::UnknownEntry:
            return "unknown entry";
        case WreckwaterContentManifestError::DefinitionMismatch:
            return "definition mismatch";
        case WreckwaterContentManifestError::DuplicateEntry:
            return "duplicate entry";
        case WreckwaterContentManifestError::MissingEntry:
            return "missing entry";
        case WreckwaterContentManifestError::EntryPayloadCapacity:
            return "entry payload capacity";
        case WreckwaterContentManifestError::TotalPayloadCapacity:
            return "total payload capacity";
        case WreckwaterContentManifestError::NonCanonicalFloat:
            return "noncanonical float";
        case WreckwaterContentManifestError::ZeroCompatibilityHash:
            return "zero compatibility hash";
    }
    return "unknown";
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::initialize(
    std::span<const WreckwaterContentEntryDefinition> definition,
    const WreckwaterContentManifestLimits& limits) noexcept {
    if (initialized_) {
        return WreckwaterContentManifestError::AlreadyInitialized;
    }
    if (!validLimits(limits)) {
        return WreckwaterContentManifestError::InvalidConfiguration;
    }
    if (definition.empty()) {
        return WreckwaterContentManifestError::InvalidDefinition;
    }
    if (definition.size() > limits.maximumEntries) {
        return WreckwaterContentManifestError::EntryCapacity;
    }
    for (size_t index = 0u; index < definition.size(); ++index) {
        const WreckwaterContentEntryDefinition& candidate =
            definition[index];
        if (!validKindAndType(
                candidate.kind, candidate.valueType)) {
            return WreckwaterContentManifestError::InvalidDefinition;
        }
        if (!canonicalPath(
                candidate.logicalPath, limits.maximumPathBytes)) {
            return WreckwaterContentManifestError::InvalidPath;
        }
        for (size_t prior = 0u; prior < index; ++prior) {
            if (definition[prior].logicalPath
                == candidate.logicalPath) {
                return WreckwaterContentManifestError::DuplicatePath;
            }
        }
    }

    limits_ = limits;
    entries_ = {};
    for (size_t index = 0u; index < definition.size(); ++index) {
        const WreckwaterContentEntryDefinition& source =
            definition[index];
        Entry& destination = entries_[index];
        destination.kind = source.kind;
        destination.valueType = source.valueType;
        destination.pathBytes =
            static_cast<uint16_t>(source.logicalPath.size());
        std::copy(
            source.logicalPath.begin(),
            source.logicalPath.end(),
            destination.path.begin());
    }
    definitionCount_ = definition.size();
    suppliedCount_ = 0u;
    totalPayloadBytes_ = 0u;
    result_ = {};
    finalized_ = false;
    initialized_ = true;
    return WreckwaterContentManifestError::None;
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::addPayload(
    WreckwaterContentEntryKind kind,
    WreckwaterContentValueType valueType,
    std::string_view logicalPath,
    std::span<const std::byte> payload) noexcept {
    if (!initialized_) {
        return WreckwaterContentManifestError::NotInitialized;
    }
    if (finalized_) {
        return WreckwaterContentManifestError::AlreadyFinalized;
    }
    if (!canonicalPath(logicalPath, limits_.maximumPathBytes)) {
        return WreckwaterContentManifestError::InvalidPath;
    }
    Entry* match = nullptr;
    for (size_t index = 0u; index < definitionCount_; ++index) {
        if (samePath(entries_[index], logicalPath)) {
            match = &entries_[index];
            break;
        }
    }
    if (match == nullptr) {
        return WreckwaterContentManifestError::UnknownEntry;
    }
    if (match->kind != kind || match->valueType != valueType) {
        return WreckwaterContentManifestError::DefinitionMismatch;
    }
    if (match->supplied) {
        return WreckwaterContentManifestError::DuplicateEntry;
    }
    const uint64_t payloadBytes = payload.size();
    if (payloadBytes > limits_.maximumEntryPayloadBytes) {
        return WreckwaterContentManifestError::EntryPayloadCapacity;
    }
    if (payloadBytes
        > limits_.maximumTotalPayloadBytes - totalPayloadBytes_) {
        return WreckwaterContentManifestError::TotalPayloadCapacity;
    }

    match->payloadBytes = payloadBytes;
    match->leafDigest = leafDigest(valueType, payload);
    match->supplied = true;
    totalPayloadBytes_ += payloadBytes;
    ++suppliedCount_;
    return WreckwaterContentManifestError::None;
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::addUnsigned32(
    WreckwaterContentEntryKind kind,
    std::string_view logicalPath,
    uint32_t value) noexcept {
    std::array<std::byte, sizeof(uint32_t)> payload{};
    for (uint32_t index = 0u; index < 4u; ++index) {
        payload[index] = std::byte{
            static_cast<uint8_t>(value >> (index * 8u))};
    }
    return addPayload(
        kind, WreckwaterContentValueType::Unsigned32,
        logicalPath, payload);
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::addUnsigned64(
    WreckwaterContentEntryKind kind,
    std::string_view logicalPath,
    uint64_t value) noexcept {
    std::array<std::byte, sizeof(uint64_t)> payload{};
    for (uint32_t index = 0u; index < 8u; ++index) {
        payload[index] = std::byte{
            static_cast<uint8_t>(value >> (index * 8u))};
    }
    return addPayload(
        kind, WreckwaterContentValueType::Unsigned64,
        logicalPath, payload);
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::addFloat32(
    WreckwaterContentEntryKind kind,
    std::string_view logicalPath,
    float value) noexcept {
    static_assert(sizeof(float) == sizeof(uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    if (!initialized_) {
        return WreckwaterContentManifestError::NotInitialized;
    }
    if (finalized_) {
        return WreckwaterContentManifestError::AlreadyFinalized;
    }
    if (!std::isfinite(value)
        || std::fpclassify(value) == FP_SUBNORMAL) {
        return WreckwaterContentManifestError::NonCanonicalFloat;
    }
    if (value == 0.0f) value = 0.0f;
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    std::array<std::byte, sizeof(uint32_t)> payload{};
    for (uint32_t index = 0u; index < 4u; ++index) {
        payload[index] = std::byte{
            static_cast<uint8_t>(bits >> (index * 8u))};
    }
    return addPayload(
        kind, WreckwaterContentValueType::Float32,
        logicalPath, payload);
}

WreckwaterContentManifestError
WreckwaterContentManifestBuilder::addBytes(
    WreckwaterContentEntryKind kind,
    std::string_view logicalPath,
    std::span<const std::byte> bytes) noexcept {
    return addPayload(
        kind, WreckwaterContentValueType::Bytes,
        logicalPath, bytes);
}

WreckwaterContentManifestResult
WreckwaterContentManifestBuilder::finalize() noexcept {
    WreckwaterContentManifestResult output;
    if (!initialized_) {
        output.error = WreckwaterContentManifestError::NotInitialized;
        return output;
    }
    if (finalized_) {
        output.error =
            WreckwaterContentManifestError::AlreadyFinalized;
        return output;
    }
    if (suppliedCount_ != definitionCount_) {
        output.error = WreckwaterContentManifestError::MissingEntry;
        return output;
    }

    std::array<
        uint16_t, kWreckwaterContentManifestMaximumEntries> order{};
    for (size_t index = 0u; index < definitionCount_; ++index) {
        order[index] = static_cast<uint16_t>(index);
    }
    for (size_t index = 1u; index < definitionCount_; ++index) {
        const uint16_t candidate = order[index];
        size_t destination = index;
        while (destination != 0u
            && pathLess(
                entries_[candidate],
                entries_[order[destination - 1u]])) {
            order[destination] = order[destination - 1u];
            --destination;
        }
        order[destination] = candidate;
    }

    Sha256 hash;
    hash.string(kRootDomain);
    hash.u32Little(kWreckwaterContentManifestSchemaVersion);
    hash.u32Little(static_cast<uint32_t>(definitionCount_));
    hash.u64Little(totalPayloadBytes_);
    for (size_t index = 0u; index < definitionCount_; ++index) {
        const Entry& entry = entries_[order[index]];
        hash.string(kEntryDomain);
        hash.u32Little(enumWord(entry.kind));
        hash.u32Little(enumWord(entry.valueType));
        hash.u32Little(entry.pathBytes);
        hash.update(std::as_bytes(std::span<const char>(
            entry.path.data(), entry.pathBytes)));
        hash.u64Little(entry.payloadBytes);
        hash.update(entry.leafDigest.bytes);
    }
    hash.string(kEndDomain);
    output.digest = hash.finish();
    output.replayContentHash = replayHash(output.digest);
    output.entryCount = static_cast<uint32_t>(definitionCount_);
    output.totalPayloadBytes = totalPayloadBytes_;
    if (output.replayContentHash == 0u) {
        output.error =
            WreckwaterContentManifestError::ZeroCompatibilityHash;
        return output;
    }

    output.error = WreckwaterContentManifestError::None;
    result_ = output;
    finalized_ = true;
    return output;
}

WreckwaterContentManifestStorageState
WreckwaterContentManifestBuilder::storageState() const noexcept {
    return {
        .entries = entries_.data(),
        .capacity = entries_.size(),
    };
}

std::array<char, kWreckwaterContentManifestDigestBytes * 2u + 1u>
wreckwaterContentDigestHex(
    const WreckwaterContentDigest& digest) noexcept {
    constexpr std::array<char, 16> digits{{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    }};
    std::array<
        char, kWreckwaterContentManifestDigestBytes * 2u + 1u> result{};
    for (size_t index = 0u; index < digest.bytes.size(); ++index) {
        const uint8_t value =
            std::to_integer<uint8_t>(digest.bytes[index]);
        result[index * 2u] = digits[value >> 4u];
        result[index * 2u + 1u] = digits[value & 0x0fu];
    }
    result.back() = '\0';
    return result;
}

} // namespace voxy::game
