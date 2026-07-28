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

#include <glm/geometric.hpp>

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
