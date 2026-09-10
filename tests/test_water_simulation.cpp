#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/water_simulation.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <array>
#include <iostream>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, const WGPUWrappedSubmissionIndex*);

namespace voxy::render {
namespace {

std::filesystem::path findShaderDirectory() {
    for (const auto& candidate : {
             std::filesystem::path("shaders"),
             std::filesystem::path("../shaders"),
             std::filesystem::path("../../shaders"),
             std::filesystem::path("../../../shaders")}) {
        if (std::filesystem::exists(candidate / "water_fft.wgsl") &&
            std::filesystem::exists(candidate / "water_finalize.wgsl") &&
            std::filesystem::exists(candidate / "water_clipmap.wgsl")) {
            return candidate;
        }
    }
    return {};
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

TEST(WaterSimulationTest, SpectralGridConstantsAreCoherent) {
    EXPECT_EQ(WaterSimulation::RESOLUTION, 1u << WaterSimulation::FFT_STAGE_COUNT);
    EXPECT_EQ(WaterSimulation::CASCADE_COUNT, 2u);
    EXPECT_EQ(WaterSimulation::OUTPUT_LAYER_COUNT, 4u);
    EXPECT_EQ(WaterSimulation::COAST_FIELD_RESOLUTION, 1024u);
    EXPECT_EQ(WaterSimulation::RESOLUTION & (WaterSimulation::RESOLUTION - 1u), 0u);
}

TEST(WaterSimulationTest, DefaultConstructionOwnsNoGPUResources) {
    WaterSimulation simulation;
    EXPECT_FALSE(simulation.isInitialized());
    EXPECT_EQ(simulation.getOutputView(), nullptr);
    EXPECT_EQ(simulation.getCoastView(), nullptr);
    EXPECT_EQ(simulation.getSampler(), nullptr);
}

TEST(WaterSimulationTest, RejectsNullGPUHandles) {
    WaterSimulation simulation;
    EXPECT_FALSE(simulation.init(nullptr, nullptr, findShaderDirectory()));
}

TEST(WaterSimulationGPUTest, PlacementHeightMatchesActualFFTAtBerthAndRepeatSeams) {
    struct Map {bool done=false,ok=false;} mapped; // Outlives context on failed assertions.
    gpu::Context context;ASSERT_TRUE(context.initHeadless());
    WaterSimulation simulation;ASSERT_TRUE(simulation.init(context.getDevice(),context.getQueue(),findShaderDirectory()));
    const auto shader=gpu::createShaderModule(context.getDevice(),R"(
        @group(0) @binding(0) var field: texture_2d_array<f32>;
        @group(0) @binding(1) var linearRepeat: sampler;
        @group(0) @binding(2) var<storage,read_write> result: array<vec4<f32>>;
        @group(0) @binding(3) var<uniform> query: vec4<f32>;
        @compute @workgroup_size(1) fn main() {
            let p=query.xy;
            let uv0=p/query.z+vec2<f32>(0.5+0.5/256.0);
            let uv1=p/query.w+vec2<f32>(0.5+0.5/256.0);
            let broad=textureSampleLevel(field,linearRepeat,uv0,0,0.0);
            let detail=textureSampleLevel(field,linearRepeat,uv1,1,0.0);
            result[0]=vec4<f32>(broad.y,detail.y,0.0,0.0);
        })");ASSERT_NE(shader,nullptr);
    WGPUComputePipelineDescriptor pd{};pd.compute.module=shader;WGPU_SET_ENTRY_POINT(pd.compute,"main");
    const auto pipeline=wgpuDeviceCreateComputePipeline(context.getDevice(),&pd);ASSERT_NE(pipeline,nullptr);
    const auto layout=wgpuComputePipelineGetBindGroupLayout(pipeline,0);ASSERT_NE(layout,nullptr);
    const auto output=gpu::createBuffer(context.getDevice(),{.size=16,.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc});
    const auto readback=gpu::createBuffer(context.getDevice(),{.size=16,.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    const auto query=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::uniform(16));
    ASSERT_NE(output,nullptr);ASSERT_NE(readback,nullptr);ASSERT_NE(query,nullptr);
    const std::array entries{gpu::BindGroupEntry(0).textureView(simulation.getOutputView()),
        gpu::BindGroupEntry(1).sampler(simulation.getSampler()),gpu::BindGroupEntry(2).buffer(output),gpu::BindGroupEntry(3).buffer(query)};
    const auto group=gpu::createBindGroup(context.getDevice(),layout,entries);ASSERT_NE(group,nullptr);
    for(int spectrum=0;spectrum<2;++spectrum){
    if(spectrum){
        auto config=simulation.spectrumConfig();config.patchLengths={1536,512};
        config.cascadeAmplitudes={.4f,.1f};config.directionalSineScale=1.2f;
        ASSERT_TRUE(simulation.reconfigure(config));
    }
    for(const auto position:std::array{glm::vec2(-19.5f,-91.f),glm::vec2(0),glm::vec2(974.49f,-163.02f),
        glm::vec2(-974.51f,-326.f),glm::vec2(1949.1f,326.4f)})
    for(float phase:std::array{.38333333f,39.1f,0.f,4095.9f}){
        const auto patch=simulation.spectrumConfig().patchLengths;
        ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),query,0,glm::vec4(position,patch)));
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        simulation.update(encoder,phase);
        WGPUComputePassDescriptor passDesc{};const auto pass=wgpuCommandEncoderBeginComputePass(encoder,&passDesc);
        wgpuComputePassEncoderSetPipeline(pass,pipeline);wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
        WGPUCommandBufferDescriptor cd{};const auto command=wgpuCommandEncoderFinish(encoder,&cd);
        wgpuQueueSubmit(context.getQueue(),1,&command);
        mapped={};
        wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,16,[](auto status,void* data){
            auto& m=*static_cast<Map*>(data);m.ok=status==WGPUBufferMapAsyncStatus_Success;m.done=true;
        },&mapped);
        (void)wgpuDevicePoll(context.getDevice(),true,nullptr);ASSERT_TRUE(mapped.done&&mapped.ok);
        const auto* result=static_cast<const float*>(wgpuBufferGetConstMappedRange(readback,0,16));ASSERT_NE(result,nullptr);
        const auto placement=simulation.samplePlacementHeight(position,phase,.1f);ASSERT_TRUE(placement);
        float swells=0;size_t waveIndex=0;
        constexpr std::array omega{.374291312f,.296825282f,.234699061f,.186378666f};
        for(const auto wave:std::array{glm::vec4(.923059017f,.384658357f,440.298507f,0.f),
                glm::vec4(.700400636f,.713749921f,701.258144f,5.553108549f),
                glm::vec4(.367164395f,.930156066f,1116.885424f,4.823031791f),
                glm::vec4(-.024039031f,.999711021f,1778.85f,4.092955033f)}){
            swells+=.51541f*std::cos(6.283185307f*(wave.x*position.x+wave.y*position.y)/wave.z-omega[waveIndex++]*phase+wave.w);
        }
        // The old sparse sampler differs by 75 cm at the first berth/phase.
        // Placement is checked against the actual GPU output, not its own formulas.
        EXPECT_NEAR(*placement,(result[0]+result[1])*.1f+swells,.005f)
            <<"spectrum "<<spectrum<<" point "<<position.x<<","<<position.y<<" phase "<<phase;
        wgpuBufferUnmap(readback);wgpuCommandBufferRelease(command);wgpuCommandEncoderRelease(encoder);
    }
    }
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    EXPECT_FALSE(simulation.samplePlacementHeight({nan,0},0,.1f));
    EXPECT_FALSE(simulation.samplePlacementHeight({0,inf},0,.1f));
    EXPECT_FALSE(simulation.samplePlacementHeight({0,0},nan,.1f));
    EXPECT_FALSE(simulation.samplePlacementHeight({0,0},0,inf));
    EXPECT_EQ(simulation.samplePlacementHeight({0,0},0,0),std::optional<float>{0});
    EXPECT_FALSE(WaterSimulation{}.samplePlacementHeight({0,0},0,.1f));
    wgpuBufferRelease(query);
    wgpuBindGroupRelease(group);wgpuBufferRelease(output);wgpuBufferRelease(readback);
    wgpuBindGroupLayoutRelease(layout);wgpuComputePipelineRelease(pipeline);wgpuShaderModuleRelease(shader);
}

