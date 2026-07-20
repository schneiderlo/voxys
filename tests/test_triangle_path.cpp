// ═══════════════════════════════════════════════════════════════════════════════
// test_triangle_path.cpp - Unit tests for Triangle Path Renderer
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for CameraUniforms struct and TrianglePath renderer class.
// These tests verify the interface and configuration without requiring actual
// GPU access for most tests.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define private public
#include "render/triangle_path.hpp"
#undef private

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// CameraUniforms Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraUniformsTest, SizeIs448Bytes) {
    // Critical: Must match WGSL struct exactly
    EXPECT_EQ(sizeof(CameraUniforms), 448u);
}

TEST(CameraUniformsTest, DefaultConstruction) {
    CameraUniforms uniforms;
    
    // Check default terrain size
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 256.0f);
    EXPECT_FLOAT_EQ(uniforms.terrainSize.y, 256.0f);
    
    // Check default inverse terrain size
    EXPECT_FLOAT_EQ(uniforms.invTerrainSize.x, 1.0f / 256.0f);
    EXPECT_FLOAT_EQ(uniforms.invTerrainSize.y, 1.0f / 256.0f);
    
    // Check default metrics (heightScale, cellScale, step, fogDensity)
    EXPECT_FLOAT_EQ(uniforms.metrics.x, 500.0f);   // heightScale
    EXPECT_FLOAT_EQ(uniforms.metrics.y, 1.0f);     // cellScale
    EXPECT_FLOAT_EQ(uniforms.metrics.z, 1.0f);     // step
    EXPECT_FLOAT_EQ(uniforms.metrics.w, 0.0001f);  // fogDensity
    
    // Check default camera position (above terrain)
    EXPECT_FLOAT_EQ(uniforms.cameraPos.y, 100.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.w, 1.0f);  // Homogeneous coordinate

    EXPECT_FLOAT_EQ(uniforms.waterParams.x, -230.0f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.y, 1.0f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.z, 1.0f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.w, 0.05f);
    EXPECT_FLOAT_EQ(uniforms.waterColorA.w, 0.42f);
    EXPECT_FLOAT_EQ(uniforms.waterColorB.w, 30.0f);
    EXPECT_FLOAT_EQ(uniforms.waterMotion.x, 0.0f);
}

TEST(CameraUniformsTest, SetWaterTime) {
    CameraUniforms uniforms;
    uniforms.setWaterTime(12.5f);
    EXPECT_FLOAT_EQ(uniforms.waterMotion.x, 12.5f);
}

TEST(CameraUniformsTest, SetWater) {
    CameraUniforms uniforms;

    uniforms.setWater(false, -12.5f,
                      glm::vec3(0.2f, 0.7f, 0.8f),
                      glm::vec3(0.0f, 0.1f, 0.2f),
                      0.12f, 0.2f, 0.75f, 32.0f);

    EXPECT_FLOAT_EQ(uniforms.waterParams.x, -12.5f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.y, 0.0f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.z, 0.2f);
    EXPECT_FLOAT_EQ(uniforms.waterParams.w, 0.12f);
    EXPECT_FLOAT_EQ(uniforms.waterColorA.x, 0.2f);
    EXPECT_FLOAT_EQ(uniforms.waterColorA.y, 0.7f);
    EXPECT_FLOAT_EQ(uniforms.waterColorA.z, 0.8f);
    EXPECT_FLOAT_EQ(uniforms.waterColorA.w, 0.75f);
    EXPECT_FLOAT_EQ(uniforms.waterColorB.x, 0.0f);
    EXPECT_FLOAT_EQ(uniforms.waterColorB.y, 0.1f);
    EXPECT_FLOAT_EQ(uniforms.waterColorB.z, 0.2f);
    EXPECT_FLOAT_EQ(uniforms.waterColorB.w, 32.0f);
}

