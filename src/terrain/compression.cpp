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
#include <sstream>

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

// CRC32 lookup table (IEEE 802.3 polynomial: 0xEDB88320)
constexpr uint32_t CRC32_TABLE[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

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
    
    using Clock = std::chrono::high_resolution_clock;
    const auto startTime = Clock::now();
    
    // Validate input
    if (input.empty()) {
        LOG_ERROR("Compression failed: empty input");
        return CompressionError::InvalidInput;
    }
    
    if (width == 0 || height == 0) {
        LOG_ERROR("Compression failed: invalid dimensions {}x{}", width, height);
        return CompressionError::InvalidDimensions;
    }
    
    const size_t sampleCount = static_cast<size_t>(width) * height;
    if (input.size() != sampleCount) {
        LOG_ERROR("Compression failed: size mismatch (expected {}, got {})", 
                  sampleCount, input.size());
        return CompressionError::InvalidDimensions;
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
}

// ═══════════════════════════════════════════════════════════════════════════════
// Decompression
// ═══════════════════════════════════════════════════════════════════════════════

CompressionResult<DecompressResult> decompress(std::span<const uint8_t> input) {
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
        const uint32_t storedChecksum = *reinterpret_cast<const uint32_t*>(
            input.data() + dataSize
        );
        const uint32_t computedChecksum = calculateCRC32(
            std::span<const uint8_t>(input.data(), dataSize)
        );
        
        if (storedChecksum != computedChecksum) {
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
    
    // Verify file size is sufficient
    const size_t expectedMinSize = header.expectedFileSize();
    if (input.size() < expectedMinSize) {
        LOG_ERROR("File too small: expected at least {} bytes, got {}", 
                  expectedMinSize, input.size());
        return CompressionError::SizeMismatch;
    }
    
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
    
    if (!std::filesystem::exists(path)) {
        LOG_ERROR("LDH file not found: {}", path.string());
        return CompressionError::FileNotFound;
    }
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open LDH file: {}", path.string());
        return CompressionError::InvalidInput;
    }
    
    const auto fileSize = file.tellg();
    if (fileSize <= 0) {
        LOG_ERROR("LDH file is empty or unreadable: {}", path.string());
        return CompressionError::InvalidInput;
    }
    
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        LOG_ERROR("Failed to read LDH file: {}", path.string());
        return CompressionError::InvalidInput;
    }
    
    return buffer;
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
    
    if (!std::filesystem::exists(inputPath)) {
        LOG_ERROR("LDH file not found: {}", inputPath.string());
        return CompressionError::FileNotFound;
    }
    
    std::ifstream file(inputPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open LDH file: {}", inputPath.string());
        return CompressionError::InvalidInput;
    }
    
    // Read just the header
    std::vector<uint8_t> headerData(LDH_HEADER_SIZE);
    if (!file.read(reinterpret_cast<char*>(headerData.data()), LDH_HEADER_SIZE)) {
        LOG_ERROR("Failed to read LDH header from: {}", inputPath.string());
        return CompressionError::InvalidHeader;
    }
    
    // Parse header without full file size validation
    LDHHeader header;
    std::memcpy(&header, headerData.data(), LDH_HEADER_SIZE);
    
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
    
    return header;
}

} // namespace voxy::terrain
