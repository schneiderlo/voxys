// ═══════════════════════════════════════════════════════════════════════════════
// test_raycast_path.cpp - Unit tests for Raycast Path Renderer
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for RaycastPath renderer class. These tests verify the interface and
// configuration without requiring actual GPU access for most tests.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "render/raycast_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "terrain/heightmap.hpp"
#include "render/day_night.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
using WGPUSubmissionIndex = uint64_t;
struct WGPUWrappedSubmissionIndex {
    WGPUQueue queue;
    WGPUSubmissionIndex submissionIndex;
};
#endif

extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(
    WGPUQueue queue, size_t commandCount,
    const WGPUCommandBuffer* commands);
extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// RaycastPathConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RaycastPathConfigTest, DefaultValues) {
    auto config = RaycastPathConfig::defaults();
    
    EXPECT_EQ(config.shaderPath, "shaders/terrain_raycast.wgsl");
    EXPECT_FLOAT_EQ(config.heightScale, 500.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
    EXPECT_FLOAT_EQ(config.fogDensity, 0.0001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RaycastPath Constants Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RaycastPathConstantsTest, WorkgroupSize) {
    // Workgroup size should match shader (8x8)
    EXPECT_EQ(RaycastPath::WORKGROUP_SIZE_X, 8u);
    EXPECT_EQ(RaycastPath::WORKGROUP_SIZE_Y, 8u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RaycastPath Class Tests (No GPU)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RaycastPathTest, DefaultConstruction) {
    RaycastPath renderer;
    
    EXPECT_FALSE(renderer.isInitialized());
    EXPECT_EQ(renderer.getUniformBuffer(), nullptr);
    EXPECT_EQ(renderer.getDepthOutputTexture(), nullptr);
    EXPECT_EQ(renderer.getDepthOutputView(), nullptr);
    EXPECT_EQ(renderer.getShadowOutputTexture(), nullptr);
    EXPECT_EQ(renderer.getShadowOutputView(), nullptr);
    EXPECT_EQ(renderer.getMaterialOutputTexture(), nullptr);
    EXPECT_EQ(renderer.getMaterialOutputView(), nullptr);
    EXPECT_EQ(renderer.getOutputWidth(), 0u);
    EXPECT_EQ(renderer.getOutputHeight(), 0u);
}

TEST(RaycastPathTest, MoveConstruction) {
    RaycastPath renderer1;
    RaycastPath renderer2(std::move(renderer1));
    
    // Both should be uninitialized (no GPU resources)
    EXPECT_FALSE(renderer1.isInitialized());
    EXPECT_FALSE(renderer2.isInitialized());
}

TEST(RaycastPathTest, MoveAssignment) {
    RaycastPath renderer1;
    RaycastPath renderer2;
    
    renderer2 = std::move(renderer1);
    
    EXPECT_FALSE(renderer1.isInitialized());
    EXPECT_FALSE(renderer2.isInitialized());
}

TEST(RaycastPathTest, InitWithNullDevice) {
    RaycastPath renderer;
    
    // Should fail gracefully with null device
    EXPECT_FALSE(renderer.init(nullptr, nullptr, 1920, 1080));
    EXPECT_FALSE(renderer.isInitialized());
}

TEST(RaycastPathTest, InitWithZeroDimensions) {
    RaycastPath renderer;
    
    // Should fail with zero dimensions
    // Note: This requires a valid device to test properly, but we can
    // document expected behavior
    EXPECT_FALSE(renderer.init(nullptr, nullptr, 0, 0));
    EXPECT_FALSE(renderer.isInitialized());
}

TEST(RaycastPathTest, ShutdownWithoutInit) {
    RaycastPath renderer;
    
    // Should not crash
    renderer.shutdown();
    EXPECT_FALSE(renderer.isInitialized());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Workgroup Count Calculation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WorkgroupCountTest, ExactDivision) {
    // For 1920x1080 output with 8x8 workgroups:
    // workgroupsX = (1920 + 7) / 8 = 240
    // workgroupsY = (1080 + 7) / 8 = 135
    
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t workgroupsX = (width + 8 - 1) / 8;
    uint32_t workgroupsY = (height + 8 - 1) / 8;
    
    EXPECT_EQ(workgroupsX, 240u);
    EXPECT_EQ(workgroupsY, 135u);
}

TEST(WorkgroupCountTest, NonDivisible) {
    // For 1921x1081 output with 8x8 workgroups:
    // workgroupsX = (1921 + 7) / 8 = 241
    // workgroupsY = (1081 + 7) / 8 = 136
    
    uint32_t width = 1921;
    uint32_t height = 1081;
    uint32_t workgroupsX = (width + 8 - 1) / 8;
    uint32_t workgroupsY = (height + 8 - 1) / 8;
    
    EXPECT_EQ(workgroupsX, 241u);
    EXPECT_EQ(workgroupsY, 136u);
}

TEST(WorkgroupCountTest, SmallOutput) {
    // For 8x8 output:
    // workgroupsX = 1
    // workgroupsY = 1
    
    uint32_t width = 8;
    uint32_t height = 8;
    uint32_t workgroupsX = (width + 8 - 1) / 8;
    uint32_t workgroupsY = (height + 8 - 1) / 8;
    
    EXPECT_EQ(workgroupsX, 1u);
    EXPECT_EQ(workgroupsY, 1u);
}

TEST(WorkgroupCountTest, VerySmallOutput) {
    // For 1x1 output:
    // workgroupsX = 1
    // workgroupsY = 1
    
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t workgroupsX = (width + 8 - 1) / 8;
    uint32_t workgroupsY = (height + 8 - 1) / 8;
    
    EXPECT_EQ(workgroupsX, 1u);
    EXPECT_EQ(workgroupsY, 1u);
}

TEST(WorkgroupCountTest, LargeOutput4K) {
    // For 4K (3840x2160) output:
    // workgroupsX = (3840 + 7) / 8 = 480
    // workgroupsY = (2160 + 7) / 8 = 270
    
    uint32_t width = 3840;
    uint32_t height = 2160;
    uint32_t workgroupsX = (width + 8 - 1) / 8;
    uint32_t workgroupsY = (height + 8 - 1) / 8;
    
    EXPECT_EQ(workgroupsX, 480u);
    EXPECT_EQ(workgroupsY, 270u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Integration Tests (requires WebGPU context)
// ═══════════════════════════════════════════════════════════════════════════════

class RaycastPathGPUTest : public ::testing::Test {
protected:
    void SetUp() override {
        gpuContextInitialized_ = gpuContext_.initHeadless();
        
        // Find the shader file - check multiple possible locations
        std::vector<std::filesystem::path> searchPaths = {
            "shaders/terrain_raycast.wgsl",
            "../shaders/terrain_raycast.wgsl",
            "../../shaders/terrain_raycast.wgsl",
            "../../../shaders/terrain_raycast.wgsl",
        };
        
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                shaderPath_ = path;
                break;
            }
        }
    }
    
    void TearDown() override {
        renderer_.shutdown();
        if (heightmapTexture_) {
            if (heightmapView_) wgpuTextureViewRelease(heightmapView_);
            wgpuTextureRelease(heightmapTexture_);
            heightmapTexture_ = nullptr;
            heightmapView_ = nullptr;
        }
        if (gpuContextInitialized_) {
            gpuContext_.shutdown();
        }
    }
    
    // Get config with correct shader path
    RaycastPathConfig getConfig() {
        auto config = RaycastPathConfig::defaults();
        if (!shaderPath_.empty()) {
            config.shaderPath = shaderPath_;
        }
        return config;
    }
    
    // Create a dummy heightmap texture for testing
    void createDummyHeightmap(uint32_t width, uint32_t height) {
        if (!gpuContextInitialized_) return;
        
        auto device = gpuContext_.getDevice();
        
        // Create heightmap texture with mip levels
        gpu::TextureDesc texDesc = gpu::TextureDesc::tex2DMipmapped(
            width, height,
            WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "test_heightmap"
        );
        
        heightmapTexture_ = gpu::createTexture(device, texDesc);
        if (heightmapTexture_) {
            gpu::TextureViewDesc viewDesc{};
            viewDesc.label = "test_heightmap_view";
            viewDesc.format = WGPUTextureFormat_R16Uint;
            viewDesc.mipLevelCount = texDesc.mipLevelCount;
            heightmapView_ = gpu::createTextureView(heightmapTexture_, viewDesc);
        }
        
        heightmapWidth_ = width;
        heightmapHeight_ = height;
    }
    
    gpu::Context gpuContext_;
    bool gpuContextInitialized_ = false;
    std::filesystem::path shaderPath_;
    RaycastPath renderer_;
    WGPUTexture heightmapTexture_ = nullptr;
    WGPUTextureView heightmapView_ = nullptr;
    uint32_t heightmapWidth_ = 0;
    uint32_t heightmapHeight_ = 0;
};

TEST_F(RaycastPathGPUTest, InitAndShutdown) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    EXPECT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    EXPECT_TRUE(renderer_.isInitialized());
    EXPECT_NE(renderer_.getUniformBuffer(), nullptr);
    EXPECT_NE(renderer_.getDepthOutputTexture(), nullptr);
    EXPECT_NE(renderer_.getDepthOutputView(), nullptr);
    EXPECT_NE(renderer_.getShadowOutputTexture(), nullptr);
    EXPECT_NE(renderer_.getShadowOutputView(), nullptr);
    EXPECT_NE(renderer_.getMaterialOutputTexture(), nullptr);
    EXPECT_NE(renderer_.getMaterialOutputView(), nullptr);
    EXPECT_EQ(renderer_.getOutputWidth(), 1920u);
    EXPECT_EQ(renderer_.getOutputHeight(), 1080u);
    
    renderer_.shutdown();
    
    EXPECT_FALSE(renderer_.isInitialized());
    EXPECT_EQ(renderer_.getDepthOutputTexture(), nullptr);
    EXPECT_EQ(renderer_.getShadowOutputTexture(), nullptr);
    EXPECT_EQ(renderer_.getMaterialOutputTexture(), nullptr);
}

TEST_F(RaycastPathGPUTest, WorkgroupCounts) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    // For 1920x1080 with 8x8 workgroups:
    EXPECT_EQ(renderer_.getWorkgroupCountX(), 240u);
    EXPECT_EQ(renderer_.getWorkgroupCountY(), 135u);
}