TEST(CameraUniformsTest, SetTerrain) {
    CameraUniforms uniforms;
    
    uniforms.setTerrain(8192, 4096, 1000.0f, 2.0f, 4.0f, 0.0005f);
    
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 8192.0f);
    EXPECT_FLOAT_EQ(uniforms.terrainSize.y, 4096.0f);
    EXPECT_FLOAT_EQ(uniforms.invTerrainSize.x, 1.0f / 8192.0f);
    EXPECT_FLOAT_EQ(uniforms.invTerrainSize.y, 1.0f / 4096.0f);
    EXPECT_FLOAT_EQ(uniforms.metrics.x, 1000.0f);  // heightScale
    EXPECT_FLOAT_EQ(uniforms.metrics.y, 2.0f);     // cellScale
    EXPECT_FLOAT_EQ(uniforms.metrics.z, 4.0f);     // step
    EXPECT_FLOAT_EQ(uniforms.metrics.w, 0.0005f);  // fogDensity
}

TEST(CameraUniformsTest, SetCamera) {
    CameraUniforms uniforms;
    
    glm::vec3 position(100.0f, 200.0f, 300.0f);
    glm::mat4 view = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 10000.0f);
    
    uniforms.setCamera(view, proj, position);
    
    // Check camera position
    EXPECT_FLOAT_EQ(uniforms.cameraPos.x, 100.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.y, 200.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.z, 300.0f);
    EXPECT_FLOAT_EQ(uniforms.cameraPos.w, 1.0f);
    
    // Check that viewProj is proj * view
    glm::mat4 expectedViewProj = proj * view;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_NEAR(uniforms.viewProj[i][j], expectedViewProj[i][j], 1e-5f);
        }
    }
    
    // Check that invViewProj is the inverse (use looser tolerance due to matrix inversion precision)
    glm::mat4 identity = uniforms.viewProj * uniforms.invViewProj;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            EXPECT_NEAR(identity[i][j], expected, 1e-3f);  // Looser tolerance for matrix product
        }
    }
    
    // Check that invView is the inverse of view
    glm::mat4 viewIdentity = view * uniforms.invView;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            EXPECT_NEAR(viewIdentity[i][j], expected, 1e-4f);
        }
    }
}

