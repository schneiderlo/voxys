#include "game/wreckwater_content_manifest.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace voxy::game {
namespace {

constexpr std::array<WreckwaterContentEntryDefinition, 4>
    kGoldenDefinition{{
        {
            WreckwaterContentEntryKind::GameplayRule,
            WreckwaterContentValueType::Float32,
            "rules/physics/gravity_y",
        },
        {
            WreckwaterContentEntryKind::PresentationAsset,
            WreckwaterContentValueType::Bytes,
            "assets/data/ocean_environment.png",
        },
        {
            WreckwaterContentEntryKind::SchemaVersion,
            WreckwaterContentValueType::Unsigned32,
            "schemas/match",
        },
        {
            WreckwaterContentEntryKind::AuthoritativeAsset,
            WreckwaterContentValueType::Bytes,
            "assets/shaders/physics_ballistic.wgsl",
        },
    }};

constexpr std::array<WreckwaterContentEntryDefinition, 4>
    kGoldenDefinitionReversed{{
        kGoldenDefinition[3],
        kGoldenDefinition[2],
        kGoldenDefinition[1],
        kGoldenDefinition[0],
    }};

constexpr std::array<std::byte, 5> kShaderBytes{{
    std::byte{0x40u}, std::byte{0x67u}, std::byte{0x70u},
    std::byte{0x75u}, std::byte{0x0au},
}};
constexpr std::array<std::byte, 4> kEnvironmentBytes{{
    std::byte{0x89u}, std::byte{0x50u},
    std::byte{0x4eu}, std::byte{0x47u},
}};

void supplyGoldenForward(
    WreckwaterContentManifestBuilder& builder) {
    ASSERT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/match", 2u),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/shaders/physics_ballistic.wgsl",
            kShaderBytes),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addBytes(
            WreckwaterContentEntryKind::PresentationAsset,
            "assets/data/ocean_environment.png",
            kEnvironmentBytes),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            "rules/physics/gravity_y", -9.81f),
        WreckwaterContentManifestError::None);
}

void supplyGoldenReverse(
    WreckwaterContentManifestBuilder& builder) {
    ASSERT_EQ(
        builder.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            "rules/physics/gravity_y", -9.81f),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addBytes(
            WreckwaterContentEntryKind::PresentationAsset,
            "assets/data/ocean_environment.png",
            kEnvironmentBytes),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/shaders/physics_ballistic.wgsl",
            kShaderBytes),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/match", 2u),
        WreckwaterContentManifestError::None);
}

TEST(WreckwaterContentManifest,
     CanonicalOrderHasCrossPlatformSha256Golden) {
    WreckwaterContentManifestBuilder first;
    ASSERT_EQ(
        first.initialize(kGoldenDefinition),
        WreckwaterContentManifestError::None);
    const WreckwaterContentManifestStorageState storage =
        first.storageState();
    supplyGoldenForward(first);
    const WreckwaterContentManifestResult firstResult =
        first.finalize();
    ASSERT_TRUE(firstResult)
        << wreckwaterContentManifestErrorName(firstResult.error);
    EXPECT_EQ(first.storageState(), storage);
    EXPECT_EQ(firstResult.entryCount, 4u);
    EXPECT_EQ(firstResult.totalPayloadBytes, 17u);
    EXPECT_EQ(
        firstResult.replayContentHash,
        0x68fd'b0d4'7ce2'5811ull);
    const auto hex = wreckwaterContentDigestHex(firstResult.digest);
    EXPECT_EQ(
        std::string(hex.data()),
        "68fdb0d47ce258110f495712c81dc97a"
        "db13c50d7b3ee9f0dc5bfbc8a527c994");

    WreckwaterContentManifestBuilder second;
    ASSERT_EQ(
        second.initialize(kGoldenDefinitionReversed),
        WreckwaterContentManifestError::None);
    supplyGoldenReverse(second);
    const WreckwaterContentManifestResult secondResult =
        second.finalize();
    ASSERT_TRUE(secondResult);
    EXPECT_EQ(secondResult.digest, firstResult.digest);
    EXPECT_EQ(
        secondResult.replayContentHash,
        firstResult.replayContentHash);

    EXPECT_EQ(
        second.finalize().error,
        WreckwaterContentManifestError::AlreadyFinalized);
    EXPECT_EQ(
        second.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/match", 2u),
        WreckwaterContentManifestError::AlreadyFinalized);
}

TEST(WreckwaterContentManifest,
     EveryAssetByteContributesToTheCompatibilityIdentity) {
    constexpr std::array<WreckwaterContentEntryDefinition, 1>
        definition{{
            {
                WreckwaterContentEntryKind::AuthoritativeAsset,
                WreckwaterContentValueType::Bytes,
                "assets/shaders/physics_attachments.wgsl",
            },
        }};
    std::array<std::byte, 4> original{{
        std::byte{1u}, std::byte{2u},
        std::byte{3u}, std::byte{4u},
    }};
    std::array<std::byte, 4> changed = original;
    changed[2] = std::byte{9u};

    WreckwaterContentManifestBuilder first;
    ASSERT_EQ(
        first.initialize(definition),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        first.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            definition[0].logicalPath, original),
        WreckwaterContentManifestError::None);
    const auto firstResult = first.finalize();
    ASSERT_TRUE(firstResult);

    WreckwaterContentManifestBuilder second;
    ASSERT_EQ(
        second.initialize(definition),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        second.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            definition[0].logicalPath, changed),
        WreckwaterContentManifestError::None);
    const auto secondResult = second.finalize();
    ASSERT_TRUE(secondResult);
    EXPECT_NE(firstResult.digest, secondResult.digest);
    EXPECT_NE(
        firstResult.replayContentHash,
        secondResult.replayContentHash);
}