TEST_F(RaycastPathGPUTest, Resize) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    EXPECT_EQ(renderer_.getOutputWidth(), 1920u);
    EXPECT_EQ(renderer_.getOutputHeight(), 1080u);
    
    // Resize
    EXPECT_TRUE(renderer_.resize(1280, 720));
    
    EXPECT_EQ(renderer_.getOutputWidth(), 1280u);
    EXPECT_EQ(renderer_.getOutputHeight(), 720u);
    EXPECT_NE(renderer_.getDepthOutputView(), nullptr);
    EXPECT_NE(renderer_.getShadowOutputView(), nullptr);
    EXPECT_NE(renderer_.getMaterialOutputView(), nullptr);
}

TEST_F(RaycastPathGPUTest, ResizeToSameSize) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    auto view = renderer_.getDepthOutputView();
    auto shadowView = renderer_.getShadowOutputView();
    auto materialView = renderer_.getMaterialOutputView();
    
    // Resize to same size should be a no-op
    EXPECT_TRUE(renderer_.resize(1920, 1080));
    
    // View should be the same (not recreated)
    EXPECT_EQ(renderer_.getDepthOutputView(), view);
    EXPECT_EQ(renderer_.getShadowOutputView(), shadowView);
    EXPECT_EQ(renderer_.getMaterialOutputView(), materialView);
}

