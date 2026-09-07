#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::terrain::lego {

// The integer level calculation is also used in the WGSL consumers. It avoids
// CPU/GPU rounding disagreements at plate boundaries. The original samples and
// their conservative maximum pyramid remain authoritative; no second heightmap.
inline constexpr float kPlateRatio = 0.32f;
inline constexpr float kStudRadius = 0.30f;
inline constexpr float kStudHeight = 0.18f;
inline constexpr uint32_t kChunkCells = 32;
inline constexpr uint32_t kMaximumStudySamples = 512;

inline uint32_t plateCount(float heightScale, float cellScale) noexcept {
    return static_cast<uint32_t>(std::clamp(
        std::floor(2.0f * heightScale / (kPlateRatio * cellScale) + 0.5f),
        1.0f, 65535.0f));
}
inline uint32_t level(uint16_t raw, uint32_t count) noexcept {
    return (uint32_t{raw} * count + 32767u) / 65535u;
}
inline float top(uint16_t raw, float heightScale, float cellScale) noexcept {
    const uint32_t count = plateCount(heightScale, cellScale);
    return -heightScale + float(level(raw, count))
        * (2.0f * heightScale / float(count));
}
inline uint32_t hash(uint32_t x, uint32_t z) noexcept {
    uint32_t h = (x + 173u) * 374761393u ^ (z + 419u) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

struct Surface {
    std::span<const uint16_t> samples;
    uint32_t width = 0, height = 0;
    float heightScale = 1.0f, cellScale = 1.0f;
    [[nodiscard]] bool valid() const noexcept {
        return width >= 2 && height >= 2 && std::isfinite(heightScale)
            && std::isfinite(cellScale) && heightScale > 0 && cellScale > 0
            && samples.size() == size_t{width} * height;
    }
    [[nodiscard]] glm::vec2 origin() const noexcept {
        return 0.5f * cellScale * glm::vec2(float(width - 1), float(height - 1));
    }
    [[nodiscard]] float cellTop(int x, int z) const noexcept {
        return top(samples[size_t(z) * width + size_t(x)], heightScale, cellScale);
    }
    [[nodiscard]] float heightAt(float x, float z) const noexcept {
        const glm::vec2 p = (glm::vec2(x, z) + origin()) / cellScale;
        if (p.x < 0 || p.y < 0 || p.x >= float(width - 1)
            || p.y >= float(height - 1)) return -heightScale;
        const int ix = int(std::floor(p.x)), iz = int(std::floor(p.y));
        const glm::vec2 d = p - glm::vec2(float(ix) + .5f, float(iz) + .5f);
        return cellTop(ix, iz) + (glm::dot(d, d) <= kStudRadius * kStudRadius
            ? kStudHeight * cellScale : 0.0f);
    }
};

struct Contact {
    float distance = std::numeric_limits<float>::max();
    glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f};
    uint32_t feature = 0;
    bool valid = false;
};

