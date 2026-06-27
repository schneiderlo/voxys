// ═══════════════════════════════════════════════════════════════════════════════
// test_shader_terrain.cpp - Unit tests for terrain.wgsl shader
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the triangle path terrain shader. These tests verify:
//   - Shader file exists and can be read
//   - Shader source has required entry points
//   - Shader compiles successfully on WebGPU device
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>

#include "gpu/resources.hpp"
#include "gpu/context.hpp"

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class TerrainShaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find the shader file - check multiple possible locations
        std::vector<std::filesystem::path> searchPaths = {
            "shaders/terrain.wgsl",
            "../shaders/terrain.wgsl",
            "../../shaders/terrain.wgsl",
            "../../../shaders/terrain.wgsl",
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

// ═══════════════════════════════════════════════════════════════════════════════
// File Existence and Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainShaderTest, ShaderFileExists) {
    ASSERT_FALSE(shaderPath_.empty()) 
        << "terrain.wgsl not found in any of the expected locations";
    EXPECT_TRUE(std::filesystem::exists(shaderPath_))
        << "Shader file does not exist at: " << shaderPath_;
}

TEST_F(TerrainShaderTest, ShaderFileNotEmpty) {
    ASSERT_FALSE(shaderSource_.empty()) 
        << "Shader source is empty or could not be read";
    EXPECT_GT(shaderSource_.size(), 100u) 
        << "Shader source seems too short";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainShaderTest, HasCameraUniformsStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct CameraUniforms"), std::string::npos)
        << "Shader missing CameraUniforms struct";
}

TEST_F(TerrainShaderTest, HasVertexShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@vertex"), std::string::npos)
        << "Shader missing @vertex attribute";
    EXPECT_NE(shaderSource_.find("fn vs("), std::string::npos)
        << "Shader missing vertex shader entry point 'vs'";
}

TEST_F(TerrainShaderTest, HasFragmentShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@fragment"), std::string::npos)
        << "Shader missing @fragment attribute";
    EXPECT_NE(shaderSource_.find("fn fs("), std::string::npos)
        << "Shader missing fragment shader entry point 'fs'";
}

TEST_F(TerrainShaderTest, HasUniformBindings) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@group(0) @binding(0)"), std::string::npos)
        << "Shader missing camera uniform binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(1)"), std::string::npos)
        << "Shader missing heightTex binding";
}

TEST_F(TerrainShaderTest, HasHeightTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var heightTex : texture_2d<u32>"), std::string::npos)
        << "Shader missing heightTex texture declaration";
}

TEST_F(TerrainShaderTest, HasSampleHeightFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn sampleHeight("), std::string::npos)
        << "Shader missing sampleHeight helper function";
}

TEST_F(TerrainShaderTest, HasVSOutStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct VSOut"), std::string::npos)
        << "Shader missing VSOut vertex output struct";
}

TEST_F(TerrainShaderTest, HasWorldPosOutput) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@location(0) worldPos"), std::string::npos)
        << "Shader missing worldPos output at location 0";
}

TEST_F(TerrainShaderTest, HasNormalOutput) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@location(1) normal"), std::string::npos)
        << "Shader missing normal output at location 1";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Feature Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainShaderTest, HasTiledInstancing) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for tiled instancing constants (module-scope)
    EXPECT_NE(shaderSource_.find("TILE_QUADS"), std::string::npos)
        << "Shader missing tile constants for instancing";
    // Check for instance_index usage
    EXPECT_NE(shaderSource_.find("@builtin(instance_index)"), std::string::npos)
        << "Shader missing instance_index builtin for instancing";
}

TEST_F(TerrainShaderTest, HasHeightSampling) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("textureLoad(heightTex"), std::string::npos)
        << "Shader missing textureLoad call for height sampling";
}

TEST_F(TerrainShaderTest, HasNormalComputation) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for central differences pattern (hL, hR, hD, hU)
    EXPECT_NE(shaderSource_.find("hL ="), std::string::npos)
        << "Shader missing left height sample for normal computation";
    EXPECT_NE(shaderSource_.find("hR ="), std::string::npos)
        << "Shader missing right height sample for normal computation";
    EXPECT_NE(shaderSource_.find("cross("), std::string::npos)
        << "Shader missing cross product for normal computation";
}

TEST_F(TerrainShaderTest, HasLighting) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for lighting calculations
    EXPECT_NE(shaderSource_.find("lightDir"), std::string::npos)
        << "Shader missing light direction variable";
    EXPECT_NE(shaderSource_.find("dot("), std::string::npos)
        << "Shader missing dot product for lighting";
}

TEST_F(TerrainShaderTest, HasTextureSampling) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for texture bindings
    EXPECT_NE(shaderSource_.find("@binding(2) var albedoTex"), std::string::npos)
        << "Shader missing albedoTex binding";
    EXPECT_NE(shaderSource_.find("@binding(3) var lightmapTex"), std::string::npos)
        << "Shader missing lightmapTex binding";
        
    // Check for sampling
    EXPECT_NE(shaderSource_.find("textureSample(albedoTex"), std::string::npos)
        << "Shader missing albedo texture sampling";
    EXPECT_NE(shaderSource_.find("textureSample(lightmapTex"), std::string::npos)
        << "Shader missing lightmap texture sampling";
}

TEST_F(TerrainShaderTest, DetectsWaterFromBakedAlbedo) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("waterMask"), std::string::npos)
        << "Shader missing water mask from baked albedo";
    EXPECT_NE(shaderSource_.find("waterSpecular"), std::string::npos)
        << "Shader missing water highlight";
}

TEST_F(TerrainShaderTest, HasFog) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fogDensity"), std::string::npos)
        << "Shader missing fog density";
    EXPECT_NE(shaderSource_.find("fogColor"), std::string::npos)
        << "Shader missing fog color";
    EXPECT_NE(shaderSource_.find("fogFactor"), std::string::npos)
        << "Shader missing fog factor computation";
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Compilation Test (requires WebGPU context)
// ═══════════════════════════════════════════════════════════════════════════════

// This test is more integration-level and requires a WebGPU device
// It's wrapped in a conditional that checks if we can create a context
class TerrainShaderGPUTest : public TerrainShaderTest {
protected:
    void SetUp() override {
        TerrainShaderTest::SetUp();
        
        // Try to create a GPU context for shader compilation testing
        // This may fail on systems without WebGPU support
        gpuContextInitialized_ = gpuContext_.initHeadless();
    }
    
    void TearDown() override {
        if (gpuContextInitialized_) {
            gpuContext_.shutdown();
        }
    }
    
    gpu::Context gpuContext_;
    bool gpuContextInitialized_ = false;
};

TEST_F(TerrainShaderGPUTest, ShaderCompilesOnGPU) {
    // Skip if GPU context is not available
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available - skipping compilation test";
    }
    
    ASSERT_FALSE(shaderSource_.empty()) << "Shader source not loaded";
    
    // Create shader module from source
    auto device = gpuContext_.getDevice();
    ASSERT_NE(device, nullptr) << "Device is null";
    
    auto shaderModule = gpu::createShaderModule(
        device, 
        shaderSource_, 
        "terrain.wgsl"
    );
    
    EXPECT_NE(shaderModule, nullptr) 
        << "Failed to compile terrain.wgsl shader on GPU";
    
    // Clean up
    if (shaderModule) {
        wgpuShaderModuleRelease(shaderModule);
    }
}

} // namespace voxy
