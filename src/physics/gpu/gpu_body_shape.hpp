#pragma once

#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace voxy::physics {

// Shared persistent-body ABI for physics and its direct renderer. Primitive
// records use a zero resource reference; materials never select authored IDs.
// The referenced atlas is world-owned. Its pool incarnation stays in the CPU
// handle; index/generation are checked within that bound atlas on the GPU.
// A nonzero reference is written only by typed authored-body admission. Generic
// primitive commands cannot set it or infer it from a material value.
struct alignas(16) GpuBodyShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 inverseInertiaMaterial{0.0f};
    glm::vec4 materialCoefficients{-1.0f, -1.0f, -1.0f, 1.0f};
    // Shape index, shape generation, resource format, reserved zero.
    glm::uvec4 authoredShape{0u};
};
static_assert(std::is_trivially_copyable_v<GpuBodyShape>);
static_assert(std::is_standard_layout_v<GpuBodyShape>);
static_assert(sizeof(GpuBodyShape)==64 && alignof(GpuBodyShape)==16);
static_assert(offsetof(GpuBodyShape,dimensionsType)==0);
static_assert(offsetof(GpuBodyShape,inverseInertiaMaterial)==16);
static_assert(offsetof(GpuBodyShape,materialCoefficients)==32);
static_assert(offsetof(GpuBodyShape,authoredShape)==48);

} // namespace voxy::physics
