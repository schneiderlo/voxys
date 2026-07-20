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
        << "Shader must consume the spectral/long-wave normal packed by the intersection pass";
    EXPECT_NE(shaderSource_.find("dielectricFresnel"), std::string::npos)
        << "Shader missing exact unpolarized dielectric Fresnel";
    EXPECT_NE(shaderSource_.find("fn oceanIor()"), std::string::npos)
        << "Shader missing runtime water index of refraction";
    EXPECT_NE(shaderSource_.find("camera.waterOptics.x"), std::string::npos)
        << "Shader index of refraction is not runtime-controlled";
    EXPECT_NE(shaderSource_.find("fn oceanAbsorption()"), std::string::npos)
        << "Shader missing runtime Beer-Lambert absorption coefficient";
    EXPECT_NE(shaderSource_.find("oceanFoamTex"), std::string::npos)
        << "Shader missing the procedural scalar foam mask";
    EXPECT_NE(shaderSource_.find("threshold = 1.0 - oceanFoamCoverage()"),
              std::string::npos)
        << "Shader missing runtime foam threshold equation";
    EXPECT_NE(shaderSource_.find("acesFilmic"), std::string::npos)
        << "Shader missing ACES output transform";
    EXPECT_NE(shaderSource_.find("totalInternalReflection"), std::string::npos)
        << "Shader missing underside total internal reflection";
    EXPECT_NE(shaderSource_.find("applyUnderwaterMedium"), std::string::npos)
        << "Shader missing underwater absorption and sun shafts";
    EXPECT_NE(shaderSource_.find("underwaterDistortionUv"), std::string::npos)
        << "Shader missing underwater distortion";
    EXPECT_NE(shaderSource_.find("fn fsBackground("), std::string::npos)
        << "Shader missing the linear-HDR opaque scene pass";
    EXPECT_NE(shaderSource_.find("fn fsCachedOpaque("), std::string::npos)
        << "Shader missing the cached opaque HDR/depth pass used before geometry water";
    EXPECT_NE(shaderSource_.find("fn fsCachedOpaqueColor("), std::string::npos)
        << "Shader missing the exact color-only cached presentation pass";
    EXPECT_NE(shaderSource_.find("backgroundDepthTex"), std::string::npos)
        << "Shader missing opaque depth for refraction rejection";
}

TEST_F(BlitShaderTest, GeometryClipmapCarriesTheCompleteOceanMaterial) {
    const auto clipmapPath = shaderPath_.parent_path() / "water_clipmap.wgsl";
    std::ifstream clipmapFile(clipmapPath);
    ASSERT_TRUE(clipmapFile.is_open())
        << "water_clipmap.wgsl not found at " << clipmapPath;
    std::stringstream clipmapBuffer;
    clipmapBuffer << clipmapFile.rdbuf();
    const std::string source = clipmapBuffer.str();

    EXPECT_NE(source.find("@vertex\nfn vs("), std::string::npos)
        << "Ocean clipmap must displace real vertices";
    EXPECT_NE(source.find("@fragment\nfn fs("), std::string::npos)
        << "Ocean clipmap must shade its own fragments";
    EXPECT_NE(source.find("fn shadeWaterFragment("), std::string::npos)
        << "Depth and color-only ocean pipelines must share one material";
    EXPECT_NE(source.find("@fragment\nfn fsColor("), std::string::npos)
        << "Ocean clipmap is missing the color-only full-quality entry point";
    EXPECT_NE(source.find(
                  "@binding(15) var displacementTexture : texture_2d_array<f32>"),
              std::string::npos)
        << "Ocean clipmap must consume live spectral displacement";
    EXPECT_NE(source.find(
                  "sampleDisplacement(base.xz, camera.waterSpectrum.x, 0)"),
              std::string::npos)
        << "Broad FFT cascade is not applied to clipmap geometry";
    EXPECT_NE(source.find(
                  "sampleDisplacement(base.xz, camera.waterSpectrum.y, 1)"),
              std::string::npos)
        << "Detail FFT cascade is not applied to clipmap geometry";
    EXPECT_NE(source.find("let swell = longWaves(base.xz, strength)"),
              std::string::npos)
        << "Analytic long swells are not applied to clipmap geometry";
    EXPECT_NE(source.find("dielectricFresnel"), std::string::npos);
    EXPECT_NE(source.find("fn oceanIor()"), std::string::npos);
    EXPECT_NE(source.find("exp(-oceanAbsorption() * thickness)"),
              std::string::npos)
        << "Geometry water is missing Beer-Lambert transmission";
    EXPECT_NE(source.find("underwaterDistortionUv"), std::string::npos);
    EXPECT_NE(source.find("material.gba"), std::string::npos)
        << "Generated seabed material is not used by refraction";
    EXPECT_NE(source.find("threshold = 1.0 - oceanFoamCoverage()"),
              std::string::npos);
    EXPECT_NE(source.find("struct FragmentOutput"), std::string::npos);
    EXPECT_NE(source.find("output.linearDepth = distanceToCamera"),
              std::string::npos)
        << "Ocean clipmap must update shared linear depth for later passes";
}

