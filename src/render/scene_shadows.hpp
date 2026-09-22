#pragma once

#include "gpu/resources.hpp"
#include "render/environment_lighting.hpp"
#include <array>
#include <glm/glm.hpp>

namespace voxy::render {

// Two animated sole centres in the mesh frame; w is the soft contact radius.
// A zero radius disables the contact. These affect indirect light only.
using FootContacts = std::array<glm::vec4, 2>;

struct alignas(16) SunShadowUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 params{0.0f};
    // Absolute world position of the caster coordinate frame's origin.
    // Mesh positions are already local to this frame; terrain/water are not.
    glm::vec4 worldOrigin{0.0f};
    FootContacts footContacts{};
    glm::mat4 farViewProj{1.0f};
    glm::vec4 farParams{0.0f};
};
static_assert(sizeof(SunShadowUniforms) == 208u);

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
    // Called after the same owner's environment bake is encoded, before color.
    // These views are borrowed for this encoding; a consumer may retain GPU
    // references only through an explicitly owned, bounded bind group.
    bool (*bindEnvironment)(void*, const FilteredEnvironmentViews&) = nullptr;
    [[nodiscard]] bool environment(const FilteredEnvironmentViews& views) const {
        return !bindEnvironment || bindEnvironment(context, views);
    }
    [[nodiscard]] bool operator()(WGPUCommandEncoder commands, WGPUBindGroup shadows) const {
        return !encode || encode(context, commands, shadows);
    }
};

} // namespace voxy::render
