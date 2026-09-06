// ═══════════════════════════════════════════════════════════════════════════════
// compression.cpp - Heightmap Compression and Decompression Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/compression.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>

// zstd for compression
#include <zstd.h>

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Error Handling
// ═══════════════════════════════════════════════════════════════════════════════

std::string_view errorToString(CompressionError error) noexcept {
    switch (error) {
        case CompressionError::None:
            return "No error";
        case CompressionError::FileNotFound:
            return "File not found";
        case CompressionError::InvalidInput:
            return "Invalid input data";
        case CompressionError::InvalidDimensions:
            return "Invalid dimensions";
        case CompressionError::InvalidHeader:
            return "Invalid LDH header";
        case CompressionError::UnsupportedVersion:
            return "Unsupported LDH version";
        case CompressionError::ZstdCompressFailed:
            return "zstd compression failed";
        case CompressionError::ZstdDecompressFailed:
            return "zstd decompression failed";
        case CompressionError::SizeMismatch:
            return "Size mismatch";
        case CompressionError::ChecksumFailed:
            return "Checksum verification failed";
        case CompressionError::OutOfMemory:
            return "Out of memory";
    }
    return "Unknown error";
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRC32 Implementation (IEEE 802.3 polynomial)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Generate the IEEE table at compile time so a mistyped entry cannot silently
// create a different checksum format. This is the CRC used by Python's zlib.
constexpr auto CRC32_TABLE = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t value = i;
        for (uint32_t bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1u) ? 0xEDB88320u : 0u);
        table[i] = value;
    }
    return table;
}();
static_assert(CRC32_TABLE[245] == 0xCDD70693u);

// Older native writers had one incorrect table entry. Existing LDH v1 terrain
// assets use that checksum. Recognize it only when reading; all new writes use
// IEEE CRC32. A mismatch against both variants remains a hard failure.
constexpr auto LEGACY_CRC32_TABLE = [] {
    auto table = CRC32_TABLE;
    table[245] = 0xCDD706B3u;
    return table;
}();