TEST_F(RaycastPathGPUTest, ResizeToZeroFails) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    EXPECT_FALSE(renderer_.resize(0, 0));
    
    // Original size should be preserved
    EXPECT_EQ(renderer_.getOutputWidth(), 1920u);
    EXPECT_EQ(renderer_.getOutputHeight(), 1080u);
}

TEST_F(RaycastPathGPUTest, SetHeightmap) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    createDummyHeightmap(256, 256);
    ASSERT_NE(heightmapView_, nullptr);
    
    // This should not crash
    ASSERT_TRUE(renderer_.setHeightmap(
        heightmapView_, heightmapWidth_, heightmapHeight_));
    
    // Uniforms should be updated with terrain size
    const auto& uniforms = renderer_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 256.0f);
    EXPECT_FLOAT_EQ(uniforms.terrainSize.y, 256.0f);

    EXPECT_FALSE(renderer_.setHeightmap(nullptr, 256, 256));
    EXPECT_FALSE(renderer_.setHeightmap(heightmapView_, 0, 256));
    EXPECT_FALSE(renderer_.setHeightmap(heightmapView_, 8'193, 256));
    EXPECT_FLOAT_EQ(renderer_.getUniforms().terrainSize.x, 256.0f);
    EXPECT_FLOAT_EQ(renderer_.getUniforms().terrainSize.y, 256.0f);
}

TEST_F(RaycastPathGPUTest, UpdateCamera) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        1920, 1080,
        getConfig()
    ));
    
    glm::vec3 position(100.0f, 200.0f, 300.0f);
    glm::mat4 view = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 10000.0f);
    
    renderer_.updateCamera(view, proj, position);
    
    const auto& uniforms = renderer_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.cameraPos.x, 100.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.y, 200.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.z, 300.0f);
}

