// ═══════════════════════════════════════════════════════════════════════════════
// heightmap.hpp - Heightmap Loading and GPU Texture Management (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides loading of 16-bit heightmap data from RAW and PNG files, storage as
// CPU-side data, and GPU texture upload functionality for WebGPU rendering.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::terrain {

// ─────────────────────────────────────────────────────────────────────────────
// Error Types
// ─────────────────────────────────────────────────────────────────────────────

/// Error codes for heightmap operations
enum class HeightmapError {
    None = 0,               ///< No error (success)
    FileNotFound,           ///< File does not exist
    ReadError,              ///< Failed to read file contents
    InvalidFormat,          ///< File format not recognized or invalid
    InvalidDimensions,      ///< Dimensions are invalid or don't match file size
    DecodeFailed,           ///< PNG decoding failed
    Not16Bit,               ///< PNG is not 16-bit depth
    TextureCreationFailed,  ///< GPU texture creation failed
    UploadFailed,           ///< GPU texture upload failed
    ExrDecodeFailed,        ///< EXR decoding failed
};

/// Convert error code to human-readable string
[[nodiscard]] std::string_view errorToString(HeightmapError error) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Result Type (C++20 compatible std::expected alternative)
// ─────────────────────────────────────────────────────────────────────────────

/// Simple result type for error handling (std::expected alternative for C++20)
template<typename T, typename E>
class Result {
public:
    /// Construct success result
    Result(T value) : data_(std::move(value)) {}
    
    /// Construct error result
    Result(E error) : data_(error) {}
    
    /// Check if result is successful
    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    
    /// Check if result is an error
    [[nodiscard]] bool has_error() const noexcept {
        return std::holds_alternative<E>(data_);
    }
    
    /// Implicit conversion to bool (true if success)
    explicit operator bool() const noexcept { return has_value(); }
    
    /// Get value (undefined if error)
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }
    
    /// Get error (undefined if success)
    [[nodiscard]] E error() const noexcept { return std::get<E>(data_); }
    
private:
    std::variant<T, E> data_;
};

/// Specialization for void success type
template<typename E>
class Result<void, E> {
public:
    /// Construct success result
    Result() : error_(std::nullopt) {}
    
    /// Construct error result
    Result(E error) : error_(error) {}
    
    /// Check if result is successful
    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
    
    /// Check if result is an error
    [[nodiscard]] bool has_error() const noexcept { return error_.has_value(); }
    
    /// Implicit conversion to bool (true if success)
    explicit operator bool() const noexcept { return has_value(); }
    
    /// Get error (undefined if success)
    [[nodiscard]] E error() const noexcept { return *error_; }
    
private:
    std::optional<E> error_;
};

// Type aliases for common result types
using VoidResult = Result<void, HeightmapError>;

// ─────────────────────────────────────────────────────────────────────────────
// Load Result
// ─────────────────────────────────────────────────────────────────────────────

/// Result of a heightmap load operation
struct LoadResult {
    std::vector<uint16_t> data;  ///< Height samples (row-major order)
    uint32_t width = 0;          ///< Width in samples
    uint32_t height = 0;         ///< Height in samples
    double loadTimeMs = 0.0;     ///< Time to load and decode (milliseconds)
    
    /// Get total number of samples
    [[nodiscard]] constexpr size_t sampleCount() const noexcept {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    
    /// Get size in bytes
    [[nodiscard]] constexpr size_t sizeBytes() const noexcept {
        return sampleCount() * sizeof(uint16_t);
    }
    
    /// Check if this result is valid
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return !data.empty() && width > 0 && height > 0 && 
               data.size() == sampleCount();
    }
    
    /// Get height value at (x, y), clamped to bounds
    [[nodiscard]] uint16_t sample(uint32_t x, uint32_t y) const noexcept {
        x = std::min(x, width - 1);
        y = std::min(y, height - 1);
        return data[y * width + x];
    }
    
