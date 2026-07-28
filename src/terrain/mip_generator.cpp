// ═══════════════════════════════════════════════════════════════════════════════
// mip_generator.cpp - CPU Max-Height Mip Chain Generation Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/mip_generator.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace voxy::terrain {
namespace {

bool validMipSource(std::span<const uint16_t> data,
                    uint32_t width, uint32_t height) noexcept {
    return !data.empty() && width != 0u && height != 0u
        && width <= kMaxMipDimension && height <= kMaxMipDimension
        && static_cast<size_t>(width)
            <= std::numeric_limits<size_t>::max() / height
        && data.size() == static_cast<size_t>(width) * height;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Free Functions
// ═══════════════════════════════════════════════════════════════════════════════

MipLevel generateNextMipLevel(std::span<const uint16_t> srcData,
                               uint32_t srcWidth, uint32_t srcHeight) {
    MipLevel result;
    if (srcWidth == 0u || srcHeight == 0u
        || static_cast<size_t>(srcWidth)
            > std::numeric_limits<size_t>::max() / srcHeight
        || srcData.size()
            != static_cast<size_t>(srcWidth) * srcHeight) {
        LOG_ERROR("Cannot generate mip from invalid {}x{} source with {} samples",
                  srcWidth, srcHeight, srcData.size());
        return result;
    }
    
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
                uint16_t maxVal = 0;
                const uint32_t beginX = static_cast<uint32_t>(
                    static_cast<uint64_t>(x) * srcWidth / result.width);
                const uint32_t endX = static_cast<uint32_t>(
                    static_cast<uint64_t>(x + 1u) * srcWidth
                    / result.width);
                const uint32_t beginY = static_cast<uint32_t>(
                    static_cast<uint64_t>(y) * srcHeight / result.height);
                const uint32_t endY = static_cast<uint32_t>(
                    static_cast<uint64_t>(y + 1u) * srcHeight
                    / result.height);
                for (uint32_t py = beginY; py < endY; ++py) {
                    for (uint32_t px = beginX; px < endX; ++px) {
                        maxVal = std::max(
                            maxVal,
                            srcData[static_cast<size_t>(py) * srcWidth + px]);
                    }
                }

                result.data[static_cast<size_t>(y) * result.width + x] =
                    maxVal;
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
    if (!validMipSource(baseData, width, height)) {
        LOG_ERROR("MipChain: Invalid {}x{} source with {} samples",
                  width, height, baseData.size());
        return false;
    }

    MaxHeightMipChain replacement;
    replacement.baseWidth_ = width;
    replacement.baseHeight_ = height;
    replacement.hasBaseLevel_ = true;

    const uint32_t levelCount = calculateMipLevelCount(width, height);
    replacement.levels_.reserve(levelCount);

    MipLevel level0;
    level0.width = width;
    level0.height = height;
    level0.data.assign(baseData.begin(), baseData.end());
    replacement.levels_.push_back(std::move(level0));

    for (uint32_t i = 1; i < levelCount; i++) {
        const MipLevel& prevLevel = replacement.levels_.back();
        if (prevLevel.width == 1 && prevLevel.height == 1) {
            break;
        }
        MipLevel nextLevel = generateNextMipLevel(
            prevLevel.data, prevLevel.width, prevLevel.height);
        if (!nextLevel.isValid()) return false;
        replacement.levels_.push_back(std::move(nextLevel));
    }

    *this = std::move(replacement);
    LOG_DEBUG("MipChain: Generated {} levels for {}×{} heightmap ({:.2f} KB total)",
              levels_.size(), width, height, 
              static_cast<double>(getTotalSizeBytes()) / 1024.0);
    return true;
}

bool MaxHeightMipChain::generateWithoutBase(std::span<const uint16_t> baseData, 
                                             uint32_t width, uint32_t height) {
    if (!validMipSource(baseData, width, height)) {
        LOG_ERROR("MipChain: Invalid {}x{} source with {} samples",
                  width, height, baseData.size());
        return false;
    }

    MaxHeightMipChain replacement;
    replacement.baseWidth_ = width;
    replacement.baseHeight_ = height;
    replacement.hasBaseLevel_ = false;

    const uint32_t levelCount = calculateMipLevelCount(width, height);
    if (levelCount <= 1) {
        *this = std::move(replacement);
        return true;
    }

    replacement.levels_.reserve(levelCount - 1);
    MipLevel level1 = generateNextMipLevel(baseData, width, height);
    if (!level1.isValid()) return false;
    replacement.levels_.push_back(std::move(level1));

    for (uint32_t i = 2; i < levelCount; i++) {
        const MipLevel& prevLevel = replacement.levels_.back();
        if (prevLevel.width == 1 && prevLevel.height == 1) {
            break;
        }
        MipLevel nextLevel = generateNextMipLevel(
            prevLevel.data, prevLevel.width, prevLevel.height);
        if (!nextLevel.isValid()) return false;
        replacement.levels_.push_back(std::move(nextLevel));
    }

    *this = std::move(replacement);
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
