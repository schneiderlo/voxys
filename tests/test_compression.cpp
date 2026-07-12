// ═══════════════════════════════════════════════════════════════════════════════
// test_compression.cpp - Unit tests for Heightmap Compression
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for LDH format compression and decompression, including planar predictor,
// byte-stream splitting, and zstd compression.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "terrain/compression.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <random>
#include <vector>

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Error String Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CompressionErrorTest, ErrorToString) {
    EXPECT_EQ(errorToString(CompressionError::None), "No error");
    EXPECT_EQ(errorToString(CompressionError::FileNotFound), "File not found");
    EXPECT_EQ(errorToString(CompressionError::InvalidInput), "Invalid input data");
    EXPECT_EQ(errorToString(CompressionError::InvalidDimensions), "Invalid dimensions");
    EXPECT_EQ(errorToString(CompressionError::InvalidHeader), "Invalid LDH header");
    EXPECT_EQ(errorToString(CompressionError::UnsupportedVersion), "Unsupported LDH version");
    EXPECT_EQ(errorToString(CompressionError::ZstdCompressFailed), "zstd compression failed");
    EXPECT_EQ(errorToString(CompressionError::ZstdDecompressFailed), "zstd decompression failed");
    EXPECT_EQ(errorToString(CompressionError::SizeMismatch), "Size mismatch");
    EXPECT_EQ(errorToString(CompressionError::ChecksumFailed), "Checksum verification failed");
    EXPECT_EQ(errorToString(CompressionError::OutOfMemory), "Out of memory");
}

// ═══════════════════════════════════════════════════════════════════════════════
// LDH Header Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LDHHeaderTest, HeaderSize) {
    EXPECT_EQ(sizeof(LDHHeader), LDH_HEADER_SIZE);
    EXPECT_EQ(LDH_HEADER_SIZE, 64u);
}

TEST(LDHHeaderTest, InitDefaults) {
    LDHHeader header;
    header.init(1024, 512);
    
    EXPECT_EQ(header.magic, LDH_MAGIC);
    EXPECT_EQ(header.version, LDH_VERSION);
    EXPECT_EQ(header.width, 1024u);
    EXPECT_EQ(header.height, 512u);
    EXPECT_EQ(header.flags, static_cast<uint32_t>(LDHFlags::SplitBytes));
    EXPECT_EQ(header.lowStreamSize, 0u);
    EXPECT_EQ(header.highStreamSize, 0u);
    
    // Reserved should be all zeros
    for (const auto& r : header.reserved) {
        EXPECT_EQ(r, 0u);
    }
}

TEST(LDHHeaderTest, IsValid) {
    LDHHeader header;
    header.init(64, 64);
    EXPECT_TRUE(header.isValid());
    
    // Invalid magic
    LDHHeader badMagic = header;
    badMagic.magic = 0x12345678;
    EXPECT_FALSE(badMagic.isValid());
    
    // Invalid version
    LDHHeader badVersion = header;
    badVersion.version = 999;
    EXPECT_FALSE(badVersion.isValid());
    
    // Invalid dimensions
    LDHHeader badWidth = header;
    badWidth.width = 0;
    EXPECT_FALSE(badWidth.isValid());
    
    LDHHeader badHeight = header;
    badHeight.height = 0;
    EXPECT_FALSE(badHeight.isValid());
}

TEST(LDHHeaderTest, SampleCount) {
    LDHHeader header;
    header.init(1024, 512);
    EXPECT_EQ(header.sampleCount(), 1024u * 512u);
    
    header.init(8192, 8192);
    EXPECT_EQ(header.sampleCount(), 8192u * 8192u);
}

TEST(LDHHeaderTest, UncompressedSize) {
    LDHHeader header;
    header.init(1024, 512);
    EXPECT_EQ(header.uncompressedSize(), 1024u * 512u * sizeof(uint16_t));
}