    /// Get normalized height value [0.0, 1.0] at (x, y)
    [[nodiscard]] float sampleNormalized(uint32_t x, uint32_t y) const noexcept {
        return static_cast<float>(sample(x, y)) / 65535.0f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Heightmap Class
// ─────────────────────────────────────────────────────────────────────────────

/// Heightmap data with optional GPU texture
class Heightmap {
public:
    Heightmap() = default;
    ~Heightmap();
    
    // Non-copyable (due to GPU resources)
    Heightmap(const Heightmap&) = delete;
    Heightmap& operator=(const Heightmap&) = delete;
    
    // Movable
    Heightmap(Heightmap&& other) noexcept;
    Heightmap& operator=(Heightmap&& other) noexcept;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Loading
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Load a heightmap from file (supports .ldh)
    [[nodiscard]] VoidResult loadFromFile(const std::filesystem::path& path);

    /// Load from LDH compressed file
    [[nodiscard]] VoidResult loadLdh(const std::filesystem::path& path);
    

    
    /// Load from memory buffer (raw 16-bit format)
    [[nodiscard]] VoidResult loadRawFromMemory(std::span<const std::byte> data, 
                                                uint32_t width, uint32_t height);
    

    
    // ─────────────────────────────────────────────────────────────────────────
    // Resizing / Upscaling
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Resize the heightmap using bilinear interpolation.
    /// If targetWidth/Height are 0, resizes to the next power of 2.
    /// @param targetWidth  Target width (0 = auto to next power of 2)
    /// @param targetHeight Target height (0 = auto to next power of 2)
    /// @return Success or error
    [[nodiscard]] VoidResult resize(uint32_t targetWidth = 0, uint32_t targetHeight = 0);
    
    /// Resize to the next power of 2 dimensions (convenience wrapper)
    [[nodiscard]] VoidResult resizeToPowerOfTwo();
    
    // ─────────────────────────────────────────────────────────────────────────
    // CPU Data Access
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Check if heightmap is loaded
    [[nodiscard]] constexpr bool isLoaded() const noexcept { return !data_.empty(); }
    
    /// Get width in samples
    [[nodiscard]] constexpr uint32_t getWidth() const noexcept { return width_; }
    
    /// Get height in samples  
    [[nodiscard]] constexpr uint32_t getHeight() const noexcept { return height_; }
    
    /// Get total sample count
    [[nodiscard]] constexpr size_t getSampleCount() const noexcept {
        return static_cast<size_t>(width_) * static_cast<size_t>(height_);
    }
    
    /// Get size in bytes
    [[nodiscard]] constexpr size_t getSizeBytes() const noexcept {
        return getSampleCount() * sizeof(uint16_t);
    }
    
    /// Get load time in milliseconds
    [[nodiscard]] constexpr double getLoadTimeMs() const noexcept { return loadTimeMs_; }
    
    /// Get raw data span
    [[nodiscard]] std::span<const uint16_t> getData() const noexcept {
        return data_;
    }
    
    /// Get raw data as bytes
    [[nodiscard]] std::span<const std::byte> getDataBytes() const noexcept {
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data_.data()),
            getSizeBytes()
        );
    }
    
    /// Sample height at (x, y), clamped to bounds
    [[nodiscard]] uint16_t sample(uint32_t x, uint32_t y) const noexcept;
    
    /// Sample height at (x, y) with bilinear interpolation, clamped to bounds
    [[nodiscard]] float sampleBilinear(float x, float y) const noexcept;
    
    /// Get normalized height [0.0, 1.0] at (x, y)
    [[nodiscard]] float sampleNormalized(uint32_t x, uint32_t y) const noexcept;
    
    /// Get min/max height values
    [[nodiscard]] std::pair<uint16_t, uint16_t> getMinMax() const noexcept;
    
    // ─────────────────────────────────────────────────────────────────────────
    // GPU Texture
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Upload heightmap data to GPU texture (single mip level, no mip chain)
    /// Creates an R16Uint texture with the heightmap data
    [[nodiscard]] VoidResult uploadToGPU(WGPUDevice device, WGPUQueue queue, 
                                          std::string_view label = "heightmap");
    
