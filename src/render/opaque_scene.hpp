#pragma once

#include "gpu/resources.hpp"

#include <filesystem>
#include <memory>

namespace voxy::render {

// Per-frame opaque inputs for water. These never alias the immutable terrain
// cache or the final water output. Color is fogged linear radiance, before
// exposure/tone mapping; depth is radial camera distance in metres (-1 = sky).
class OpaqueScene final {
public:
    OpaqueScene();
    ~OpaqueScene();
    OpaqueScene(const OpaqueScene&) = delete;
    OpaqueScene& operator=(const OpaqueScene&) = delete;
    [[nodiscard]] bool init(WGPUDevice device, const std::filesystem::path& shader);
    [[nodiscard]] bool resize(uint32_t width, uint32_t height);
    // Every frame starts with this copy, including stationary-camera frames.
    // It removes previous objects without ever modifying the terrain cache.
    [[nodiscard]] bool seed(WGPUCommandEncoder encoder, WGPUTextureView terrainColor,
                            WGPUTextureView terrainDepth, WGPUQuerySet query = nullptr,
                            uint32_t beginQuery = WGPU_QUERY_SET_INDEX_UNDEFINED);
    [[nodiscard]] WGPUTextureView colorView() const noexcept;
    [[nodiscard]] WGPUTextureView depthView() const noexcept;
    [[nodiscard]] WGPUTexture colorTexture() const noexcept;
    [[nodiscard]] WGPUTexture depthTexture() const noexcept;
    [[nodiscard]] uint64_t requestedBytes() const noexcept;
    static constexpr uint64_t bytesPerPixel = 12;
    // A 4K CSS viewport at the browser's 1.5 DPR cap needs over 200 MiB.
    // Rejecting it here leaves an initialized UI above a black canvas.
    static constexpr uint64_t maximumBytes = 256ull * 1024 * 1024;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::render
