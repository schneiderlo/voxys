// ═══════════════════════════════════════════════════════════════════════════════
// compression.hpp - Heightmap Compression and Decompression (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements the LDH (Limited Detail Heightmap) compression format with:
// - Planar predictor encoding for spatial coherence exploitation
// - Byte-stream splitting for improved entropy coding
// - zstd compression for final output
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace voxy::terrain {

// ─────────────────────────────────────────────────────────────────────────────
// LDH File Format Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Magic number for LDH files: 'LDH1' (0x4C444831)
constexpr uint32_t LDH_MAGIC = 0x4C444831;

/// Current LDH format version
constexpr uint32_t LDH_VERSION = 1;

/// LDH header size in bytes
constexpr size_t LDH_HEADER_SIZE = 64;

// ─────────────────────────────────────────────────────────────────────────────
// LDH Flags
// ─────────────────────────────────────────────────────────────────────────────

/// Feature flags for LDH files
enum class LDHFlags : uint32_t {
    None          = 0x00,  ///< No flags
    SplitBytes    = 0x01,  ///< Byte-split encoding used
    Split10_6     = 0x02,  ///< 10/6 bit split encoding (reserved for future)
    HasChecksum   = 0x04,  ///< CRC32 checksum present at end of file
    HasMetadata   = 0x08,  ///< Optional metadata block present
};

/// Bitwise OR for LDHFlags
inline constexpr LDHFlags operator|(LDHFlags lhs, LDHFlags rhs) noexcept {
    return static_cast<LDHFlags>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)
    );
}

/// Bitwise AND for LDHFlags
inline constexpr LDHFlags operator&(LDHFlags lhs, LDHFlags rhs) noexcept {
    return static_cast<LDHFlags>(
        static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)
    );
}

