#pragma once

#include "gpu/webgpu_compat.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace voxy::render {

struct EnvironmentLightingConfig {
    std::filesystem::path shaderPath = "shaders/environment_lighting.wgsl";
    uint32_t specularSize = 128; // Power of two, 16..512, full roughness mip chain.
    uint32_t diffuseSize = 32;   // Power of two, 4..64.
    uint32_t brdfSize = 128;     // Power of two, 16..256.
    uint32_t samples = 1024;     // 64..4096; one bounded bake, not per frame.
};

struct FilteredEnvironmentViews {
    WGPUTextureView specular = nullptr; // RGBA16F cube, mip r*(levels-1).
    WGPUTextureView diffuse = nullptr;  // RGBA16F cube, irradiance divided by pi.
    WGPUTextureView brdf = nullptr;     // RGBA16F 2D, R=A / G=B, UV=(NoV,r).
};

/// Bakes linear HDR environment lighting from an equirectangular source.
/// Encoding does not mean submission/completion: callers must queue the bake
/// before consumption, retry after abandoning its encoder, and own source
/// revisions. All output views are borrowed. Releasing this owner never calls
/// Destroy, so existing bind groups/encoded commands retain their dependencies.
class EnvironmentLighting {
public:
    EnvironmentLighting();
    ~EnvironmentLighting();
    EnvironmentLighting(const EnvironmentLighting&) = delete;
    EnvironmentLighting& operator=(const EnvironmentLighting&) = delete;

    /// CPU rejection preserves the previous state. The caller must also check
    /// WebGPU error scopes for asynchronous creation/encoding errors.
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const EnvironmentLightingConfig& config = {});
    /// Source must be a filterable 2D texture view of linear radiance, ideally
    /// with its full image mip chain. Longitude wraps; latitude clamps.
    [[nodiscard]] bool encodeBake(WGPUCommandEncoder encoder,
                                  WGPUTextureView source);
    void releaseHandles() noexcept;
    [[nodiscard]] FilteredEnvironmentViews views() const noexcept;
    /// Texture texels and uniform bytes, not driver allocation/opaque handles.
    [[nodiscard]] uint64_t requestedBytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::render