TEST(WreckwaterContentManifest,
     DefinitionIsAnExactAllowlistAndPathsCannotAlias) {
    constexpr std::array<WreckwaterContentEntryDefinition, 2>
        definition{{
            {
                WreckwaterContentEntryKind::SchemaVersion,
                WreckwaterContentValueType::Unsigned32,
                "schemas/wire",
            },
            {
                WreckwaterContentEntryKind::GameplayRule,
                WreckwaterContentValueType::Unsigned64,
                "rules/match/live_ticks",
            },
        }};
    WreckwaterContentManifestBuilder builder;
    ASSERT_EQ(
        builder.initialize(definition),
        WreckwaterContentManifestError::None);
    EXPECT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/unknown", 1u),
        WreckwaterContentManifestError::UnknownEntry);
    EXPECT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::GameplayRule,
            "schemas/wire", 2u),
        WreckwaterContentManifestError::DefinitionMismatch);
    EXPECT_EQ(
        builder.addUnsigned64(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/wire", 2u),
        WreckwaterContentManifestError::DefinitionMismatch);
    ASSERT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/wire", 2u),
        WreckwaterContentManifestError::None);
    EXPECT_EQ(
        builder.addUnsigned32(
            WreckwaterContentEntryKind::SchemaVersion,
            "schemas/wire", 2u),
        WreckwaterContentManifestError::DuplicateEntry);
    EXPECT_EQ(
        builder.finalize().error,
        WreckwaterContentManifestError::MissingEntry);
    ASSERT_EQ(
        builder.addUnsigned64(
            WreckwaterContentEntryKind::GameplayRule,
            "rules/match/live_ticks", 14'400u),
        WreckwaterContentManifestError::None);
    EXPECT_TRUE(builder.finalize());

    constexpr std::array<WreckwaterContentEntryDefinition, 2>
        duplicate{{
            definition[0],
            {
                WreckwaterContentEntryKind::GameplayRule,
                WreckwaterContentValueType::Unsigned32,
                "schemas/wire",
            },
        }};
    WreckwaterContentManifestBuilder duplicateBuilder;
    EXPECT_EQ(
        duplicateBuilder.initialize(duplicate),
        WreckwaterContentManifestError::DuplicatePath);

    constexpr std::array<WreckwaterContentEntryDefinition, 1>
        parentAlias{{
            {
                WreckwaterContentEntryKind::AuthoritativeAsset,
                WreckwaterContentValueType::Bytes,
                "assets/../secret",
            },
        }};
    WreckwaterContentManifestBuilder aliasBuilder;
    EXPECT_EQ(
        aliasBuilder.initialize(parentAlias),
        WreckwaterContentManifestError::InvalidPath);

    constexpr std::array<WreckwaterContentEntryDefinition, 1>
        caseAlias{{
            {
                WreckwaterContentEntryKind::AuthoritativeAsset,
                WreckwaterContentValueType::Bytes,
                "Assets/shader.wgsl",
            },
        }};
    WreckwaterContentManifestBuilder caseBuilder;
    EXPECT_EQ(
        caseBuilder.initialize(caseAlias),
        WreckwaterContentManifestError::InvalidPath);
}

