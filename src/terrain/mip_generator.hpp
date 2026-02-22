// ═══════════════════════════════════════════════════════════════════════════════
// mip_generator.hpp - CPU Max-Height Mip Chain Generation (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides CPU-side generation of max-height mip chains for hierarchical
// ray-casting. Each mip level stores the maximum height of its corresponding
// 2×2 block from the previous level.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace voxy::terrain {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum supported mip levels (enough for 16384×16384 textures)
constexpr uint32_t kMaxMipLevels = 15;

// ─────────────────────────────────────────────────────────────────────────────
// MipLevel Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Represents a single mip level in the chain
struct MipLevel {
    std::vector<uint16_t> data;  ///< Height samples (row-major order)
    uint32_t width = 0;          ///< Width in samples
    uint32_t height = 0;         ///< Height in samples
    
    /// Get total number of samples
    [[nodiscard]] constexpr size_t sampleCount() const noexcept {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    
    /// Get size in bytes
    [[nodiscard]] constexpr size_t sizeBytes() const noexcept {
        return sampleCount() * sizeof(uint16_t);
    }
    
    /// Check if this level is valid
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return !data.empty() && width > 0 && height > 0 && 
               data.size() == sampleCount();
    }
    
    /// Sample height at (x, y), clamped to bounds
    [[nodiscard]] uint16_t sample(uint32_t x, uint32_t y) const noexcept {
        if (data.empty()) return 0;
        x = std::min(x, width - 1);
        y = std::min(y, height - 1);
        return data[y * width + x];
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MaxHeightMipChain Class
// ─────────────────────────────────────────────────────────────────────────────

/// Complete mip chain storing maximum height values for hierarchical ray-casting.
/// 
/// Each level stores the maximum height of 2×2 blocks from the previous level.
/// This allows ray-casters to quickly skip regions where the terrain is below
/// the ray, improving performance significantly.
class MaxHeightMipChain {
public:
    MaxHeightMipChain() = default;
    ~MaxHeightMipChain() = default;
    
    // Movable
    MaxHeightMipChain(MaxHeightMipChain&&) = default;
    MaxHeightMipChain& operator=(MaxHeightMipChain&&) = default;
    
    // Copyable
    MaxHeightMipChain(const MaxHeightMipChain&) = default;
    MaxHeightMipChain& operator=(const MaxHeightMipChain&) = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Generation
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Generate a full mip chain from the base heightmap data.
    /// 
    /// @param baseData   The base (level 0) heightmap data, row-major order
    /// @param width      Width of the base heightmap
    /// @param height     Height of the base heightmap
    /// @return True if generation succeeded, false on error
    /// 
    /// @note For best results, width and height should be powers of two.
    ///       Non-power-of-two dimensions are supported but will be clamped
    ///       at edges during downsampling.
    bool generate(std::span<const uint16_t> baseData, uint32_t width, uint32_t height);
    
    /// Generate a mip chain without storing the base level (saves memory).
    /// Use this when you already have the base heightmap elsewhere.
    /// 
    /// @param baseData   The base heightmap data (not stored, only used for generation)
    /// @param width      Width of the base heightmap
    /// @param height     Height of the base heightmap
    /// @return True if generation succeeded
    bool generateWithoutBase(std::span<const uint16_t> baseData, uint32_t width, uint32_t height);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Access
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Check if the mip chain is valid (has at least one level)
    [[nodiscard]] bool isValid() const noexcept { return !levels_.empty(); }
    
    /// Get the number of mip levels
    [[nodiscard]] uint32_t getLevelCount() const noexcept { 
        return static_cast<uint32_t>(levels_.size()); 
    }
    
    /// Get base level dimensions (level 0)
    [[nodiscard]] uint32_t getBaseWidth() const noexcept { return baseWidth_; }
    [[nodiscard]] uint32_t getBaseHeight() const noexcept { return baseHeight_; }
    
    /// Get a specific mip level (0 = base level, if stored)
    /// @param level  The mip level index
    /// @return Pointer to the mip level, or nullptr if level doesn't exist
    [[nodiscard]] const MipLevel* getLevel(uint32_t level) const noexcept;
    
    /// Get all mip levels
    [[nodiscard]] std::span<const MipLevel> getLevels() const noexcept { return levels_; }
    
    /// Calculate the total memory used by all mip levels (in bytes)
    [[nodiscard]] size_t getTotalSizeBytes() const noexcept;
    
    /// Check if the base level is stored
    [[nodiscard]] bool hasBaseLevel() const noexcept { return hasBaseLevel_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Utilities
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Release all mip data
    void clear();
    
private:
    std::vector<MipLevel> levels_;
    uint32_t baseWidth_ = 0;
    uint32_t baseHeight_ = 0;
    bool hasBaseLevel_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Free Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Generate a single mip level from the source level using 2×2 max reduction.
/// 
/// @param srcData    Source level data (row-major order)
/// @param srcWidth   Source level width
/// @param srcHeight  Source level height
/// @return The downsampled mip level with max values
[[nodiscard]] MipLevel generateNextMipLevel(std::span<const uint16_t> srcData,
                                             uint32_t srcWidth, uint32_t srcHeight);

/// Calculate the number of mip levels for given dimensions.
/// 
/// @param width   Base width
/// @param height  Base height
/// @return Number of mip levels (including base level)
[[nodiscard]] constexpr uint32_t calculateMipLevelCount(uint32_t width, uint32_t height) noexcept {
    uint32_t size = std::max(width, height);
    uint32_t levels = 1;
    while (size > 1) {
        size >>= 1;
        levels++;
    }
    return levels;
}

/// Calculate dimensions for a specific mip level.
/// 
/// @param baseWidth   Base level width
/// @param baseHeight  Base level height
/// @param level       Mip level index (0 = base)
/// @return Pair of (width, height) for the requested level
[[nodiscard]] constexpr std::pair<uint32_t, uint32_t> 
getMipLevelDimensions(uint32_t baseWidth, uint32_t baseHeight, uint32_t level) noexcept {
    uint32_t w = std::max(1u, baseWidth >> level);
    uint32_t h = std::max(1u, baseHeight >> level);
    return {w, h};
}

} // namespace voxy::terrain