TEST(LDHHeaderTest, Flags) {
    LDHHeader header;
    header.init(64, 64);
    
    EXPECT_TRUE(hasFlag(header.getFlags(), LDHFlags::SplitBytes));
    EXPECT_FALSE(hasFlag(header.getFlags(), LDHFlags::HasChecksum));
    
    header.setFlags(LDHFlags::SplitBytes | LDHFlags::HasChecksum);
    EXPECT_TRUE(hasFlag(header.getFlags(), LDHFlags::SplitBytes));
    EXPECT_TRUE(hasFlag(header.getFlags(), LDHFlags::HasChecksum));
}

// ═══════════════════════════════════════════════════════════════════════════════
// LDH Flags Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LDHFlagsTest, FlagOperations) {
    LDHFlags flags = LDHFlags::None;
    EXPECT_FALSE(hasFlag(flags, LDHFlags::SplitBytes));
    
    flags = LDHFlags::SplitBytes;
    EXPECT_TRUE(hasFlag(flags, LDHFlags::SplitBytes));
    EXPECT_FALSE(hasFlag(flags, LDHFlags::HasChecksum));
    
    flags = LDHFlags::SplitBytes | LDHFlags::HasChecksum;
    EXPECT_TRUE(hasFlag(flags, LDHFlags::SplitBytes));
    EXPECT_TRUE(hasFlag(flags, LDHFlags::HasChecksum));
    
    LDHFlags combined = flags & LDHFlags::SplitBytes;
    EXPECT_TRUE(hasFlag(combined, LDHFlags::SplitBytes));
    EXPECT_FALSE(hasFlag(combined, LDHFlags::HasChecksum));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Predictor Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PredictorTest, FirstPixel) {
    std::vector<uint16_t> data = {1000};
    EXPECT_EQ(detail::predict(data.data(), 0, 0, 1), 0u);
}

TEST(PredictorTest, FirstRow) {
    std::vector<uint16_t> data = {100, 200, 300, 400};
    
    // x=0 should return 0
    EXPECT_EQ(detail::predict(data.data(), 0, 0, 4), 0u);
    
    // Rest of first row should use left neighbor
    EXPECT_EQ(detail::predict(data.data(), 1, 0, 4), 100u);
    EXPECT_EQ(detail::predict(data.data(), 2, 0, 4), 200u);
    EXPECT_EQ(detail::predict(data.data(), 3, 0, 4), 300u);
}

TEST(PredictorTest, FirstColumn) {
    // 4x2 heightmap
    std::vector<uint16_t> data = {
        100, 200, 300, 400,
        150, 250, 350, 450
    };
    
    // First column should use top neighbor
    EXPECT_EQ(detail::predict(data.data(), 0, 1, 4), 100u);
}

TEST(PredictorTest, PlanarPrediction) {
    // B -- C
    // |    |
    // A -- ?
    // ? = A + C - B
    
    // 2x2 heightmap
    std::vector<uint16_t> data = {
        100, 200,  // B=100, C=200
        150, 0     // A=150, predicted for (1,1) = 150 + 200 - 100 = 250
    };
    
    EXPECT_EQ(detail::predict(data.data(), 1, 1, 2), 250u);
}

TEST(PredictorTest, PlanarPredictionFlat) {
    // Flat terrain: all same value
    std::vector<uint16_t> data = {
        1000, 1000,
        1000, 0
    };
    
    // Predicted for (1,1) = 1000 + 1000 - 1000 = 1000
    EXPECT_EQ(detail::predict(data.data(), 1, 1, 2), 1000u);
}

