#include "game/construction/construction_types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace voxy::game::construction {
namespace {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

constexpr auto makeRotations() noexcept {
    constexpr std::array<std::array<int8_t, 3>, 6> axes{{
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
        {-1, 0, 0}, {0, -1, 0}, {0, 0, -1},
    }};
    std::array<RotationMatrix, 24> result{};
    size_t index = 0;
    for (const auto& x : axes) {
        for (const auto& y : axes) {
            if (x[0] * y[0] + x[1] * y[1] + x[2] * y[2] != 0) {
                continue;
            }
            const std::array<int8_t, 3> z{
                static_cast<int8_t>(x[1] * y[2] - x[2] * y[1]),
                static_cast<int8_t>(x[2] * y[0] - x[0] * y[2]),
                static_cast<int8_t>(x[0] * y[1] - x[1] * y[0]),
            };
            result[index++].elements = {
                x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2],
            };
        }
    }
    return result;
}

constexpr auto kRotations = makeRotations();
static_assert(kRotations[0].elements == std::array<int8_t, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1});

[[nodiscard]] std::optional<GridPosition> narrow(int64_t x, int64_t y, int64_t z) noexcept {
    constexpr int64_t limit = kMaximumGridCoordinate;
    if (x < -limit || x > limit || y < -limit || y > limit || z < -limit || z > limit) {
        return std::nullopt;
    }
    return GridPosition{static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)};
}

[[nodiscard]] bool finite(MetresPosition position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

[[nodiscard]] std::optional<GridPosition> quantize(MetresPosition position, bool snap) noexcept {
    if (!finite(position)) {
        return std::nullopt;
    }
    const std::array<double, 3> metres{position.x, position.y, position.z};
    std::array<int32_t, 3> ticks{};
    for (size_t axis = 0; axis < metres.size(); ++axis) {
        const double rounded = std::round(metres[axis] * kTicksPerMetre);
        if (!std::isfinite(rounded) || rounded < -static_cast<double>(kMaximumGridCoordinate)
            || rounded > static_cast<double>(kMaximumGridCoordinate)
            || (!snap && std::abs(metres[axis] - rounded / kTicksPerMetre) > kLatticeToleranceMetres)) {
            return std::nullopt;
        }
        ticks[axis] = static_cast<int32_t>(rounded);
    }
    return GridPosition{ticks[0], ticks[1], ticks[2]};
}

[[nodiscard]] float cleanFloat(float value) noexcept {
    return value == 0.0f || std::fpclassify(value) == FP_SUBNORMAL ? 0.0f : value;
}

// Little-endian primitives operate only after complete size validation.
void put32(std::span<std::byte> bytes, size_t offset, uint32_t value) noexcept {
    for (size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 0xffu);
    }
}

void put64(std::span<std::byte> bytes, size_t offset, uint64_t value) noexcept {
    put32(bytes, offset, static_cast<uint32_t>(value));
    put32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

[[nodiscard]] uint32_t get32(std::span<const std::byte> bytes, size_t offset) noexcept {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<uint32_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

[[nodiscard]] uint64_t get64(std::span<const std::byte> bytes, size_t offset) noexcept {
    return static_cast<uint64_t>(get32(bytes, offset))
        | (static_cast<uint64_t>(get32(bytes, offset + 4)) << 32);
}

[[nodiscard]] bool validRecord(const PlacementRecord& record) noexcept {
    return isValid(record.id) && isValid(record.placement.translation) && isValid(record.placement.rotation);
}

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'S'}, std::byte{'V'}, std::byte{'C'}, std::byte{'P'},
};

} // namespace

bool isValid(GridPosition position) noexcept {
    constexpr int32_t invalid = std::numeric_limits<int32_t>::min();
    return position.x != invalid && position.y != invalid && position.z != invalid;
}

bool isValid(CubeRotation rotation) noexcept { return rotation.value < kRotations.size(); }

std::optional<RotationMatrix> rotationMatrix(CubeRotation rotation) noexcept {
    return isValid(rotation) ? std::optional{kRotations[rotation.value]} : std::nullopt;
}

std::optional<GridPosition> checkedAdd(GridPosition a, GridPosition b) noexcept {
    if (!isValid(a) || !isValid(b)) {
        return std::nullopt;
    }
    return narrow(static_cast<int64_t>(a.x) + b.x, static_cast<int64_t>(a.y) + b.y,
                  static_cast<int64_t>(a.z) + b.z);
}

