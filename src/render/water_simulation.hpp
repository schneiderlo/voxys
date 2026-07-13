#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

/// Three-cascade Tessendorf ocean evaluated entirely on the GPU.
///
/// Each cascade evolves a directional Phillips spectrum, performs a 2D inverse
/// FFT, and writes height, horizontal displacement, and Jacobian compression to
/// a filterable texture array. The renderer consumes this one authoritative
/// surface in both the ray intersection and lighting passes.
class WaterSimulation {
public:
    static constexpr uint32_t RESOLUTION = 256;
    static constexpr uint32_t CASCADE_COUNT = 3;
    static constexpr uint32_t FFT_STAGE_COUNT = 8;

    WaterSimulation() = default;
    ~WaterSimulation();

    WaterSimulation(const WaterSimulation&) = delete;
    WaterSimulation& operator=(const WaterSimulation&) = delete;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const std::filesystem::path& shaderDirectory);
    void shutdown();

    /// Record spectrum evolution, 16 Stockham/Cooley FFT stages, and resolve.
    void update(WGPUCommandEncoder encoder, float timeSeconds);

    [[nodiscard]] bool isInitialized() const noexcept {
        return evolvePipeline_ != nullptr && outputView_ != nullptr;
    }
    [[nodiscard]] WGPUTextureView getOutputView() const noexcept { return outputView_; }
    [[nodiscard]] WGPUTextureView getFoamView() const noexcept { return foamView_; }
    [[nodiscard]] WGPUSampler getSampler() const noexcept { return sampler_; }

private:
    struct SimParams {
        float time = 0.0f;
        uint32_t stage = 0;
        uint32_t axis = 0;
        uint32_t size = RESOLUTION;
    };
    static_assert(sizeof(SimParams) == 16);

    bool createSpectrum();
    bool createBuffers();
    bool createOutputTexture();
    bool createPipelines(const std::filesystem::path& shaderDirectory);
    bool createBindGroups();
    bool createFoamResources(const std::filesystem::path& shaderDirectory);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    WGPUBuffer initialSpectrumBuffer_ = nullptr;
    WGPUBuffer pingBuffer_ = nullptr;
    WGPUBuffer pongBuffer_ = nullptr;
    WGPUBuffer simulationUniformBuffer_ = nullptr;
    std::array<WGPUBuffer, FFT_STAGE_COUNT * 2> stageUniformBuffers_{};

    WGPUTexture outputTexture_ = nullptr;
    WGPUTextureView outputView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    WGPUBuffer foamUniformBuffer_ = nullptr;
    std::array<WGPUBuffer, 2> foamBuffers_{};
    WGPUTexture foamTexture_ = nullptr;
    WGPUTextureView foamView_ = nullptr;
    WGPUShaderModule foamShader_ = nullptr;
    WGPUBindGroupLayout foamBindGroupLayout_ = nullptr;
    WGPUPipelineLayout foamPipelineLayout_ = nullptr;
    WGPUComputePipeline foamPipeline_ = nullptr;
    std::array<WGPUBindGroup, 2> foamBindGroups_{};
    uint32_t foamFrame_ = 0;
    float lastUpdateTime_ = 0.0f;

    WGPUShaderModule fftShader_ = nullptr;
    WGPUBindGroupLayout fftBindGroupLayout_ = nullptr;
    WGPUPipelineLayout fftPipelineLayout_ = nullptr;
    WGPUComputePipeline evolvePipeline_ = nullptr;
    WGPUComputePipeline fftPipeline_ = nullptr;
    WGPUBindGroup evolveBindGroup_ = nullptr;
    std::array<WGPUBindGroup, FFT_STAGE_COUNT * 2> fftBindGroups_{};

    WGPUShaderModule finalizeShader_ = nullptr;
    WGPUBindGroupLayout finalizeBindGroupLayout_ = nullptr;
    WGPUPipelineLayout finalizePipelineLayout_ = nullptr;
    WGPUComputePipeline finalizePipeline_ = nullptr;
    WGPUBindGroup finalizeBindGroup_ = nullptr;
};

} // namespace voxy::render