TEST(WaterSimulationGPUTest, BuildsAndDispatchesCompleteOceanPipeline) {
    const auto shaderDirectory = findShaderDirectory();
    ASSERT_FALSE(shaderDirectory.empty()) << "FFT water shaders not found";

    gpu::Context context;
    if (!context.initHeadless()) {
        GTEST_SKIP() << "GPU context not available";
    }

    WaterSimulation simulation;
    constexpr uint32_t terrainSize = 64;
    std::vector<uint16_t> terrain(terrainSize * terrainSize, 18000u);
    for (uint32_t y = 0; y < terrainSize; ++y) {
        for (uint32_t x = 44; x < terrainSize; ++x) {
            terrain[y * terrainSize + x] = 47000u;
        }
    }
    ASSERT_TRUE(simulation.init(context.getDevice(), context.getQueue(), shaderDirectory,
                                terrain, terrainSize, terrainSize, 500.0f, 1.0f, 0.0f));
    ASSERT_TRUE(simulation.isInitialized());
    ASSERT_NE(simulation.getOutputView(), nullptr);
    ASSERT_NE(simulation.getCoastView(), nullptr);

    const auto noGpuSurface = WaterSimulation{}.sampleSurface(
        glm::vec2(1.0f), 1.0f, 1.0f);
    EXPECT_EQ(noGpuSurface.heightOffset, 0.0f);
    EXPECT_EQ(noGpuSurface.slope, glm::vec2(0.0f));
    EXPECT_EQ(noGpuSurface.velocity, glm::vec3(0.0f));

    const auto still = simulation.sampleSurface(glm::vec2(14.0f, -27.0f),
                                                0.0f, 0.0f);
    EXPECT_FLOAT_EQ(still.heightOffset, 0.0f);
    EXPECT_EQ(still.slope, glm::vec2(0.0f));
    EXPECT_EQ(still.velocity, glm::vec3(0.0f));
    const auto first = simulation.sampleSurface(glm::vec2(14.0f, -27.0f),
                                                0.0f, 0.08f);
    const auto later = simulation.sampleSurface(glm::vec2(14.0f, -27.0f),
                                                1.25f, 0.08f);
    EXPECT_TRUE(std::isfinite(first.heightOffset));
    EXPECT_TRUE(std::isfinite(later.heightOffset));
    EXPECT_NE(first.heightOffset, later.heightOffset);

    const glm::vec2 parityPosition(137.25f, -81.5f);
    constexpr float parityTime = 3.125f;
    WaterSpectrumConfig liveSpectrum = simulation.spectrumConfig();
    liveSpectrum.directionalSineScale = 0.0f;
    ASSERT_TRUE(simulation.reconfigure(liveSpectrum));
    const auto zeroSine = simulation.sampleSurface(
        parityPosition, parityTime, 1.0f);
    liveSpectrum.directionalSineScale = 1.5f;
    ASSERT_TRUE(simulation.reconfigure(liveSpectrum));
    const auto strongSine = simulation.sampleSurface(
        parityPosition, parityTime, 1.0f);
    EXPECT_GT(std::abs(strongSine.heightOffset - zeroSine.heightOffset),
              1.0e-5f);
    EXPECT_GT(glm::length(strongSine.velocity - zeroSine.velocity),
              1.0e-5f);

    const WaterSpectrumConfig beforeInvalid = simulation.spectrumConfig();
    WaterSpectrumConfig invalidSpectrum = beforeInvalid;
    invalidSpectrum.significantWaveHeight =
        std::numeric_limits<float>::quiet_NaN();
    invalidSpectrum.directionRadians =
        std::numeric_limits<float>::infinity();
    invalidSpectrum.choppiness =
        -std::numeric_limits<float>::infinity();
    invalidSpectrum.patchLengths.x =
        std::numeric_limits<float>::quiet_NaN();
    invalidSpectrum.cascadeAmplitudes.y =
        std::numeric_limits<float>::quiet_NaN();
    invalidSpectrum.directionalSineScale =
        std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(simulation.reconfigure(invalidSpectrum));
    const WaterSpectrumConfig afterInvalid = simulation.spectrumConfig();
    EXPECT_FLOAT_EQ(afterInvalid.significantWaveHeight,
                    beforeInvalid.significantWaveHeight);
    EXPECT_FLOAT_EQ(afterInvalid.directionRadians,
                    beforeInvalid.directionRadians);
    EXPECT_FLOAT_EQ(afterInvalid.choppiness,
                    beforeInvalid.choppiness);
    EXPECT_FLOAT_EQ(afterInvalid.patchLengths.x,
                    beforeInvalid.patchLengths.x);
    EXPECT_FLOAT_EQ(afterInvalid.cascadeAmplitudes.y,
                    beforeInvalid.cascadeAmplitudes.y);
    EXPECT_FLOAT_EQ(afterInvalid.directionalSineScale,
                    beforeInvalid.directionalSineScale);

    WaterSpectrumConfig rebuiltSpectrum = simulation.spectrumConfig();
    rebuiltSpectrum.significantWaveHeight += 0.75f;
    rebuiltSpectrum.directionRadians += 0.1f;
    WGPUTextureView outputBeforeRebuild = simulation.getOutputView();
    ASSERT_TRUE(simulation.reconfigure(rebuiltSpectrum));
    EXPECT_TRUE(simulation.isInitialized());
    EXPECT_EQ(simulation.getOutputView(), outputBeforeRebuild);
    EXPECT_FLOAT_EQ(simulation.spectrumConfig().significantWaveHeight,
                    rebuiltSpectrum.significantWaveHeight);
    EXPECT_FLOAT_EQ(simulation.spectrumConfig().directionRadians,
                    rebuiltSpectrum.directionRadians);
    const auto rebuiltSample = simulation.sampleSurface(
        parityPosition, parityTime, 1.0f);
    EXPECT_TRUE(std::isfinite(rebuiltSample.heightOffset));
    EXPECT_TRUE(std::isfinite(glm::length(rebuiltSample.slope)));
    EXPECT_TRUE(std::isfinite(glm::length(rebuiltSample.velocity)));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto invalidSample : {
             simulation.sampleSurface({nan, 0.0f}, 1.0f, 1.0f),
             simulation.sampleSurface({0.0f, nan}, 1.0f, 1.0f),
             simulation.sampleSurface({0.0f, 0.0f}, nan, 1.0f),
             simulation.sampleSurface({0.0f, 0.0f}, 1.0f, nan)}) {
        EXPECT_EQ(invalidSample.heightOffset, 0.0f);
        EXPECT_EQ(invalidSample.slope, glm::vec2(0.0f));
        EXPECT_EQ(invalidSample.velocity, glm::vec3(0.0f));
    }
    WGPUTextureView coastBeforeInvalid = simulation.getCoastView();
    EXPECT_FALSE(simulation.rebuildCoastField(
        terrain, terrainSize, terrainSize, 500.0f, 1.0f, nan));
    EXPECT_EQ(simulation.getCoastView(), coastBeforeInvalid);

    // Compile both consumers as part of the isolated ocean test. Full pipeline
    // binding is exercised by the native screenshot run; this catches WGSL
    // regressions without pulling unrelated application dependencies here.
    for (const char* shaderName : {
             "terrain_raycast.wgsl", "ray_blit.wgsl",
             "water_clipmap.wgsl"}) {
        const std::string source = readTextFile(shaderDirectory / shaderName);
        ASSERT_FALSE(source.empty()) << shaderName;
        WGPUShaderModule module = gpu::createShaderModule(
            context.getDevice(), source, shaderName);
        ASSERT_NE(module, nullptr) << shaderName;
        wgpuShaderModuleRelease(module);
    }

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDesc);
    ASSERT_NE(encoder, nullptr);
    simulation.update(encoder, nan);
    simulation.update(encoder, 1.25f);

    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    ASSERT_NE(command, nullptr);
    wgpuQueueSubmit(context.getQueue(), 1, &command);

    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    simulation.shutdown();
    context.shutdown();
}

} // namespace voxy::render