TEST_F(BlitShaderTest, UnderwaterParticlesAreRealDepthOccludedBillboards) {
    const auto particlePath =
        shaderPath_.parent_path() / "underwater_particles.wgsl";
    std::ifstream particleFile(particlePath);
    ASSERT_TRUE(particleFile.is_open())
        << "underwater_particles.wgsl not found at " << particlePath;
    std::stringstream particleBuffer;
    particleBuffer << particleFile.rdbuf();
    const std::string source = particleBuffer.str();
    EXPECT_NE(source.find("@builtin(instance_index)"), std::string::npos)
        << "Underwater particles must be instanced billboards";
    EXPECT_NE(source.find("var<storage, read> particles"), std::string::npos)
        << "Underwater particle positions must come from a camera-local shell";
    EXPECT_NE(source.find("PARTICLE_NEAR : f32 = 9.0"), std::string::npos);
    EXPECT_NE(source.find("PARTICLE_FAR : f32 = 209.0"), std::string::npos);
    EXPECT_NE(source.find("textureLoad(rayDepth"), std::string::npos)
        << "Particles must be occluded by the ray-rendered scene";
    EXPECT_NE(source.find("radialFade"), std::string::npos)
        << "Particle fragments must have a soft radial profile";
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
    // The complete static HDR environment is baked once by sky_lut.wgsl.
    EXPECT_NE(shaderSource_.find("sampleSkyLUT"), std::string::npos)
        << "Shader missing sky LUT sample";
    EXPECT_NE(shaderSource_.find("skyColor"), std::string::npos)
        << "Shader missing skyColor variable";
    EXPECT_NE(shaderSource_.find("visibleSkyRadiance"), std::string::npos)
        << "Shader missing visible HDR sky composition";
    EXPECT_NE(shaderSource_.find("environmentUv"), std::string::npos)
        << "Shader missing full-sphere environment mapping";
}

TEST_F(BlitShaderTest, ProceduralEnvironmentLivesInSkyLut) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_EQ(shaderSource_.find("simplexNoise2D"), std::string::npos)
        << "Procedural sky noise must not run in the per-pixel blit";

    // The LUT shader itself must provide the atmosphere, clouds, and HDR
    // emitter used by both visible sky and water reflections.
    const auto lutPath = shaderPath_.parent_path() / "sky_lut.wgsl";
    std::ifstream lutFile(lutPath);
    ASSERT_TRUE(lutFile.is_open()) << "sky_lut.wgsl not found at " << lutPath;
    std::stringstream lutBuffer;
    lutBuffer << lutFile.rdbuf();
    const std::string lutSource = lutBuffer.str();
    EXPECT_NE(lutSource.find("fn skyRadiance"), std::string::npos)
        << "sky_lut.wgsl missing procedural sky radiance";
    EXPECT_NE(lutSource.find("cloudNoise"), std::string::npos)
        << "sky_lut.wgsl missing procedural clouds";
    EXPECT_NE(lutSource.find("corona"), std::string::npos)
        << "sky_lut.wgsl missing the HDR directional emitter";
    EXPECT_NE(lutSource.find("equirectangularDirection"), std::string::npos)
        << "sky_lut.wgsl missing full-sphere mapping";
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
    EXPECT_NE(shaderSource_.find("fn atmosphericFog("), std::string::npos)
        << "Shader missing atmospheric fog";
    EXPECT_NE(shaderSource_.find("camera.metrics.w"), std::string::npos)
        << "Shader fog density is not runtime-controlled";
    EXPECT_NE(shaderSource_.find("camera.fogColor.rgb"), std::string::npos)
        << "Shader fog colour is not runtime-controlled";
}

TEST_F(BlitShaderTest, UsesExponentialRuntimeFog) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find(
                  "1.0 - exp(-max(camera.metrics.w, 0.0) * distanceToCamera)"),
              std::string::npos)
        << "Shader should use exponential runtime fog";
}

TEST_F(BlitShaderTest, UnderwaterUsesBeerLambert) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find(
                  "exp(-oceanAbsorption() * pathLength)"),
              std::string::npos)
        << "Submerged geometry must use the runtime Beer-Lambert medium";
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

    const auto particlePath =
        shaderPath_.parent_path() / "underwater_particles.wgsl";
    std::ifstream particleFile(particlePath);
    ASSERT_TRUE(particleFile.is_open());
    std::stringstream particleBuffer;
    particleBuffer << particleFile.rdbuf();
    auto particleModule = gpu::createShaderModule(
        device, particleBuffer.str(), "underwater_particles.wgsl");
    EXPECT_NE(particleModule, nullptr)
        << "Failed to compile underwater particle shader on GPU";
    if (particleModule) {
        wgpuShaderModuleRelease(particleModule);
    }

    const auto clipmapPath =
        shaderPath_.parent_path() / "water_clipmap.wgsl";
    std::ifstream clipmapFile(clipmapPath);
    ASSERT_TRUE(clipmapFile.is_open());
    std::stringstream clipmapBuffer;
    clipmapBuffer << clipmapFile.rdbuf();
    auto clipmapModule = gpu::createShaderModule(
        device, clipmapBuffer.str(), "water_clipmap.wgsl");
    EXPECT_NE(clipmapModule, nullptr)
        << "Failed to compile geometry ocean clipmap shader on GPU";
    if (clipmapModule) {
        wgpuShaderModuleRelease(clipmapModule);
    }
}

} // namespace voxy
