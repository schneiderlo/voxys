#pragma once

#include <cstdint>
#include <vector>

namespace voxy::render::detail {

struct WaterClipmapVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static_assert(sizeof(WaterClipmapVertex) == 12u);

struct WaterClipmapMesh {
    std::vector<WaterClipmapVertex> vertices;
    std::vector<uint32_t> indices;
};

[[nodiscard]] WaterClipmapMesh makeWaterClipmap();

} // namespace voxy::render::detail
