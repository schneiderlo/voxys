#pragma once

#include "gpu/resources.hpp"
#include <array>
#include <glm/glm.hpp>

namespace voxy::render {

struct alignas(16) SunShadowUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 params{0.0f};
    // Absolute world position of the caster coordinate frame's origin.
    // Mesh positions are already local to this frame; terrain/water are not.
    glm::vec4 worldOrigin{0.0f};
};
static_assert(sizeof(SunShadowUniforms) == 96u);

inline auto sceneShadowLayoutEntries() {
    using Entry = gpu::BindGroupLayoutEntry;
    return std::array{
        Entry(0).vertexVisible().fragmentVisible().uniformBuffer(false, sizeof(SunShadowUniforms)),
        Entry(1).fragmentVisible().texture(WGPUTextureSampleType_Depth),
        Entry(2).fragmentVisible().sampler(WGPUSamplerBindingType_Comparison)};
}

// Runs after the current shadow map is encoded and before object color. The
// binding is borrowed from the same fixture generation and submission ticket.
// Consumers must not retain it. A null binding means no active caster map.
struct SceneShadowConsumer {
    void* context = nullptr;
    bool (*encode)(void*, WGPUCommandEncoder, WGPUBindGroup) = nullptr;
    [[nodiscard]] bool operator()(WGPUCommandEncoder commands, WGPUBindGroup shadows) const {
        return !encode || encode(context, commands, shadows);
    }
};

} // namespace voxy::render
