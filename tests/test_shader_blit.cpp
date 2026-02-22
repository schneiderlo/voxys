// ═══════════════════════════════════════════════════════════════════════════════
// test_shader_blit.cpp - Unit tests for ray_blit.wgsl shader
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the fullscreen blit/lighting shader. These tests verify:
//   - Shader file exists and can be read
//   - Shader source has required entry points and bindings
//   - Shader has position reconstruction components
//   - Shader has lighting calculation components
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

class BlitShaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find the shader file - check multiple possible locations
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

TEST_F(BlitShaderTest, ShaderFileExists) {
    ASSERT_FALSE(shaderPath_.empty()) 
        << "ray_blit.wgsl not found in any of the expected locations";
    EXPECT_TRUE(std::filesystem::exists(shaderPath_))
        << "Shader file does not exist at: " << shaderPath_;
}

TEST_F(BlitShaderTest, ShaderFileNotEmpty) {
    ASSERT_FALSE(shaderSource_.empty()) 
        << "Shader source is empty or could not be read";
    EXPECT_GT(shaderSource_.size(), 100u) 
        << "Shader source seems too short";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasCameraUniformsStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct CameraUniforms"), std::string::npos)
        << "Shader missing CameraUniforms struct";
}

TEST_F(BlitShaderTest, HasVertexShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@vertex"), std::string::npos)
        << "Shader missing @vertex attribute";
    EXPECT_NE(shaderSource_.find("fn vs("), std::string::npos)
        << "Shader missing vertex shader entry point 'vs'";
}

TEST_F(BlitShaderTest, HasFragmentShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@fragment"), std::string::npos)
        << "Shader missing @fragment attribute";
    EXPECT_NE(shaderSource_.find("fn fs("), std::string::npos)
        << "Shader missing fragment shader entry point 'fs'";
}

TEST_F(BlitShaderTest, HasVSOutStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct VSOut"), std::string::npos)
        << "Shader missing VSOut struct for vertex-to-fragment data";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Binding Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasUniformBindings) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@group(0) @binding(0)"), std::string::npos)
        << "Shader missing camera uniform binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(1)"), std::string::npos)
        << "Shader missing depthTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(2)"), std::string::npos)
        << "Shader missing terrainTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(3)"), std::string::npos)
        << "Shader missing lightmapTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(4)"), std::string::npos)
        << "Shader missing terrainSampler binding";
}

TEST_F(BlitShaderTest, HasDepthTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var depthTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing depthTex texture declaration";
}

TEST_F(BlitShaderTest, HasTerrainTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var terrainTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing terrainTex texture declaration";
}

TEST_F(BlitShaderTest, HasLightmapTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var lightmapTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing lightmapTex texture declaration";
}

TEST_F(BlitShaderTest, HasSamplerBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var terrainSampler : sampler"), std::string::npos)
        << "Shader missing terrainSampler declaration";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Fullscreen Triangle Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasFullscreenTriangleVertices) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for oversized triangle coordinates
    EXPECT_NE(shaderSource_.find("-1.0, -3.0"), std::string::npos)
        << "Shader missing fullscreen triangle bottom-left vertex";
    EXPECT_NE(shaderSource_.find("3.0, 1.0"), std::string::npos)
        << "Shader missing fullscreen triangle right vertex";
    EXPECT_NE(shaderSource_.find("-1.0, 1.0"), std::string::npos)
        << "Shader missing fullscreen triangle top-left vertex";
}

TEST_F(BlitShaderTest, HasUVCoordinates) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for UV coordinates in vertex shader output
    EXPECT_NE(shaderSource_.find("@location(0) uv"), std::string::npos)
        << "Shader missing UV output in VSOut";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasSampleDepthFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn sampleDepth("), std::string::npos)
        << "Shader missing sampleDepth helper function";
}