uint32_t legacyCRC32(std::span<const uint8_t> data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data)
        crc = LEGACY_CRC32_TABLE[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

class ZstdByteStream {
public:
    ZstdByteStream(const uint8_t* data, size_t size)
        : input_{data, size, 0} {}

    ZstdByteStream(const ZstdByteStream&) = delete;
    ZstdByteStream& operator=(const ZstdByteStream&) = delete;

    ~ZstdByteStream() {
        if (stream_) {
            ZSTD_freeDStream(stream_);
        }
    }

    [[nodiscard]] CompressionError init() {
        stream_ = ZSTD_createDStream();
        if (!stream_) {
            return CompressionError::OutOfMemory;
        }

        const size_t result = ZSTD_initDStream(stream_);
        if (ZSTD_isError(result)) {
            LOG_ERROR("zstd stream init failed: {}", ZSTD_getErrorName(result));
            return CompressionError::ZstdDecompressFailed;
        }

        return CompressionError::None;
    }

    [[nodiscard]] CompressionError read(uint8_t* dst, size_t size) {
        size_t copied = 0;

        while (copied < size) {
            if (readPos_ == writePos_) {
                const CompressionError refillResult = refill();
                if (refillResult != CompressionError::None) {
                    return refillResult;
                }
            }

            const size_t available = writePos_ - readPos_;
            const size_t toCopy = std::min(available, size - copied);
            std::memcpy(dst + copied, buffer_.data() + readPos_, toCopy);
            readPos_ += toCopy;
            copied += toCopy;
        }

        return CompressionError::None;
    }

    [[nodiscard]] CompressionError finish(size_t expectedSize) {
        if (decodedBytes_ > expectedSize || readPos_ != writePos_) {
            return CompressionError::SizeMismatch;
        }

        while (!finished_) {
            readPos_ = 0;
            writePos_ = 0;

            ZSTD_outBuffer output{buffer_.data(), buffer_.size(), 0};
            const size_t result = ZSTD_decompressStream(stream_, &output, &input_);
            if (ZSTD_isError(result)) {
                LOG_ERROR("zstd streaming decompression failed: {}", ZSTD_getErrorName(result));
                return CompressionError::ZstdDecompressFailed;
            }

            if (output.pos != 0) {
                decodedBytes_ += output.pos;
                return CompressionError::SizeMismatch;
            }

            if (result == 0) {
                finished_ = true;
            } else if (input_.pos == input_.size) {
                return CompressionError::SizeMismatch;
            }
        }

        if (decodedBytes_ != expectedSize || input_.pos != input_.size) {
            return CompressionError::SizeMismatch;
        }

        return CompressionError::None;
    }

private:
    [[nodiscard]] CompressionError refill() {
        if (finished_) {
            return CompressionError::SizeMismatch;
        }

        readPos_ = 0;
        writePos_ = 0;

        while (writePos_ == 0) {
            ZSTD_outBuffer output{buffer_.data(), buffer_.size(), 0};
            const size_t result = ZSTD_decompressStream(stream_, &output, &input_);
            if (ZSTD_isError(result)) {
                LOG_ERROR("zstd streaming decompression failed: {}", ZSTD_getErrorName(result));
                return CompressionError::ZstdDecompressFailed;
            }

            writePos_ = output.pos;
            decodedBytes_ += output.pos;

            if (result == 0) {
                finished_ = true;
                break;
            }

            if (input_.pos == input_.size && writePos_ == 0) {
                return CompressionError::SizeMismatch;
            }
        }

        return writePos_ == 0 ? CompressionError::SizeMismatch : CompressionError::None;
    }

    static constexpr size_t BUFFER_SIZE = 64 * 1024;

    ZSTD_DStream* stream_ = nullptr;
    ZSTD_inBuffer input_{};
    std::array<uint8_t, BUFFER_SIZE> buffer_{};
    size_t readPos_ = 0;
    size_t writePos_ = 0;
    size_t decodedBytes_ = 0;
    bool finished_ = false;
};

[[nodiscard]] CompressionError validateHeaderAndSize(
    const LDHHeader& header, size_t inputSize) {
    if (header.magic != LDH_MAGIC) {
        LOG_ERROR("Invalid LDH magic: expected 0x{:08X}, got 0x{:08X}",
                  LDH_MAGIC, header.magic);
        return CompressionError::InvalidHeader;
    }
    if (header.version != LDH_VERSION) {
        LOG_ERROR("Unsupported LDH version: {}", header.version);
        return CompressionError::UnsupportedVersion;
    }
    if (!header.isValid()) {
        LOG_ERROR("Invalid LDH header");
        return CompressionError::InvalidHeader;
    }

    constexpr uint32_t supportedFlags =
        static_cast<uint32_t>(LDHFlags::SplitBytes)
        | static_cast<uint32_t>(LDHFlags::HasChecksum);
    const size_t maximumCanonicalStreamSize =
        ZSTD_compressBound(header.sampleCount());
    if (!hasFlag(header.getFlags(), LDHFlags::SplitBytes)
        || (header.flags & ~supportedFlags) != 0u
        || header.lowStreamSize == 0u || header.highStreamSize == 0u
        || header.lowStreamSize > maximumCanonicalStreamSize
        || header.highStreamSize > maximumCanonicalStreamSize
        || std::any_of(std::begin(header.reserved), std::end(header.reserved),
                       [](uint32_t value) { return value != 0u; })) {
        LOG_ERROR("Unsupported or malformed LDH v1 payload descriptor");
        return CompressionError::InvalidHeader;
    }

    const size_t expectedSize = header.expectedFileSize();
    if (expectedSize == 0u || inputSize != expectedSize) {
        LOG_ERROR("LDH size mismatch: expected {} bytes, got {}",
                  expectedSize, inputSize);
        return CompressionError::SizeMismatch;
    }
    return CompressionError::None;
}

} // anonymous namespace

