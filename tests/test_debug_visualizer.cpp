// ═══════════════════════════════════════════════════════════════════════════════
// test_debug_visualizer.cpp - Unit tests for Debug Visualizer
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for DebugVisualizer class and debug_depth.wgsl shader.
// Includes depth output verification tests for ray-caster validation.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "render/debug_visualizer.hpp"
#include "render/raycast_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/context.hpp"
#include "gpu/resources.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// Shader File Tests
// ═══════════════════════════════════════════════════════════════════════════════

class DebugShaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<std::filesystem::path> searchPaths = {
            "shaders/debug_depth.wgsl",
            "../shaders/debug_depth.wgsl",
            "../../shaders/debug_depth.wgsl",
            "../../../shaders/debug_depth.wgsl",
        };
        
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                shaderPath_ = path;
                break;
            }
        }
        
        if (!shaderPath_.empty()) {
            std::ifstream file(shaderPath_);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                shaderSource_ = buffer.str();
            }
        }
    }
    
    std::filesystem::path shaderPath_;
    std::string shaderSource_;
};

TEST_F(DebugShaderTest, ShaderFileExists) {
    ASSERT_FALSE(shaderPath_.empty()) 
        << "debug_depth.wgsl not found in any of the expected locations";
    EXPECT_TRUE(std::filesystem::exists(shaderPath_))
        << "Shader file does not exist at: " << shaderPath_;
}

TEST_F(DebugShaderTest, HasDebugParamsStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct DebugParams"), std::string::npos)
        << "Shader missing DebugParams struct";
}

TEST_F(DebugShaderTest, HasVertexShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@vertex"), std::string::npos)
        << "Shader missing @vertex attribute";
    EXPECT_NE(shaderSource_.find("fn vs("), std::string::npos)
        << "Shader missing vertex shader entry point 'vs'";
}

TEST_F(DebugShaderTest, HasFragmentShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@fragment"), std::string::npos)
        << "Shader missing @fragment attribute";
    EXPECT_NE(shaderSource_.find("fn fs("), std::string::npos)
        << "Shader missing fragment shader entry point 'fs'";
}

TEST_F(DebugShaderTest, HasDepthTextureBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var depthTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing depthTex texture binding";
}

TEST_F(DebugShaderTest, HasSkyPixelHandling) {
    ASSERT_FALSE(shaderSource_.empty());
    // Sky pixels have negative depth
    EXPECT_NE(shaderSource_.find("depth < 0.0"), std::string::npos)
        << "Shader should handle sky pixels (negative depth)";
}

TEST_F(DebugShaderTest, HasVisualizationModes) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for mode-based branching
    EXPECT_NE(shaderSource_.find("params.mode"), std::string::npos)
        << "Shader should use visualization mode parameter";
}

// ═══════════════════════════════════════════════════════════════════════════════
// DebugVisualizerConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DebugVisualizerConfigTest, DefaultValues) {
    auto config = DebugVisualizerConfig::defaults();
    
    EXPECT_EQ(config.shaderPath, "shaders/debug_depth.wgsl");
    EXPECT_EQ(config.colorFormat, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_FLOAT_EQ(config.nearDist, 1.0f);
    EXPECT_FLOAT_EQ(config.farDist, 5000.0f);
    EXPECT_EQ(config.mode, DebugVisMode::GrayscaleDepth);
}

// ═══════════════════════════════════════════════════════════════════════════════
// DebugParams Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DebugParamsTest, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(DebugParams), 16u);
}