TEST_F(RaycastPathGPUTest, DispatchWithHeightmap) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(shaderPath_.empty()) << "Shader file terrain_raycast.wgsl not found";
    
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), 
        gpuContext_.getQueue(),
        64, 64,  // Small output for test
        getConfig()
    ));
    
    createDummyHeightmap(256, 256);
    ASSERT_NE(heightmapView_, nullptr);
    
    ASSERT_TRUE(renderer_.setHeightmap(
        heightmapView_, heightmapWidth_, heightmapHeight_));
    
    // Create command encoder and dispatch
    WGPUCommandEncoderDescriptor encDesc{};
    encDesc.label = "test_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_.getDevice(), &encDesc);
    ASSERT_NE(encoder, nullptr);
    
    // This should not crash
    renderer_.dispatch(encoder);
    
    // Submit commands
    WGPUCommandBufferDescriptor bufDesc{};
    bufDesc.label = "test_command_buffer";
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &bufDesc);
    ASSERT_NE(commands, nullptr);
    
    wgpuQueueSubmit(gpuContext_.getQueue(), 1, &commands);
    
    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

TEST_F(RaycastPathGPUTest, LegoHorizonBenchmark) {
    constexpr uint32_t benchmarkWidth = 256u;
    constexpr uint32_t benchmarkHeight = 144u;
    const char* benchmarkShader =
        std::getenv("VOXY_RAYCAST_BENCHMARK_SHADER");
    if (!benchmarkShader || *benchmarkShader == '\0') {
        GTEST_SKIP() << "Set VOXY_RAYCAST_BENCHMARK_SHADER to run";
    }
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    auto config = RaycastPathConfig::defaults();
    config.shaderPath = benchmarkShader;
    ASSERT_TRUE(renderer_.init(
        gpuContext_.getDevice(), gpuContext_.getQueue(),
        benchmarkWidth, benchmarkHeight, config));
    createDummyHeightmap(256, 256);
    ASSERT_NE(heightmapView_, nullptr);
    ASSERT_TRUE(renderer_.setHeightmap(
        heightmapView_, heightmapWidth_, heightmapHeight_));
    gpu::TextureDesc displacementDesc = gpu::TextureDesc::tex2D(
        1, 1, WGPUTextureFormat_RGBA16Float,
        WGPUTextureUsage_TextureBinding, "benchmark_water_displacement");
    displacementDesc.depthOrArrayLayers = 4u;
    WGPUTexture displacementTexture = gpu::createTexture(
        gpuContext_.getDevice(), displacementDesc);
    gpu::TextureViewDesc displacementViewDesc{};
    displacementViewDesc.dimension = WGPUTextureViewDimension_2DArray;
    displacementViewDesc.arrayLayerCount = 4u;
    WGPUTextureView displacementView = gpu::createTextureView(
        displacementTexture, displacementViewDesc);
    WGPUTexture coastTexture = gpu::createTexture(
        gpuContext_.getDevice(), gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_RGBA16Float,
            WGPUTextureUsage_TextureBinding, "benchmark_water_coast"));
    WGPUTextureView coastView = gpu::createTextureView(coastTexture);
    WGPUSampler sampler = gpu::createSampler(
        gpuContext_.getDevice(), gpu::SamplerDesc::linear(
            "benchmark_water_sampler"));
    ASSERT_NE(displacementTexture, nullptr);
    ASSERT_NE(displacementView, nullptr);
    ASSERT_NE(coastTexture, nullptr);
    ASSERT_NE(coastView, nullptr);
    ASSERT_NE(sampler, nullptr);
    renderer_.setWaterSimulation(displacementView, coastView, sampler);
    const glm::vec3 cameraPosition(0.0f, -450.0f, -120.0f);
    renderer_.updateCamera(
        glm::lookAt(cameraPosition, glm::vec3(0.0f, -500.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::perspective(glm::radians(70.0f), 16.0f / 9.0f,
                         0.1f, 2'000.0f),
        cameraPosition);
    renderer_.setLegoMode(true);

    constexpr uint32_t warmupFrames = 5u;
    constexpr uint32_t measuredFrames = 60u;
    std::vector<double> samples;
    samples.reserve(measuredFrames);
    for (uint32_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            gpuContext_.getDevice(), &encoderDesc);
        renderer_.dispatch(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
            gpuContext_.getQueue(), 1u, &command);
        const WGPUWrappedSubmissionIndex submission{
            gpuContext_.getQueue(), submissionIndex};
        static_cast<void>(wgpuDevicePoll(
            gpuContext_.getDevice(), true, &submission));
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        if (frame >= warmupFrames) {
            samples.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
    }
    std::sort(samples.begin(), samples.end());
    const double p50 = samples[samples.size() / 2u];
    const double p95 = samples[static_cast<size_t>(
        std::ceil(0.95 * static_cast<double>(samples.size()))) - 1u];
    std::cout << std::fixed << std::setprecision(3)
              << "raycast_lego_horizon resolution="
              << benchmarkWidth << 'x' << benchmarkHeight
              << " retired_p50_ms=" << p50
              << " retired_p95_ms=" << p95 << '\n';
    renderer_.shutdown();
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(coastView);
    wgpuTextureRelease(coastTexture);
    wgpuTextureViewRelease(displacementView);
    wgpuTextureRelease(displacementTexture);
}

// Opt-in: measures retired CPU+GPU terrain work, not whole-game frame time.
// Use the shipped free-build heightmap to include long near-horizon traversals.
TEST_F(RaycastPathGPUTest, DayNightTerrainBenchmark) {
    const char* mapPath = std::getenv("VOXY_DAY_NIGHT_BENCHMARK_HEIGHTMAP");
    if (!mapPath || !*mapPath) GTEST_SKIP() << "Set VOXY_DAY_NIGHT_BENCHMARK_HEIGHTMAP to run";
    ASSERT_TRUE(gpuContextInitialized_);
    terrain::Heightmap map;
    ASSERT_TRUE(map.loadLdh(mapPath));
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    ASSERT_TRUE(map.uploadToGPUWithMips(device, queue, true, "shaders/mip_generate.wgsl"));
    auto config = getConfig(); config.heightScale = 600.0f;
    ASSERT_TRUE(renderer_.init(device, queue, 1280, 720, config));
    ASSERT_TRUE(renderer_.setHeightmap(map.getTextureView(), map.getWidth(), map.getHeight()));
    struct WaterBindings {
        WGPUTexture waves = nullptr, coast = nullptr;
        WGPUTextureView waveView = nullptr, coastView = nullptr;
        WGPUSampler sampler = nullptr;
        ~WaterBindings() {
            if (sampler) wgpuSamplerRelease(sampler);
            if (waveView) wgpuTextureViewRelease(waveView);
            if (coastView) wgpuTextureViewRelease(coastView);
            if (waves) wgpuTextureRelease(waves);
            if (coast) wgpuTextureRelease(coast);
        }
    } water;
    auto textureDesc = gpu::TextureDesc::tex2D(1, 1, WGPUTextureFormat_RGBA16Float,
        WGPUTextureUsage_TextureBinding, "benchmark_neutral_water");
    textureDesc.depthOrArrayLayers = 4;
    water.waves = gpu::createTexture(device, textureDesc);
    gpu::TextureViewDesc viewDesc{};
    viewDesc.dimension = WGPUTextureViewDimension_2DArray; viewDesc.arrayLayerCount = 4;
    water.waveView = gpu::createTextureView(water.waves, viewDesc);
    textureDesc.depthOrArrayLayers = 1;
    water.coast = gpu::createTexture(device, textureDesc);
    water.coastView = gpu::createTextureView(water.coast);
    water.sampler = gpu::createSampler(device, gpu::SamplerDesc::linear("benchmark_water"));
    ASSERT_NE(water.waveView, nullptr); ASSERT_NE(water.coastView, nullptr); ASSERT_NE(water.sampler, nullptr);
    renderer_.setWaterSimulation(water.waveView, water.coastView, water.sampler);
    auto uniforms = renderer_.getUniforms();
    uniforms.invProjParams.z = 2.0f; // Physical LEGO terrain, cached production path.
    uniforms.waterParams.y = 0.0f;
    const auto projection = glm::perspective(glm::radians(78.f), 1280.f / 720.f, .1f, 10000.f);
    const float ground = (float(map.sample(map.getWidth() / 2, map.getHeight() / 2)) / 65535.f * 2.f - 1.f) * 600.f;
    const glm::vec3 origin(0, ground + 4.f, 0);
    for (float hour : {12.0f, 17.5f}) {
        for (bool moving : {false, true}) {
            for (bool cycling : {false, true}) {
                std::vector<double> samples;
                uint32_t refreshed = 0;
                for (uint32_t frame = 0; frame < 120; ++frame) {
                    // A 60 Hz session refreshes clock-driven light every six frames.
                    const double sampleHour = double(hour) + (cycling ? double(frame / 6) * .1 / 60.0 : 0.0);
                    uniforms.lightDirWS = glm::vec4(sampleDayNight(sampleHour).lightDirection, cycling ? 1.f : 0.f);
                    const glm::vec3 eye = origin + glm::vec3(moving ? float(frame) * .01f : 0.f, 0, 0);
                    ASSERT_TRUE(uniforms.setCamera(glm::lookAt(eye, eye + glm::vec3(.2f, -.15f, 1.f), {0, 1, 0}), projection, eye));
                    renderer_.setCameraUniforms(uniforms);
                    const auto start = std::chrono::steady_clock::now();
                    auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
                    renderer_.dispatch(encoder, nullptr, WGPU_QUERY_SET_INDEX_UNDEFINED, WGPU_QUERY_SET_INDEX_UNDEFINED, true);
                    auto command = wgpuCommandEncoderFinish(encoder, nullptr);
                    const auto index = wgpuQueueSubmitForIndex(queue, 1, &command);
                    const WGPUWrappedSubmissionIndex submission{queue, index};
                    static_cast<void>(wgpuDevicePoll(device, true, &submission));
                    wgpuCommandBufferRelease(command); wgpuCommandEncoderRelease(encoder);
                    if (frame >= 24) {
                        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                        refreshed += renderer_.didRefreshStaticCache() ? 1u : 0u;
                    }
                }
                double mean = 0; for (double sample : samples) mean += sample / double(samples.size());
                std::sort(samples.begin(), samples.end());
                EXPECT_EQ(refreshed, moving ? 96u : cycling ? 16u : 0u);
                std::cout << std::fixed << std::setprecision(3)
                    << "day_night_terrain hour=" << hour << " moving=" << moving << " cycle=" << cycling
                    << " retired_mean_ms=" << mean << " p50_ms=" << samples[samples.size()/2]
                    << " p95_ms=" << samples[static_cast<size_t>(std::ceil(.95*double(samples.size())))-1]
                    << " refreshes=" << refreshed << '/' << samples.size() << '\n';
            }
        }
    }
    renderer_.shutdown();
}

} // namespace voxy::render