/// Check if a flag is set
inline constexpr bool hasFlag(LDHFlags flags, LDHFlags flag) noexcept {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// LDH Header Structure
// ─────────────────────────────────────────────────────────────────────────────

/// LDH file header (64 bytes, little-endian)
struct LDHHeader {
    uint32_t magic;           ///< 'LDH1' (0x4C444831)
    uint32_t version;         ///< Format version (1)
    uint32_t width;           ///< Heightmap width in samples
    uint32_t height;          ///< Heightmap height in samples
    uint32_t flags;           ///< Feature flags (LDHFlags)
    uint32_t lowStreamSize;   ///< Compressed low-byte stream size in bytes
    uint32_t highStreamSize;  ///< Compressed high-byte stream size in bytes
    uint32_t reserved[9];     ///< Reserved for future use, zero-filled
    
    /// Initialize with default values
    void init(uint32_t w, uint32_t h) noexcept {
        magic = LDH_MAGIC;
        version = LDH_VERSION;
        width = w;
        height = h;
        flags = static_cast<uint32_t>(LDHFlags::SplitBytes);
        lowStreamSize = 0;
        highStreamSize = 0;
        for (auto& r : reserved) r = 0;
    }
    
    /// Validate header
    [[nodiscard]] bool isValid() const noexcept {
        return magic == LDH_MAGIC && 
               version == LDH_VERSION &&
               width > 0 && 
               height > 0;
    }
    
    /// Get flags as enum
    [[nodiscard]] LDHFlags getFlags() const noexcept {
        return static_cast<LDHFlags>(flags);
    }
    
    /// Set flags
    void setFlags(LDHFlags f) noexcept {
        flags = static_cast<uint32_t>(f);
    }
    
    /// Get total sample count
    [[nodiscard]] size_t sampleCount() const noexcept {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    
    /// Get uncompressed size in bytes
    [[nodiscard]] size_t uncompressedSize() const noexcept {
        return sampleCount() * sizeof(uint16_t);
    }
    
    /// Get total compressed data size (low + high streams)
    [[nodiscard]] size_t compressedDataSize() const noexcept {
        return static_cast<size_t>(lowStreamSize) + static_cast<size_t>(highStreamSize);
    }
    
    /// Get expected file size
    [[nodiscard]] size_t expectedFileSize() const noexcept {
        size_t size = LDH_HEADER_SIZE + compressedDataSize();
        if (hasFlag(getFlags(), LDHFlags::HasChecksum)) {
            size += sizeof(uint32_t);
        }
        return size;
    }
};

static_assert(sizeof(LDHHeader) == LDH_HEADER_SIZE, "LDHHeader must be 64 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Error Types
// ─────────────────────────────────────────────────────────────────────────────

/// Error codes for compression operations
enum class CompressionError {
    None = 0,               ///< No error (success)
    FileNotFound,           ///< File not found
    InvalidInput,           ///< Input data is invalid or empty
    InvalidDimensions,      ///< Dimensions are invalid or don't match data size
    InvalidHeader,          ///< LDH header is invalid
    UnsupportedVersion,     ///< LDH version is not supported
    ZstdCompressFailed,     ///< zstd compression failed
    ZstdDecompressFailed,   ///< zstd decompression failed
    SizeMismatch,           ///< Decompressed size doesn't match expected
    ChecksumFailed,         ///< Checksum verification failed
    OutOfMemory,            ///< Memory allocation failed
};

/// Convert error code to human-readable string
[[nodiscard]] std::string_view errorToString(CompressionError error) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Result Type (reuse from heightmap.hpp pattern)
// ─────────────────────────────────────────────────────────────────────────────

/// Simple result type for compression operations
template<typename T>
class CompressionResult {
public:
    CompressionResult(T value) : data_(std::move(value)) {}
    CompressionResult(CompressionError error) : data_(error) {}
    
    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    
    [[nodiscard]] bool has_error() const noexcept {
        return std::holds_alternative<CompressionError>(data_);
    }
    
    explicit operator bool() const noexcept { return has_value(); }
    
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }
    
    [[nodiscard]] CompressionError error() const noexcept { 
        return std::get<CompressionError>(data_); 
    }
    
private:
    std::variant<T, CompressionError> data_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Compression Options
// ─────────────────────────────────────────────────────────────────────────────

/// Options for compression
struct CompressionOptions {
    int zstdLevel = 19;         ///< zstd compression level (1-22, default: 19)
    bool addChecksum = false;   ///< Add CRC32 checksum at end of file
    
    /// Default compression options (high compression)
    static CompressionOptions defaults() noexcept {
        return CompressionOptions{};
    }
    
    /// Fast compression options
    static CompressionOptions fast() noexcept {
        return CompressionOptions{.zstdLevel = 3, .addChecksum = false};
    }
    
    /// Maximum compression options
    static CompressionOptions maximum() noexcept {
        return CompressionOptions{.zstdLevel = 22, .addChecksum = true};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Compression Statistics
// ─────────────────────────────────────────────────────────────────────────────

/// Statistics from a compression/decompression operation
struct CompressionStats {
    size_t inputSize = 0;       ///< Input data size in bytes
    size_t outputSize = 0;      ///< Output data size in bytes
    size_t lowStreamSize = 0;   ///< Compressed low-byte stream size
    size_t highStreamSize = 0;  ///< Compressed high-byte stream size
    double compressionRatio = 0.0;  ///< Compression ratio (input/output)
    double encodeTimeMs = 0.0;  ///< Time for encoding/decoding (excluding zstd)
    double zstdTimeMs = 0.0;    ///< Time for zstd compression/decompression
    double totalTimeMs = 0.0;   ///< Total time
};

// ─────────────────────────────────────────────────────────────────────────────
// Compress Result
// ─────────────────────────────────────────────────────────────────────────────

/// Result of compression operation
struct CompressResult {
    std::vector<uint8_t> data;  ///< Compressed LDH file data
    CompressionStats stats;     ///< Compression statistics
};

// ─────────────────────────────────────────────────────────────────────────────
// Decompress Result
// ─────────────────────────────────────────────────────────────────────────────

/// Result of decompression operation
struct DecompressResult {
    std::vector<uint16_t> data;  ///< Decompressed heightmap data
    uint32_t width = 0;          ///< Heightmap width
    uint32_t height = 0;         ///< Heightmap height
    CompressionStats stats;      ///< Decompression statistics
};

// ─────────────────────────────────────────────────────────────────────────────
// Compression Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Compress a heightmap to LDH format
/// @param input Heightmap data (16-bit unsigned, row-major order)
/// @param width Heightmap width in samples
/// @param height Heightmap height in samples
/// @param options Compression options
/// @return Compressed LDH data or error
[[nodiscard]] CompressionResult<CompressResult> compress(
    std::span<const uint16_t> input,
    uint32_t width,
    uint32_t height,
    const CompressionOptions& options = CompressionOptions::defaults()
);

/// Decompress LDH data to heightmap
/// @param input Compressed LDH file data
/// @return Decompressed heightmap data or error
[[nodiscard]] CompressionResult<DecompressResult> decompress(
    std::span<const uint8_t> input
);

/// Decompress LDH data and validate against known dimensions
/// @param input Compressed LDH file data
/// @param expectedWidth Expected width (for validation)
/// @param expectedHeight Expected height (for validation)
/// @return Decompressed heightmap data or error
[[nodiscard]] CompressionResult<DecompressResult> decompress(
    std::span<const uint8_t> input,
    uint32_t expectedWidth,
    uint32_t expectedHeight
);

// ─────────────────────────────────────────────────────────────────────────────
// File I/O Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Compress a heightmap and write to LDH file
/// @param input Heightmap data (16-bit unsigned, row-major order)
/// @param width Heightmap width in samples
/// @param height Heightmap height in samples
/// @param outputPath Path to write the LDH file
/// @param options Compression options
/// @return Compression statistics or error
[[nodiscard]] CompressionResult<CompressionStats> compressToFile(
    std::span<const uint16_t> input,
    uint32_t width,
    uint32_t height,
    const std::filesystem::path& outputPath,
    const CompressionOptions& options = CompressionOptions::defaults()
);

/// Decompress an LDH file to heightmap
/// @param inputPath Path to the LDH file
/// @return Decompressed heightmap data or error
[[nodiscard]] CompressionResult<DecompressResult> decompressFromFile(
    const std::filesystem::path& inputPath
);

/// Decompress an LDH file and validate against known dimensions
/// @param inputPath Path to the LDH file
/// @param expectedWidth Expected width (for validation)
/// @param expectedHeight Expected height (for validation)
/// @return Decompressed heightmap data or error
[[nodiscard]] CompressionResult<DecompressResult> decompressFromFile(
    const std::filesystem::path& inputPath,
    uint32_t expectedWidth,
    uint32_t expectedHeight
);

// ─────────────────────────────────────────────────────────────────────────────
// Info Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Read and validate LDH header from data
/// @param input Compressed LDH file data (at least LDH_HEADER_SIZE bytes)
/// @return Header or error
[[nodiscard]] CompressionResult<LDHHeader> readHeader(
    std::span<const uint8_t> input
);

/// Read and validate LDH header from file
/// @param inputPath Path to the LDH file
/// @return Header or error
[[nodiscard]] CompressionResult<LDHHeader> readHeaderFromFile(
    const std::filesystem::path& inputPath
);

/// Get compression info string for display
[[nodiscard]] std::string getInfoString(const LDHHeader& header);

// ─────────────────────────────────────────────────────────────────────────────
// CRC32 Checksum
// ─────────────────────────────────────────────────────────────────────────────

/// Calculate CRC32 checksum
[[nodiscard]] uint32_t calculateCRC32(std::span<const uint8_t> data);

// ─────────────────────────────────────────────────────────────────────────────
// Low-Level Encoding/Decoding (for testing)
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

/// Compute planar prediction for pixel at (x, y)
/// @param data Source data (for encoding) or already-decoded data (for decoding)
/// @param x X coordinate
/// @param y Y coordinate
/// @param width Heightmap width
/// @return Predicted value
[[nodiscard]] inline uint16_t predict(const uint16_t* data, 
                                       uint32_t x, uint32_t y, 
                                       uint32_t width) noexcept {
    if (x == 0 && y == 0) return 0;
    if (y == 0) return data[x - 1];
    if (x == 0) return data[(y - 1) * width];
    
    // Planar prediction: A + C - B
    // B -- C
    // |    |
    // A -- ?
    const uint16_t A = data[y * width + x - 1];        // Left
    const uint16_t B = data[(y - 1) * width + x - 1];  // Diagonal (top-left)
    const uint16_t C = data[(y - 1) * width + x];      // Top
    
    // Use signed arithmetic to handle wraparound correctly
    return static_cast<uint16_t>(A + C - B);
}

/// Encode delta (unsigned wraparound)
[[nodiscard]] inline uint16_t encodeDelta(uint16_t actual, uint16_t predicted) noexcept {
    return actual - predicted;  // Natural wraparound for uint16_t
}

/// Decode delta (unsigned wraparound)
[[nodiscard]] inline uint16_t decodeDelta(uint16_t delta, uint16_t predicted) noexcept {
    return predicted + delta;  // Natural wraparound for uint16_t
}

/// Apply planar predictor encoding to heightmap
/// @param input Source heightmap data
/// @param width Heightmap width
/// @param height Heightmap height
/// @param lowStream Output low-byte stream (must be pre-sized)
/// @param highStream Output high-byte stream (must be pre-sized)
void encodeWithPredictor(std::span<const uint16_t> input,
                         uint32_t width, uint32_t height,
                         std::span<uint8_t> lowStream,
                         std::span<uint8_t> highStream);

/// Decode planar predictor encoding to heightmap
/// @param lowStream Low-byte stream
/// @param highStream High-byte stream
/// @param width Heightmap width
/// @param height Heightmap height
/// @param output Output heightmap data (must be pre-sized)
void decodeWithPredictor(std::span<const uint8_t> lowStream,
                         std::span<const uint8_t> highStream,
                         uint32_t width, uint32_t height,
                         std::span<uint16_t> output);

} // namespace detail

} // namespace voxy::terrain