TEST(DebugParamsTest, DefaultValues) {
    DebugParams params;
    
    EXPECT_FLOAT_EQ(params.nearDist, 1.0f);
    EXPECT_FLOAT_EQ(params.farDist, 5000.0f);
    EXPECT_EQ(params.mode, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// DebugVisualizer Class Tests (No GPU)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DebugVisualizerTest, DefaultConstruction) {
    DebugVisualizer visualizer;
    
    EXPECT_FALSE(visualizer.isInitialized());
    EXPECT_FLOAT_EQ(visualizer.getNearDist(), 1.0f);
    EXPECT_FLOAT_EQ(visualizer.getFarDist(), 5000.0f);
    EXPECT_EQ(visualizer.getMode(), DebugVisMode::GrayscaleDepth);
}

TEST(DebugVisualizerTest, MoveConstruction) {
    DebugVisualizer viz1;
    DebugVisualizer viz2(std::move(viz1));
    
    EXPECT_FALSE(viz1.isInitialized());
    EXPECT_FALSE(viz2.isInitialized());
}

TEST(DebugVisualizerTest, MoveAssignment) {
    DebugVisualizer viz1;
    DebugVisualizer viz2;
    
    viz2 = std::move(viz1);
    
    EXPECT_FALSE(viz1.isInitialized());
    EXPECT_FALSE(viz2.isInitialized());
}

TEST(DebugVisualizerTest, InitWithNullDevice) {
    DebugVisualizer visualizer;
    
    EXPECT_FALSE(visualizer.init(nullptr, nullptr));
    EXPECT_FALSE(visualizer.isInitialized());
}

TEST(DebugVisualizerTest, ShutdownWithoutInit) {
    DebugVisualizer visualizer;
    
    // Should not crash
    visualizer.shutdown();
    EXPECT_FALSE(visualizer.isInitialized());
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Integration Tests
// ═══════════════════════════════════════════════════════════════════════════════

class DebugVisualizerGPUTest : public ::testing::Test {
protected:
    void SetUp() override {
        gpuContextInitialized_ = gpuContext_.initHeadless();
        
        // Find shader files
        std::vector<std::filesystem::path> debugShaderPaths = {
            "shaders/debug_depth.wgsl",
            "../shaders/debug_depth.wgsl",
            "../../shaders/debug_depth.wgsl",
            "../../../shaders/debug_depth.wgsl",
        };
        
        for (const auto& path : debugShaderPaths) {
            if (std::filesystem::exists(path)) {
                debugShaderPath_ = path;
                break;
            }
        }
        
        std::vector<std::filesystem::path> raycastShaderPaths = {
            "shaders/terrain_raycast.wgsl",
            "../shaders/terrain_raycast.wgsl",
            "../../shaders/terrain_raycast.wgsl",
            "../../../shaders/terrain_raycast.wgsl",
        };
        
        for (const auto& path : raycastShaderPaths) {
            if (std::filesystem::exists(path)) {
                raycastShaderPath_ = path;
                break;
            }
        }
    }
    
    void TearDown() override {
        visualizer_.shutdown();
        raycastPath_.shutdown();
        if (heightmapTexture_) {
            if (heightmapView_) wgpuTextureViewRelease(heightmapView_);
            wgpuTextureRelease(heightmapTexture_);
            heightmapTexture_ = nullptr;
            heightmapView_ = nullptr;
        }
        if (colorTexture_) {
            if (colorView_) wgpuTextureViewRelease(colorView_);
            wgpuTextureRelease(colorTexture_);
            colorTexture_ = nullptr;
            colorView_ = nullptr;
        }
        if (gpuContextInitialized_) {
            gpuContext_.shutdown();
        }
    }
    
    DebugVisualizerConfig getDebugConfig() {
        auto config = DebugVisualizerConfig::defaults();
        if (!debugShaderPath_.empty()) {
            config.shaderPath = debugShaderPath_;
        }
        return config;
    }
    
    RaycastPathConfig getRaycastConfig() {
        auto config = RaycastPathConfig::defaults();
        if (!raycastShaderPath_.empty()) {
            config.shaderPath = raycastShaderPath_;
        }
        return config;
    }
    
    // Create a test heightmap with known values
    void createTestHeightmap(uint32_t width, uint32_t height) {
        if (!gpuContextInitialized_) return;
        
        auto device = gpuContext_.getDevice();
        auto queue = gpuContext_.getQueue();
        
        // Create heightmap texture with mip levels
        gpu::TextureDesc texDesc = gpu::TextureDesc::tex2DMipmapped(
            width, height,
            WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "test_heightmap"
        );
        
        heightmapTexture_ = gpu::createTexture(device, texDesc);
        if (!heightmapTexture_) return;
        
        // Fill with test pattern (flat terrain with a hill in the center)
        std::vector<uint16_t> data(width * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float cx = static_cast<float>(x) - static_cast<float>(width) * 0.5f;
                float cy = static_cast<float>(y) - static_cast<float>(height) * 0.5f;
                float dist = std::sqrt(cx * cx + cy * cy);
                float radius = static_cast<float>(std::min(width, height)) * 0.25f;
                float h = 32768.0f;  // Base height (middle of range)
                if (dist < radius) {
                    // Hill in center
                    float t = 1.0f - dist / radius;
                    h += t * t * 16000.0f;  // Smooth hill
                }
                data[y * width + x] = static_cast<uint16_t>(h);
            }
        }
        
        // Upload data
        gpu::writeTexture(queue, heightmapTexture_,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()),
                                       data.size() * sizeof(uint16_t)),
            width, height, width * sizeof(uint16_t), 0);
        
        // Create view with all mip levels
        gpu::TextureViewDesc viewDesc{};
        viewDesc.label = "test_heightmap_view";
        viewDesc.format = WGPUTextureFormat_R16Uint;
        viewDesc.mipLevelCount = texDesc.mipLevelCount;
        heightmapView_ = gpu::createTextureView(heightmapTexture_, viewDesc);
        
        heightmapWidth_ = width;
        heightmapHeight_ = height;
    }
    
    // Create a color output texture
    void createColorTexture(uint32_t width, uint32_t height) {
        if (!gpuContextInitialized_) return;
        
        auto device = gpuContext_.getDevice();
        
        gpu::TextureDesc texDesc = gpu::TextureDesc::renderTarget(
            width, height,
            WGPUTextureFormat_BGRA8Unorm,
            "test_color_output"
        );
        
        colorTexture_ = gpu::createTexture(device, texDesc);
        if (colorTexture_) {
            gpu::TextureViewDesc viewDesc{};
            viewDesc.label = "test_color_view";
            viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
            colorView_ = gpu::createTextureView(colorTexture_, viewDesc);
        }
    }
    
    gpu::Context gpuContext_;
    bool gpuContextInitialized_ = false;
    std::filesystem::path debugShaderPath_;
    std::filesystem::path raycastShaderPath_;
    DebugVisualizer visualizer_;
    RaycastPath raycastPath_;
    WGPUTexture heightmapTexture_ = nullptr;
    WGPUTextureView heightmapView_ = nullptr;
    uint32_t heightmapWidth_ = 0;
    uint32_t heightmapHeight_ = 0;
    WGPUTexture colorTexture_ = nullptr;
    WGPUTextureView colorView_ = nullptr;
};