uint32_t calculateCRC32(std::span<const uint8_t> data) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t byte : data) {
        crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Detail Namespace (Encoding/Decoding)
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {

void encodeWithPredictor(std::span<const uint16_t> input,
                         uint32_t width, uint32_t height,
                         std::span<uint8_t> lowStream,
                         std::span<uint8_t> highStream) {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            const uint16_t actual = input[idx];
            const uint16_t predicted = predict(input.data(), x, y, width);
            const uint16_t delta = encodeDelta(actual, predicted);
            
            lowStream[idx] = static_cast<uint8_t>(delta & 0xFF);
            highStream[idx] = static_cast<uint8_t>(delta >> 8);
        }
    }
}

void decodeWithPredictor(std::span<const uint8_t> lowStream,
                         std::span<const uint8_t> highStream,
                         uint32_t width, uint32_t height,
                         std::span<uint16_t> output) {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            const uint16_t delta = static_cast<uint16_t>(static_cast<uint16_t>(lowStream[idx]) | 
                                   (static_cast<uint16_t>(highStream[idx]) << 8));
            const uint16_t predicted = predict(output.data(), x, y, width);
            output[idx] = decodeDelta(delta, predicted);
        }
    }
}

} // namespace detail

namespace {

[[nodiscard]] CompressionError decodeStreamingWithPredictor(
    const uint8_t* lowCompressed,
    size_t lowCompressedSize,
    const uint8_t* highCompressed,
    size_t highCompressedSize,
    uint32_t width,
    uint32_t height,
    std::span<uint16_t> output) {

    ZstdByteStream lowStream(lowCompressed, lowCompressedSize);
    ZstdByteStream highStream(highCompressed, highCompressedSize);

    CompressionError result = lowStream.init();
    if (result != CompressionError::None) {
        return result;
    }

    result = highStream.init();
    if (result != CompressionError::None) {
        return result;
    }

    std::vector<uint8_t> lowRow(width);
    std::vector<uint8_t> highRow(width);

    for (uint32_t y = 0; y < height; ++y) {
        result = lowStream.read(lowRow.data(), width);
        if (result != CompressionError::None) {
            return result;
        }

        result = highStream.read(highRow.data(), width);
        if (result != CompressionError::None) {
            return result;
        }

        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            const uint16_t delta = static_cast<uint16_t>(
                static_cast<uint16_t>(lowRow[x]) |
                (static_cast<uint16_t>(highRow[x]) << 8));
            const uint16_t predicted = detail::predict(output.data(), x, y, width);
            output[idx] = detail::decodeDelta(delta, predicted);
        }
    }

    const size_t sampleCount = static_cast<size_t>(width) * height;

    result = lowStream.finish(sampleCount);
    if (result != CompressionError::None) {
        return result;
    }

    result = highStream.finish(sampleCount);
    if (result != CompressionError::None) {
        return result;
    }

