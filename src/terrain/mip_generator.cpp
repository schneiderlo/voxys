// ═══════════════════════════════════════════════════════════════════════════════
// mip_generator.cpp - CPU Max-Height Mip Chain Generation Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/mip_generator.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <numeric>

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Free Functions
// ═══════════════════════════════════════════════════════════════════════════════

MipLevel generateNextMipLevel(std::span<const uint16_t> srcData,
                               uint32_t srcWidth, uint32_t srcHeight) {
    MipLevel result;
    
    // Calculate output dimensions (half of source, minimum 1)
    result.width = std::max(1u, srcWidth / 2);
    result.height = std::max(1u, srcHeight / 2);
    result.data.resize(result.sampleCount());
    
    const bool evenWidth = (srcWidth % 2) == 0;
    const bool evenHeight = (srcHeight % 2) == 0;

    // Perform 2×2 max reduction
    if (evenWidth && evenHeight && srcWidth > 1 && srcHeight > 1) {
        for (uint32_t y = 0; y < result.height; y++) {
            const uint32_t sy = y * 2;
            const uint16_t* row0 = srcData.data() + sy * srcWidth;
            const uint16_t* row1 = row0 + srcWidth;
            uint16_t* dst = result.data.data() + y * result.width;

            uint32_t x = 0;
            for (; x + 3 < result.width; x += 4) {
                const uint32_t sx0 = x * 2;
                const uint32_t sx1 = sx0 + 2;
                const uint32_t sx2 = sx0 + 4;
                const uint32_t sx3 = sx0 + 6;

                const uint16_t a0 = std::max(std::max(row0[sx0], row0[sx0 + 1]),
                                             std::max(row1[sx0], row1[sx0 + 1]));
                const uint16_t a1 = std::max(std::max(row0[sx1], row0[sx1 + 1]),
                                             std::max(row1[sx1], row1[sx1 + 1]));
                const uint16_t a2 = std::max(std::max(row0[sx2], row0[sx2 + 1]),
                                             std::max(row1[sx2], row1[sx2 + 1]));
                const uint16_t a3 = std::max(std::max(row0[sx3], row0[sx3 + 1]),
                                             std::max(row1[sx3], row1[sx3 + 1]));

                dst[x] = a0;
                dst[x + 1] = a1;
                dst[x + 2] = a2;
                dst[x + 3] = a3;
            }

            for (; x < result.width; x++) {
                const uint32_t sx = x * 2;
                const uint16_t s00 = row0[sx];
                const uint16_t s10 = row0[sx + 1];
                const uint16_t s01 = row1[sx];
                const uint16_t s11 = row1[sx + 1];
                dst[x] = std::max(std::max(s00, s10), std::max(s01, s11));
            }
        }
    } else {
        for (uint32_t y = 0; y < result.height; y++) {
            for (uint32_t x = 0; x < result.width; x++) {
                // Source coordinates
                const uint32_t sx = x * 2;
                const uint32_t sy = y * 2;

                // Sample 2×2 block with bounds checking
                uint16_t maxVal = 0;

                for (uint32_t dy = 0; dy < 2; dy++) {
                    for (uint32_t dx = 0; dx < 2; dx++) {
                        const uint32_t px = std::min(sx + dx, srcWidth - 1);
                        const uint32_t py = std::min(sy + dy, srcHeight - 1);
                        const uint16_t sample = srcData[py * srcWidth + px];
                        maxVal = std::max(maxVal, sample);
                    }
                }

                result.data[y * result.width + x] = maxVal;
            }
        }
    }
    
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MaxHeightMipChain Implementation
// ═══════════════════════════════════════════════════════════════════════════════

bool MaxHeightMipChain::generate(std::span<const uint16_t> baseData, 
                                  uint32_t width, uint32_t height) {
    // Validate input
    if (baseData.empty() || width == 0 || height == 0) {
        LOG_ERROR("MipChain: Invalid input (empty data or zero dimensions)");
        return false;
    }
    
    const size_t expectedSize = static_cast<size_t>(width) * height;
    if (baseData.size() != expectedSize) {
        LOG_ERROR("MipChain: Data size mismatch (expected {}, got {})", 
                  expectedSize, baseData.size());
        return false;
    }
    
    // Clear any existing data
    clear();
    
    // Store base dimensions
    baseWidth_ = width;
    baseHeight_ = height;
    hasBaseLevel_ = true;
    
    // Calculate number of mip levels
    const uint32_t levelCount = calculateMipLevelCount(width, height);
    levels_.reserve(levelCount);
    
    // Store level 0 (base level)
    MipLevel level0;
    level0.width = width;
    level0.height = height;
    level0.data.assign(baseData.begin(), baseData.end());
    levels_.push_back(std::move(level0));
    
    // Generate subsequent levels
    for (uint32_t i = 1; i < levelCount; i++) {
        const MipLevel& prevLevel = levels_.back();
        
        // Stop if previous level is 1×1
        if (prevLevel.width == 1 && prevLevel.height == 1) {
            break;
        }
        
        MipLevel nextLevel = generateNextMipLevel(prevLevel.data, 
                                                   prevLevel.width, 
                                                   prevLevel.height);
        levels_.push_back(std::move(nextLevel));
    }
    
    LOG_DEBUG("MipChain: Generated {} levels for {}×{} heightmap ({:.2f} KB total)",
              levels_.size(), width, height, 
              static_cast<double>(getTotalSizeBytes()) / 1024.0);
    
    return true;
}

bool MaxHeightMipChain::generateWithoutBase(std::span<const uint16_t> baseData, 
                                             uint32_t width, uint32_t height) {
    // Validate input
    if (baseData.empty() || width == 0 || height == 0) {
        LOG_ERROR("MipChain: Invalid input (empty data or zero dimensions)");
        return false;
    }
    
    const size_t expectedSize = static_cast<size_t>(width) * height;
    if (baseData.size() != expectedSize) {
        LOG_ERROR("MipChain: Data size mismatch (expected {}, got {})", 
                  expectedSize, baseData.size());
        return false;
    }
    
    // Clear any existing data
    clear();
    
    // Store base dimensions
    baseWidth_ = width;
    baseHeight_ = height;
    hasBaseLevel_ = false;
    
    // Calculate number of mip levels (excluding base)
    const uint32_t levelCount = calculateMipLevelCount(width, height);
    if (levelCount <= 1) {
        // Only base level exists, nothing to generate
        return true;
    }
    
    levels_.reserve(levelCount - 1);
    
    // Generate level 1 from the provided base data
    MipLevel level1 = generateNextMipLevel(baseData, width, height);
    levels_.push_back(std::move(level1));
    
    // Generate subsequent levels
    for (uint32_t i = 2; i < levelCount; i++) {
        const MipLevel& prevLevel = levels_.back();
        
        // Stop if previous level is 1×1
        if (prevLevel.width == 1 && prevLevel.height == 1) {
            break;
        }
        
        MipLevel nextLevel = generateNextMipLevel(prevLevel.data, 
                                                   prevLevel.width, 
                                                   prevLevel.height);
        levels_.push_back(std::move(nextLevel));
    }
    
    LOG_DEBUG("MipChain: Generated {} levels (without base) for {}×{} heightmap ({:.2f} KB total)",
              levels_.size(), width, height, 
              static_cast<double>(getTotalSizeBytes()) / 1024.0);
    
    return true;
}

const MipLevel* MaxHeightMipChain::getLevel(uint32_t level) const noexcept {
    // If we have the base level, index directly
    if (hasBaseLevel_) {
        if (level < levels_.size()) {
            return &levels_[level];
        }
        return nullptr;
    }
    
    // Without base level, level 0 doesn't exist in our storage
    // Level 1 is at index 0, level 2 at index 1, etc.
    if (level == 0) {
        return nullptr;  // Base level not stored
    }
    
    const uint32_t index = level - 1;
    if (index < levels_.size()) {
        return &levels_[index];
    }
    return nullptr;
}

size_t MaxHeightMipChain::getTotalSizeBytes() const noexcept {
    size_t total = 0;
    for (const auto& level : levels_) {
        total += level.sizeBytes();
    }
    return total;
}

void MaxHeightMipChain::clear() {
    levels_.clear();
    levels_.shrink_to_fit();
    baseWidth_ = 0;
    baseHeight_ = 0;
    hasBaseLevel_ = false;
}

} // namespace voxy::terrain

