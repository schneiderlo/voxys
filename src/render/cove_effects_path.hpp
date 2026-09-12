#pragma once

#include "game/expedition/cove_effects.hpp"
#include "render/triangle_path.hpp"
#include <array>
#include <filesystem>

namespace voxy::render {

struct CoveEffectsFrame {
    const CameraUniforms* camera=nullptr; // Same world-space block as BlitPath.
    glm::dvec3 sceneOrigin{}; // Adds Cove-local instance points to that world.
    physics::WaterGpuResources water{}; // Same live FFT binding as water.
};

// Bounded composed-output overlay. Uses immutable visible radial depth after
// water, no depth writes, and water's ACES/sRGB presentation. Blending occurs
// after tone mapping; this is explicitly not an HDR/temporal reconstruction
// implementation. It cannot supply refracted airborne effects across water.
class CoveEffectsPath {
public:
    using Instance=game::expedition::CoveEffects::Instance;
    static constexpr size_t maximumInstances=game::expedition::CoveEffects::maximumInstances;
    static constexpr uint64_t residentBytes=maximumInstances*sizeof(Instance)+sizeof(CameraUniforms)+16;
    static_assert(residentBytes<=128u*1024u);
    CoveEffectsPath()=default;
    ~CoveEffectsPath(){shutdown();}
    CoveEffectsPath(const CoveEffectsPath&)=delete;
    CoveEffectsPath& operator=(const CoveEffectsPath&)=delete;
    [[nodiscard]] bool init(WGPUDevice,WGPUQueue,WGPUTextureFormat,const std::filesystem::path& shader);
    // Caller drains owned frame tickets before ordinary retirement. Release,
    // never Destroy, lets already submitted commands retain their resources.
    void shutdown() noexcept;
    [[nodiscard]] bool render(WGPUCommandEncoder,WGPUTextureView color,WGPUTextureView visibleLinearDepth,
        const CoveEffectsFrame&,std::span<const Instance>);
    [[nodiscard]] bool initialized() const noexcept {return pipeline_!=nullptr;}
    void clearEncodedObservation() noexcept {encoded_=0;}
    [[nodiscard]] uint32_t lastEncodedInstances() const noexcept {return encoded_;}
    [[nodiscard]] uint64_t bindingChanges() const noexcept {return bindingChanges_;}
    [[nodiscard]] uint64_t uploadCount() const noexcept {return uploads_;}
private:
    struct alignas(16) Uniforms {CameraUniforms camera;glm::vec4 sceneOrigin{};};
    static_assert(sizeof(Uniforms)==sizeof(CameraUniforms)+16);
    WGPUDevice device_=nullptr;
    WGPUQueue queue_=nullptr;
    WGPUShaderModule shader_=nullptr;
    WGPUBuffer instances_=nullptr,uniforms_=nullptr;
    WGPUBindGroupLayout layout_=nullptr;
    WGPUPipelineLayout pipelineLayout_=nullptr;
    WGPURenderPipeline pipeline_=nullptr;
    WGPUBindGroup bindings_=nullptr;
    // Identity values only; bindings_ retains the actual immutable GPU refs.
    WGPUTextureView boundDepth_=nullptr,boundWater_=nullptr;
    WGPUSampler boundSampler_=nullptr;
    std::array<Instance,maximumInstances> sorted_{};
    uint32_t encoded_=0;
    uint64_t bindingChanges_=0,uploads_=0;
};

} // namespace voxy::render