    /// Upload heightmap data to GPU texture with full max-height mip chain.
    /// Creates an R16Uint texture with mip levels where each level stores
    /// the maximum height of its 2×2 block from the previous level.
    /// This is essential for hierarchical ray-casting.
    /// 
    /// @param device       WebGPU device
    /// @param queue        WebGPU queue
    /// @param useGPUMips   If true, uses GPU compute shader for mip generation.
    ///                     If false (or GPU pipeline unavailable), uses CPU fallback.
    /// @param shaderPath   Path to mip_generate.wgsl (only needed if useGPUMips=true)
    /// @param label        Debug label for the texture
    /// @return Success or error
    [[nodiscard]] VoidResult uploadToGPUWithMips(WGPUDevice device, WGPUQueue queue,
                                                  bool useGPUMips = true,
                                                  const std::filesystem::path& shaderPath = {},
                                                  std::string_view label = "heightmap_mipped");
    
    /// Check if GPU texture exists
    [[nodiscard]] constexpr bool hasGPUTexture() const noexcept { return texture_ != nullptr; }
    
    /// Get GPU texture
    [[nodiscard]] constexpr WGPUTexture getTexture() const noexcept { return texture_; }
    
    /// Get GPU texture view (creates one if needed)
    [[nodiscard]] WGPUTextureView getTextureView();
    
    /// Get texture view for a specific mip level
    [[nodiscard]] WGPUTextureView getMipView(uint32_t level);
    
    /// Get texture format
    [[nodiscard]] static constexpr WGPUTextureFormat getTextureFormat() noexcept {
        return WGPUTextureFormat_R16Uint;
    }
    
    /// Get the number of mip levels in the GPU texture (0 if not uploaded)
    [[nodiscard]] constexpr uint32_t getMipLevelCount() const noexcept { return mipLevelCount_; }
    
    /// Check if the GPU texture has a mip chain (more than 1 level)
    [[nodiscard]] constexpr bool hasMipChain() const noexcept { return mipLevelCount_ > 1; }
    
    /// Release GPU resources
    void releaseGPU();
    
    /// Release all resources (CPU and GPU)
    void release();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Static Factory Functions
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Load heightmap from file and return result (for simple usage)
    [[nodiscard]] static Result<LoadResult, HeightmapError>
    load(const std::filesystem::path& path);
    
    /// Create a flat heightmap filled with a constant value
    [[nodiscard]] static Heightmap createFlat(uint32_t width, uint32_t height, 
                                               uint16_t value = 0);
    
    /// Create a heightmap from existing data (takes ownership)
    [[nodiscard]] static Heightmap createFromData(std::vector<uint16_t>&& data,
                                                   uint32_t width, uint32_t height);

private:
    std::vector<uint16_t> data_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    double loadTimeMs_ = 0.0;
    
    // GPU resources
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    uint32_t mipLevelCount_ = 0;
    
    // Cached min/max (lazy computed)
    mutable std::optional<std::pair<uint16_t, uint16_t>> cachedMinMax_;
    
    /// Helper to read file to memory
    [[nodiscard]] static Result<std::vector<std::byte>, HeightmapError>
    readFileToMemory(const std::filesystem::path& path);
    
    /// Helper for CPU mip generation and upload
    [[nodiscard]] bool uploadMipsFromCPU(WGPUDevice device, WGPUQueue queue);
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Calculate number of mip levels for a heightmap (for max-height mip pyramid)
[[nodiscard]] constexpr uint32_t calculateMipLevels(uint32_t width, uint32_t height) noexcept {
    uint32_t size = std::max(width, height);
    uint32_t levels = 1;
    while (size > 1) {
        size >>= 1;
        levels++;
    }
    return levels;
}

/// Check if dimensions are power of two
[[nodiscard]] constexpr bool isPowerOfTwo(uint32_t value) noexcept {
    return value > 0 && (value & (value - 1)) == 0;
}

/// Round up to next power of two
[[nodiscard]] constexpr uint32_t nextPowerOfTwo(uint32_t value) noexcept {
    if (value == 0) return 1;
    if (isPowerOfTwo(value)) return value;
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

} // namespace voxy::terrain