    return CompressionError::None;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Compression
// ═══════════════════════════════════════════════════════════════════════════════

CompressionResult<CompressResult> compress(
    std::span<const uint16_t> input,
    uint32_t width,
    uint32_t height,
    const CompressionOptions& options) {
#if defined(__cpp_exceptions)
    try {
#endif
    
    using Clock = std::chrono::high_resolution_clock;
    const auto startTime = Clock::now();
    
    // Validate input
    if (input.empty()) {
        LOG_ERROR("Compression failed: empty input");
        return CompressionError::InvalidInput;
    }
    
    if (width == 0 || height == 0
        || width > LDH_MAX_DIMENSION || height > LDH_MAX_DIMENSION) {
        LOG_ERROR("Compression failed: invalid dimensions {}x{}", width, height);
        return CompressionError::InvalidDimensions;
    }
    
    const size_t sampleCount = static_cast<size_t>(width) * height;
    if (input.size() != sampleCount) {
        LOG_ERROR("Compression failed: size mismatch (expected {}, got {})", 
                  sampleCount, input.size());
        return CompressionError::InvalidDimensions;
    }
    if (options.zstdLevel < ZSTD_minCLevel()
        || options.zstdLevel > ZSTD_maxCLevel()) {
        LOG_ERROR("Compression failed: invalid zstd level {}",
                  options.zstdLevel);
        return CompressionError::InvalidInput;
    }
    
    CompressionStats stats;
    stats.inputSize = sampleCount * sizeof(uint16_t);
    
    // Allocate streams for predictor encoding
    std::vector<uint8_t> lowStream(sampleCount);
    std::vector<uint8_t> highStream(sampleCount);
    
    // Apply planar predictor encoding
    const auto encodeStart = Clock::now();
    detail::encodeWithPredictor(input, width, height, lowStream, highStream);
    const auto encodeEnd = Clock::now();
    stats.encodeTimeMs = std::chrono::duration<double, std::milli>(encodeEnd - encodeStart).count();
    
    // Compress streams with zstd
    const auto zstdStart = Clock::now();
    
    const size_t lowBound = ZSTD_compressBound(lowStream.size());
    const size_t highBound = ZSTD_compressBound(highStream.size());
    
    std::vector<uint8_t> compressedLow(lowBound);
    std::vector<uint8_t> compressedHigh(highBound);
    
    const size_t lowCompressedSize = ZSTD_compress(
        compressedLow.data(), compressedLow.size(),
        lowStream.data(), lowStream.size(),
        options.zstdLevel
    );
    
    if (ZSTD_isError(lowCompressedSize)) {
        LOG_ERROR("zstd compression failed for low stream: {}", 
                  ZSTD_getErrorName(lowCompressedSize));
        return CompressionError::ZstdCompressFailed;
    }
    
    const size_t highCompressedSize = ZSTD_compress(
        compressedHigh.data(), compressedHigh.size(),
        highStream.data(), highStream.size(),
        options.zstdLevel
    );
    
    if (ZSTD_isError(highCompressedSize)) {
        LOG_ERROR("zstd compression failed for high stream: {}", 
                  ZSTD_getErrorName(highCompressedSize));
        return CompressionError::ZstdCompressFailed;
    }
    if (lowCompressedSize > std::numeric_limits<uint32_t>::max()
        || highCompressedSize > std::numeric_limits<uint32_t>::max()) {
        LOG_ERROR("Compressed LDH streams exceed the v1 size fields");
        return CompressionError::InvalidInput;
    }
    
    const auto zstdEnd = Clock::now();
    stats.zstdTimeMs = std::chrono::duration<double, std::milli>(zstdEnd - zstdStart).count();
    
    // Build output file
    LDHHeader header;
    header.init(width, height);
    header.lowStreamSize = static_cast<uint32_t>(lowCompressedSize);
    header.highStreamSize = static_cast<uint32_t>(highCompressedSize);
    
    if (options.addChecksum) {
        header.setFlags(header.getFlags() | LDHFlags::HasChecksum);
    }
    
    // Calculate output size
    size_t outputSize = LDH_HEADER_SIZE + lowCompressedSize + highCompressedSize;
    if (options.addChecksum) {
        outputSize += sizeof(uint32_t);
    }
    
    std::vector<uint8_t> output(outputSize);
    
    // Write header
    std::memcpy(output.data(), &header, LDH_HEADER_SIZE);
    
    // Write compressed streams
    std::memcpy(output.data() + LDH_HEADER_SIZE, 
                compressedLow.data(), lowCompressedSize);
    std::memcpy(output.data() + LDH_HEADER_SIZE + lowCompressedSize, 
                compressedHigh.data(), highCompressedSize);
    
    // Write checksum if requested
    if (options.addChecksum) {
        const uint32_t checksum = calculateCRC32(
            std::span<const uint8_t>(output.data(), outputSize - sizeof(uint32_t))
        );
        std::memcpy(output.data() + outputSize - sizeof(uint32_t), 
                    &checksum, sizeof(uint32_t));
    }
    
    const auto endTime = Clock::now();
    stats.totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    stats.outputSize = output.size();
    stats.lowStreamSize = lowCompressedSize;
    stats.highStreamSize = highCompressedSize;
    stats.compressionRatio = static_cast<double>(stats.inputSize) / 
                             static_cast<double>(stats.outputSize);
    
    LOG_INFO("Compressed {}x{} heightmap: {} -> {} bytes ({:.2f}x, {:.2f} ms)",
             width, height, stats.inputSize, stats.outputSize, 
             stats.compressionRatio, stats.totalTimeMs);
    
    return CompressResult{
        .data = std::move(output),
        .stats = stats
    };
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        LOG_ERROR("Compression failed: out of memory");
        return CompressionError::OutOfMemory;
    } catch (const std::length_error&) {
        LOG_ERROR("Compression failed: allocation size is not representable");
        return CompressionError::OutOfMemory;
    }
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
// Decompression
// ═══════════════════════════════════════════════════════════════════════════════

CompressionResult<DecompressResult> decompress(std::span<const uint8_t> input) {
#if defined(__cpp_exceptions)
    try {
#endif
    using Clock = std::chrono::high_resolution_clock;
    const auto startTime = Clock::now();
    
    // Read and validate header
    auto headerResult = readHeader(input);
    if (!headerResult) {
        return headerResult.error();
    }
    
    const LDHHeader& header = headerResult.value();
    const size_t sampleCount = header.sampleCount();
    
    CompressionStats stats;
    stats.inputSize = input.size();
    stats.lowStreamSize = header.lowStreamSize;
    stats.highStreamSize = header.highStreamSize;
    
    // Get compressed stream pointers
    const uint8_t* lowCompressed = input.data() + LDH_HEADER_SIZE;
    const uint8_t* highCompressed = lowCompressed + header.lowStreamSize;
    
    // Verify checksum if present
    if (hasFlag(header.getFlags(), LDHFlags::HasChecksum)) {
        const size_t dataSize = input.size() - sizeof(uint32_t);
        uint32_t storedChecksum = 0;
        std::memcpy(&storedChecksum, input.data() + dataSize, sizeof(storedChecksum));
        const uint32_t computedChecksum = calculateCRC32(
            std::span<const uint8_t>(input.data(), dataSize)
        );
        
        if (storedChecksum != computedChecksum
            && storedChecksum != legacyCRC32(input.first(dataSize))) {
            LOG_ERROR("Checksum verification failed: expected 0x{:08X}, got 0x{:08X}",
                      storedChecksum, computedChecksum);
            return CompressionError::ChecksumFailed;
        }
    }
    
    // Stream zstd byte planes and decode the predictor directly into final output.
    const auto zstdStart = Clock::now();
    std::vector<uint16_t> output(sampleCount);

    const CompressionError decodeResult = decodeStreamingWithPredictor(
        lowCompressed,
        header.lowStreamSize,
        highCompressed,
        header.highStreamSize,
        header.width,
        header.height,
        output
    );

    if (decodeResult != CompressionError::None) {
        if (decodeResult == CompressionError::SizeMismatch) {
            LOG_ERROR("Decompressed stream size mismatch: expected {} bytes per stream", sampleCount);
        }
        return decodeResult;
    }

    const auto zstdEnd = Clock::now();
    stats.zstdTimeMs = std::chrono::duration<double, std::milli>(zstdEnd - zstdStart).count();
    stats.encodeTimeMs = 0.0;
    
    const auto endTime = Clock::now();
    stats.totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    stats.outputSize = sampleCount * sizeof(uint16_t);
    stats.compressionRatio = static_cast<double>(stats.outputSize) / 
                             static_cast<double>(stats.inputSize);
    
    LOG_INFO("Decompressed {}x{} heightmap: {} -> {} bytes ({:.2f} ms)",
             header.width, header.height, stats.inputSize, stats.outputSize, 
             stats.totalTimeMs);
    
    return DecompressResult{
        .data = std::move(output),
        .width = header.width,
        .height = header.height,
        .stats = stats
    };
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        LOG_ERROR("Decompression failed: out of memory");
        return CompressionError::OutOfMemory;
    } catch (const std::length_error&) {
        LOG_ERROR("Decompression failed: allocation size is not representable");
        return CompressionError::OutOfMemory;
    }
#endif
}

CompressionResult<DecompressResult> decompress(
    std::span<const uint8_t> input,
    uint32_t expectedWidth,
    uint32_t expectedHeight) {
    
    auto result = decompress(input);
    if (!result) {
        return result.error();
    }
    
    auto& decompressed = result.value();
    if (decompressed.width != expectedWidth || decompressed.height != expectedHeight) {
        LOG_ERROR("Dimension mismatch: expected {}x{}, got {}x{}",
                  expectedWidth, expectedHeight, 
                  decompressed.width, decompressed.height);
        return CompressionError::InvalidDimensions;
    }
    
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Info Functions
// ═══════════════════════════════════════════════════════════════════════════════

CompressionResult<LDHHeader> readHeader(std::span<const uint8_t> input) {
    if (input.size() < LDH_HEADER_SIZE) {
        LOG_ERROR("Input too small for LDH header: {} bytes", input.size());
        return CompressionError::InvalidHeader;
    }
    
    LDHHeader header;
    std::memcpy(&header, input.data(), LDH_HEADER_SIZE);

    const CompressionError validation =
        validateHeaderAndSize(header, input.size());
    if (validation != CompressionError::None) return validation;
    
    return header;
}

std::string getInfoString(const LDHHeader& header) {
    std::ostringstream oss;
    oss << "LDH File Info:\n";
    oss << "  Magic: LDH1\n";
    oss << "  Version: " << header.version << "\n";
    oss << "  Dimensions: " << header.width << " x " << header.height << "\n";
    oss << "  Flags: ";
    
    auto flags = header.getFlags();
    if (flags == LDHFlags::None) {
        oss << "None";
    } else {
        bool first = true;
        if (hasFlag(flags, LDHFlags::SplitBytes)) {
            oss << "SPLIT_BYTES";
            first = false;
        }
        if (hasFlag(flags, LDHFlags::HasChecksum)) {
            if (!first) oss << " | ";
            oss << "HAS_CHECKSUM";
            first = false;
        }
        if (hasFlag(flags, LDHFlags::HasMetadata)) {
            if (!first) oss << " | ";
            oss << "HAS_METADATA";
        }
    }
    oss << "\n";
    
    oss << "  Low stream: " << header.lowStreamSize << " bytes\n";
    oss << "  High stream: " << header.highStreamSize << " bytes\n";
    oss << "  Total compressed: " << header.compressedDataSize() << " bytes\n";
    oss << "  Uncompressed size: " << header.uncompressedSize() << " bytes\n";
    
    const double ratio = static_cast<double>(header.uncompressedSize()) / 
                         static_cast<double>(header.compressedDataSize() + LDH_HEADER_SIZE);
    oss << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << "x\n";
    
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════════
// File I/O Functions
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Helper to read entire file into memory
[[nodiscard]] CompressionResult<std::vector<uint8_t>> readFileToMemory(
    const std::filesystem::path& path) {

    // Validate the fixed-size header and exact on-disk length before allocating
    // storage proportional to a file controlled by the caller.
    auto headerResult = readHeaderFromFile(path);
    if (!headerResult) return headerResult.error();
    const size_t expectedSize = headerResult.value().expectedFileSize();

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open LDH file: {}", path.string());
        return CompressionError::InvalidInput;
    }

#if defined(__cpp_exceptions)
    try {
#endif
    std::vector<uint8_t> buffer(expectedSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(expectedSize))) {
        LOG_ERROR("Failed to read LDH file: {}", path.string());
        return CompressionError::InvalidInput;
    }
    if (file.peek() != std::char_traits<char>::eof()) {
        LOG_ERROR("LDH file changed or contains trailing data: {}",
                  path.string());
        return CompressionError::SizeMismatch;
    }
    return buffer;
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        return CompressionError::OutOfMemory;
    } catch (const std::length_error&) {
        return CompressionError::OutOfMemory;
    }
#endif
}

/// Helper to write data to file
[[nodiscard]] bool writeFileFromMemory(
    const std::filesystem::path& path,
    std::span<const uint8_t> data) {
    
    // Ensure parent directory exists
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        // Ignore error if directory already exists
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to create LDH file: {}", path.string());
        return false;
    }
    
    if (!file.write(reinterpret_cast<const char*>(data.data()), 
                    static_cast<std::streamsize>(data.size()))) {
        LOG_ERROR("Failed to write LDH file: {}", path.string());
        return false;
    }

    // Closing flushes buffered writes and can fail even when write succeeded.
    file.close();
    if (!file) {
        LOG_ERROR("Failed to finish LDH file: {}", path.string());
        return false;
    }
    
    return true;
}

} // anonymous namespace