TEST(WreckwaterContentManifest,
     FloatEncodingNormalizesZeroAndRejectsNonPortableValues) {
    constexpr std::array<WreckwaterContentEntryDefinition, 1>
        definition{{
            {
                WreckwaterContentEntryKind::GameplayRule,
                WreckwaterContentValueType::Float32,
                "rules/water/height",
            },
        }};
    WreckwaterContentManifestBuilder positive;
    ASSERT_EQ(
        positive.initialize(definition),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        positive.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath, 0.0f),
        WreckwaterContentManifestError::None);
    const auto positiveResult = positive.finalize();
    ASSERT_TRUE(positiveResult);

    WreckwaterContentManifestBuilder negative;
    ASSERT_EQ(
        negative.initialize(definition),
        WreckwaterContentManifestError::None);
    ASSERT_EQ(
        negative.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath, -0.0f),
        WreckwaterContentManifestError::None);
    const auto negativeResult = negative.finalize();
    ASSERT_TRUE(negativeResult);
    EXPECT_EQ(positiveResult.digest, negativeResult.digest);

    WreckwaterContentManifestBuilder invalid;
    ASSERT_EQ(
        invalid.initialize(definition),
        WreckwaterContentManifestError::None);
    EXPECT_EQ(
        invalid.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath,
            std::numeric_limits<float>::infinity()),
        WreckwaterContentManifestError::NonCanonicalFloat);
    EXPECT_EQ(
        invalid.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath,
            std::numeric_limits<float>::quiet_NaN()),
        WreckwaterContentManifestError::NonCanonicalFloat);
    EXPECT_EQ(
        invalid.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath,
            std::numeric_limits<float>::denorm_min()),
        WreckwaterContentManifestError::NonCanonicalFloat);
    EXPECT_EQ(
        invalid.addFloat32(
            WreckwaterContentEntryKind::GameplayRule,
            definition[0].logicalPath,
            -std::numeric_limits<float>::denorm_min()),
        WreckwaterContentManifestError::NonCanonicalFloat);
}

TEST(WreckwaterContentManifest,
     FixedBoundsRejectCapacityAndOverflowInputsBeforeHashing) {
    constexpr std::array<WreckwaterContentEntryDefinition, 2>
        definition{{
            {
                WreckwaterContentEntryKind::AuthoritativeAsset,
                WreckwaterContentValueType::Bytes,
                "assets/a.bin",
            },
            {
                WreckwaterContentEntryKind::AuthoritativeAsset,
                WreckwaterContentValueType::Bytes,
                "assets/b.bin",
            },
        }};
    WreckwaterContentManifestLimits oneEntry;
    oneEntry.maximumEntries = 1u;
    WreckwaterContentManifestBuilder tooMany;
    EXPECT_EQ(
        tooMany.initialize(definition, oneEntry),
        WreckwaterContentManifestError::EntryCapacity);

    WreckwaterContentManifestLimits small;
    small.maximumEntries = 2u;
    small.maximumEntryPayloadBytes = 3u;
    small.maximumTotalPayloadBytes = 4u;
    WreckwaterContentManifestBuilder bounded;
    ASSERT_EQ(
        bounded.initialize(definition, small),
        WreckwaterContentManifestError::None);
    constexpr std::array<std::byte, 4> fourBytes{};
    constexpr std::array<std::byte, 3> threeBytes{};
    constexpr std::array<std::byte, 2> twoBytes{};
    constexpr std::array<std::byte, 1> oneByte{};
    EXPECT_EQ(
        bounded.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/a.bin", fourBytes),
        WreckwaterContentManifestError::EntryPayloadCapacity);
    ASSERT_EQ(
        bounded.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/a.bin", threeBytes),
        WreckwaterContentManifestError::None);
    EXPECT_EQ(
        bounded.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/b.bin", twoBytes),
        WreckwaterContentManifestError::TotalPayloadCapacity);
    ASSERT_EQ(
        bounded.addBytes(
            WreckwaterContentEntryKind::AuthoritativeAsset,
            "assets/b.bin", oneByte),
        WreckwaterContentManifestError::None);
    EXPECT_TRUE(bounded.finalize());

    WreckwaterContentManifestLimits unbounded;
    unbounded.maximumTotalPayloadBytes =
        std::numeric_limits<uint64_t>::max();
    WreckwaterContentManifestBuilder overflowLimit;
    EXPECT_EQ(
        overflowLimit.initialize(definition, unbounded),
        WreckwaterContentManifestError::InvalidConfiguration);

    WreckwaterContentManifestLimits inverted;
    inverted.maximumEntryPayloadBytes = 8u;
    inverted.maximumTotalPayloadBytes = 4u;
    WreckwaterContentManifestBuilder invertedBuilder;
    EXPECT_EQ(
        invertedBuilder.initialize(definition, inverted),
        WreckwaterContentManifestError::InvalidConfiguration);
}

} // namespace
} // namespace voxy::game