TEST_F(DebugVisualizerGPUTest, ShaderCompilesOnGPU) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(debugShaderPath_.empty()) << "Debug shader file not found";
    
    // Read shader source
    std::ifstream file(debugShaderPath_);
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string shaderSource = buffer.str();
    
    auto device = gpuContext_.getDevice();
    auto shaderModule = gpu::createShaderModule(device, shaderSource, "debug_depth.wgsl");
    
    EXPECT_NE(shaderModule, nullptr) << "Failed to compile debug_depth.wgsl";
    
    if (shaderModule) {
        wgpuShaderModuleRelease(shaderModule);
    }
}

TEST_F(DebugVisualizerGPUTest, InitAndShutdown) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(debugShaderPath_.empty()) << "Debug shader file not found";
    
    EXPECT_TRUE(visualizer_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        getDebugConfig()
    ));
    
    EXPECT_TRUE(visualizer_.isInitialized());
    
    visualizer_.shutdown();
    EXPECT_FALSE(visualizer_.isInitialized());
}

TEST_F(DebugVisualizerGPUTest, SetDepthRange) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(debugShaderPath_.empty()) << "Debug shader file not found";
    
    ASSERT_TRUE(visualizer_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        getDebugConfig()
    ));
    
    visualizer_.setDepthRange(10.0f, 10000.0f);
    
    EXPECT_FLOAT_EQ(visualizer_.getNearDist(), 10.0f);
    EXPECT_FLOAT_EQ(visualizer_.getFarDist(), 10000.0f);
}

TEST_F(DebugVisualizerGPUTest, SetMode) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(debugShaderPath_.empty()) << "Debug shader file not found";
    
    ASSERT_TRUE(visualizer_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        getDebugConfig()
    ));
    
    visualizer_.setMode(DebugVisMode::ColorGradient);
    EXPECT_EQ(visualizer_.getMode(), DebugVisMode::ColorGradient);
    
    visualizer_.setMode(DebugVisMode::RawDepth);
    EXPECT_EQ(visualizer_.getMode(), DebugVisMode::RawDepth);
}