std::optional<GridPosition> checkedSubtract(GridPosition a, GridPosition b) noexcept {
    if (!isValid(a) || !isValid(b)) {
        return std::nullopt;
    }
    return narrow(static_cast<int64_t>(a.x) - b.x, static_cast<int64_t>(a.y) - b.y,
                  static_cast<int64_t>(a.z) - b.z);
}

std::optional<GridPosition> rotate(CubeRotation rotation, GridPosition position) noexcept {
    if (!isValid(rotation) || !isValid(position)) {
        return std::nullopt;
    }
    const auto& m = kRotations[rotation.value].elements;
    const int64_t x = position.x;
    const int64_t y = position.y;
    const int64_t z = position.z;
    return narrow(m[0] * x + m[1] * y + m[2] * z,
                  m[3] * x + m[4] * y + m[5] * z,
                  m[6] * x + m[7] * y + m[8] * z);
}

std::optional<GridPosition> transformPosition(GridTransform transform, GridPosition position) noexcept {
    const auto rotated = rotate(transform.rotation, position);
    return rotated ? checkedAdd(transform.translation, *rotated) : std::nullopt;
}

std::optional<CubeRotation> compose(CubeRotation a, CubeRotation b) noexcept {
    if (!isValid(a) || !isValid(b)) {
        return std::nullopt;
    }
    RotationMatrix result{};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            int value = 0;
            for (size_t k = 0; k < 3; ++k) {
                value += kRotations[a.value].elements[row * 3 + k]
                    * kRotations[b.value].elements[k * 3 + column];
            }
            result.elements[row * 3 + column] = static_cast<int8_t>(value);
        }
    }
    for (uint8_t i = 0; i < kRotations.size(); ++i) {
        if (kRotations[i] == result) {
            return CubeRotation{i};
        }
    }
    return std::nullopt;
}

std::optional<GridTransform> compose(GridTransform a, GridTransform b) noexcept {
    const auto rotation = compose(a.rotation, b.rotation);
    const auto translation = transformPosition(a, b.translation);
    if (!rotation || !translation) {
        return std::nullopt;
    }
    return GridTransform{*translation, *rotation};
}

std::optional<CubeRotation> inverse(CubeRotation rotation) noexcept {
    if (!isValid(rotation)) {
        return std::nullopt;
    }
    for (uint8_t i = 0; i < kRotations.size(); ++i) {
        if (compose(rotation, CubeRotation{i}) == CubeRotation{}) {
            return CubeRotation{i};
        }
    }
    return std::nullopt;
}

std::optional<GridTransform> inverse(GridTransform transform) noexcept {
    const auto rotation = inverse(transform.rotation);
    if (!rotation || !isValid(transform.translation)) {
        return std::nullopt;
    }
    const GridPosition negative{-transform.translation.x, -transform.translation.y, -transform.translation.z};
    return GridTransform{*rotate(*rotation, negative), *rotation};
}

std::optional<MetresPosition> toMetres(GridPosition position) noexcept {
    if (!isValid(position)) {
        return std::nullopt;
    }
    return MetresPosition{static_cast<double>(position.x) / kTicksPerMetre,
                          static_cast<double>(position.y) / kTicksPerMetre,
                          static_cast<double>(position.z) / kTicksPerMetre};
}

std::optional<GridPosition> gridFromMetres(MetresPosition position) noexcept { return quantize(position, false); }
std::optional<GridPosition> snapToGrid(MetresPosition position) noexcept { return quantize(position, true); }

std::optional<CubeRotation> basisToCanonical(
    SourceFrame source, std::optional<CubeRotation> gltfToCanonical) noexcept {
    if (source == SourceFrame::RawBlenderZUpMinusYForward && !gltfToCanonical) {
        return CubeRotation{13}; // (x,y,z) -> (-x,z,y), determinant +1.
    }
    if (source == SourceFrame::Canonical && !gltfToCanonical) {
        return CubeRotation{};
    }
    if (source != SourceFrame::ExportedGltf || !gltfToCanonical || !isValid(*gltfToCanonical)) {
        return std::nullopt;
    }
    return gltfToCanonical;
}

