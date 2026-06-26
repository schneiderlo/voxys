// ═══════════════════════════════════════════════════════════════════════════════
// test_shader_raycast.cpp - Unit tests for terrain_raycast.wgsl shader
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the compute ray-caster shader. These tests verify:
//   - Shader file exists and can be read
//   - Shader source has required entry points and bindings
//   - Shader has hierarchical DDA algorithm components
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

class RaycastShaderTest : public ::testing::Test {
protected:
    void SetUp() override {
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

TEST_F(RaycastShaderTest, ShaderFileExists) {
    ASSERT_FALSE(shaderPath_.empty()) 
        << "terrain_raycast.wgsl not found in any of the expected locations";
    EXPECT_TRUE(std::filesystem::exists(shaderPath_))
        << "Shader file does not exist at: " << shaderPath_;
}

TEST_F(RaycastShaderTest, ShaderFileNotEmpty) {
    ASSERT_FALSE(shaderSource_.empty()) 
        << "Shader source is empty or could not be read";
    EXPECT_GT(shaderSource_.size(), 100u) 
        << "Shader source seems too short";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasCameraUniformsStruct) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct CameraUniforms"), std::string::npos)
        << "Shader missing CameraUniforms struct";
}

TEST_F(RaycastShaderTest, HasComputeShaderEntryPoint) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@compute"), std::string::npos)
        << "Shader missing @compute attribute";
    EXPECT_NE(shaderSource_.find("@workgroup_size(8, 8, 1)"), std::string::npos)
        << "Shader missing workgroup_size declaration";
    EXPECT_NE(shaderSource_.find("fn main("), std::string::npos)
        << "Shader missing compute shader entry point 'main'";
}

TEST_F(RaycastShaderTest, HasUniformBindings) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("@group(0) @binding(0)"), std::string::npos)
        << "Shader missing camera uniform binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(1)"), std::string::npos)
        << "Shader missing heightTex binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(2)"), std::string::npos)
        << "Shader missing outDepth binding";
    EXPECT_NE(shaderSource_.find("@group(0) @binding(3)"), std::string::npos)
        << "Shader missing outShadow binding";
}

TEST_F(RaycastShaderTest, HasHeightTexBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var heightTex : texture_2d<u32>"), std::string::npos)
        << "Shader missing heightTex texture declaration";
}

TEST_F(RaycastShaderTest, HasOutDepthBinding) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("var outDepth : texture_storage_2d<r32float, write>"), std::string::npos)
        << "Shader missing outDepth storage texture declaration";
    EXPECT_NE(shaderSource_.find("var outShadow : texture_storage_2d<r32float, write>"), std::string::npos)
        << "Shader missing outShadow storage texture declaration";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Coordinate Space Conversion Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasToHeightmapCoordinateFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn toHeightmapCoordinate("), std::string::npos)
        << "Shader missing toHeightmapCoordinate helper function";
}

TEST_F(RaycastShaderTest, HasToHeightmapScaleFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn toHeightmapScale("), std::string::npos)
        << "Shader missing toHeightmapScale helper function";
}

// ═══════════════════════════════════════════════════════════════════════════════
// AABB Intersection Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasAABBIntersectionFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn intersectAabb("), std::string::npos)
        << "Shader missing intersectAabb function";
}

TEST_F(RaycastShaderTest, AABBIntersectionUsesInverseDirection) {
    ASSERT_FALSE(shaderSource_.empty());
    // AABB intersection should compute inverse direction for efficiency
    // Updated to check for robust division (using epsilon/sign)
    EXPECT_NE(shaderSource_.find("1.0 / (dir"), std::string::npos)
        << "AABB intersection should compute robust inverse ray direction";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ray Generation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasRayDirFromPixelFunction) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("fn rayDirFromPixel("), std::string::npos)
        << "Shader missing rayDirFromPixel function";
}

TEST_F(RaycastShaderTest, RayGenerationUsesInverseViewProj) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("invViewProj"), std::string::npos)
        << "Ray generation should use inverse view-projection matrix";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hierarchical DDA Traversal Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasMipLevelVariable) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("mipLevel"), std::string::npos)
        << "Shader missing mipLevel variable for hierarchical traversal";
}