// Exact exterior distance to the union of solid columns and circular studs.
// Visit the horizontal sphere footprint plus one cell for conservative sweeps.
// No brick physics bodies,
// allocations, camera-dependent geometry, or collision at decorative seams.
inline Contact sphereContact(const Surface& s, glm::vec3 center,
                             float radius) noexcept {
    Contact best;
    if (!s.valid()) return best;
    const glm::vec2 o = s.origin();
    const glm::vec2 p = (glm::vec2(center.x, center.z) + o) / s.cellScale;
    const float reach = (radius + s.cellScale) / s.cellScale;
    const int x0 = std::max(0, int(std::floor(p.x - reach)));
    const int z0 = std::max(0, int(std::floor(p.y - reach)));
    const int x1 = std::min(int(s.width) - 2, int(std::floor(p.x + reach)));
    const int z1 = std::min(int(s.height) - 2, int(std::floor(p.y + reach)));
    auto consider = [&](glm::vec3 q, float interior, uint32_t feature) {
        const glm::vec3 delta = center - q;
        const float length = glm::length(delta);
        const float distance = interior < 0 ? interior : length;
        if (!best.valid || distance - radius < best.distance) {
            best = {distance - radius, q,
                interior < 0 || length < 1e-7f ? glm::vec3(0, 1, 0)
                                             : delta / length,
                feature, true};
        }
    };
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const glm::vec2 low = glm::vec2(float(x), float(z)) * s.cellScale - o;
        const glm::vec2 high = low + glm::vec2(s.cellScale);
        const float y = s.cellTop(x, z);
        const glm::vec2 q = glm::clamp(glm::vec2(center.x, center.z), low, high);
        const uint32_t feature = (uint32_t(z) * s.width + uint32_t(x)) * 8u;
        const bool inside = center.x >= low.x && center.x <= high.x
            && center.z >= low.y && center.z <= high.y && center.y < y;
        consider({q.x, inside ? y : std::min(center.y, y), q.y},
                 inside ? center.y - y : 0.0f, feature + 1u);
        const glm::vec2 c = low + glm::vec2(.5f * s.cellScale);
        const glm::vec2 d = glm::vec2(center.x, center.z) - c;
        const float r = kStudRadius * s.cellScale;
        const float length = glm::length(d);
        const float cap = y + kStudHeight * s.cellScale;
        const glm::vec2 radial = c + d * std::min(1.0f, r / std::max(length, 1e-7f));
        const bool insideStud = length < r && center.y >= y && center.y < cap;
        consider({radial.x, insideStud ? cap : std::clamp(center.y, y, cap), radial.y},
                 insideStud ? center.y - cap : 0.0f, feature + 2u);
    }
    return best;
}

// Exact vertical support of the capsule's lower hemisphere, including a stud
// or terrace edge under any part of its footprint (not nine height probes).
inline float supportHeight(const Surface& s, glm::vec2 position, float radius) noexcept {
    const glm::vec2 p = (position + s.origin()) / s.cellScale;
    const float reach = radius / s.cellScale;
    float result = -s.heightScale;
    const int x0 = std::max(0, int(std::floor(p.x - reach)));
    const int z0 = std::max(0, int(std::floor(p.y - reach)));
    const int x1 = std::min(int(s.width) - 2, int(std::floor(p.x + reach)));
    const int z1 = std::min(int(s.height) - 2, int(std::floor(p.y + reach)));
    auto support = [&](float y, float distance) {
        if (distance <= radius) result = std::max(result,
            y + std::sqrt(std::max(0.0f, radius * radius - distance * distance)) - radius);
    };
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const glm::vec2 low = glm::vec2(float(x), float(z)) * s.cellScale - s.origin();
        const float y = s.cellTop(x, z);
        support(y, glm::length(position - glm::clamp(position, low, low + glm::vec2(s.cellScale))));
        support(y + kStudHeight * s.cellScale, std::max(0.0f,
            glm::length(position - low - glm::vec2(.5f * s.cellScale)) - kStudRadius * s.cellScale));
    }
    return result;
}

struct Layout {
    // 2 bits each: offset x/z, width-1/depth-1. 4 bits palette. 1 valid bit.
    // Every cell needs exactly two bytes; look up only after a ray hit.
    std::vector<uint16_t> cells;
    uint32_t bricks = 0, largeBricks = 0, chunks = 0;
};

// One chunk is shared by the small study and the large-world cache. It reads
// only the source heightmap and a two-cell color-classification halo.
inline uint32_t colorFamily(const Surface& s, uint32_t x, uint32_t z,
                            float waterHeight) noexcept {
    const float y = s.cellTop(int(x), int(z)) - waterHeight;
    if (y < 2.4f * s.cellScale) return 0u;
    const uint32_t l = x > 2 ? x - 2 : 0, r = std::min(x + 2, s.width - 2);
    const uint32_t t = z > 2 ? z - 2 : 0, b = std::min(z + 2, s.height - 2);
    const float relief = std::max(std::abs(s.cellTop(int(l), int(z)) - s.cellTop(int(r), int(z))),
                                  std::abs(s.cellTop(int(x), int(t)) - s.cellTop(int(x), int(b))));
    if (y > 6 * s.cellScale && relief > 1.8f * s.cellScale) return 3u;
    return y > 12 * s.cellScale ? 2u : 1u;
}

struct ChunkLayout {
    std::array<uint16_t, kChunkCells * kChunkCells> cells{};
    uint32_t bricks = 0, largeBricks = 0;
};