CompressionResult<CompressionStats> compressToFile(
    std::span<const uint16_t> input,
    uint32_t width,
    uint32_t height,
    const std::filesystem::path& outputPath,
    const CompressionOptions& options) {
    
    // Compress to memory
    auto compressResult = compress(input, width, height, options);
    if (!compressResult) {
        return compressResult.error();
    }
    
    auto& result = compressResult.value();
    
    // Write to file
    if (!writeFileFromMemory(outputPath, result.data)) {
        return CompressionError::InvalidInput;  // File write error
    }
    
    LOG_INFO("Wrote LDH file: {} ({} bytes)", outputPath.string(), result.data.size());
    
    return result.stats;
}

CompressionResult<DecompressResult> decompressFromFile(
    const std::filesystem::path& inputPath) {
    
    // Read file into memory
    auto fileDataResult = readFileToMemory(inputPath);
    if (!fileDataResult) {
        return fileDataResult.error();
    }
    
    // Decompress
    return decompress(fileDataResult.value());
}

CompressionResult<DecompressResult> decompressFromFile(
    const std::filesystem::path& inputPath,
    uint32_t expectedWidth,
    uint32_t expectedHeight) {
    
    // Read file into memory
    auto fileDataResult = readFileToMemory(inputPath);
    if (!fileDataResult) {
        return fileDataResult.error();
    }
    
    // Decompress with dimension validation
    return decompress(fileDataResult.value(), expectedWidth, expectedHeight);
}