TEST_F(RaycastShaderTest, StartsAtCoarseMipLevel) {
    ASSERT_FALSE(shaderSource_.empty());
    // Should start at mip level 7 for 8192×8192 terrain
    EXPECT_NE(shaderSource_.find("mipLevel : u32 = 7u"), std::string::npos)
        << "Shader should initialize mipLevel to 7 for hierarchical traversal";
}

TEST_F(RaycastShaderTest, HasDDAStepVariables) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("stepX"), std::string::npos)
        << "Shader missing stepX for DDA";
    EXPECT_NE(shaderSource_.find("stepZ"), std::string::npos)
        << "Shader missing stepZ for DDA";
    EXPECT_NE(shaderSource_.find("tMaxX"), std::string::npos)
        << "Shader missing tMaxX for DDA";
    EXPECT_NE(shaderSource_.find("tMaxZ"), std::string::npos)
        << "Shader missing tMaxZ for DDA";
    EXPECT_NE(shaderSource_.find("tDeltaX"), std::string::npos)
        << "Shader missing tDeltaX for DDA";
    EXPECT_NE(shaderSource_.find("tDeltaZ"), std::string::npos)
        << "Shader missing tDeltaZ for DDA";
}

TEST_F(RaycastShaderTest, HasMipLevelTransitions) {
    ASSERT_FALSE(shaderSource_.empty());
    // Check for mip level descent (finer)
    EXPECT_NE(shaderSource_.find("mipLevel--"), std::string::npos)
        << "Shader missing mip level descent";
    // Check for mip level ascent (coarser)
    EXPECT_NE(shaderSource_.find("mipLevel++"), std::string::npos)
        << "Shader missing mip level ascent";
}

TEST_F(RaycastShaderTest, HasLODTermination) {
    ASSERT_FALSE(shaderSource_.empty());
    // LOD termination: f32(mipLevel) <= max(log(t) - 6.0, 0.0)
    EXPECT_NE(shaderSource_.find("log(t)"), std::string::npos)
        << "Shader missing LOD termination based on distance";
}

TEST_F(RaycastShaderTest, HasLevelUpCheck) {
    ASSERT_FALSE(shaderSource_.empty());
    // Level-up threshold: 128 << mipLevel
    EXPECT_NE(shaderSource_.find("levelUpHeight"), std::string::npos)
        << "Shader missing level-up height threshold";
}

TEST_F(RaycastShaderTest, UsesTextureLoadWithMipLevel) {
    ASSERT_FALSE(shaderSource_.empty());
    // textureLoad should use mipLevel parameter
    EXPECT_NE(shaderSource_.find("textureLoad(heightTex"), std::string::npos)
        << "Shader should sample heightmap using textureLoad";
    EXPECT_NE(shaderSource_.find("i32(mipLevel)"), std::string::npos)
        << "Shader should use mipLevel in textureLoad";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Output Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RaycastShaderTest, HasTextureStoreOutput) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("textureStore(outDepth"), std::string::npos)
        << "Shader missing textureStore for depth output";
}

TEST_F(RaycastShaderTest, OutputsSkyMarker) {
    ASSERT_FALSE(shaderSource_.empty());
    // Sky should be marked with negative depth (-1.0)
    EXPECT_NE(shaderSource_.find("-1.0"), std::string::npos)
        << "Shader should output -1.0 for sky pixels";
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Compilation Test (requires WebGPU context)
// ═══════════════════════════════════════════════════════════════════════════════

class RaycastShaderGPUTest : public RaycastShaderTest {
protected:
    void SetUp() override {
        RaycastShaderTest::SetUp();
        
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

TEST_F(RaycastShaderGPUTest, ShaderCompilesOnGPU) {
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
        "terrain_raycast.wgsl"
    );
    
    EXPECT_NE(shaderModule, nullptr) 
        << "Failed to compile terrain_raycast.wgsl shader on GPU";
    
    // Clean up
    if (shaderModule) {
        wgpuShaderModuleRelease(shaderModule);
    }
}

} // namespace voxy