TEST(PredictorTest, PlanarPredictionSlope) {
    // Linear slope
    // 100 -- 200
    //  |      |
    // 200 -- ?
    // Predicted: 200 + 200 - 100 = 300
    std::vector<uint16_t> data = {
        100, 200,
        200, 0
    };
    
    EXPECT_EQ(detail::predict(data.data(), 1, 1, 2), 300u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Delta Encoding Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DeltaEncodingTest, PositiveDelta) {
    uint16_t delta = detail::encodeDelta(150, 100);
    EXPECT_EQ(delta, 50u);
    
    uint16_t decoded = detail::decodeDelta(delta, 100);
    EXPECT_EQ(decoded, 150u);
}

TEST(DeltaEncodingTest, NegativeDelta) {
    // 100 - 150 = -50 -> wraps to 65486 (0xFFCE)
    uint16_t delta = detail::encodeDelta(100, 150);
    EXPECT_EQ(delta, 65486u);
    
    // 150 + 65486 wraps around to 100
    uint16_t decoded = detail::decodeDelta(delta, 150);
    EXPECT_EQ(decoded, 100u);
}

TEST(DeltaEncodingTest, ZeroDelta) {
    uint16_t delta = detail::encodeDelta(1000, 1000);
    EXPECT_EQ(delta, 0u);
    
    uint16_t decoded = detail::decodeDelta(delta, 1000);
    EXPECT_EQ(decoded, 1000u);
}

TEST(DeltaEncodingTest, LargeDelta) {
    uint16_t delta = detail::encodeDelta(65535, 0);
    EXPECT_EQ(delta, 65535u);
    
    uint16_t decoded = detail::decodeDelta(delta, 0);
    EXPECT_EQ(decoded, 65535u);
}

TEST(DeltaEncodingTest, Wraparound) {
    // Test maximum negative delta
    uint16_t delta = detail::encodeDelta(0, 65535);
    EXPECT_EQ(delta, 1u);  // 0 - 65535 = -65535 -> wraps to 1
    
    uint16_t decoded = detail::decodeDelta(delta, 65535);
    EXPECT_EQ(decoded, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRC32 Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CRC32Test, EmptyData) {
    std::vector<uint8_t> empty;
    uint32_t crc = calculateCRC32(empty);
    EXPECT_EQ(crc, 0x00000000u);
}

TEST(CRC32Test, KnownValue) {
    // "123456789" should have CRC32 = 0xCBF43926
    std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc = calculateCRC32(data);
    EXPECT_EQ(crc, 0xCBF43926u);
}

TEST(CRC32Test, Consistency) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    uint32_t crc1 = calculateCRC32(data);
    uint32_t crc2 = calculateCRC32(data);
    EXPECT_EQ(crc1, crc2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Compression/Decompression Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CompressionTest, EmptyInput) {
    std::vector<uint16_t> empty;
    auto result = compress(empty, 0, 0);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompressionError::InvalidInput);
}

TEST(CompressionTest, InvalidDimensions) {
    std::vector<uint16_t> data(16, 1000);
    
    // Width 0
    auto result1 = compress(data, 0, 4);
    EXPECT_FALSE(result1.has_value());
    EXPECT_EQ(result1.error(), CompressionError::InvalidDimensions);
    
    // Height 0
    auto result2 = compress(data, 4, 0);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), CompressionError::InvalidDimensions);
    
    // Size mismatch
    auto result3 = compress(data, 8, 8);  // Would need 64 samples
    EXPECT_FALSE(result3.has_value());
    EXPECT_EQ(result3.error(), CompressionError::InvalidDimensions);
}

TEST(CompressionTest, SmallFlat) {
    // 4x4 flat heightmap (too small for actual compression due to header overhead)
    std::vector<uint16_t> data(16, 32768);
    
    auto compressResult = compress(data, 4, 4, CompressionOptions::fast());
    ASSERT_TRUE(compressResult.has_value());
    
    const auto& compressed = compressResult.value();
    // Note: Very small data may not compress well due to header overhead (64 bytes)
    // For a 4x4 (32 bytes) heightmap, the compressed output will be larger
    EXPECT_EQ(compressed.stats.inputSize, 16 * sizeof(uint16_t));
    EXPECT_GT(compressed.data.size(), 0u);
    
    // Decompress and verify - lossless round-trip is the important property
    auto decompressResult = decompress(compressed.data);
    ASSERT_TRUE(decompressResult.has_value());
    
    const auto& decompressed = decompressResult.value();
    EXPECT_EQ(decompressed.width, 4u);
    EXPECT_EQ(decompressed.height, 4u);
    EXPECT_EQ(decompressed.data.size(), 16u);
    EXPECT_EQ(decompressed.data, data);
}

TEST(CompressionTest, SmallGradient) {
    // 8x8 gradient heightmap
    std::vector<uint16_t> data(64);
    for (size_t i = 0; i < 64; ++i) {
        data[i] = static_cast<uint16_t>(i * 1000);
    }
    
    auto compressResult = compress(data, 8, 8);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    const auto& decompressed = decompressResult.value();
    EXPECT_EQ(decompressed.width, 8u);
    EXPECT_EQ(decompressed.height, 8u);
    EXPECT_EQ(decompressed.data, data);
}

