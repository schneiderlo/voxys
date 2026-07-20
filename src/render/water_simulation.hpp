#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

struct WaterSpectrumConfig {
    float significantWaveHeight = 25.9f;
    float directionRadians = 0.9948376736367679f;
    float choppiness = 2.24f;
    float peakEnhancement = 0.65f;
    float windAlignment = 0.32f;
    float animationSpeed = 2.0f;
    glm::vec2 patchLengths = {1949.0f, 326.0f};
    glm::vec2 cascadeAmplitudes = {0.33f, 0.07f};
    float directionalSineScale = 0.68f;
};

/// Two-cascade ocean evaluated entirely on the GPU.
///
/// Each cascade evolves a band-limited peaked spectrum, performs a 2D inverse
/// FFT, and writes displacement plus the displaced-surface normal to a
/// filterable texture array. Four quantized long swells are evaluated by every
/// consumer on top of the spectral surface.
class WaterSimulation {
public:
    static constexpr uint32_t RESOLUTION = 256;
    static constexpr uint32_t CASCADE_COUNT = 2;
    static constexpr uint32_t OUTPUT_LAYER_COUNT = CASCADE_COUNT * 2;
    static constexpr uint32_t FFT_STAGE_COUNT = 8;
    static constexpr uint32_t COAST_FIELD_RESOLUTION = 1024;
    static constexpr float SPECTRAL_UPDATE_HZ = 120.0f;

    WaterSimulation() = default;
    ~WaterSimulation();

    WaterSimulation(const WaterSimulation&) = delete;
    WaterSimulation& operator=(const WaterSimulation&) = delete;

    struct SurfaceSample {
        float heightOffset = 0.0f;
        glm::vec2 slope{0.0f};
        glm::vec3 velocity{0.0f};
    };

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const std::filesystem::path& shaderDirectory,
                            std::span<const uint16_t> terrainHeights = {},
                            uint32_t terrainWidth = 0,
                            uint32_t terrainHeight = 0,
                            float terrainHeightScale = 1.0f,
                            float cellScale = 1.0f,
                            float waterHeight = 0.0f,
                            const WaterSpectrumConfig& spectrum = {});
    void shutdown();

    /// Apply live wave controls. Spectrum-shape changes rebuild the 4 MiB
    /// initial spectrum and its bind groups; animation-only changes are cheap.
    [[nodiscard]] bool reconfigure(const WaterSpectrumConfig& spectrum);

    /// Rebuild the terrain-dependent coastal field after changing water level.
    [[nodiscard]] bool rebuildCoastField(
        std::span<const uint16_t> terrainHeights,
        uint32_t terrainWidth, uint32_t terrainHeight,
        float terrainHeightScale, float cellScale, float waterHeight);

    [[nodiscard]] const WaterSpectrumConfig& spectrumConfig() const noexcept {
        return spectrumConfig_;
    }

    /// Record spectrum evolution, two workgroup-local FFT axes, and resolve.
    void update(
        WGPUCommandEncoder encoder, float timeSeconds,
        WGPUQuerySet timestampQuerySet = nullptr,
        uint32_t timestampBegin = WGPU_QUERY_SET_INDEX_UNDEFINED,
        uint32_t timestampEnd = WGPU_QUERY_SET_INDEX_UNDEFINED);

    [[nodiscard]] bool isInitialized() const noexcept {
        return evolvePipeline_ != nullptr && outputView_ != nullptr;
    }
    [[nodiscard]] WGPUTextureView getOutputView() const noexcept { return outputView_; }
    [[nodiscard]] WGPUTextureView getFoamView() const noexcept { return foamView_; }
    [[nodiscard]] WGPUTextureView getCoastView() const noexcept { return coastView_; }
    [[nodiscard]] WGPUSampler getSampler() const noexcept { return sampler_; }

    /// Approximate the rendered FFT surface from its strongest spectral modes.
    [[nodiscard]] SurfaceSample sampleSurface(glm::vec2 worldPosition,
                                              float timeSeconds,
                                              float strength = 1.0f) const;

private:
    struct SimParams {
        float time = 0.0f;
        uint32_t stage = 0;
        uint32_t axis = 0;
        uint32_t size = RESOLUTION;
        glm::vec2 patchLengths = {1949.0f, 326.0f};
        glm::vec2 cascadeAmplitudes = {0.33f, 0.07f};
        float choppiness = 2.24f;
        float directionalSineScale = 0.68f;
        glm::vec2 padding{0.0f};
    };
    static_assert(sizeof(SimParams) == 48);

    struct CpuWaveMode {
        glm::vec2 waveVector{0.0f};
        glm::vec2 sampleOffset{0.0f};
        glm::vec2 initialPositive{0.0f};
        glm::vec2 conjugateNegative{0.0f};
        float angularFrequency = 0.0f;
        float amplitude = 1.0f;
        uint32_t cascade = 0u;
    };

    bool createSpectrum();
    bool createBuffers();
    bool createOutputTexture();
    bool createCoastField(std::span<const uint16_t> terrainHeights,
                          uint32_t terrainWidth, uint32_t terrainHeight,
                          float terrainHeightScale, float cellScale,
                          float waterHeight);
    bool createPipelines(const std::filesystem::path& shaderDirectory);
    bool createBindGroups();
    bool createFoamResources(const std::filesystem::path& shaderDirectory);
    void releaseSimulationBindGroups();
    void updateCpuWaveCache(float timeSeconds) const;

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WaterSpectrumConfig spectrumConfig_{};

    WGPUBuffer initialSpectrumBuffer_ = nullptr;
    WGPUBuffer pongBuffer_ = nullptr;
    WGPUBuffer simulationUniformBuffer_ = nullptr;
    WGPUBuffer fftTwiddleBuffer_ = nullptr;
    std::array<WGPUBuffer, 2> axisUniformBuffers_{};

    WGPUTexture outputTexture_ = nullptr;
    WGPUTextureView outputView_ = nullptr;
    WGPUTexture coastTexture_ = nullptr;
    WGPUTextureView coastView_ = nullptr;
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

    std::vector<CpuWaveMode> cpuWaveModes_;
    mutable std::vector<glm::vec2> cpuEvolvedHeight_;
    mutable std::vector<glm::vec2> cpuEvolvedVelocity_;
    mutable float cpuCacheTime_ = -1.0f;

    WGPUShaderModule fftShader_ = nullptr;
    WGPUBindGroupLayout fftBindGroupLayout_ = nullptr;
    WGPUPipelineLayout fftPipelineLayout_ = nullptr;
    WGPUComputePipeline evolvePipeline_ = nullptr;
    WGPUComputePipeline fftPipeline_ = nullptr;
    WGPUBindGroup evolveBindGroup_ = nullptr;
    std::array<WGPUBindGroup, 2> fftAxisBindGroups_{};

    WGPUShaderModule finalizeShader_ = nullptr;
    WGPUBindGroupLayout finalizeBindGroupLayout_ = nullptr;
    WGPUPipelineLayout finalizePipelineLayout_ = nullptr;
    WGPUComputePipeline finalizePipeline_ = nullptr;
    WGPUBindGroup finalizeBindGroup_ = nullptr;
};

} // namespace voxy::render
