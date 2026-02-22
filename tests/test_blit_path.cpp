// ═══════════════════════════════════════════════════════════════════════════════
// test_blit_path.cpp - Unit tests for BlitPath renderer
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the fullscreen blit/lighting pass renderer. These tests verify:
//   - BlitPath initialization and shutdown
//   - Input texture binding
//   - Camera uniform updates
//   - Render pipeline creation
//   - Fullscreen triangle rendering
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <filesystem>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "render/blit_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/context.hpp"
#include "gpu/resources.hpp"

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class BlitPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try to initialize GPU context for rendering tests
        gpuContextInitialized_ = gpuContext_.initHeadless();
        
        // Find shader path
        std::vector<std::filesystem::path> searchPaths = {
            "shaders/ray_blit.wgsl",
            "../shaders/ray_blit.wgsl",
            "../../shaders/ray_blit.wgsl",
            "../../../shaders/ray_blit.wgsl",
        };
        
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                shaderPath_ = path;
                break;
            }
        }
    }
    
    render::BlitPathConfig getConfig() {
        render::BlitPathConfig config = render::BlitPathConfig::defaults();
        if (!shaderPath_.empty()) {
            config.shaderPath = shaderPath_;
        }
        return config;
    }
    
    void TearDown() override {
        blitPath_.shutdown();
        
        // Release test textures
        if (depthView_) {
            wgpuTextureViewRelease(depthView_);
            depthView_ = nullptr;
        }
        if (depthTexture_) {
            wgpuTextureRelease(depthTexture_);
            depthTexture_ = nullptr;
        }
        if (terrainView_) {
            wgpuTextureViewRelease(terrainView_);
            terrainView_ = nullptr;
        }
        if (terrainTexture_) {
            wgpuTextureRelease(terrainTexture_);
            terrainTexture_ = nullptr;
        }
        if (lightmapView_) {
            wgpuTextureViewRelease(lightmapView_);
            lightmapView_ = nullptr;
        }
        if (lightmapTexture_) {
            wgpuTextureRelease(lightmapTexture_);
            lightmapTexture_ = nullptr;
        }
        if (colorView_) {
            wgpuTextureViewRelease(colorView_);
            colorView_ = nullptr;
        }
        if (colorTexture_) {
            wgpuTextureRelease(colorTexture_);
            colorTexture_ = nullptr;
        }
        
        if (gpuContextInitialized_) {
            gpuContext_.shutdown();
        }
    }
    
    bool createTestTextures() {
        if (!gpuContextInitialized_) return false;
        
        auto device = gpuContext_.getDevice();
        
        // Create depth texture (R32Float - from ray-caster)
        gpu::TextureDesc depthDesc = gpu::TextureDesc::tex2D(
            320, 240,
            WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
            "test_depth_texture"
        );
        depthTexture_ = gpu::createTexture(device, depthDesc);
        if (!depthTexture_) return false;
        
        gpu::TextureViewDesc depthViewDesc{};
        depthViewDesc.label = "test_depth_view";
        depthViewDesc.format = WGPUTextureFormat_R32Float;
        depthView_ = gpu::createTextureView(depthTexture_, depthViewDesc);
        if (!depthView_) return false;
        
        // Create terrain texture (RGBA8)
        gpu::TextureDesc terrainDesc = gpu::TextureDesc::tex2D(
            256, 256,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "test_terrain_texture"
        );
        terrainTexture_ = gpu::createTexture(device, terrainDesc);
        if (!terrainTexture_) return false;
        
        gpu::TextureViewDesc terrainViewDesc{};
        terrainViewDesc.label = "test_terrain_view";
        terrainViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        terrainView_ = gpu::createTextureView(terrainTexture_, terrainViewDesc);
        if (!terrainView_) return false;
        
        // Create lightmap texture (R8)
        gpu::TextureDesc lightmapDesc = gpu::TextureDesc::tex2D(
            256, 256,
            WGPUTextureFormat_R8Unorm,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "test_lightmap_texture"
        );
        lightmapTexture_ = gpu::createTexture(device, lightmapDesc);
        if (!lightmapTexture_) return false;
        
        gpu::TextureViewDesc lightmapViewDesc{};
        lightmapViewDesc.label = "test_lightmap_view";
        lightmapViewDesc.format = WGPUTextureFormat_R8Unorm;
        lightmapView_ = gpu::createTextureView(lightmapTexture_, lightmapViewDesc);
        if (!lightmapView_) return false;
        
        // Create color output texture (BGRA8 - swapchain format)
        gpu::TextureDesc colorDesc = gpu::TextureDesc::renderTarget(
            320, 240,
            WGPUTextureFormat_BGRA8Unorm,
            "test_color_output"
        );
        colorTexture_ = gpu::createTexture(device, colorDesc);
        if (!colorTexture_) return false;
        
        gpu::TextureViewDesc colorViewDesc{};
        colorViewDesc.label = "test_color_view";
        colorViewDesc.format = WGPUTextureFormat_BGRA8Unorm;
        colorView_ = gpu::createTextureView(colorTexture_, colorViewDesc);
        if (!colorView_) return false;
        
        return true;
    }
    
    gpu::Context gpuContext_;
    render::BlitPath blitPath_;
    bool gpuContextInitialized_ = false;
    std::filesystem::path shaderPath_;
    
    // Test textures
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    WGPUTexture terrainTexture_ = nullptr;
    WGPUTextureView terrainView_ = nullptr;
    WGPUTexture lightmapTexture_ = nullptr;
    WGPUTextureView lightmapView_ = nullptr;
    WGPUTexture colorTexture_ = nullptr;
    WGPUTextureView colorView_ = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, DefaultConfigValues) {
    auto config = render::BlitPathConfig::defaults();
    
    EXPECT_EQ(config.shaderPath, "shaders/ray_blit.wgsl");
    EXPECT_EQ(config.colorFormat, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_FLOAT_EQ(config.heightScale, 500.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
    EXPECT_FLOAT_EQ(config.fogDensity, 0.0001f);
}

TEST_F(BlitPathTest, InitWithNullDeviceFails) {
    EXPECT_FALSE(blitPath_.init(nullptr, nullptr));
}

TEST_F(BlitPathTest, NotInitializedByDefault) {
    EXPECT_FALSE(blitPath_.isInitialized());
}

TEST_F(BlitPathTest, InitializationSucceeds) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    EXPECT_TRUE(blitPath_.init(device, queue, getConfig()));
    EXPECT_TRUE(blitPath_.isInitialized());
}

TEST_F(BlitPathTest, ShutdownReleasesResources) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    blitPath_.shutdown();
    
    EXPECT_FALSE(blitPath_.isInitialized());
}

