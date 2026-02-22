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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

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
    EXPECT_EQ(renderer_.getOutputWidth(), 1920u);
    EXPECT_EQ(renderer_.getOutputHeight(), 1080u);
    
    renderer_.shutdown();
    
    EXPECT_FALSE(renderer_.isInitialized());
    EXPECT_EQ(renderer_.getDepthOutputTexture(), nullptr);
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
    
    // Resize to same size should be a no-op
    EXPECT_TRUE(renderer_.resize(1920, 1080));
    
    // View should be the same (not recreated)
    EXPECT_EQ(renderer_.getDepthOutputView(), view);
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
    renderer_.setHeightmap(heightmapView_, heightmapWidth_, heightmapHeight_);
    
    // Uniforms should be updated with terrain size
    const auto& uniforms = renderer_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 256.0f);
    EXPECT_FLOAT_EQ(uniforms.terrainSize.y, 256.0f);
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
    
    renderer_.setHeightmap(heightmapView_, heightmapWidth_, heightmapHeight_);
    
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

} // namespace voxy::render