TEST_F(DebugVisualizerGPUTest, RenderWithRaycastOutput) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(debugShaderPath_.empty()) << "Debug shader file not found";
    ASSERT_FALSE(raycastShaderPath_.empty()) << "Raycast shader file not found";
    
    const uint32_t outputSize = 64;
    const uint32_t terrainSize = 256;
    
    // Initialize raycast path
    ASSERT_TRUE(raycastPath_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        outputSize, outputSize,
        getRaycastConfig()
    ));
    
    // Create test heightmap
    createTestHeightmap(terrainSize, terrainSize);
    ASSERT_NE(heightmapView_, nullptr);
    
    raycastPath_.setHeightmap(heightmapView_, heightmapWidth_, heightmapHeight_);
    
    // Set up camera looking at terrain
    glm::vec3 camPos(0.0f, 200.0f, 0.0f);
    glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 10000.0f);
    raycastPath_.updateCamera(view, proj, camPos);
    
    // Initialize debug visualizer
    ASSERT_TRUE(visualizer_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        getDebugConfig()
    ));
    
    // Create color output
    createColorTexture(outputSize, outputSize);
    ASSERT_NE(colorView_, nullptr);
    
    // Set depth texture from raycast output
    visualizer_.setDepthTexture(raycastPath_.getDepthOutputView());
    
    // Create command encoder
    WGPUCommandEncoderDescriptor encDesc{};
    encDesc.label = "test_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_.getDevice(), &encDesc);
    ASSERT_NE(encoder, nullptr);
    
    // Dispatch raycast
    raycastPath_.dispatch(encoder);
    
    // Render debug visualization
    visualizer_.render(encoder, colorView_);
    
    // Submit
    WGPUCommandBufferDescriptor bufDesc{};
    bufDesc.label = "test_command_buffer";
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &bufDesc);
    ASSERT_NE(commands, nullptr);
    
    wgpuQueueSubmit(gpuContext_.getQueue(), 1, &commands);
    
    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    
    // Test passes if no crashes/errors
}

// ═══════════════════════════════════════════════════════════════════════════════
// Depth Output Verification Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DebugVisualizerGPUTest, VerifyDepthOutputFormat) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(raycastShaderPath_.empty()) << "Raycast shader file not found";
    
    const uint32_t outputSize = 32;
    
    ASSERT_TRUE(raycastPath_.init(
        gpuContext_.getDevice(),
        gpuContext_.getQueue(),
        outputSize, outputSize,
        getRaycastConfig()
    ));
    
    // Verify depth output texture exists and has correct properties
    EXPECT_NE(raycastPath_.getDepthOutputTexture(), nullptr);
    EXPECT_NE(raycastPath_.getDepthOutputView(), nullptr);
    EXPECT_EQ(raycastPath_.getOutputWidth(), outputSize);
    EXPECT_EQ(raycastPath_.getOutputHeight(), outputSize);
}

TEST_F(DebugVisualizerGPUTest, VerifyWorkgroupCoverage) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    ASSERT_FALSE(raycastShaderPath_.empty()) << "Raycast shader file not found";
    
    // Test various output sizes to ensure workgroup coverage is correct
    std::vector<std::pair<uint32_t, uint32_t>> testSizes = {
        {8, 8},      // Exact workgroup size
        {16, 16},    // Multiple of workgroup
        {9, 9},      // Just over workgroup size
        {64, 64},    // Typical small size
        {1920, 1080} // Full HD
    };
    
    for (const auto& [width, height] : testSizes) {
        RaycastPath path;
        ASSERT_TRUE(path.init(
            gpuContext_.getDevice(),
            gpuContext_.getQueue(),
            width, height,
            getRaycastConfig()
        )) << "Failed to init for " << width << "x" << height;
        
        // Verify workgroup counts cover all pixels
        uint32_t wgX = path.getWorkgroupCountX();
        uint32_t wgY = path.getWorkgroupCountY();
        
        // Workgroups * 8 should be >= output size
        EXPECT_GE(wgX * 8, width) << "X workgroups insufficient for width " << width;
        EXPECT_GE(wgY * 8, height) << "Y workgroups insufficient for height " << height;
        
        // But shouldn't be more than 1 extra workgroup
        EXPECT_LT(wgX * 8, width + 8) << "Too many X workgroups for width " << width;
        EXPECT_LT(wgY * 8, height + 8) << "Too many Y workgroups for height " << height;
        
        path.shutdown();
    }
}

} // namespace voxy::render