TEST(CompressionTest, RandomData) {
    // 32x32 random data
    std::vector<uint16_t> data(32 * 32);
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<uint16_t> dist(0, 65535);
    
    for (auto& val : data) {
        val = dist(gen);
    }
    
    auto compressResult = compress(data, 32, 32);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(CompressionTest, LargerHeightmap) {
    // 128x128 terrain-like data (smooth with some variation)
    const uint32_t size = 128;
    std::vector<uint16_t> data(size * size);
    std::mt19937 gen(12345);
    std::uniform_int_distribution<int16_t> noise(-100, 100);
    
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            // Base height with smooth gradient
            int32_t height = 30000 + static_cast<int32_t>(x * 100) + static_cast<int32_t>(y * 50);
            // Add noise
            height += noise(gen);
            // Clamp to valid range
            height = std::clamp(height, 0, 65535);
            data[y * size + x] = static_cast<uint16_t>(height);
        }
    }
    
    auto compressResult = compress(data, size, size);
    ASSERT_TRUE(compressResult.has_value());
    
    // Should have some compression for terrain-like data
    EXPECT_GT(compressResult.value().stats.compressionRatio, 1.5);
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(CompressionTest, WithChecksum) {
    std::vector<uint16_t> data(64, 12345);
    
    CompressionOptions options;
    options.addChecksum = true;
    
    auto compressResult = compress(data, 8, 8, options);
    ASSERT_TRUE(compressResult.has_value());
    
    // Verify header has checksum flag
    auto headerResult = readHeader(compressResult.value().data);
    ASSERT_TRUE(headerResult.has_value());
    EXPECT_TRUE(hasFlag(headerResult.value().getFlags(), LDHFlags::HasChecksum));
    
    // Decompression should verify checksum
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(CompressionTest, CorruptedChecksum) {
    std::vector<uint16_t> data(64, 12345);
    
    CompressionOptions options;
    options.addChecksum = true;
    
    auto compressResult = compress(data, 8, 8, options);
    ASSERT_TRUE(compressResult.has_value());
    
    // Corrupt the checksum (last 4 bytes)
    auto& compressed = compressResult.value().data;
    compressed[compressed.size() - 1] ^= 0xFF;
    
    auto decompressResult = decompress(compressed);
    EXPECT_FALSE(decompressResult.has_value());
    EXPECT_EQ(decompressResult.error(), CompressionError::ChecksumFailed);
}

TEST(CompressionTest, ExpectedDimensions) {
    std::vector<uint16_t> data(64, 5000);
    
    auto compressResult = compress(data, 8, 8);
    ASSERT_TRUE(compressResult.has_value());
    
    // Correct dimensions
    auto result1 = decompress(compressResult.value().data, 8, 8);
    EXPECT_TRUE(result1.has_value());
    
    // Wrong dimensions
    auto result2 = decompress(compressResult.value().data, 4, 16);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), CompressionError::InvalidDimensions);
}

TEST(CompressionTest, CompressionLevels) {
    std::vector<uint16_t> data(256, 10000);
    
    // Fast compression
    auto fastResult = compress(data, 16, 16, CompressionOptions::fast());
    ASSERT_TRUE(fastResult.has_value());
    
    // Default compression
    auto defaultResult = compress(data, 16, 16, CompressionOptions::defaults());
    ASSERT_TRUE(defaultResult.has_value());
    
    // Maximum compression (should be same or smaller)
    auto maxResult = compress(data, 16, 16, CompressionOptions::maximum());
    ASSERT_TRUE(maxResult.has_value());
    
    // All should decompress correctly
    EXPECT_EQ(decompress(fastResult.value().data).value().data, data);
    EXPECT_EQ(decompress(defaultResult.value().data).value().data, data);
    EXPECT_EQ(decompress(maxResult.value().data).value().data, data);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Header Reading Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeaderTest, ReadValidHeader) {
    std::vector<uint16_t> data(16, 1000);
    auto compressResult = compress(data, 4, 4);
    ASSERT_TRUE(compressResult.has_value());
    
    auto headerResult = readHeader(compressResult.value().data);
    ASSERT_TRUE(headerResult.has_value());
    
    const auto& header = headerResult.value();
    EXPECT_EQ(header.magic, LDH_MAGIC);
    EXPECT_EQ(header.version, LDH_VERSION);
    EXPECT_EQ(header.width, 4u);
    EXPECT_EQ(header.height, 4u);
}

