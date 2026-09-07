#pragma once

#include "terrain/lego_surface.hpp"
#include <optional>

namespace voxy::terrain::lego {

// A toroidal 1024x1024 R32Uint atlas, independent of the source map size.
// Low 16 bits are the shared layout; high 16 bits identify its world chunk.
// The shader rejects stale slots after movement/teleports and uses the same
// palette with single-cell seams until a nearby chunk is ready.
inline constexpr uint32_t kCacheChunks = 32;
inline constexpr uint32_t kCacheCells = kCacheChunks * kChunkCells;
inline constexpr uint32_t kCacheSlots = kCacheChunks * kCacheChunks;
inline constexpr uint32_t kCacheGpuBytes = kCacheCells * kCacheCells * 4u;
inline constexpr uint32_t kChunksPerFrame = 4;

struct ChunkRequest {
    uint32_t x = 0, z = 0, key = 0;
    [[nodiscard]] uint32_t slot() const noexcept {
        return (z % kCacheChunks) * kCacheChunks + x % kCacheChunks;
    }
};

class LayoutCache {
public:
    LayoutCache() { invalidate(); }
    void invalidate() noexcept {
        resident_.fill(UINT32_MAX);
        centerX_ = centerZ_ = UINT32_MAX;
        cursor_ = pendingCount_ = 0;
    }

    // Recompute the missing-chunk queue only when crossing a chunk boundary.
    // No per-frame heap allocation, full-heightmap traversal, or terrain copy.
    void request(uint32_t width, uint32_t height, glm::vec2 samplePosition) {
        if (width < 2 || height < 2 || width > 8192 || height > 8192
            || !std::isfinite(samplePosition.x) || !std::isfinite(samplePosition.y)) return;
        const uint32_t nx = (width - 2) / kChunkCells + 1;
        const uint32_t nz = (height - 2) / kChunkCells + 1;
        const uint32_t x = uint32_t(std::clamp(std::floor(samplePosition.x / kChunkCells),0.0f,float(nx-1)));
        const uint32_t z = uint32_t(std::clamp(std::floor(samplePosition.y / kChunkCells),0.0f,float(nz-1)));
        if (x == centerX_ && z == centerZ_) return;
        centerX_ = x; centerZ_ = z;
        const uint32_t wx = std::min(nx,kCacheChunks), wz = std::min(nz,kCacheChunks);
        const uint32_t x0 = std::min(x > wx/2 ? x-wx/2 : 0u,nx-wx);
        const uint32_t z0 = std::min(z > wz/2 ? z-wz/2 : 0u,nz-wz);
        cursor_ = pendingCount_ = 0;
        for (uint32_t cz=z0; cz<z0+wz; ++cz) for (uint32_t cx=x0; cx<x0+wx; ++cx) {
            const ChunkRequest q{cx,cz,cz*nx+cx};
            if (resident_[q.slot()] != q.key) pending_[pendingCount_++] = q;
        }
        const auto distance = [=](const ChunkRequest& q) {
            const int dx=int(q.x)-int(x), dz=int(q.z)-int(z);
            return dx*dx+dz*dz;
        };
        std::sort(pending_.begin(),pending_.begin()+pendingCount_,[&](const auto& a,const auto& b) {
            const int da=distance(a), db=distance(b);
            return da==db ? a.key<b.key : da<db;
        });
    }

    [[nodiscard]] std::optional<ChunkRequest> next() noexcept {
        if (cursor_ == pendingCount_) return std::nullopt;
        return pending_[cursor_++];
    }
    void uploaded(const ChunkRequest& q) noexcept { resident_[q.slot()] = q.key; }
    [[nodiscard]] bool contains(const ChunkRequest& q) const noexcept { return resident_[q.slot()] == q.key; }
    [[nodiscard]] uint32_t pendingCount() const noexcept { return pendingCount_ - cursor_; }

    static uint32_t pack(uint16_t cell, uint32_t chunkKey) noexcept {
        return uint32_t(cell) | (chunkKey << 16u);
    }
    static bool matches(uint32_t packed, uint32_t chunkKey) noexcept {
        return (packed & 0x1000u) != 0u && (packed >> 16u) == chunkKey;
    }

private:
    std::array<uint32_t,kCacheSlots> resident_{};
    std::array<ChunkRequest,kCacheSlots> pending_{};
    uint32_t centerX_ = UINT32_MAX, centerZ_ = UINT32_MAX;
    uint32_t cursor_ = 0, pendingCount_ = 0;
};
static_assert(sizeof(LayoutCache) < 20 * 1024);
static_assert(kCacheGpuBytes == 4 * 1024 * 1024);

} // namespace voxy::terrain::lego
