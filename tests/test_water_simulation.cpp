#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/water_simulation.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

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
            std::filesystem::exists(candidate / "water_foam.wgsl")) {
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
    EXPECT_EQ(WaterSimulation::CASCADE_COUNT, 3u);
    EXPECT_EQ(WaterSimulation::RESOLUTION & (WaterSimulation::RESOLUTION - 1u), 0u);
}

TEST(WaterSimulationTest, DefaultConstructionOwnsNoGPUResources) {
    WaterSimulation simulation;
    EXPECT_FALSE(simulation.isInitialized());
    EXPECT_EQ(simulation.getOutputView(), nullptr);
    EXPECT_EQ(simulation.getFoamView(), nullptr);
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
    ASSERT_TRUE(simulation.init(context.getDevice(), context.getQueue(), shaderDirectory));
    ASSERT_TRUE(simulation.isInitialized());
    ASSERT_NE(simulation.getOutputView(), nullptr);
    ASSERT_NE(simulation.getFoamView(), nullptr);

    // Compile both consumers as part of the isolated ocean test. Full pipeline
    // binding is exercised by the native screenshot run; this catches WGSL
    // regressions without pulling unrelated application dependencies here.
    for (const char* shaderName : {"terrain_raycast.wgsl", "ray_blit.wgsl"}) {
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