std::optional<MetresPosition> toCanonicalFrame(
    MetresPosition position, SourceFrame source, std::optional<CubeRotation> gltfToCanonical) noexcept {
    const auto basis = basisToCanonical(source, gltfToCanonical);
    if (!basis || !finite(position)) {
        return std::nullopt;
    }
    const auto& m = kRotations[basis->value].elements;
    return MetresPosition{m[0] * position.x + m[1] * position.y + m[2] * position.z,
                          m[3] * position.x + m[4] * position.y + m[5] * position.z,
                          m[6] * position.x + m[7] * position.y + m[8] * position.z};
}

std::optional<CanonicalQuaternion> canonicalQuaternion(double x, double y, double z, double w) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) {
        return std::nullopt;
    }
    // Scaling first prevents overflow/underflow for finite authored inputs.
    const double scale = std::max({std::abs(x), std::abs(y), std::abs(z), std::abs(w)});
    if (scale == 0.0) {
        return std::nullopt;
    }
    x /= scale;
    y /= scale;
    z /= scale;
    w /= scale;
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    CanonicalQuaternion result{cleanFloat(static_cast<float>(x / length)), cleanFloat(static_cast<float>(y / length)),
                               cleanFloat(static_cast<float>(z / length)), cleanFloat(static_cast<float>(w / length))};
    for (float component : {result.w, result.x, result.y, result.z}) {
        if (component == 0.0f) {
            continue;
        }
        if (component < 0.0f) {
            result = {cleanFloat(-result.x), cleanFloat(-result.y), cleanFloat(-result.z), cleanFloat(-result.w)};
        }
        break;
    }
    return isCanonical(result) ? std::optional{result} : std::nullopt;
}

bool isCanonical(CanonicalQuaternion quaternion) noexcept {
    double lengthSquared = 0.0;
    bool foundSign = false;
    for (float component : {quaternion.w, quaternion.x, quaternion.y, quaternion.z}) {
        if (!std::isfinite(component) || std::abs(component) > 1.0f
            || (component == 0.0f && std::signbit(component)) || std::fpclassify(component) == FP_SUBNORMAL) {
            return false;
        }
        if (!foundSign && component != 0.0f) {
            if (component < 0.0f) {
                return false;
            }
            foundSign = true;
        }
        lengthSquared += static_cast<double>(component) * static_cast<double>(component);
    }
    return foundSign && std::abs(lengthSquared - 1.0) <= kQuaternionUnitTolerance;
}

std::optional<std::array<std::byte, 16>> encodeQuaternion(CanonicalQuaternion quaternion) noexcept {
    if (!isCanonical(quaternion)) {
        return std::nullopt;
    }
    std::array<std::byte, 16> output{};
    const std::array<float, 4> components{quaternion.x, quaternion.y, quaternion.z, quaternion.w};
    for (size_t i = 0; i < components.size(); ++i) {
        put32(output, i * 4, std::bit_cast<uint32_t>(components[i]));
    }
    return output;
}

std::optional<CanonicalQuaternion> decodeQuaternion(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != 16) {
        return std::nullopt;
    }
    const CanonicalQuaternion result{std::bit_cast<float>(get32(bytes, 0)), std::bit_cast<float>(get32(bytes, 4)),
                                     std::bit_cast<float>(get32(bytes, 8)), std::bit_cast<float>(get32(bytes, 12))};
    return isCanonical(result) ? std::optional{result} : std::nullopt;
}

bool isValid(WorldNamespace world) noexcept {
    return std::any_of(world.bytes.begin(), world.bytes.end(), [](uint8_t value) { return value != 0; });
}

bool isValid(DurableId id) noexcept { return isValid(id.world) && id.counter != 0; }

IdAllocator::IdAllocator(WorldNamespace world, uint64_t lastIssued) noexcept : world_(world), lastIssued_(lastIssued) {}

std::optional<DurableId> IdAllocator::allocate() noexcept {
    if (!isValid(world_) || lastIssued_ == std::numeric_limits<uint64_t>::max()) {
        return std::nullopt;
    }
    return DurableId{world_, ++lastIssued_};
}

std::string u64ToDecimal(uint64_t value) {
    std::array<char, 20> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), value);
    return std::string(text.data(), result.ptr);
}

std::optional<uint64_t> u64FromDecimal(std::string_view text) noexcept {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}

