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
        << "Shader missing materialTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(4)"), std::string::npos)
        << "Shader missing terrainTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(5)"), std::string::npos)
        << "Shader missing lightmapTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(6)"), std::string::npos)
        << "Shader missing terrainSampler binding";
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

TEST_F(BlitShaderTest, HasMaterialTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var materialTex : texture_2d<f32>"), std::string::npos)
        << "Shader missing materialTex texture declaration";
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

TEST_F(BlitShaderTest, HasWaterShading) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("MATERIAL_WATER"), std::string::npos)
        << "Shader missing water material path";
    EXPECT_NE(shaderSource_.find("fn shadeOcean("), std::string::npos)
        << "Shader missing ocean material";
    EXPECT_NE(shaderSource_.find("let waterColor = shadeOcean("),
              std::string::npos)
        << "Fragment paths must call the ocean material";
    EXPECT_NE(shaderSource_.find("waterWaveNormal"), std::string::npos)
        << "Shader must consume the FFT/coastal slope packed by the intersection pass";
    EXPECT_NE(shaderSource_.find("dielectricFresnel"), std::string::npos)
        << "Shader missing exact unpolarized dielectric Fresnel";
    EXPECT_NE(shaderSource_.find("OCEAN_IOR : f32 = 1.31"), std::string::npos)
        << "Shader missing water index of refraction";
    EXPECT_NE(shaderSource_.find("OCEAN_ABSORPTION"), std::string::npos)
        << "Shader missing Beer-Lambert absorption coefficient";
    EXPECT_NE(shaderSource_.find("oceanFoamTex"), std::string::npos)
        << "Shader missing the procedural scalar foam mask";
    EXPECT_NE(shaderSource_.find("threshold = 1.0 - OCEAN_FOAM_COVERAGE"),
              std::string::npos)
        << "Shader missing foam threshold equation";
    EXPECT_NE(shaderSource_.find("acesFilmic"), std::string::npos)
        << "Shader missing ACES output transform";
    EXPECT_NE(shaderSource_.find("totalInternalReflection"), std::string::npos)
        << "Shader missing underside total internal reflection";
    EXPECT_NE(shaderSource_.find("applyUnderwaterMedium"), std::string::npos)
        << "Shader missing underwater absorption and sun shafts";
    EXPECT_NE(shaderSource_.find("underwaterDistortionUv"), std::string::npos)
        << "Shader missing underwater distortion";
}

TEST_F(BlitShaderTest, OceanMaterialIsLive) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_EQ(shaderSource_.find("let waterColor = shadeWater("),
              std::string::npos)
        << "No fragment entry point may invoke the old SSR/GGX material";
    EXPECT_NE(shaderSource_.find("let environment = sampleWaterEnvironment"),
              std::string::npos)
        << "Ocean environment reflection is not live";
}

TEST_F(BlitShaderTest, OceanRoughReflectionsHaveARealMipChain) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find(
                  "textureSampleLevel(skyLUT, terrainSampler, uv, lod)"),
              std::string::npos)
        << "Ocean roughness must select the baked environment mip";

    const auto mipPath = shaderPath_.parent_path() / "sky_lut_mip.wgsl";
    std::ifstream mipFile(mipPath);
    ASSERT_TRUE(mipFile.is_open())
        << "sky_lut_mip.wgsl not found at " << mipPath;
    std::stringstream mipBuffer;
    mipBuffer << mipFile.rdbuf();
    const std::string mipSource = mipBuffer.str();
    EXPECT_NE(mipSource.find("textureLoad(sourceMip"), std::string::npos)
        << "Sky mip shader must read the preceding roughness level";
    EXPECT_NE(mipSource.find("textureStore(destinationMip"), std::string::npos)
        << "Sky mip shader must write the next roughness level";
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
    // The static parts of the sky (scattering, gradient, clouds) are baked
    // into a LUT by sky_lut.wgsl; the blit samples it and adds the sun disc.
    EXPECT_NE(shaderSource_.find("sampleSkyLUT"), std::string::npos)
        << "Shader missing sky LUT sample";
    EXPECT_NE(shaderSource_.find("skyColor"), std::string::npos)
        << "Shader missing skyColor variable";
    EXPECT_NE(shaderSource_.find("sunDisc"), std::string::npos)
        << "Shader missing analytic sun disc";
}

TEST_F(BlitShaderTest, AtmosphericScatteringLivesInSkyLut) {
    ASSERT_FALSE(shaderSource_.empty());
    // Per-pixel scattering was moved to the baked LUT; recomputing it every
    // frame in the blit pass would be a performance regression.
    EXPECT_EQ(shaderSource_.find("rayleigh"), std::string::npos)
        << "Rayleigh scattering should be baked in sky_lut.wgsl, not "
           "recomputed per pixel in the blit pass";

    // The LUT shader itself must implement the scattering.
    const auto lutPath = shaderPath_.parent_path() / "sky_lut.wgsl";
    std::ifstream lutFile(lutPath);
    ASSERT_TRUE(lutFile.is_open()) << "sky_lut.wgsl not found at " << lutPath;
    std::stringstream lutBuffer;
    lutBuffer << lutFile.rdbuf();
    const std::string lutSource = lutBuffer.str();
    EXPECT_NE(lutSource.find("rayleigh"), std::string::npos)
        << "sky_lut.wgsl missing Rayleigh scattering";
    EXPECT_NE(lutSource.find("mie"), std::string::npos)
        << "sky_lut.wgsl missing Mie scattering";
    EXPECT_NE(lutSource.find("cloudNoise"), std::string::npos)
        << "sky_lut.wgsl missing procedural clouds";
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
    EXPECT_NE(shaderSource_.find("OCEAN_FOG_NEAR"), std::string::npos)
        << "Shader missing ocean fog near distance";
    EXPECT_NE(shaderSource_.find("OCEAN_FOG_FAR"), std::string::npos)
        << "Shader missing ocean fog far distance";
    EXPECT_NE(shaderSource_.find("OCEAN_FOG_COLOR"), std::string::npos)
        << "Shader missing ocean fog color";
}

TEST_F(BlitShaderTest, UsesSourceSmoothstepFog) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find(
                  "smoothstep(OCEAN_FOG_NEAR, OCEAN_FOG_FAR"),
              std::string::npos)
        << "Shader should use smoothstep ocean fog";
}

TEST_F(BlitShaderTest, UnderwaterUsesBeerLambert) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find(
                  "exp(-OCEAN_ABSORPTION * pathLength)"),
              std::string::npos)
        << "Submerged geometry must use the source Beer-Lambert medium";
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
