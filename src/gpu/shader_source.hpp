#pragma once

#include <string_view>

namespace voxy::gpu {

// Immutable WGSL supplied by a trusted build artifact. An empty source bundle
// preserves the normal filesystem loader. A nonempty bundle is strict:
// loadShaderModule fails when the requested logical path is absent.
struct ShaderSource {
    std::string_view logicalPath{};
    std::string_view wgsl{};

    [[nodiscard]] bool operator==(const ShaderSource&) const = default;
};

} // namespace voxy::gpu