TEST(HeaderTest, TooSmall) {
    std::vector<uint8_t> small(32, 0);  // Less than header size
    auto result = readHeader(small);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompressionError::InvalidHeader);
}

TEST(HeaderTest, InvalidMagic) {
    std::vector<uint8_t> data(LDH_HEADER_SIZE, 0);
    // Set some invalid magic
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;
    data[3] = 0x78;
    
    auto result = readHeader(data);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompressionError::InvalidHeader);
}

TEST(HeaderTest, GetInfoString) {
    LDHHeader header;
    header.init(1024, 512);
    header.lowStreamSize = 10000;
    header.highStreamSize = 5000;
    
    std::string info = getInfoString(header);
    
    EXPECT_NE(info.find("LDH1"), std::string::npos);
    EXPECT_NE(info.find("1024"), std::string::npos);
    EXPECT_NE(info.find("512"), std::string::npos);
    EXPECT_NE(info.find("10000"), std::string::npos);
    EXPECT_NE(info.find("5000"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Compression Options Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CompressionOptionsTest, Defaults) {
    auto opts = CompressionOptions::defaults();
    EXPECT_EQ(opts.zstdLevel, 19);
    EXPECT_FALSE(opts.addChecksum);
}

TEST(CompressionOptionsTest, Fast) {
    auto opts = CompressionOptions::fast();
    EXPECT_EQ(opts.zstdLevel, 3);
    EXPECT_FALSE(opts.addChecksum);
}

TEST(CompressionOptionsTest, Maximum) {
    auto opts = CompressionOptions::maximum();
    EXPECT_EQ(opts.zstdLevel, 22);
    EXPECT_TRUE(opts.addChecksum);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Encode/Decode Detail Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EncodeDecodeTest, RoundTrip) {
    const uint32_t width = 8;
    const uint32_t height = 8;
    const size_t count = width * height;
    
    // Create test data
    std::vector<uint16_t> input(count);
    std::iota(input.begin(), input.end(), 1000);  // 1000, 1001, 1002, ...
    
    // Encode
    std::vector<uint8_t> lowStream(count);
    std::vector<uint8_t> highStream(count);
    detail::encodeWithPredictor(input, width, height, lowStream, highStream);
    
    // Decode
    std::vector<uint16_t> output(count);
    detail::decodeWithPredictor(lowStream, highStream, width, height, output);
    
    EXPECT_EQ(output, input);
}

TEST(EncodeDecodeTest, FlatData) {
    const uint32_t width = 16;
    const uint32_t height = 16;
    const size_t count = width * height;
    
    // Flat heightmap should produce mostly zeros in streams
    std::vector<uint16_t> input(count, 50000);
    
    std::vector<uint8_t> lowStream(count);
    std::vector<uint8_t> highStream(count);
    detail::encodeWithPredictor(input, width, height, lowStream, highStream);
    
    // Count non-zero values (first row will have non-zero)
    auto nonZeroLow = static_cast<size_t>(std::count_if(lowStream.begin(), lowStream.end(), 
                                       [](uint8_t v) { return v != 0; }));
    
    // Most values should be zero (only first row and column have non-zero predictions)
    EXPECT_LT(nonZeroLow, count / 4);  // Should be much less than 25%
    
    // Verify round-trip
    std::vector<uint16_t> output(count);
    detail::decodeWithPredictor(lowStream, highStream, width, height, output);
    EXPECT_EQ(output, input);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Compression Statistics Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CompressionStatsTest, ValidStatistics) {
    std::vector<uint16_t> data(256, 12345);
    
    auto result = compress(data, 16, 16);
    ASSERT_TRUE(result.has_value());
    
    const auto& stats = result.value().stats;
    
    EXPECT_EQ(stats.inputSize, 256 * sizeof(uint16_t));
    EXPECT_GT(stats.outputSize, 0u);
    EXPECT_GT(stats.lowStreamSize, 0u);
    EXPECT_GT(stats.highStreamSize, 0u);
    EXPECT_GT(stats.compressionRatio, 0.0);
    EXPECT_GE(stats.encodeTimeMs, 0.0);
    EXPECT_GE(stats.zstdTimeMs, 0.0);
    EXPECT_GE(stats.totalTimeMs, 0.0);
    
    // Total time should be >= encode + zstd time
    EXPECT_GE(stats.totalTimeMs, stats.encodeTimeMs + stats.zstdTimeMs - 1.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EdgeCaseTest, SinglePixel) {
    std::vector<uint16_t> data = {42000};
    
    auto compressResult = compress(data, 1, 1);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().width, 1u);
    EXPECT_EQ(decompressResult.value().height, 1u);
    EXPECT_EQ(decompressResult.value().data[0], 42000u);
}

TEST(EdgeCaseTest, SingleRow) {
    std::vector<uint16_t> data = {100, 200, 300, 400};
    
    auto compressResult = compress(data, 4, 1);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().width, 4u);
    EXPECT_EQ(decompressResult.value().height, 1u);
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(EdgeCaseTest, SingleColumn) {
    std::vector<uint16_t> data = {100, 200, 300, 400};
    
    auto compressResult = compress(data, 1, 4);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().width, 1u);
    EXPECT_EQ(decompressResult.value().height, 4u);
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(EdgeCaseTest, MaxValues) {
    std::vector<uint16_t> data(16, 65535);
    
    auto compressResult = compress(data, 4, 4);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(EdgeCaseTest, MinValues) {
    std::vector<uint16_t> data(16, 0);
    
    auto compressResult = compress(data, 4, 4);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST(EdgeCaseTest, AlternatingValues) {
    // Worst case for predictor: alternating high/low values
    std::vector<uint16_t> data(64);
    for (size_t i = 0; i < 64; ++i) {
        data[i] = (i % 2 == 0) ? 0 : 65535;
    }
    
    auto compressResult = compress(data, 8, 8);
    ASSERT_TRUE(compressResult.has_value());
    
    auto decompressResult = decompress(compressResult.value().data);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().data, data);
}

// ═══════════════════════════════════════════════════════════════════════════════
// File I/O Tests
// ═══════════════════════════════════════════════════════════════════════════════

class FileIOTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir_;
    
    void SetUp() override {
        // Create a temporary directory for test files
        tempDir_ = std::filesystem::temp_directory_path() / "voxy_compression_test";
        std::filesystem::create_directories(tempDir_);
    }
    
    void TearDown() override {
        // Clean up temporary directory
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }
    
    std::filesystem::path getTempPath(const std::string& filename) {
        return tempDir_ / filename;
    }
};

TEST_F(FileIOTest, CompressAndDecompressFile) {
    // Create test data
    std::vector<uint16_t> data(64, 12345);
    const auto filePath = getTempPath("test.ldh");
    
    // Compress to file
    auto compressResult = compressToFile(data, 8, 8, filePath);
    ASSERT_TRUE(compressResult.has_value());
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filePath));
    EXPECT_GT(std::filesystem::file_size(filePath), 0u);
    
    // Decompress from file
    auto decompressResult = decompressFromFile(filePath);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().width, 8u);
    EXPECT_EQ(decompressResult.value().height, 8u);
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST_F(FileIOTest, CompressWithChecksum) {
    std::vector<uint16_t> data(64, 5000);
    const auto filePath = getTempPath("test_checksum.ldh");
    
    CompressionOptions options;
    options.addChecksum = true;
    
    auto compressResult = compressToFile(data, 8, 8, filePath, options);
    ASSERT_TRUE(compressResult.has_value());
    
    // Decompress and verify checksum is validated
    auto decompressResult = decompressFromFile(filePath);
    ASSERT_TRUE(decompressResult.has_value());
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST_F(FileIOTest, DecompressWithExpectedDimensions) {
    std::vector<uint16_t> data(64, 9999);
    const auto filePath = getTempPath("test_dims.ldh");
    
    auto compressResult = compressToFile(data, 8, 8, filePath);
    ASSERT_TRUE(compressResult.has_value());
    
    // Correct dimensions
    auto result1 = decompressFromFile(filePath, 8, 8);
    EXPECT_TRUE(result1.has_value());
    
    // Wrong dimensions
    auto result2 = decompressFromFile(filePath, 4, 16);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), CompressionError::InvalidDimensions);
}