TEST_F(BlitPathTest, DoubleInitFails) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    // Second init should fail (already initialized)
    EXPECT_FALSE(blitPath_.init(device, queue, getConfig()));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resource Accessors Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, UniformBufferCreated) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    EXPECT_NE(blitPath_.getUniformBuffer(), nullptr);
}

TEST_F(BlitPathTest, SamplerCreated) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    EXPECT_NE(blitPath_.getSampler(), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Camera Uniforms Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, CameraUpdateStoresValues) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    glm::mat4 view = glm::lookAt(
        glm::vec3(100.0f, 200.0f, 100.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 10000.0f);
    glm::vec3 cameraPos(100.0f, 200.0f, 100.0f);
    
    blitPath_.updateCamera(view, proj, cameraPos);
    
    const auto& uniforms = blitPath_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.cameraPos.x, 100.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.y, 200.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.z, 100.0f);
}

TEST_F(BlitPathTest, TerrainSizeUpdatesUniforms) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    blitPath_.setTerrainSize(512, 512);
    
    const auto& uniforms = blitPath_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 512.0f);
    EXPECT_FLOAT_EQ(uniforms.terrainSize.y, 512.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Input Binding Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, CanSetInputTextures) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    ASSERT_TRUE(createTestTextures());
    
    // These should not throw/crash
    blitPath_.setDepthTexture(depthView_);
    blitPath_.setTerrainTexture(terrainView_);
    blitPath_.setLightmapTexture(lightmapView_);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rendering Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, RenderWithoutTexturesFails) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    ASSERT_TRUE(createTestTextures());
    
    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.label = "test_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    ASSERT_NE(encoder, nullptr);
    
    // Render without textures should not crash (will warn and return early)
    blitPath_.render(encoder, colorView_);
    
    wgpuCommandEncoderRelease(encoder);
}

TEST_F(BlitPathTest, RenderWithAllTexturesSucceeds) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    ASSERT_TRUE(createTestTextures());
    
    // Set all required textures
    blitPath_.setDepthTexture(depthView_);
    blitPath_.setTerrainTexture(terrainView_);
    blitPath_.setLightmapTexture(lightmapView_);
    blitPath_.setTerrainSize(256, 256);
    
    // Update camera
    glm::mat4 view = glm::lookAt(
        glm::vec3(100.0f, 200.0f, 100.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 320.0f / 240.0f, 0.1f, 10000.0f);
    blitPath_.updateCamera(view, proj, glm::vec3(100.0f, 200.0f, 100.0f));
    
    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.label = "test_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    ASSERT_NE(encoder, nullptr);
    
    // Render should succeed
    blitPath_.render(encoder, colorView_);
    
    // Submit commands
    WGPUCommandBufferDescriptor cmdBufDesc{};
    cmdBufDesc.label = "test_command_buffer";
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufDesc);
    ASSERT_NE(cmdBuffer, nullptr);
    
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Move Semantics Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, MoveConstructor) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    render::BlitPath moved(std::move(blitPath_));
    
    EXPECT_FALSE(blitPath_.isInitialized());
    EXPECT_TRUE(moved.isInitialized());
    
    moved.shutdown();
}

TEST_F(BlitPathTest, MoveAssignment) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(blitPath_.init(device, queue, getConfig()));
    
    render::BlitPath other;
    other = std::move(blitPath_);
    
    EXPECT_FALSE(blitPath_.isInitialized());
    EXPECT_TRUE(other.isInitialized());
    
    other.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Custom Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, CustomConfig) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    render::BlitPathConfig config = getConfig();
    config.heightScale = 1000.0f;
    config.cellScale = 2.0f;
    config.fogDensity = 0.0002f;
    
    EXPECT_TRUE(blitPath_.init(device, queue, config));
    
    const auto& uniforms = blitPath_.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.metrics.x, 1000.0f);  // heightScale
    EXPECT_FLOAT_EQ(uniforms.metrics.y, 2.0f);     // cellScale
    EXPECT_FLOAT_EQ(uniforms.metrics.w, 0.0002f);  // fogDensity
}

} // namespace voxy

