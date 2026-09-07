#include "game/construction/construction_types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

namespace voxy::game::construction {
namespace {

static_assert(!std::is_convertible_v<uint64_t, SimulationTick>);
static_assert(!std::is_constructible_v<SimulationTick, TopologyRevision>);
static_assert(!std::is_constructible_v<AuthorityEpoch, RequestSequence>);
static_assert(!std::is_copy_constructible_v<IdAllocator>);
static_assert(!std::is_move_constructible_v<IdAllocator>);

WorldNamespace testWorld() {
    WorldNamespace world{};
    for (size_t i = 0; i < world.bytes.size(); ++i) {
        world.bytes[i] = static_cast<uint8_t>(i);
    }
    return world;
}

PlacementEnvelope goldenEnvelope() {
    PlacementEnvelope result{};
    result.tick = SimulationTick{9007199254740993ull};
    result.revision = TopologyRevision{7};
    result.epoch = AuthorityEpoch{2};
    result.sequence = RequestSequence{std::numeric_limits<uint64_t>::max()};
    result.records = {
        {{testWorld(), 256}, {{-50, 16, 9}, {1}}},
        {{testWorld(), std::numeric_limits<uint64_t>::max()}, {{50, -48, 0}, {2}}},
    };
    return result;
}

std::vector<std::byte> hexBytes(std::string_view text) {
    const auto nibble = [](char c) -> uint8_t {
        return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    std::vector<std::byte> result;
    for (size_t i = 0; i < text.size(); i += 2) {
        result.push_back(static_cast<std::byte>(nibble(text[i]) * 16u + nibble(text[i + 1])));
    }
    return result;
}

// Independent frozen wire fixture; also consumed by the Node conformance test.
const auto kGoldenBytes = hexBytes(
    "5356435001000000010000000000200007000000000000000200000000000000"
    "ffffffffffffffff02000000"
    "000102030405060708090a0b0c0d0e0f0001000000000000ceffffff100000000900000001"
    "000102030405060708090a0b0c0d0e0fffffffffffffffff32000000d0ffffff0000000002");

TEST(ConstructionTypes, RotationsFormExactProperGroup) {
    std::set<std::array<int8_t, 9>> unique;
    const GridPosition point{-50, 16, 9};
    constexpr std::array<GridPosition, 24> frozenBasisPoints{{
        {1, 2, 3}, {1, -3, 2}, {1, -2, -3}, {1, 3, -2},
        {2, 1, -3}, {3, 1, 2}, {-2, 1, 3}, {-3, 1, -2},
        {2, 3, 1}, {-3, 2, 1}, {-2, -3, 1}, {3, -2, 1},
        {-1, 2, -3}, {-1, 3, 2}, {-1, -2, 3}, {-1, -3, -2},
        {2, -1, 3}, {-3, -1, 2}, {-2, -1, -3}, {3, -1, -2},
        {2, -3, -1}, {3, 2, -1}, {-2, 3, -1}, {-3, -2, -1},
    }};
    for (uint8_t a = 0; a < 24; ++a) {
        const auto matrix = rotationMatrix(CubeRotation{a});
        ASSERT_TRUE(matrix);
        unique.insert(matrix->elements);
        EXPECT_EQ(rotate(CubeRotation{a}, {1, 2, 3}), frozenBasisPoints[a]);
        const auto& m = matrix->elements;
        for (size_t axis = 0; axis < 3; ++axis) {
            EXPECT_EQ(std::abs(m[axis * 3]) + std::abs(m[axis * 3 + 1]) + std::abs(m[axis * 3 + 2]), 1);
            EXPECT_EQ(std::abs(m[axis]) + std::abs(m[3 + axis]) + std::abs(m[6 + axis]), 1);
        }
        const int determinant = m[0] * (m[4] * m[8] - m[5] * m[7])
            - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
        EXPECT_EQ(determinant, 1);
        const auto inv = inverse(CubeRotation{a});
        ASSERT_TRUE(inv);
        EXPECT_EQ(compose(CubeRotation{a}, *inv), CubeRotation{});
        EXPECT_EQ(compose(*inv, CubeRotation{a}), CubeRotation{});
        EXPECT_EQ(rotate(*inv, *rotate(CubeRotation{a}, point)), point);
        for (uint8_t b = 0; b < 24; ++b) {
            const auto combined = compose(CubeRotation{a}, CubeRotation{b});
            ASSERT_TRUE(combined);
            const auto n = rotationMatrix(CubeRotation{b})->elements;
            const auto actual = rotationMatrix(*combined)->elements;
            // Independent column-vector oracle, in integer arithmetic.
            for (size_t row = 0; row < 3; ++row) {
                for (size_t column = 0; column < 3; ++column) {
                    EXPECT_EQ(actual[row * 3 + column],
                              m[row * 3] * n[column] + m[row * 3 + 1] * n[3 + column]
                                  + m[row * 3 + 2] * n[6 + column]);
                }
            }
            EXPECT_EQ(rotate(*combined, point), rotate(CubeRotation{a}, *rotate(CubeRotation{b}, point)));
        }
    }
    EXPECT_EQ(unique.size(), 24u);
    EXPECT_FALSE(rotationMatrix(CubeRotation{24}));
    EXPECT_FALSE(compose(CubeRotation{0}, CubeRotation{255}));
}

TEST(ConstructionTypes, SidewaysAndUpsideDownSocketFramesComposeExactly) {
    const GridTransform sideways{{-50, 100, -9}, {1}};
    EXPECT_EQ(transformPosition(sideways, {0, kPlateTicks, kStudInsertionTicks}),
              (GridPosition{-50, 91, 7}));
    const GridTransform upsideDown{{50, 48, -50}, {2}};
    EXPECT_EQ(transformPosition(upsideDown, {0, kBrickBodyTicks, kStudInsertionTicks}),
              (GridPosition{50, 0, -59}));
    const GridPosition socket{13, 16, 9};
    for (uint8_t a = 0; a < 24; ++a) {
        for (uint8_t b = 0; b < 24; ++b) {
            const GridTransform parent{{-50, 16, 100}, {a}};
            const GridTransform child{{9, -48, -16}, {b}};
            const auto combined = compose(parent, child);
            ASSERT_TRUE(combined);
            EXPECT_EQ(transformPosition(*combined, socket),
                      transformPosition(parent, *transformPosition(child, socket)));
            const auto inv = inverse(*combined);
            ASSERT_TRUE(inv);
            EXPECT_EQ(compose(*combined, *inv), GridTransform{});
            EXPECT_EQ(transformPosition(*inv, *transformPosition(*combined, socket)), socket);
        }
    }
}

TEST(ConstructionTypes, LegalBoundariesRemainRotatableAndOverflowRejects) {
    constexpr int32_t high = kMaximumGridCoordinate;
    constexpr int32_t invalid = std::numeric_limits<int32_t>::min();
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        for (const auto point : {GridPosition{high, -high, high}, GridPosition{-high, high, -high}}) {
            const auto moved = rotate(CubeRotation{rotation}, point);
            ASSERT_TRUE(moved);
            EXPECT_EQ(rotate(*inverse(CubeRotation{rotation}), *moved), point);
        }
    }
    EXPECT_FALSE(rotate(CubeRotation{}, {invalid, 0, 0}));
    EXPECT_FALSE(checkedAdd({high, 0, 0}, {1, 0, 0}));
    EXPECT_FALSE(checkedAdd({-high, 0, 0}, {-1, 0, 0}));
    EXPECT_EQ(checkedAdd({high, -high, 0}, {-high, high, 0}), GridPosition{});
    EXPECT_EQ(checkedSubtract({high, -high, 0}, {high, -high, 0}), GridPosition{});
    EXPECT_FALSE(checkedSubtract({high, 0, 0}, {-1, 0, 0}));
    EXPECT_FALSE(checkedSubtract({-high, 0, 0}, {1, 0, 0}));
    EXPECT_FALSE(checkedSubtract({invalid, 0, 0}, {0, 0, 0}));
    EXPECT_FALSE(compose(GridTransform{{high, 0, 0}, {}}, GridTransform{{1, 0, 0}, {}}));
    EXPECT_FALSE(inverse(GridTransform{{invalid, 0, 0}, {}}));
}

TEST(ConstructionTypes, MetresConversionSeparatesValidationFromSnapping) {
    EXPECT_EQ(gridFromMetres({1.0, 0.32, 0.18}), (GridPosition{50, 16, 9}));
    EXPECT_EQ(gridFromMetres({-0.96, 0.02, -1.0}), (GridPosition{-48, 1, -50}));
    EXPECT_FALSE(gridFromMetres({0.01, 0, 0}));
    EXPECT_EQ(snapToGrid({0.01, -0.01, 0}), (GridPosition{1, -1, 0}));
    EXPECT_FALSE(gridFromMetres({std::numeric_limits<double>::quiet_NaN(), 0, 0}));
    EXPECT_FALSE(snapToGrid({std::numeric_limits<double>::max(), 0, 0}));
    EXPECT_FALSE(toMetres({std::numeric_limits<int32_t>::min(), 0, 0}));
    for (const int32_t x : {-kMaximumGridCoordinate, -1, 0, 1, kMaximumGridCoordinate}) {
        EXPECT_EQ(gridFromMetres(*toMetres({x, x, x})), (GridPosition{x, x, x}));
    }
}

TEST(ConstructionTypes, SourceFrameConversionRequiresExporterMetadata) {
    constexpr auto raw = SourceFrame::RawBlenderZUpMinusYForward;
    EXPECT_EQ(toCanonicalFrame({1, 2, 3}, raw), (MetresPosition{-1, 3, 2}));
    EXPECT_EQ(toCanonicalFrame({0, 0, 1}, raw), (MetresPosition{0, 1, 0}));
    EXPECT_EQ(toCanonicalFrame({0, -1, 0}, raw), (MetresPosition{0, 0, -1}));
    EXPECT_FALSE(toCanonicalFrame({1, 2, 3}, SourceFrame::ExportedGltf));
    EXPECT_FALSE(toCanonicalFrame({1, 2, 3}, raw, CubeRotation{}));
    EXPECT_FALSE(toCanonicalFrame({1, 2, 3}, SourceFrame::Canonical, CubeRotation{}));
    EXPECT_EQ(toCanonicalFrame({1, 2, 3}, SourceFrame::ExportedGltf, CubeRotation{}),
              (MetresPosition{1, 2, 3}));
    EXPECT_FALSE(toCanonicalFrame({1, 2, 3}, SourceFrame::ExportedGltf, CubeRotation{24}));
    EXPECT_FALSE(toCanonicalFrame({1, 2, 3}, static_cast<SourceFrame>(255)));
    EXPECT_EQ(basisToCanonical(raw), CubeRotation{13});
    // Known exporter rotates -90 degrees about X: (x,y,z)->(x,z,-y).
    // Its recorded remaining conversion is 180 degrees about Y, NOT raw Blender again.
    EXPECT_EQ(toCanonicalFrame({1, 3, -2}, SourceFrame::ExportedGltf, CubeRotation{12}),
              toCanonicalFrame({1, 2, 3}, raw));
    EXPECT_EQ(compose(CubeRotation{12}, CubeRotation{3}), basisToCanonical(raw));
}

TEST(ConstructionTypes, QuaternionNormalizationSignAndWireOrderAreCanonical) {
    EXPECT_EQ(canonicalQuaternion(0, 0, 0, 3), CanonicalQuaternion{});
    EXPECT_EQ(canonicalQuaternion(-2, -0.0, -0.0, -0.0), (CanonicalQuaternion{1, 0, 0, 0}));
    const auto positive = canonicalQuaternion(1, 2, 3, 4);
    const auto negative = canonicalQuaternion(-1, -2, -3, -4);
    ASSERT_TRUE(positive);
    EXPECT_EQ(positive, negative);
    EXPECT_EQ(encodeQuaternion(*positive), encodeQuaternion(*negative));
    EXPECT_EQ(canonicalQuaternion(std::numeric_limits<double>::max(), 0, 0, 0),
              (CanonicalQuaternion{1, 0, 0, 0}));
    EXPECT_EQ(canonicalQuaternion(std::numeric_limits<double>::min(), 0, 0, 0),
              (CanonicalQuaternion{1, 0, 0, 0}));
    const auto halfTurnBytes = encodeQuaternion({1, 0, 0, 0});
    ASSERT_TRUE(halfTurnBytes);
    const auto golden = hexBytes("0000803f000000000000000000000000");
    EXPECT_TRUE(std::equal(halfTurnBytes->begin(), halfTurnBytes->end(), golden.begin(), golden.end()));
    const auto quarterTurn = canonicalQuaternion(1, 0, 0, 1);
    ASSERT_TRUE(quarterTurn);
    const auto quarterBytes = *encodeQuaternion(*quarterTurn);
    const auto quarterGolden = hexBytes("f304353f0000000000000000f304353f");
    EXPECT_TRUE(std::equal(quarterBytes.begin(), quarterBytes.end(), quarterGolden.begin(), quarterGolden.end()));
    // Right-handed +90 degrees about X maps +Y to +Z.
    EXPECT_NEAR(2.0f * quarterTurn->x * quarterTurn->w, 1.0f, 1.0e-6f);
    EXPECT_NEAR(1.0f - 2.0f * quarterTurn->x * quarterTurn->x, 0.0f, 1.0e-6f);
    auto bytes = *encodeQuaternion(*positive);
    for (size_t i = 0; i < 100; ++i) {
        const auto decoded = decodeQuaternion(bytes);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded, positive);
        const auto encoded = encodeQuaternion(*decoded);
        ASSERT_TRUE(encoded);
        EXPECT_EQ(*encoded, bytes);
        bytes = *encoded;
    }
}

TEST(ConstructionTypes, QuaternionDecoderRejectsInsteadOfRepairing) {
    EXPECT_FALSE(canonicalQuaternion(0, 0, 0, 0));
    EXPECT_FALSE(canonicalQuaternion(0, 0, 0, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(canonicalQuaternion(0, std::numeric_limits<double>::quiet_NaN(), 0, 1));
    EXPECT_FALSE(encodeQuaternion({0, 0, 0, -1}));
    EXPECT_FALSE(encodeQuaternion({-0.0f, 0, 0, 1}));
    EXPECT_FALSE(encodeQuaternion({std::numeric_limits<float>::denorm_min(), 0, 0, 1}));
    EXPECT_FALSE(encodeQuaternion({0, 0, 0, 0.5f}));
    const auto identity = *encodeQuaternion({});
    for (size_t size = 0; size < identity.size(); ++size) {
        EXPECT_FALSE(decodeQuaternion(std::span{identity}.first(size)));
    }
    EXPECT_FALSE(decodeQuaternion(hexBytes("0000008000000000000000000000803f")));
    EXPECT_FALSE(decodeQuaternion(hexBytes("000000000000000000000000000080bf")));
    EXPECT_FALSE(decodeQuaternion(hexBytes("0000c07f00000000000000000000803f")));
}

TEST(ConstructionTypes, DurableIdsAllocateOnceAndRejectExhaustion) {
    EXPECT_FALSE(isValid(WorldNamespace{}));
    EXPECT_FALSE(isValid(DurableId{testWorld(), 0}));
    IdAllocator invalid(WorldNamespace{});
    EXPECT_FALSE(invalid.allocate());
    EXPECT_EQ(invalid.lastIssued(), 0u);
    IdAllocator fresh(testWorld());
    EXPECT_EQ(fresh.allocate(), (DurableId{testWorld(), 1}));
    constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
    IdAllocator restored(testWorld(), maximum - 1);
    EXPECT_EQ(restored.allocate(), (DurableId{testWorld(), maximum}));
    EXPECT_FALSE(restored.allocate());
    EXPECT_FALSE(restored.allocate());
    EXPECT_EQ(restored.lastIssued(), maximum);
    EXPECT_EQ(next(SimulationTick{}), SimulationTick{1});
    EXPECT_EQ(next(TopologyRevision{}), TopologyRevision{1});
    EXPECT_FALSE(next(AuthorityEpoch{}));
    EXPECT_FALSE(next(RequestSequence{}));
    EXPECT_EQ(next(AuthorityEpoch{1}), AuthorityEpoch{2});
    EXPECT_FALSE(next(SimulationTick{maximum}));
    EXPECT_FALSE(next(TopologyRevision{maximum}));
    EXPECT_FALSE(next(AuthorityEpoch{maximum}));
    EXPECT_FALSE(next(RequestSequence{maximum}));
}

TEST(ConstructionTypes, DecimalExchangeIsLosslessAndStrict) {
    constexpr std::array<uint64_t, 6> values{0, 1, 9007199254740991ull, 9007199254740992ull,
                                           9007199254740993ull, std::numeric_limits<uint64_t>::max()};
    for (const uint64_t value : values) {
        EXPECT_EQ(u64FromDecimal(u64ToDecimal(value)), value);
    }
    EXPECT_EQ(u64ToDecimal(std::numeric_limits<uint64_t>::max()), "18446744073709551615");
    for (const auto text : {"", "00", "01", "-0", "-1", "+1", " 1", "1 ", "1.0", "1e2", "18446744073709551616"}) {
        EXPECT_FALSE(u64FromDecimal(text)) << text;
    }
}

TEST(ConstructionTypes, CanonicalEncodingMatchesFrozenCrossLanguageBytes) {
    auto envelope = goldenEnvelope();
    std::reverse(envelope.records.begin(), envelope.records.end());
    const auto unchanged = envelope;
    std::vector<std::byte> encoded;
    ASSERT_EQ(encodePlacements(envelope, encoded), CodecError::None);
    EXPECT_EQ(encoded, kGoldenBytes);
    EXPECT_EQ(envelope, unchanged);
    PlacementEnvelope decoded{};
    ASSERT_EQ(decodePlacements(kGoldenBytes, decoded), CodecError::None);
    EXPECT_EQ(decoded, goldenEnvelope());
    ASSERT_EQ(encodePlacements(decoded, encoded), CodecError::None);
    EXPECT_EQ(encoded, kGoldenBytes);
    auto first = goldenEnvelope();
    first.records.front().id.counter = 1;
    first.records.back().id.counter = 256;
    std::reverse(first.records.begin(), first.records.end());
    ASSERT_EQ(encodePlacements(first, encoded), CodecError::None);
    ASSERT_EQ(decodePlacements(encoded, decoded), CodecError::None);
    EXPECT_EQ(decoded.records.front().id.counter, 1u); // Numeric, not LE-byte ordering.
}

TEST(ConstructionTypes, MalformedEncodingNeverPartiallyUpdatesOutput) {
    const auto sentinel = goldenEnvelope();
    auto decoded = sentinel;
    for (size_t size = 0; size < kGoldenBytes.size(); ++size) {
        EXPECT_NE(decodePlacements(std::span{kGoldenBytes}.first(size), decoded), CodecError::None);
        EXPECT_EQ(decoded, sentinel);
    }
    const auto reject = [&](std::vector<std::byte> bytes, CodecError expected) {
        EXPECT_EQ(decodePlacements(bytes, decoded), expected);
        EXPECT_EQ(decoded, sentinel);
    };
    auto bytes = kGoldenBytes;
    bytes.push_back(std::byte{0});
    reject(bytes, CodecError::InvalidEncoding);
    bytes = kGoldenBytes;
    bytes[0] = std::byte{0};
    reject(bytes, CodecError::InvalidEncoding);
    bytes = kGoldenBytes;
    bytes[4] = std::byte{2};
    reject(bytes, CodecError::UnsupportedSchema);
    bytes = kGoldenBytes;
    bytes[24] = std::byte{0};
    reject(bytes, CodecError::InvalidValue);
    bytes = kGoldenBytes;
    bytes[40] = bytes[41] = bytes[42] = bytes[43] = std::byte{255};
    reject(bytes, CodecError::RecordCapacity);
    bytes = kGoldenBytes;
    bytes[80] = std::byte{24};
    reject(bytes, CodecError::InvalidValue);
    bytes = kGoldenBytes;
    std::fill(bytes.begin() + 68, bytes.begin() + 71, std::byte{0});
    bytes[71] = std::byte{128};
    reject(bytes, CodecError::InvalidValue);
    bytes = kGoldenBytes;
    std::copy_n(bytes.begin() + 44, 24, bytes.begin() + 81);
    reject(bytes, CodecError::DuplicateId);
    bytes = kGoldenBytes;
    std::swap_ranges(bytes.begin() + 44, bytes.begin() + 81, bytes.begin() + 81);
    reject(bytes, CodecError::NonCanonicalOrder);
}

TEST(ConstructionTypes, EncoderBoundsValuesDuplicatesAndRecordCapacity) {
    auto envelope = goldenEnvelope();
    std::vector<std::byte> output{std::byte{123}};
    const auto sentinel = output;
    envelope.records.push_back(envelope.records.front());
    EXPECT_EQ(encodePlacements(envelope, output), CodecError::DuplicateId);
    EXPECT_EQ(output, sentinel);
    envelope = goldenEnvelope();
    envelope.sequence = RequestSequence{};
    EXPECT_EQ(encodePlacements(envelope, output), CodecError::InvalidValue);
    envelope = goldenEnvelope();
    envelope.records.front().id.world = {};
    EXPECT_EQ(encodePlacements(envelope, output), CodecError::InvalidValue);
    envelope = goldenEnvelope();
    envelope.records.resize(kMaximumPlacementRecords + 1);
    EXPECT_EQ(encodePlacements(envelope, output), CodecError::RecordCapacity);
    EXPECT_EQ(output, sentinel);
    envelope = {};
    ASSERT_EQ(encodePlacements(envelope, output), CodecError::None);
    EXPECT_EQ(output.size(), kPlacementHeaderBytes);
    PlacementEnvelope decoded{};
    EXPECT_EQ(decodePlacements(output, decoded), CodecError::None);
    EXPECT_EQ(decoded, envelope);
}

TEST(ConstructionTypes, MaximumRecordCountRoundTripsWithinTheDeclaredBound) {
    PlacementEnvelope envelope{};
    envelope.records.reserve(kMaximumPlacementRecords);
    for (size_t i = 0; i < kMaximumPlacementRecords; ++i) {
        envelope.records.push_back({{testWorld(), i + 1}, {{}, {static_cast<uint8_t>(i % 24)}}});
    }
    std::vector<std::byte> bytes;
    ASSERT_EQ(encodePlacements(envelope, bytes), CodecError::None);
    EXPECT_EQ(bytes.size(), kPlacementHeaderBytes + kMaximumPlacementRecords * kPlacementRecordBytes);
    PlacementEnvelope decoded{};
    ASSERT_EQ(decodePlacements(bytes, decoded), CodecError::None);
    EXPECT_EQ(decoded, envelope);
}

} // namespace
} // namespace voxy::game::construction