TEST_F(FileIOTest, DecompressNonExistentFile) {
    const auto filePath = getTempPath("nonexistent.ldh");
    
    auto result = decompressFromFile(filePath);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompressionError::FileNotFound);
}

TEST_F(FileIOTest, ReadHeaderFromFile) {
    std::vector<uint16_t> data(256, 7777);
    const auto filePath = getTempPath("test_header.ldh");
    
    auto compressResult = compressToFile(data, 16, 16, filePath);
    ASSERT_TRUE(compressResult.has_value());
    
    auto headerResult = readHeaderFromFile(filePath);
    ASSERT_TRUE(headerResult.has_value());
    
    const auto& header = headerResult.value();
    EXPECT_EQ(header.magic, LDH_MAGIC);
    EXPECT_EQ(header.version, LDH_VERSION);
    EXPECT_EQ(header.width, 16u);
    EXPECT_EQ(header.height, 16u);
}

TEST_F(FileIOTest, ReadHeaderFromNonExistentFile) {
    const auto filePath = getTempPath("nonexistent_header.ldh");
    
    auto result = readHeaderFromFile(filePath);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompressionError::FileNotFound);
}

TEST_F(FileIOTest, CreateNestedDirectory) {
    std::vector<uint16_t> data(16, 1234);
    const auto filePath = tempDir_ / "nested" / "dir" / "test.ldh";
    
    auto compressResult = compressToFile(data, 4, 4, filePath);
    ASSERT_TRUE(compressResult.has_value());
    
    EXPECT_TRUE(std::filesystem::exists(filePath));
}