CodecError encodePlacements(const PlacementEnvelope& envelope, std::vector<std::byte>& output) {
    if (!envelope.epoch.valid() || !envelope.sequence.valid()) {
        return CodecError::InvalidValue;
    }
    if (envelope.records.size() > kMaximumPlacementRecords) {
        return CodecError::RecordCapacity;
    }
    auto records = envelope.records;
    for (const auto& record : records) {
        if (!validRecord(record)) {
            return CodecError::InvalidValue;
        }
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (size_t i = 1; i < records.size(); ++i) {
        if (records[i - 1].id == records[i].id) {
            return CodecError::DuplicateId;
        }
    }
    std::vector<std::byte> bytes(kPlacementHeaderBytes + records.size() * kPlacementRecordBytes);
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    put32(bytes, 4, kPlacementSchemaVersion);
    put64(bytes, 8, envelope.tick.value());
    put64(bytes, 16, envelope.revision.value());
    put64(bytes, 24, envelope.epoch.value());
    put64(bytes, 32, envelope.sequence.value());
    put32(bytes, 40, static_cast<uint32_t>(records.size()));
    size_t offset = kPlacementHeaderBytes;
    for (const auto& record : records) {
        for (size_t i = 0; i < record.id.world.bytes.size(); ++i) {
            bytes[offset + i] = static_cast<std::byte>(record.id.world.bytes[i]);
        }
        put64(bytes, offset + 16, record.id.counter);
        put32(bytes, offset + 24, std::bit_cast<uint32_t>(record.placement.translation.x));
        put32(bytes, offset + 28, std::bit_cast<uint32_t>(record.placement.translation.y));
        put32(bytes, offset + 32, std::bit_cast<uint32_t>(record.placement.translation.z));
        bytes[offset + 36] = static_cast<std::byte>(record.placement.rotation.value);
        offset += kPlacementRecordBytes;
    }
    output = std::move(bytes);
    return CodecError::None;
}

CodecError decodePlacements(std::span<const std::byte> bytes, PlacementEnvelope& output) {
    if (bytes.size() < kPlacementHeaderBytes || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return CodecError::InvalidEncoding;
    }
    if (get32(bytes, 4) != kPlacementSchemaVersion) {
        return CodecError::UnsupportedSchema;
    }
    const uint32_t count = get32(bytes, 40);
    if (count > kMaximumPlacementRecords) {
        return CodecError::RecordCapacity;
    }
    if (bytes.size() != kPlacementHeaderBytes + static_cast<size_t>(count) * kPlacementRecordBytes) {
        return CodecError::InvalidEncoding;
    }
    PlacementEnvelope result{};
    result.tick = SimulationTick{get64(bytes, 8)};
    result.revision = TopologyRevision{get64(bytes, 16)};
    result.epoch = AuthorityEpoch{get64(bytes, 24)};
    result.sequence = RequestSequence{get64(bytes, 32)};
    if (!result.epoch.valid() || !result.sequence.valid()) {
        return CodecError::InvalidValue;
    }
    result.records.reserve(count);
    size_t offset = kPlacementHeaderBytes;
    for (uint32_t i = 0; i < count; ++i) {
        PlacementRecord record{};
        for (size_t j = 0; j < record.id.world.bytes.size(); ++j) {
            record.id.world.bytes[j] = std::to_integer<uint8_t>(bytes[offset + j]);
        }
        record.id.counter = get64(bytes, offset + 16);
        record.placement.translation = {std::bit_cast<int32_t>(get32(bytes, offset + 24)),
                                        std::bit_cast<int32_t>(get32(bytes, offset + 28)),
                                        std::bit_cast<int32_t>(get32(bytes, offset + 32))};
        record.placement.rotation = CubeRotation{std::to_integer<uint8_t>(bytes[offset + 36])};
        if (!validRecord(record)) {
            return CodecError::InvalidValue;
        }
        if (!result.records.empty()) {
            if (result.records.back().id == record.id) {
                return CodecError::DuplicateId;
            }
            if (result.records.back().id > record.id) {
                return CodecError::NonCanonicalOrder;
            }
        }
        result.records.push_back(record);
        offset += kPlacementRecordBytes;
    }
    output = std::move(result);
    return CodecError::None;
}

} // namespace voxy::game::construction