CompressionResult<LDHHeader> readHeaderFromFile(
    const std::filesystem::path& inputPath) {

    std::error_code existsError;
    if (!std::filesystem::exists(inputPath, existsError)) {
        LOG_ERROR("LDH file not found: {}", inputPath.string());
        return existsError ? CompressionError::InvalidInput
                           : CompressionError::FileNotFound;
    }
    
    std::ifstream file(inputPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open LDH file: {}", inputPath.string());
        return CompressionError::InvalidInput;
    }
    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    if (fileSize < static_cast<std::streamoff>(LDH_HEADER_SIZE)) {
        LOG_ERROR("LDH file is too small for a header: {}", inputPath.string());
        return CompressionError::InvalidHeader;
    }
    const auto fileSizeOffset = static_cast<std::streamoff>(fileSize);
    file.seekg(0, std::ios::beg);
    
    // Read just the header
    std::vector<uint8_t> headerData(LDH_HEADER_SIZE);
    if (!file.read(reinterpret_cast<char*>(headerData.data()), LDH_HEADER_SIZE)) {
        LOG_ERROR("Failed to read LDH header from: {}", inputPath.string());
        return CompressionError::InvalidHeader;
    }
    
    // Parse header without full file size validation
    LDHHeader header;
    std::memcpy(&header, headerData.data(), LDH_HEADER_SIZE);

    if (static_cast<uintmax_t>(fileSizeOffset)
        > std::numeric_limits<size_t>::max()) {
        return CompressionError::SizeMismatch;
    }
    const CompressionError validation = validateHeaderAndSize(
        header, static_cast<size_t>(fileSizeOffset));
    if (validation != CompressionError::None) return validation;
    
    return header;
}

} // namespace voxy::terrain