TEST_F(FileIOTest, LargerFileRoundTrip) {
    // 128x128 terrain-like data
    const uint32_t size = 128;
    std::vector<uint16_t> data(size * size);
    std::mt19937 gen(54321);
    std::uniform_int_distribution<int16_t> noise(-50, 50);
    
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            int32_t height = 30000 + static_cast<int32_t>(x * 50) + static_cast<int32_t>(y * 30);
            height += noise(gen);
            height = std::clamp(height, 0, 65535);
            data[y * size + x] = static_cast<uint16_t>(height);
        }
    }
    
    const auto filePath = getTempPath("large_terrain.ldh");
    
    auto compressResult = compressToFile(data, size, size, filePath);
    ASSERT_TRUE(compressResult.has_value());
    
    // Check file size is reasonable (should have good compression for terrain data)
    const auto fileSize = std::filesystem::file_size(filePath);
    const auto uncompressedSize = data.size() * sizeof(uint16_t);
    EXPECT_LT(fileSize, uncompressedSize);  // Should compress
    
    auto decompressResult = decompressFromFile(filePath);
    ASSERT_TRUE(decompressResult.has_value());
    
    EXPECT_EQ(decompressResult.value().width, size);
    EXPECT_EQ(decompressResult.value().height, size);
    EXPECT_EQ(decompressResult.value().data, data);
}

TEST_F(FileIOTest, CompressionStatsFromFile) {
    std::vector<uint16_t> data(256, 8888);
    const auto filePath = getTempPath("stats_test.ldh");
    
    auto result = compressToFile(data, 16, 16, filePath);
    ASSERT_TRUE(result.has_value());
    
    const auto& stats = result.value();
    EXPECT_EQ(stats.inputSize, 256 * sizeof(uint16_t));
    EXPECT_GT(stats.outputSize, 0u);
    EXPECT_GE(stats.totalTimeMs, 0.0);
}

} // namespace voxy::terrain

