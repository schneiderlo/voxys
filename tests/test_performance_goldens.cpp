#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

#include "terrain/compression.hpp"
#include "terrain/shadow_bake.hpp"

namespace voxy::terrain {

namespace {

constexpr std::array<uint32_t, 256> makeCrc32Table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t value = i;
        for (uint32_t bit = 0; bit < 8; ++bit) {
            value = (value & 1u) != 0u ? (value >> 1u) ^ 0xedb88320u
                                       : value >> 1u;
        }
        table[i] = value;
    }
    return table;
}

uint32_t goldenCrc32(std::span<const uint8_t> bytes) {
    static constexpr auto table = makeCrc32Table();
    uint32_t crc = 0xffffffffu;
    for (const uint8_t byte : bytes) {
        crc = table[(crc ^ byte) & 0xffu] ^ (crc >> 8u);
    }
    return ~crc;
}

} // namespace

TEST(PerformanceGoldenTest, CanyonHeightAndShadowOutputsAreBitExact) {
    const std::filesystem::path input = "data/canyon_8k.ldh";
    ASSERT_TRUE(std::filesystem::exists(input));

    auto decoded = decompressFromFile(input);
    ASSERT_TRUE(decoded.has_value());
    const auto& heightmap = decoded.value();
    const auto heightBytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(heightmap.data.data()),
        heightmap.data.size() * sizeof(uint16_t));
    EXPECT_EQ(goldenCrc32(heightBytes), 0xefda2eacu);

    ShadowBakeConfig config;
    config.lightDir = {0.4f, 0.8f, 0.4f};
    config.heightScale = 600.0f;
    config.cellScale = 1.0f;
    config.downsample = 2;
    const auto shadow = bakeShadowHeightField(
        heightmap.data, heightmap.width, heightmap.height, config);
    ASSERT_EQ(shadow.width, 4096u);
    ASSERT_EQ(shadow.height, 4096u);
    const auto shadowBytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(shadow.data.data()),
        shadow.data.size() * sizeof(uint16_t));
    EXPECT_EQ(goldenCrc32(shadowBytes), 0x0c01a740u);
}

} // namespace voxy::terrain