TEST_F(BlitShaderTest, HasNdcFromPixelFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn ndcFromPixel("), std::string::npos)
        << "Shader missing ndcFromPixel helper function";
}

TEST_F(BlitShaderTest, HasRayDirFromPixelFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn rayDirFromPixel("), std::string::npos)
        << "Shader missing rayDirFromPixel helper function";
}

TEST_F(BlitShaderTest, HasViewPosFromDepthFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn viewPosFromDepth("), std::string::npos)
        << "Shader missing viewPosFromDepth helper function";
}

TEST_F(BlitShaderTest, HasViewToWorldFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn viewToWorld("), std::string::npos)
        << "Shader missing viewToWorld helper function";
}

TEST_F(BlitShaderTest, HasTerrainUVFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn terrainUV("), std::string::npos)
        << "Shader missing terrainUV helper function";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sky Rendering Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasSkyRendering) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for atmospheric scattering components
    EXPECT_NE(shaderSource_.find("rayleigh"), std::string::npos)
        << "Shader missing Rayleigh scattering";
    EXPECT_NE(shaderSource_.find("skyColor"), std::string::npos)
        << "Shader missing skyColor variable";
}

TEST_F(BlitShaderTest, HasAtmosphericScattering) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for Mie scattering (sun haze)
    EXPECT_NE(shaderSource_.find("mie"), std::string::npos)
        << "Shader missing Mie scattering for sun haze";
}

TEST_F(BlitShaderTest, ChecksForSkyPixels) {
    ASSERT_FALSE(shaderSource_.empty());
    // Sky pixels have depth < 0
    EXPECT_NE(shaderSource_.find("depthCenter < 0.0"), std::string::npos)
        << "Shader should check for sky pixels (depth < 0)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Normal Reconstruction Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasNeighborDepthSampling) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for neighbor depth sampling
    EXPECT_NE(shaderSource_.find("depthNegX"), std::string::npos)
        << "Shader missing negative X neighbor depth";
    EXPECT_NE(shaderSource_.find("depthPosX"), std::string::npos)
        << "Shader missing positive X neighbor depth";
    EXPECT_NE(shaderSource_.find("depthNegY"), std::string::npos)
        << "Shader missing negative Y neighbor depth";
    EXPECT_NE(shaderSource_.find("depthPosY"), std::string::npos)
        << "Shader missing positive Y neighbor depth";
}

TEST_F(BlitShaderTest, ChoosesCloserNeighbor) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for closer neighbor selection
    EXPECT_NE(shaderSource_.find("useNegX"), std::string::npos)
        << "Shader missing useNegX closer neighbor selection";
    EXPECT_NE(shaderSource_.find("useNegY"), std::string::npos)
        << "Shader missing useNegY closer neighbor selection";
}

TEST_F(BlitShaderTest, HasNormalFlipRule) {
    ASSERT_FALSE(shaderSource_.empty());
    // Normal flip rule: ensure dx/dy are flipped to point in positive axis direction
    // Current implementation: if (useNegX) { dx = -dx; }
    bool hasDxFlip = shaderSource_.find("dx = -dx") != std::string::npos;
    bool hasDyFlip = shaderSource_.find("dy = -dy") != std::string::npos;
    
    EXPECT_TRUE(hasDxFlip || hasDyFlip)
        << "Shader missing normal flip rule (dx = -dx or dy = -dy)";
}

TEST_F(BlitShaderTest, ComputesNormalFromCrossProduct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("cross(dx, dy)"), std::string::npos)
        << "Shader should compute normal using cross product";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lighting Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasDiffuseLighting) {
    ASSERT_FALSE(shaderSource_.empty());
    // Diffuse: max(dot(normal, lightDir), 0.0)
    EXPECT_NE(shaderSource_.find("diffuse"), std::string::npos)
        << "Shader missing diffuse lighting";
    EXPECT_NE(shaderSource_.find("dot(normal, lightDir)"), std::string::npos)
        << "Shader missing dot product for diffuse";
}

TEST_F(BlitShaderTest, HasAmbientLighting) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("ambient"), std::string::npos)
        << "Shader missing ambient lighting";
    // Ambient should be from uniform now (camera.lightDirVS.w)
    EXPECT_NE(shaderSource_.find("camera.lightDirVS.w"), std::string::npos)
        << "Shader missing ambient intensity from uniform";
}

TEST_F(BlitShaderTest, HasSpecularLighting) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("specular"), std::string::npos)
        << "Shader missing specular lighting";
    EXPECT_NE(shaderSource_.find("halfVec"), std::string::npos)
        << "Shader missing half vector for specular";
    EXPECT_NE(shaderSource_.find("roughness"), std::string::npos)
        << "Shader missing roughness for specular";
}

TEST_F(BlitShaderTest, UsesLightDirFromUniforms) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("camera.lightDirVS"), std::string::npos)
        << "Shader should use light direction from camera uniforms";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Fog Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, HasFogCalculation) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fogDensity"), std::string::npos)
        << "Shader missing fog density";
    EXPECT_NE(shaderSource_.find("fogColor"), std::string::npos)
        << "Shader missing fog color";
    EXPECT_NE(shaderSource_.find("fogFactor"), std::string::npos)
        << "Shader missing fog factor";
}

TEST_F(BlitShaderTest, HasExponentialFog) {
    ASSERT_FALSE(shaderSource_.empty());
    // Exponential fog: exp(-fogDensity * dist)
    EXPECT_NE(shaderSource_.find("exp(-fogDensity"), std::string::npos)
        << "Shader should use exponential fog";
}

TEST_F(BlitShaderTest, FogIsCappedAt07) {
    ASSERT_FALSE(shaderSource_.empty());
    // Fog should be capped at 0.7
    EXPECT_NE(shaderSource_.find("0.0, 0.7"), std::string::npos)
        << "Fog factor should be clamped to max 0.7";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture Sampling Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, SamplesTerrainTexture) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("textureSampleLevel(terrainTex"), std::string::npos)
        << "Shader should sample terrain texture";
}

TEST_F(BlitShaderTest, SamplesLightmapTexture) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("textureSampleLevel(lightmapTex"), std::string::npos)
        << "Shader should sample lightmap texture";
}

TEST_F(BlitShaderTest, UsesLightVisibility) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("lightVisibility"), std::string::npos)
        << "Shader should use lightmap visibility";
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Compilation Test (requires WebGPU context)
// ═══════════════════════════════════════════════════════════════════════════════

class BlitShaderGPUTest : public BlitShaderTest {
protected:
    void SetUp() override {
        BlitShaderTest::SetUp();
        
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

TEST_F(BlitShaderGPUTest, ShaderCompilesOnGPU) {
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
        "ray_blit.wgsl"
    );
    
    EXPECT_NE(shaderModule, nullptr) 
        << "Failed to compile ray_blit.wgsl shader on GPU";
    
    // Clean up
    if (shaderModule) {
        wgpuShaderModuleRelease(shaderModule);
    }
}

} // namespace voxy

