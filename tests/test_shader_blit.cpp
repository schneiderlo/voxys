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
        << "Shader missing shadowTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(3)"), std::string::npos)
        << "Shader missing terrainTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(4)"), std::string::npos)
        << "Shader missing lightmapTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(5)"), std::string::npos)
        << "Shader missing terrainSampler binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(6)"), std::string::npos)
        << "Shader missing normalTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(7)"), std::string::npos)
        << "Shader missing debug uniform binding";
}

TEST_F(BlitShaderTest, HasDepthTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var depthTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing depthTex texture declaration";
}

TEST_F(BlitShaderTest, HasShadowTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var shadowTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing shadowTex texture declaration";
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

TEST_F(BlitShaderTest, HasNormalTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var normalTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing normalTex texture declaration";
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

TEST_F(BlitShaderTest, HasSeaLevelWaterSurface) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn terrainCoord("), std::string::npos)
        << "Shader missing unclamped terrain coordinate helper";
    EXPECT_NE(shaderSource_.find("fn waterMaskFromAlbedo("), std::string::npos)
        << "Shader missing albedo-derived water mask";
    EXPECT_NE(shaderSource_.find("fn waterSurfaceColor("), std::string::npos)
        << "Shader missing sea-level water surface shading";
    EXPECT_NE(shaderSource_.find("let seaLevel = 0.0"), std::string::npos)
        << "Shader should composite water at sea level";
    EXPECT_NE(shaderSource_.find("Sky pixels can still see the sea-level plane"), std::string::npos)
        << "Shader should composite the ocean plane for sky pixels";
    EXPECT_NE(shaderSource_.find("deepOceanDistance"), std::string::npos)
        << "Shader should estimate deep water for sky-ocean pixels";
    EXPECT_NE(shaderSource_.find("shoreFoam"), std::string::npos)
        << "Shader missing shoreline foam treatment";
    EXPECT_NE(shaderSource_.find("fn waterWaveNormal("), std::string::npos)
        << "Shader missing multi-wave water normal";
    EXPECT_NE(shaderSource_.find("fn waterSpectrum("), std::string::npos)
        << "Shader missing shared water spectrum model";
    EXPECT_NE(shaderSource_.find("fn waterCaustics("), std::string::npos)
        << "Shader missing shallow-water caustic treatment";
    EXPECT_NE(shaderSource_.find("reflectionDir"), std::string::npos)
        << "Shader missing Fresnel sky reflection for water";
    EXPECT_NE(shaderSource_.find("deepFade"), std::string::npos)
        << "Shader missing depth absorption for water";
    EXPECT_NE(shaderSource_.find("farFlatten"), std::string::npos)
        << "Shader missing distant-water smoothing";
    EXPECT_NE(shaderSource_.find("distanceHaze"), std::string::npos)
        << "Shader missing far-water atmospheric haze";
    EXPECT_NE(shaderSource_.find("oceanMask"), std::string::npos)
        << "Shader missing stable sky-ocean mask";
    EXPECT_NE(shaderSource_.find("camera.invProjParams.w"), std::string::npos)
        << "Shader should use animated water time from camera uniforms";
}

TEST_F(BlitShaderTest, HasAnimatedCloudShadows) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn valueNoise2D("), std::string::npos)
        << "Shader missing cheap value-noise helper for cloud fields";
    EXPECT_NE(shaderSource_.find("fn cloudField("), std::string::npos)
        << "Shader missing shaped cloud field";
    EXPECT_NE(shaderSource_.find("fn cloudCoverageAt("), std::string::npos)
        << "Shader missing animated cloud coverage helper";
    EXPECT_NE(shaderSource_.find("fn cloudShadowAtWorld("), std::string::npos)
        << "Shader missing terrain cloud-shadow helper";
    EXPECT_NE(shaderSource_.find("terrainCloudShadow"), std::string::npos)
        << "Shader should apply cloud shadowing to terrain lighting";
    EXPECT_NE(shaderSource_.find("let cloudShadow = 0.96 + farFlatten * 0.04"), std::string::npos)
        << "Water lighting should use a cheap bounded cloud-shadow term";
    EXPECT_NE(shaderSource_.find("camera.invProjParams.w"), std::string::npos)
        << "Clouds should use real time from camera uniforms";
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
// Normal Map Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(BlitShaderTest, SamplesTerrainNormalMap) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("textureSampleLevel(normalTex"), std::string::npos)
        << "Shader should sample terrain normals from normalTex";
}

TEST_F(BlitShaderTest, DecodesTerrainNormalMap) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("* 2.0 - vec3<f32>(1.0, 1.0, 1.0)"), std::string::npos)
        << "Shader should decode normals from [0,1] to [-1,1]";
}

TEST_F(BlitShaderTest, UsesWorldSpaceTerrainLighting) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("camera.lightDirWS.xyz"), std::string::npos)
        << "Shader should light terrain with world-space light direction";
    EXPECT_NE(shaderSource_.find("camera.cameraPos.xyz - posCWorld"), std::string::npos)
        << "Shader should compute terrain view direction in world space";
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
    EXPECT_NE(shaderSource_.find("camera.lightDirWS"), std::string::npos)
        << "Shader should use light direction from camera uniforms";
}

TEST_F(BlitShaderTest, UsesWarmSunAndCoolAmbientTint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("warmSunTint"), std::string::npos)
        << "Shader missing warm direct sunlight tint";
    EXPECT_NE(shaderSource_.find("coolAmbientTint"), std::string::npos)
        << "Shader missing cool ambient skylight tint";
}

TEST_F(BlitShaderTest, UsesBrighterReflectiveWaterPalette) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("shallowColor = vec3<f32>(0.180"), std::string::npos)
        << "Shader missing brighter shallow water palette";
    EXPECT_NE(shaderSource_.find("windStreak"), std::string::npos)
        << "Shader missing wind-aligned water ripple highlights";
    EXPECT_NE(shaderSource_.find("let absorption = 1.0 /"), std::string::npos)
        << "Shader missing depth absorption term";
    EXPECT_NE(shaderSource_.find("whitecap"), std::string::npos)
        << "Shader missing crest-driven whitecap term";
    EXPECT_NE(shaderSource_.find("fresnel * mix(0.32, 0.58"), std::string::npos)
        << "Shader missing stronger water reflection weighting";
    EXPECT_NE(shaderSource_.find("vec3<f32>(1.00, 0.93, 0.78)"), std::string::npos)
        << "Shader missing warm water glint color";
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

TEST_F(BlitShaderTest, DetectsWaterFromBakedAlbedo) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("waterMask"), std::string::npos)
        << "Shader missing water mask from baked albedo";
}

TEST_F(BlitShaderTest, WaterAffectsRoughness) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("mix(0.72, 0.18, waterMask)"), std::string::npos)
        << "Water should lower roughness for brighter highlights";
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
