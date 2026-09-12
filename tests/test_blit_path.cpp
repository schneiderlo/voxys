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
#include <cstdint>
#include <filesystem>
#include <limits>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <chrono>
#include <thread>
#include <glm/gtc/packing.hpp>
#include "render/mesh_path.hpp"
#include "render/inspection_guides.hpp"
#include "render/primitive_path.hpp"

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
        if (shadowView_) {
            wgpuTextureViewRelease(shadowView_);
            shadowView_ = nullptr;
        }
        if (shadowTexture_) {
            wgpuTextureRelease(shadowTexture_);
            shadowTexture_ = nullptr;
        }
        if (materialView_) {
            wgpuTextureViewRelease(materialView_);
            materialView_ = nullptr;
        }
        if (materialTexture_) {
            wgpuTextureRelease(materialTexture_);
            materialTexture_ = nullptr;
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
        if (terrainMaterialAlbedoView_) {
            wgpuTextureViewRelease(terrainMaterialAlbedoView_);
            terrainMaterialAlbedoView_ = nullptr;
        }
        if (terrainMaterialAlbedoTexture_) {
            wgpuTextureRelease(terrainMaterialAlbedoTexture_);
            terrainMaterialAlbedoTexture_ = nullptr;
        }
        if (terrainMaterialNormalRoughnessView_) {
            wgpuTextureViewRelease(terrainMaterialNormalRoughnessView_);
            terrainMaterialNormalRoughnessView_ = nullptr;
        }
        if (terrainMaterialNormalRoughnessTexture_) {
            wgpuTextureRelease(terrainMaterialNormalRoughnessTexture_);
            terrainMaterialNormalRoughnessTexture_ = nullptr;
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
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
            "test_depth_texture"
        );
        depthTexture_ = gpu::createTexture(device, depthDesc);
        if (!depthTexture_) return false;
        
        gpu::TextureViewDesc depthViewDesc{};
        depthViewDesc.label = "test_depth_view";
        depthViewDesc.format = WGPUTextureFormat_R32Float;
        depthView_ = gpu::createTextureView(depthTexture_, depthViewDesc);
        if (!depthView_) return false;

        // Create shadow texture (R32Float - from ray-caster)
        gpu::TextureDesc shadowDesc = gpu::TextureDesc::tex2D(
            320, 240,
            WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
            "test_shadow_texture"
        );
        shadowTexture_ = gpu::createTexture(device, shadowDesc);
        if (!shadowTexture_) return false;

        gpu::TextureViewDesc shadowViewDesc{};
        shadowViewDesc.label = "test_shadow_view";
        shadowViewDesc.format = WGPUTextureFormat_R32Float;
        shadowView_ = gpu::createTextureView(shadowTexture_, shadowViewDesc);
        if (!shadowView_) return false;

        // Create water-attribute texture (RGBA16Float - from ray-caster)
        gpu::TextureDesc materialDesc = gpu::TextureDesc::tex2D(
            320, 240,
            WGPUTextureFormat_RGBA16Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
            "test_material_texture"
        );
        materialTexture_ = gpu::createTexture(device, materialDesc);
        if (!materialTexture_) return false;

        gpu::TextureViewDesc materialViewDesc{};
        materialViewDesc.label = "test_material_view";
        materialViewDesc.format = WGPUTextureFormat_RGBA16Float;
        materialView_ = gpu::createTextureView(materialTexture_, materialViewDesc);
        if (!materialView_) return false;
        
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

        gpu::TextureDesc terrainMaterialDesc =
            gpu::TextureDesc::tex2D(
                16, 16, WGPUTextureFormat_RGBA8UnormSrgb,
                WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                "test_terrain_material_albedo");
        terrainMaterialDesc.depthOrArrayLayers = 4u;
        terrainMaterialAlbedoTexture_ =
            gpu::createTexture(device, terrainMaterialDesc);
        if (!terrainMaterialAlbedoTexture_) return false;
        terrainMaterialDesc.label = "test_terrain_material_normal_roughness";
        terrainMaterialDesc.format = WGPUTextureFormat_RGBA8Unorm;
        terrainMaterialNormalRoughnessTexture_ =
            gpu::createTexture(device, terrainMaterialDesc);
        if (!terrainMaterialNormalRoughnessTexture_) return false;

        gpu::TextureViewDesc terrainMaterialViewDesc{};
        terrainMaterialViewDesc.dimension =
            WGPUTextureViewDimension_2DArray;
        terrainMaterialViewDesc.arrayLayerCount = 4u;
        terrainMaterialViewDesc.format = WGPUTextureFormat_RGBA8UnormSrgb;
        terrainMaterialViewDesc.label =
            "test_terrain_material_albedo_view";
        terrainMaterialAlbedoView_ = gpu::createTextureView(
            terrainMaterialAlbedoTexture_, terrainMaterialViewDesc);
        if (!terrainMaterialAlbedoView_) return false;
        terrainMaterialViewDesc.label =
            "test_terrain_material_normal_roughness_view";
        terrainMaterialViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        terrainMaterialNormalRoughnessView_ = gpu::createTextureView(
            terrainMaterialNormalRoughnessTexture_,
            terrainMaterialViewDesc);
        if (!terrainMaterialNormalRoughnessView_) return false;
        
        // Create color output texture (BGRA8 - swapchain format)
        gpu::TextureDesc colorDesc = gpu::TextureDesc::renderTarget(
            320, 240,
            WGPUTextureFormat_BGRA8Unorm,
            "test_color_output"
        );
        colorDesc.usage |= WGPUTextureUsage_CopySrc;
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
    WGPUTexture shadowTexture_ = nullptr;
    WGPUTextureView shadowView_ = nullptr;
    WGPUTexture materialTexture_ = nullptr;
    WGPUTextureView materialView_ = nullptr;
    WGPUTexture terrainTexture_ = nullptr;
    WGPUTextureView terrainView_ = nullptr;
    WGPUTexture lightmapTexture_ = nullptr;
    WGPUTextureView lightmapView_ = nullptr;
    WGPUTexture terrainMaterialAlbedoTexture_ = nullptr;
    WGPUTextureView terrainMaterialAlbedoView_ = nullptr;
    WGPUTexture terrainMaterialNormalRoughnessTexture_ = nullptr;
    WGPUTextureView terrainMaterialNormalRoughnessView_ = nullptr;
    WGPUTexture colorTexture_ = nullptr;
    WGPUTextureView colorView_ = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitPathTest, DefaultConfigValues) {
    auto config = render::BlitPathConfig::defaults();
    
    EXPECT_EQ(config.shaderPath, "shaders/ray_blit.wgsl");
    EXPECT_EQ(config.environmentPath,
              "data/generated/ocean_environment.png");
    EXPECT_EQ(config.colorFormat, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_FLOAT_EQ(config.heightScale, 500.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
    EXPECT_FLOAT_EQ(config.fogDensity, 0.0001f);
}

TEST_F(BlitPathTest, InitWithNullDeviceFails) {
    EXPECT_FALSE(blitPath_.init(nullptr, nullptr));
}

TEST_F(BlitPathTest, RejectsInvalidConfigBeforeGpuCalls) {
    auto config = render::BlitPathConfig::defaults();
    config.heightScale = std::numeric_limits<float>::quiet_NaN();
    const auto device = reinterpret_cast<WGPUDevice>(uintptr_t{1});
    const auto queue = reinterpret_cast<WGPUQueue>(uintptr_t{2});
    EXPECT_FALSE(blitPath_.init(device, queue, config));
}

TEST_F(BlitPathTest, RejectsInvalidDebugControls) {
    const float originalMaxDepth = blitPath_.getDebugMaxDepth();
    blitPath_.setDebugMode(99u);
    blitPath_.setDebugMaxDepth(
        std::numeric_limits<float>::quiet_NaN());
    blitPath_.setDebugMaxDepth(0.0f);
    EXPECT_EQ(blitPath_.getDebugMode(), 0u);
    EXPECT_FLOAT_EQ(blitPath_.getDebugMaxDepth(), originalMaxDepth);

    blitPath_.setDebugMode(3u);
    blitPath_.setDebugMaxDepth(2500.0f);
    EXPECT_EQ(blitPath_.getDebugMode(), 3u);
    EXPECT_FLOAT_EQ(blitPath_.getDebugMaxDepth(), 2500.0f);
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

TEST_F(BlitPathTest, ProductionShaderHasNoGpuValidationErrors) {
    ASSERT_TRUE(gpuContextInitialized_) << "This shader gate requires WebGPU";
    gpuContext_.setErrorCallback([](WGPUErrorType, const char* message) {
        ADD_FAILURE() << "GPU validation: " << message;
    });
    EXPECT_TRUE(blitPath_.init(gpuContext_.getDevice(), gpuContext_.getQueue(), getConfig()));
    gpuContext_.tick();
    blitPath_.shutdown();
    gpuContext_.setErrorCallback({});
}

TEST_F(BlitPathTest, SceneTerrainAndWaterShadowPipelinesFitBaselineBindingsAndMoveSafely) {
    ASSERT_TRUE(gpuContextInitialized_);
    gpuContext_.setErrorCallback([](WGPUErrorType, const char* message) {
        ADD_FAILURE() << "Scene shadow validation: " << message;
    });
    auto config = getConfig(); config.enableOpaqueScene = true;
    ASSERT_TRUE(blitPath_.init(gpuContext_.getDevice(), gpuContext_.getQueue(), config));
    ASSERT_TRUE(blitPath_.resize(64, 64));
    render::BlitPath moved(std::move(blitPath_));
    blitPath_ = std::move(moved);
    EXPECT_EQ(blitPath_.opaqueSceneBytes(), 64u * 64u * 12u);
    blitPath_.shutdown();
    gpuContext_.tick();
    gpuContext_.setErrorCallback({});
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

    blitPath_.setTerrainSize(0, 512);
    EXPECT_FLOAT_EQ(blitPath_.getUniforms().terrainSize.x, 512.0f);
    EXPECT_FLOAT_EQ(blitPath_.getUniforms().terrainSize.y, 512.0f);
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
    blitPath_.setShadowTexture(shadowView_);
    blitPath_.setMaterialTexture(materialView_);
    blitPath_.setTerrainTexture(terrainView_);
    blitPath_.setTerrainMaterialTextures(
        terrainMaterialAlbedoView_,
        terrainMaterialNormalRoughnessView_);
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
    EXPECT_FALSE(blitPath_.render(encoder, colorView_));
    
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
    blitPath_.setShadowTexture(shadowView_);
    blitPath_.setMaterialTexture(materialView_);
    blitPath_.setTerrainTexture(terrainView_);
    blitPath_.setTerrainMaterialTextures(
        terrainMaterialAlbedoView_,
        terrainMaterialNormalRoughnessView_);
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
    EXPECT_TRUE(blitPath_.render(encoder, colorView_));
    
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


TEST_F(BlitPathTest, MovingCasterUpdatesTerrainAndWaterWithoutChangingCameraOrStaticDepth) {
    ASSERT_TRUE(gpuContextInitialized_);
    gpuContext_.setErrorCallback([](WGPUErrorType, const char* message) { ADD_FAILURE() << message; });
    auto device=gpuContext_.getDevice();auto queue=gpuContext_.getQueue();
    auto config=getConfig();config.enableOpaqueScene=true;
    ASSERT_TRUE(blitPath_.init(device,queue,config));ASSERT_TRUE(createTestTextures());
    ASSERT_TRUE(blitPath_.resize(320,240));
    struct Owned {
        std::vector<WGPUTextureView> views;std::vector<WGPUTexture> textures;
        WGPUSampler sampler=nullptr;WGPUBuffer readback=nullptr;
        ~Owned(){for(auto v:views)wgpuTextureViewRelease(v);for(auto t:textures)wgpuTextureRelease(t);
            if(sampler)wgpuSamplerRelease(sampler);
            if(readback)wgpuBufferRelease(readback);}
    } owned;
    const auto texture=[&](uint32_t width,uint32_t height,WGPUTextureFormat format,uint32_t layers=1u) {
        auto desc=gpu::TextureDesc::tex2D(width,height,format,WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst|WGPUTextureUsage_CopySrc);
        desc.depthOrArrayLayers=layers;auto t=gpu::createTexture(device,desc);owned.textures.push_back(t);return t;
    };
    const auto viewOf=[&](WGPUTexture t,uint32_t layers=1u){gpu::TextureViewDesc desc;
        if(layers>1){desc.dimension=WGPUTextureViewDimension_2DArray;desc.arrayLayerCount=layers;}
        auto v=gpu::createTextureView(t,desc);owned.views.push_back(v);return v;};
    const auto upload=[&](WGPUTexture t,const auto& values,uint32_t width,uint32_t height,uint32_t layer=0u){
        return gpu::writeTexture(queue,t,std::as_bytes(std::span(values)),width,height,
            width*static_cast<uint32_t>(sizeof(values[0])),0,{0,0,layer});};
    auto staticDepth=texture(320,240,WGPUTextureFormat_R32Float);auto staticDepthView=viewOf(staticDepth);
    auto heights=texture(16,16,WGPUTextureFormat_R32Uint);auto heightView=viewOf(heights);
    auto shadowHeights=texture(16,16,WGPUTextureFormat_R32Uint);auto shadowHeightView=viewOf(shadowHeights);
    auto waves=texture(1,1,WGPUTextureFormat_RGBA16Float,4);auto waveView=viewOf(waves,4);
    auto depthDesc=gpu::TextureDesc::depth(320,240,WGPUTextureFormat_Depth32Float);
    auto objectDepth=gpu::createTexture(device,depthDesc);owned.textures.push_back(objectDepth);auto objectDepthView=viewOf(objectDepth);
    owned.sampler=gpu::createSampler(device,gpu::SamplerDesc{});
    owned.readback=gpu::createBuffer(device,gpu::BufferDesc{.label="scene_shadow_numeric_samples",.size=768,
        .usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    ASSERT_NE(owned.readback,nullptr);
    std::vector<float> visibility(320u*240u,1);ASSERT_TRUE(upload(shadowTexture_,visibility,320,240));
    std::vector<glm::u16vec4> normals(320u*240u,glm::u16vec4(0,glm::packHalf1x16(1.f),0,0));
    ASSERT_TRUE(upload(materialTexture_,normals,320,240));
    std::vector<glm::u8vec4> albedo(256u*256u,glm::u8vec4(200,165,100,255));ASSERT_TRUE(upload(terrainTexture_,albedo,256,256));
    std::vector<uint8_t> ao(256u*256u,255);ASSERT_TRUE(upload(lightmapTexture_,ao,256,256));
    const std::array<glm::u16vec4,1> flatWave{glm::u16vec4(0,glm::packHalf1x16(1.f),0,0)};
    for(uint32_t i=0;i<4;++i) {
        gpu::CompatImageCopyTexture destination{};destination.texture=waves;destination.origin.z=i;
        destination.aspect=WGPUTextureAspect_All;
        WGPUTextureDataLayout layout{};layout.bytesPerRow=8;layout.rowsPerImage=1;
        const WGPUExtent3D extent{1,1,1};
        wgpuQueueWriteTexture(queue,&destination,flatWave.data(),sizeof(flatWave),&layout,&extent);
    }
    std::vector<uint32_t> clearShadow(256,0);ASSERT_TRUE(upload(shadowHeights,clearShadow,16,16));
    blitPath_.setDepthTexture(depthView_);blitPath_.setShadowTexture(shadowView_);
    blitPath_.setStaticTerrainTextures(staticDepthView,shadowView_);blitPath_.setMaterialTexture(materialView_);
    blitPath_.setTerrainTexture(terrainView_);blitPath_.setLightmapTexture(lightmapView_);
    blitPath_.setTerrainMaterialTextures(terrainMaterialAlbedoView_,terrainMaterialNormalRoughnessView_);
    blitPath_.setWaterCompositeResources(heightView,shadowHeightView,waveView,owned.sampler);
    blitPath_.setTerrainSize(16,16);blitPath_.setLinearDepthRequired(true);
    const glm::vec3 eye(-3,3,0);const auto view=glm::lookAtLH(eye,glm::vec3(0),glm::vec3(0,1,0));
    const auto projection=glm::perspectiveLH_ZO(glm::radians(60.f),320.f/240.f,.1f,100.f);
    blitPath_.updateCamera(view,projection,eye,.12f);
    auto uniforms=blitPath_.getUniforms();uniforms.metrics={10,1,1,0};uniforms.invProjParams.z=1;
    const auto direction=glm::normalize(glm::vec3(1,1,0));uniforms.lightDirWS=glm::vec4(direction,0);
    uniforms.lightDirVS=glm::vec4(glm::mat3(view)*direction,.12f);uniforms.lightingColor={1,1,1,1};
    // Keep the diagnostic pixel below the tone curve's highlight shoulder so
    // the direct-sun change survives 8-bit quantization of the final output.
    uniforms.ambientExposure={1,1,1,.5f};uniforms.fogColor={0,0,0,0};uniforms.waterMotion={0,0,0,0};
    uniforms.waterParams={0,0,0,.2f};uniforms.waterColorA={.1f,.2f,.3f,1};uniforms.waterColorB={.03f,.09f,.12f,1};
    uniforms.waterOptics={1.333f,0,1,1};uniforms.waterFoam={1,0,0,500};uniforms.waterSpectrum={128,16,0,0};
    render::MeshPath mesh;render::MeshPathConfig meshConfig;meshConfig.colorFormat=WGPUTextureFormat_RGBA16Float;
    meshConfig.linearHdrOutput=true;meshConfig.sunShadows=true;
    ASSERT_TRUE(mesh.init(device,queue,meshConfig));ASSERT_TRUE(mesh.loadMeshData(render::inspectionGuideMesh()));
    ASSERT_TRUE(mesh.setSceneTextures(nullptr,staticDepthView));
    render::PrimitiveLighting lighting;lighting.direction=direction;lighting.fogDensity=0;
    struct Draw {render::MeshPath* mesh;WGPUTextureView depth;glm::mat4 view,projection;glm::vec3 eye;
        render::PrimitiveLighting light;glm::vec3 worldOrigin{0};};
    Draw draw{&mesh,objectDepthView,view,projection,eye,lighting};
    render::OpaqueSceneDraw objects{&draw,[](void* value,WGPUCommandEncoder encoder,WGPUTextureView color,WGPUTextureView depth,render::SceneShadowConsumer background){
        auto& d=*static_cast<Draw*>(value);return d.mesh->render(encoder,color,d.depth,d.view,d.projection,d.eye,d.light,320,240,false,depth,background,d.worldOrigin);}};
    float expectedDepth=0, finalDepth=0;
    const auto sample=[&](bool caster,bool discard=false,float overrideX=0){
        mesh.clearInstances();
        // The control caster must miss both the water point and the refracted
        // seabed point. At x=5 its shadow still crosses the latter (x=2,y=-2).
        mesh.addInstance({.modelMatrix=glm::translate(glm::mat4(1),glm::vec3(overrideX!=0?overrideX:(caster?1.f:15.f),1,0)-draw.worldOrigin)*glm::scale(glm::mat4(1),glm::vec3(1.2f,.1f,1.2f))});
        auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);
        WGPURenderPassDepthStencilAttachment clear{};clear.view=objectDepthView;clear.depthLoadOp=WGPULoadOp_Clear;
        clear.depthStoreOp=WGPUStoreOp_Store;clear.depthClearValue=1;clear.stencilReadOnly=true;
        WGPURenderPassDescriptor descriptor{};descriptor.depthStencilAttachment=&clear;
        auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
        const bool rendered=blitPath_.render(encoder,colorView_,nullptr,WGPU_QUERY_SET_INDEX_UNDEFINED,WGPU_QUERY_SET_INDEX_UNDEFINED,objects);
        if(!rendered){wgpuCommandEncoderRelease(encoder);return -1;}
        if(discard){wgpuCommandEncoderRelease(encoder);blitPath_.discardEncoding();return 0;}
        gpu::CompatImageCopyTexture source{};source.texture=colorTexture_;source.origin={160,120,0};source.aspect=WGPUTextureAspect_All;
        WGPUImageCopyBuffer destination{};destination.buffer=owned.readback;destination.layout.bytesPerRow=256;destination.layout.rowsPerImage=1;
        const WGPUExtent3D extent{1,1,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        source.texture=staticDepth;destination.layout.offset=256;wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        source.texture=depthTexture_;destination.layout.offset=512;wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        auto commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue,1,&commands);wgpuCommandBufferRelease(commands);
        auto done=std::make_shared<std::atomic<int>>(0);
        using Completion=std::shared_ptr<std::atomic<int>>;
        wgpuBufferMapAsync(owned.readback,WGPUMapMode_Read,0,768,
            [](WGPUBufferMapAsyncStatus status,void* context){
                std::unique_ptr<Completion> completion(static_cast<Completion*>(context));
                (*completion)->store(status==WGPUBufferMapAsyncStatus_Success?1:2);
            },new Completion(done));
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!done->load()&&std::chrono::steady_clock::now()<until){gpuContext_.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(done->load()!=1)return -2;
        const auto* bytes=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(owned.readback,0,768));
        const int red=bytes[2]; // BGRA8 output.
        std::memcpy(&finalDepth,bytes+512,sizeof(float));
        std::printf("  caster=%d rgba=%u,%u,%u finalDepth=%g staticDepth=%g water=%g scene=%d\n",caster,bytes[2],bytes[1],bytes[0],double(finalDepth),double(expectedDepth),double(blitPath_.getUniforms().waterParams.y),blitPath_.didUseSceneSunShadows());
        float retainedDepth;std::memcpy(&retainedDepth,bytes+256,sizeof(float));wgpuBufferUnmap(owned.readback);
        EXPECT_FLOAT_EQ(retainedDepth,expectedDepth);return red;
    };
    for(int water=0;water<2;++water){
        const float plane=water?-2.f:0.f;
        std::vector<float> depths(320u*240u);
        for(uint32_t y=0;y<240;++y)for(uint32_t x=0;x<320;++x){
            const auto ray=glm::normalize(glm::mat3(glm::inverse(view))*glm::vec3((2*(float(x)+.5f)/320-1)/projection[0][0],
                (1-2*(float(y)+.5f)/240)/projection[1][1],1));
            depths[y*320+x]=ray.y<0?(plane-eye.y)/ray.y:-1;
        }
        expectedDepth=depths[120u*320u+160u];
        ASSERT_TRUE(upload(staticDepth,depths,320,240));
        std::vector<uint32_t> field(256,static_cast<uint32_t>(std::round((plane/10.f+1.f)*.5f*65535.f)));
        ASSERT_TRUE(upload(heights,field,16,16));
        uniforms.waterParams.y=float(water);blitPath_.setCameraUniforms(uniforms);blitPath_.setStaticCacheState(true,true);
        const int clear=sample(false);blitPath_.setStaticCacheState(true,false);const int shadowed=sample(true);
        const int moved=sample(false);ASSERT_GE(clear,0);ASSERT_GE(shadowed,0);
        std::printf("Scene receiver water=%d clear=%d shadow=%d moved=%d\n",water,clear,shadowed,moved);
        EXPECT_LT(shadowed,clear-5);EXPECT_NEAR(moved,clear,2);
        EXPECT_EQ(sample(true,true),0);EXPECT_NEAR(sample(false),clear,2);
        if(water) {
            const int shadedBed=sample(false,false,5);
            EXPECT_LT(shadedBed,clear-20); // Shadowed seabed remains visible through refraction.
            EXPECT_LT(finalDepth,expectedDepth-1); // Surface depth still belongs to the ocean.
        }
        uniforms.lightingColor.w=0;blitPath_.setCameraUniforms(uniforms);blitPath_.setStaticCacheState(true,true);
        const int ambient=sample(false);blitPath_.setStaticCacheState(true,false);
        EXPECT_NEAR(sample(true),ambient,2);uniforms.lightingColor.w=1;
        blitPath_.setCameraUniforms(uniforms);blitPath_.setStaticCacheState(true,true);
        // Re-express the identical absolute world in independent caster
        // frames. Terrain/water uniforms deliberately remain absolute. The
        // negative-Y frame reproduces Cove's sector, while mixed X/Z checks
        // that the bridge is a vector rather than a vertical-only correction.
        for(const auto origin:{glm::vec3(0,-256,0),glm::vec3(256,0,-256)}) {
            SCOPED_TRACE("shadow frame origin " + std::to_string(origin.x) + ","
                + std::to_string(origin.y) + "," + std::to_string(origin.z));
            draw.worldOrigin=origin;draw.eye=eye-origin;
            draw.view=view*glm::translate(glm::mat4(1),origin);
            EXPECT_NEAR(sample(false),clear,2);
            blitPath_.setStaticCacheState(true,false);
            EXPECT_NEAR(sample(true),shadowed,2);
        }
        draw.worldOrigin={0,0,0};draw.eye=eye;draw.view=view;
    }
    // A high terrain-shadow boundary must remove sunlight inside the map,
    // yet must have no effect on the ocean beyond any of its four edges.
    // This compares the real sun glint with the same cached seabed/depth.
    const std::vector<uint32_t> blockedShadow(256,65535);
    const int insideOpen=sample(false);
    ASSERT_TRUE(upload(shadowHeights,blockedShadow,16,16));
    const int insideBlocked=sample(false);
    ASSERT_GE(insideOpen,0);ASSERT_GE(insideBlocked,0);
    EXPECT_LT(insideBlocked,insideOpen-5);
    for(const auto offset:{glm::vec3(30,0,0),glm::vec3(-30,0,0),
            glm::vec3(0,0,30),glm::vec3(0,0,-30)}) {
        SCOPED_TRACE("ocean outside finite terrain " + std::to_string(offset.x) + "," + std::to_string(offset.z));
        draw.eye=eye+offset;draw.view=view*glm::translate(glm::mat4(1),-offset);
        ASSERT_TRUE(uniforms.setCamera(draw.view,projection,draw.eye));
        blitPath_.setCameraUniforms(uniforms);blitPath_.setStaticCacheState(true,true);
        ASSERT_TRUE(upload(shadowHeights,clearShadow,16,16));
        const int open=sample(false);blitPath_.setStaticCacheState(true,false);
        ASSERT_TRUE(upload(shadowHeights,blockedShadow,16,16));
        const int borderBlocked=sample(false);
        ASSERT_GE(open,0);ASSERT_GE(borderBlocked,0);
        EXPECT_NEAR(borderBlocked,open,2);
        EXPECT_LT(finalDepth,expectedDepth-1);
    }
    mesh.shutdown();blitPath_.shutdown();gpuContext_.tick();gpuContext_.setErrorCallback({});
}

} // namespace voxy