TEST(CameraUniformsTest, SetLightDirection) {
    CameraUniforms uniforms;
    
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 10.0f),  // Camera at z=10
        glm::vec3(0.0f, 0.0f, 0.0f),   // Looking at origin
        glm::vec3(0.0f, 1.0f, 0.0f)    // Up is Y
    );
    
    uniforms.setLightDirection(worldLightDir, view);
    
    // Light direction should be normalized
    float length = glm::length(glm::vec3(uniforms.lightDirVS));
    EXPECT_NEAR(length, 1.0f, 1e-5f);
    
    // W component should be ambient intensity (default 0.3)
    EXPECT_FLOAT_EQ(uniforms.lightDirVS.w, 0.3f);
    
    // Test with explicit ambient intensity
    uniforms.setLightDirection(worldLightDir, view, 0.5f);
    EXPECT_FLOAT_EQ(uniforms.lightDirVS.w, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TrianglePathConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TrianglePathConfigTest, DefaultValues) {
    auto config = TrianglePathConfig::defaults();
    
    EXPECT_EQ(config.shaderPath, "shaders/terrain.wgsl");
    EXPECT_EQ(config.colorFormat, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_EQ(config.depthFormat, WGPUTextureFormat_Depth32Float);
    EXPECT_FLOAT_EQ(config.heightScale, 500.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
    EXPECT_FLOAT_EQ(config.fogDensity, 0.0001f);
    EXPECT_EQ(config.lodStep, 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TrianglePath Constants Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TrianglePathConstantsTest, TileDimensions) {
    EXPECT_EQ(TrianglePath::TILE_QUADS, 32u);
    EXPECT_EQ(TrianglePath::TILE_VERTS, 33u);
    EXPECT_EQ(TrianglePath::BASE_VERTICES_PER_TILE, 1089u);
    EXPECT_EQ(TrianglePath::SKIRT_VERTICES_PER_TILE, 132u);
    EXPECT_EQ(TrianglePath::VERTICES_PER_TILE, 1221u);
    EXPECT_EQ(TrianglePath::BASE_INDICES_PER_TILE, 6144u);
    EXPECT_EQ(TrianglePath::SKIRT_INDICES_PER_TILE, 768u);
    EXPECT_EQ(TrianglePath::INDICES_PER_TILE, 6912u);
    EXPECT_LT(TrianglePath::VERTICES_PER_TILE, 65536u);
}

TEST(TrianglePathConstantsTest, IndicesPerTileCalculation) {
    // Base grid plus four 32-segment transition skirts.
    EXPECT_EQ(TrianglePath::INDICES_PER_TILE,
              (32u * 32u + 4u * 32u) * 6u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TrianglePath Class Tests (No GPU)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TrianglePathTest, DefaultConstruction) {
    TrianglePath renderer;
    
    EXPECT_FALSE(renderer.isInitialized());
    EXPECT_EQ(renderer.getUniformBuffer(), nullptr);
    EXPECT_EQ(renderer.getTileCount(), 0u);
    EXPECT_EQ(renderer.getTriangleCount(), 0u);
}

TEST(TrianglePathTest, MoveConstruction) {
    TrianglePath renderer1;
    TrianglePath renderer2(std::move(renderer1));
    
    // Both should be uninitialized (no GPU resources)
    EXPECT_FALSE(renderer1.isInitialized());
    EXPECT_FALSE(renderer2.isInitialized());
}

TEST(TrianglePathTest, MoveAssignment) {
    TrianglePath renderer1;
    TrianglePath renderer2;
    
    renderer2 = std::move(renderer1);
    
    EXPECT_FALSE(renderer1.isInitialized());
    EXPECT_FALSE(renderer2.isInitialized());
}

TEST(TrianglePathTest, InitWithNullDevice) {
    TrianglePath renderer;
    
    // Should fail gracefully with null device
    EXPECT_FALSE(renderer.init(nullptr, nullptr));
    EXPECT_FALSE(renderer.isInitialized());
}

TEST(TrianglePathTest, GetUniformsDefault) {
    TrianglePath renderer;
    
    const auto& uniforms = renderer.getUniforms();
    
    // Should have default uniforms
    EXPECT_FLOAT_EQ(uniforms.terrainSize.x, 256.0f);
    EXPECT_FLOAT_EQ(uniforms.metrics.x, 500.0f);  // heightScale
}

TEST(TrianglePathTest, LODStepDefault) {
    TrianglePath renderer;
    
    // Default LOD step should be 1 (full detail)
    EXPECT_EQ(renderer.getLODStep(), 1u);
}

TEST(TrianglePathTest, SetLODStep) {
    TrianglePath renderer;
    
    renderer.setLODStep(4);
    EXPECT_EQ(renderer.getLODStep(), 4u);
    
    // Check that uniforms were updated
    const auto& uniforms = renderer.getUniforms();
    EXPECT_FLOAT_EQ(uniforms.metrics.z, 4.0f);
}

TEST(TrianglePathTest, SetLODStepMinimum) {
    TrianglePath renderer;
    
    // Setting to 0 should clamp to 1
    renderer.setLODStep(0);
    EXPECT_EQ(renderer.getLODStep(), 1u);
}

TEST(TrianglePathTest, ShutdownWithoutInit) {
    TrianglePath renderer;
    
    // Should not crash
    renderer.shutdown();
    EXPECT_FALSE(renderer.isInitialized());
}

namespace {

template <typename T>
T fakeHandle(uintptr_t value) {
    return reinterpret_cast<T>(value);
}

struct FakeComputeHandles {
    WGPUShaderModule computeModule = fakeHandle<WGPUShaderModule>(0x101);
    WGPUPipelineLayout computePipelineLayout = fakeHandle<WGPUPipelineLayout>(0x102);
    WGPUComputePipeline computePipeline = fakeHandle<WGPUComputePipeline>(0x103);
    WGPUBindGroupLayout computeBindGroupLayout = fakeHandle<WGPUBindGroupLayout>(0x104);
    WGPUBindGroup computeBindGroup = fakeHandle<WGPUBindGroup>(0x105);
    WGPUBuffer indirectBuffer = fakeHandle<WGPUBuffer>(0x106);
    WGPUBuffer visibleIndicesBuffer = fakeHandle<WGPUBuffer>(0x107);
    WGPUBuffer indirectTemplateBuffer = fakeHandle<WGPUBuffer>(0x108);
    WGPUBuffer tileBoundsBuffer = fakeHandle<WGPUBuffer>(0x109);
};

void installFakeComputeHandles(TrianglePath& renderer, const FakeComputeHandles& handles) {
    renderer.computeModule_ = handles.computeModule;
    renderer.computePipelineLayout_ = handles.computePipelineLayout;
    renderer.computePipeline_ = handles.computePipeline;
    renderer.computeBindGroupLayout_ = handles.computeBindGroupLayout;
    renderer.computeBindGroup_ = handles.computeBindGroup;
    renderer.indirectBuffer_ = handles.indirectBuffer;
    renderer.visibleIndicesBuffer_ = handles.visibleIndicesBuffer;
    renderer.indirectTemplateBuffer_ = handles.indirectTemplateBuffer;
    renderer.tileBoundsBuffer_ = handles.tileBoundsBuffer;
}

void expectComputeHandles(const TrianglePath& renderer, const FakeComputeHandles& handles) {
    EXPECT_EQ(renderer.computeModule_, handles.computeModule);
    EXPECT_EQ(renderer.computePipelineLayout_, handles.computePipelineLayout);
    EXPECT_EQ(renderer.computePipeline_, handles.computePipeline);
    EXPECT_EQ(renderer.computeBindGroupLayout_, handles.computeBindGroupLayout);
    EXPECT_EQ(renderer.computeBindGroup_, handles.computeBindGroup);
    EXPECT_EQ(renderer.indirectBuffer_, handles.indirectBuffer);
    EXPECT_EQ(renderer.visibleIndicesBuffer_, handles.visibleIndicesBuffer);
    EXPECT_EQ(renderer.indirectTemplateBuffer_, handles.indirectTemplateBuffer);
    EXPECT_EQ(renderer.tileBoundsBuffer_, handles.tileBoundsBuffer);
}

void expectComputeHandlesCleared(const TrianglePath& renderer) {
    EXPECT_EQ(renderer.computeModule_, nullptr);
    EXPECT_EQ(renderer.computePipelineLayout_, nullptr);
    EXPECT_EQ(renderer.computePipeline_, nullptr);
    EXPECT_EQ(renderer.computeBindGroupLayout_, nullptr);
    EXPECT_EQ(renderer.computeBindGroup_, nullptr);
    EXPECT_EQ(renderer.indirectBuffer_, nullptr);
    EXPECT_EQ(renderer.visibleIndicesBuffer_, nullptr);
    EXPECT_EQ(renderer.indirectTemplateBuffer_, nullptr);
    EXPECT_EQ(renderer.tileBoundsBuffer_, nullptr);
}

void clearFakeComputeHandles(TrianglePath& renderer) {
    renderer.computeModule_ = nullptr;
    renderer.computePipelineLayout_ = nullptr;
    renderer.computePipeline_ = nullptr;
    renderer.computeBindGroupLayout_ = nullptr;
    renderer.computeBindGroup_ = nullptr;
    renderer.indirectBuffer_ = nullptr;
    renderer.visibleIndicesBuffer_ = nullptr;
    renderer.indirectTemplateBuffer_ = nullptr;
    renderer.tileBoundsBuffer_ = nullptr;
}

} // namespace

TEST(TrianglePathLifetimeTest, MoveConstructorTransfersComputeCullingResources) {
    TrianglePath source;
    const FakeComputeHandles handles;
    installFakeComputeHandles(source, handles);

    TrianglePath moved(std::move(source));

    expectComputeHandles(moved, handles);
    expectComputeHandlesCleared(source);

    clearFakeComputeHandles(moved);
}

TEST(TrianglePathLifetimeTest, MoveAssignmentTransfersComputeCullingResources) {
    TrianglePath source;
    const FakeComputeHandles handles;
    installFakeComputeHandles(source, handles);

    TrianglePath moved;
    moved = std::move(source);

    expectComputeHandles(moved, handles);
    expectComputeHandlesCleared(source);

    clearFakeComputeHandles(moved);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tile Count Calculation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TileCountTest, SmallTerrain) {
    // For a 64x64 terrain with step=1, 63 quads need two 32-quad patches.
    
    // This tests the formula:
    // quads = ((terrainSize - 1) + step - 1) / step
    // tiles = (quads + TILE_QUADS - 1) / TILE_QUADS
    
    uint32_t terrainSize = 64;
    uint32_t step = 1;
    uint32_t quads = ((terrainSize - 1) + step - 1) / step;
    uint32_t tiles = (quads + TrianglePath::TILE_QUADS - 1) /
                     TrianglePath::TILE_QUADS;
    
    EXPECT_EQ(quads, 63u);
    EXPECT_EQ(tiles, 2u);
}

TEST(TileCountTest, LargeTerrain) {
    // For 8192x8192 terrain with step=1:
    // quadsX = (8192-1)/1 = 8191 quads
    // tilesX = ceil(8191/32) = 256 tiles
    
    uint32_t terrainSize = 8192;
    uint32_t step = 1;
    uint32_t quads = ((terrainSize - 1) + step - 1) / step;
    uint32_t tiles = (quads + TrianglePath::TILE_QUADS - 1) /
                     TrianglePath::TILE_QUADS;
    
    EXPECT_EQ(quads, 8191u);
    EXPECT_EQ(tiles, 256u);
}

TEST(TileCountTest, WithLODStep) {
    // For 8192x8192 terrain with step=8:
    // quadsX = (8192-1+8-1)/8 = 1024 quads
    // tilesX = 1024/32 = 32 tiles
    
    uint32_t terrainSize = 8192;
    uint32_t step = 8;
    uint32_t quads = ((terrainSize - 1) + step - 1) / step;
    uint32_t tiles = (quads + TrianglePath::TILE_QUADS - 1) /
                     TrianglePath::TILE_QUADS;
    
    EXPECT_EQ(quads, 1024u);
    EXPECT_EQ(tiles, 32u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Triangle Count Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TriangleCountTest, PerTile) {
    uint32_t trianglesPerTile = TrianglePath::TILE_QUADS *
                                TrianglePath::TILE_QUADS * 2;
    EXPECT_EQ(trianglesPerTile, 2048u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Index Buffer Pattern Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IndexBufferTest, FirstQuadIndices) {
    // For a 33x33 vertex grid, the first quad uses vertices 0, 1, 33, 34.
    uint32_t TILE_VERTS = TrianglePath::TILE_VERTS;
    uint32_t topLeft = 0;
    uint32_t topRight = topLeft + 1;
    uint32_t bottomLeft = topLeft + TILE_VERTS;
    uint32_t bottomRight = bottomLeft + 1;
    
    EXPECT_EQ(topLeft, 0u);
    EXPECT_EQ(topRight, 1u);
    EXPECT_EQ(bottomLeft, 33u);
    EXPECT_EQ(bottomRight, 34u);
}

TEST(IndexBufferTest, LastQuadIndices) {
    // Last quad in a 32x32 grid is at position (31, 31).
    uint32_t TILE_VERTS = TrianglePath::TILE_VERTS;
    uint32_t x = 31, y = 31;
    uint32_t topLeft = y * TILE_VERTS + x;
    uint32_t topRight = topLeft + 1;
    uint32_t bottomLeft = topLeft + TILE_VERTS;
    uint32_t bottomRight = bottomLeft + 1;
    
    EXPECT_EQ(topLeft, 1054u);
    EXPECT_EQ(topRight, 1055u);
    EXPECT_EQ(bottomLeft, 1087u);
    EXPECT_EQ(bottomRight, 1088u);
    
    // Verify bottomRight is the last vertex
    EXPECT_EQ(bottomRight, TILE_VERTS * TILE_VERTS - 1);
}

} // namespace voxy::render