inline ChunkLayout buildChunk(const Surface& s, uint32_t chunkX, uint32_t chunkZ,
                               float waterHeight, uint32_t sourceX = 0,
                               uint32_t sourceZ = 0) {
    ChunkLayout out;
    if (!s.valid() || chunkX >= (s.width - 2) / kChunkCells + 1
        || chunkZ >= (s.height - 2) / kChunkCells + 1) return out;
    const uint32_t cx = chunkX * kChunkCells, cz = chunkZ * kChunkCells;
    const uint32_t endX = std::min(cx + kChunkCells, s.width - 1);
    const uint32_t endZ = std::min(cz + kChunkCells, s.height - 1);
    const uint32_t count = plateCount(s.heightScale, s.cellScale);
    const auto at = [&](uint32_t x, uint32_t z) { return level(s.samples[size_t{z} * s.width + x], count); };
    const auto index = [&](uint32_t x, uint32_t z) { return size_t{z - cz} * kChunkCells + x - cx; };
    constexpr std::array<std::array<uint32_t, 2>, 7> shapes{{{2,4},{4,2},{2,2},{1,2},{2,1},{1,1},{1,1}}};
    for (uint32_t pass = 0; pass < 4; ++pass)
        for (uint32_t z = cz; z < endZ; ++z) for (uint32_t x = cx; x < endX; ++x) {
            if (out.cells[index(x,z)]) continue;
            for (uint32_t rotation = 0; rotation < (pass == 0 || pass == 2 ? 2u : 1u); ++rotation) {
                const uint32_t turn = rotation ^ (hash((x + sourceX) >> 2, (z + sourceZ) >> 1) & 1u);
                const uint32_t shape = pass == 0 ? turn : pass == 1 ? 2u : pass == 2 ? 3u + turn : 5u;
                const auto [w,d] = shapes[shape];
                if (x + w > endX || z + d > endZ) continue;
                bool fits = true;
                for (uint32_t dz=0; dz<d; ++dz) for (uint32_t dx=0; dx<w; ++dx)
                    fits = fits && !out.cells[index(x+dx,z+dz)] && at(x+dx,z+dz)==at(x,z);
                if (!fits) continue;
                std::array<uint32_t,4> votes{};
                for (uint32_t dz=0; dz<d; ++dz) for (uint32_t dx=0; dx<w; ++dx)
                    ++votes[colorFamily(s,x+dx,z+dz,waterHeight)];
                const uint32_t family = uint32_t(std::max_element(votes.begin(),votes.end())-votes.begin());
                const uint32_t shadeHash = hash(x+sourceX,z+sourceZ) % 10u;
                const uint32_t palette = family*3u + (shadeHash<2u ? 0u : shadeHash>=8u ? 2u : 1u);
                for (uint32_t dz=0; dz<d; ++dz) for (uint32_t dx=0; dx<w; ++dx)
                    out.cells[index(x+dx,z+dz)] = uint16_t(0x1000u | dx | dz<<2u | (w-1u)<<4u | (d-1u)<<6u | palette<<8u);
                ++out.bricks;
                if (w*d==8u) ++out.largeBricks;
                break;
            }
        }
    return out;
}

inline Layout buildLayout(const Surface& s, float waterHeight,
                          uint32_t sourceX = 0, uint32_t sourceZ = 0) {
    Layout out;
    if (!s.valid() || s.width > kMaximumStudySamples || s.height > kMaximumStudySamples) return out;
    out.cells.resize(size_t{s.width} * s.height);
    for (uint32_t cz = 0; cz < s.height - 1; cz += kChunkCells)
        for (uint32_t cx = 0; cx < s.width - 1; cx += kChunkCells) {
            const auto chunk = buildChunk(s,cx/kChunkCells,cz/kChunkCells,waterHeight,sourceX,sourceZ);
            ++out.chunks;
            out.bricks += chunk.bricks;
            out.largeBricks += chunk.largeBricks;
            for (uint32_t z=cz; z<std::min(cz+kChunkCells,s.height-1); ++z)
                for (uint32_t x=cx; x<std::min(cx+kChunkCells,s.width-1); ++x)
                    out.cells[size_t{z}*s.width+x] = chunk.cells[size_t{z-cz}*kChunkCells+x-cx];
        }
    return out;
}
} // namespace voxy::terrain::lego
