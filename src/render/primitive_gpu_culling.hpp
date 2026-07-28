#pragma once

#include "physics/physics_types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace voxy::render {

struct PrimitiveDrawGeometry {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
};

class PrimitiveGpuCulling {
public:
    static constexpr uint32_t kShapeCount =
        static_cast<uint32_t>(physics::ThrowableShape::Count);

    PrimitiveGpuCulling() = default;
    ~PrimitiveGpuCulling();

    PrimitiveGpuCulling(const PrimitiveGpuCulling&) = delete;
    PrimitiveGpuCulling& operator=(const PrimitiveGpuCulling&) = delete;

    [[nodiscard]] bool initialize(
        WGPUDevice device, WGPUQueue queue,
        const std::filesystem::path& shaderPath,
        const std::array<PrimitiveDrawGeometry, kShapeCount>& geometry);
    void shutdown();

    [[nodiscard]] bool setBodyView(
        const physics::PhysicsRenderView& view);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder,
                              const glm::mat4& viewProjection,
                              const glm::ivec3& cameraSector = glm::ivec3(0));

    [[nodiscard]] WGPUBuffer visibleBodyIds() const noexcept {
        return visibleBodyIds_;
    }
    [[nodiscard]] WGPUBuffer indirectDrawArgs() const noexcept {
        return indirectDrawArgs_;
    }
    // The compact vertex path must consume the exact pose frame used by
    // culling. This is the physics pose buffer for CPU uploads and the
    // camera-sector-rebased buffer for direct GPU physics rendering.
    [[nodiscard]] WGPUBuffer renderPoseBuffer() const noexcept {
        return bodyView_.metadataBuffer ? cameraRelativePoses_
                                        : bodyView_.poseBuffer;
    }
    [[nodiscard]] uint32_t segmentCapacity() const noexcept {
        return segmentCapacity_;
    }
    [[nodiscard]] bool hasBodies() const noexcept {
        return bodyView_.valid() && bodyView_.residentBodyCapacity != 0;
    }

private:
    [[nodiscard]] bool initializeFresh(
        WGPUDevice device, WGPUQueue queue,
        const std::filesystem::path& shaderPath,
        const std::array<PrimitiveDrawGeometry, kShapeCount>& geometry);
    void swap(PrimitiveGpuCulling& other) noexcept;
    [[nodiscard]] bool ensureCapacity(uint32_t bodyCapacity);
    [[nodiscard]] bool updateBindGroup();
    void releaseCapacityBuffers();

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    physics::PhysicsRenderView bodyView_{};
    std::array<PrimitiveDrawGeometry, kShapeCount> geometry_{};
    uint32_t allocatedBodyCapacity_ = 0;
    uint32_t segmentCapacity_ = 0;
    uint32_t blockCapacity_ = 0;
    bool bindGroupDirty_ = true;
    bool rebaseBindGroupDirty_ = true;

    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline blocksPipeline_ = nullptr;
    WGPUComputePipeline scanPipeline_ = nullptr;
    WGPUComputePipeline scatterPipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBindGroupLayout rebaseBindGroupLayout_ = nullptr;
    WGPUPipelineLayout rebasePipelineLayout_ = nullptr;
    WGPUComputePipeline rebasePipeline_ = nullptr;
    WGPUBindGroup rebaseBindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer cameraRelativePoses_ = nullptr;
    WGPUBuffer visibility_ = nullptr;
    WGPUBuffer localOffsets_ = nullptr;
    WGPUBuffer blockSums_ = nullptr;
    WGPUBuffer blockPrefix_ = nullptr;
    WGPUBuffer visibleBodyIds_ = nullptr;
    WGPUBuffer indirectDrawArgs_ = nullptr;
};

} // namespace voxy::render
