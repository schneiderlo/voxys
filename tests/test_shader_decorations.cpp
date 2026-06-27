// ═══════════════════════════════════════════════════════════════════════════════
// test_shader_decorations.cpp - Unit tests for decorations.wgsl shader
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace voxy {

class DecorationShaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<std::filesystem::path> searchPaths = {
            "shaders/decorations.wgsl",
            "../shaders/decorations.wgsl",
            "../../shaders/decorations.wgsl",
            "../../../shaders/decorations.wgsl",
        };

        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                shaderPath_ = path;
                break;
            }
        }

        if (!shaderPath_.empty()) {
            std::ifstream file(shaderPath_);
            std::stringstream buffer;
            buffer << file.rdbuf();
            shaderSource_ = buffer.str();
        }
    }

    std::filesystem::path shaderPath_;
    std::string shaderSource_;
};

TEST_F(DecorationShaderTest, ShaderFileExists) {
    ASSERT_FALSE(shaderPath_.empty()) << "decorations.wgsl not found";
    EXPECT_TRUE(std::filesystem::exists(shaderPath_));
}

TEST_F(DecorationShaderTest, UsesInstancedDecorationStorage) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct DecorationInstance"), std::string::npos);
    EXPECT_NE(shaderSource_.find("variant : vec4<f32>"), std::string::npos);
    EXPECT_NE(shaderSource_.find("var<storage, read> decorations"), std::string::npos);
    EXPECT_NE(shaderSource_.find("@builtin(instance_index)"), std::string::npos);
}

TEST_F(DecorationShaderTest, BuildsProceduralGroundQuadsAndTreeVolumes) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("vertexIndex / 6u"), std::string::npos);
    EXPECT_NE(shaderSource_.find("quadIndex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("localIndex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("TREE_CUBOID_COUNT"), std::string::npos);
    EXPECT_NE(shaderSource_.find("treeVolumeVertex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("GROUND_DECORATION_VERTICES"), std::string::npos);
}

TEST_F(DecorationShaderTest, SupportsTreeKindSpecificShapes) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("KIND_PINE"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_JUNGLE"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_ACACIA"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_CYPRESS"), std::string::npos);
    EXPECT_NE(shaderSource_.find("isTreeKind"), std::string::npos);
    EXPECT_NE(shaderSource_.find("isPine"), std::string::npos);
    EXPECT_NE(shaderSource_.find("coneWidth"), std::string::npos);
    EXPECT_NE(shaderSource_.find("umbrella"), std::string::npos);
    EXPECT_NE(shaderSource_.find("columnWidth"), std::string::npos);
    EXPECT_NE(shaderSource_.find("blockyLobes"), std::string::npos);
    EXPECT_NE(shaderSource_.find("raggedEdge"), std::string::npos);
    EXPECT_NE(shaderSource_.find("innerHole"), std::string::npos);
    EXPECT_NE(shaderSource_.find("autumnTint"), std::string::npos);
    EXPECT_NE(shaderSource_.find("youngLeafTint"), std::string::npos);
}

TEST_F(DecorationShaderTest, SupportsBiomeGroundCoverShapes) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("KIND_GRASS"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_FLOWER"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_REED"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_ROCK"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_DRY_SHRUB"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_MUSHROOM"), std::string::npos);
    EXPECT_NE(shaderSource_.find("KIND_CACTUS"), std::string::npos);
    EXPECT_NE(shaderSource_.find("bladeAlpha"), std::string::npos);
    EXPECT_NE(shaderSource_.find("petalAlpha"), std::string::npos);
    EXPECT_NE(shaderSource_.find("seedHead"), std::string::npos);
    EXPECT_NE(shaderSource_.find("capAlpha"), std::string::npos);
    EXPECT_NE(shaderSource_.find("spotMask"), std::string::npos);
    EXPECT_NE(shaderSource_.find("armBand"), std::string::npos);
    EXPECT_NE(shaderSource_.find("thorn"), std::string::npos);
    EXPECT_NE(shaderSource_.find("Rock"), std::string::npos);
}

TEST_F(DecorationShaderTest, AnimatesFoliageWithCameraTime) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("windPhase"), std::string::npos);
    EXPECT_NE(shaderSource_.find("windBend"), std::string::npos);
    EXPECT_NE(shaderSource_.find("windScale"), std::string::npos);
    EXPECT_NE(shaderSource_.find("camera.invProjParams.w"), std::string::npos);
}

TEST_F(DecorationShaderTest, SupportsRayDepthOcclusion) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("useRayDepth"), std::string::npos);
    EXPECT_NE(shaderSource_.find("textureLoad(rayDepthTex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("terrainDepth > 0.0"), std::string::npos);
    EXPECT_NE(shaderSource_.find("discard"), std::string::npos);
}

} // namespace voxy
