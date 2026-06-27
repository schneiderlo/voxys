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

TEST_F(DecorationShaderTest, UsesInstancedTreeStorage) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("struct TreeInstance"), std::string::npos);
    EXPECT_NE(shaderSource_.find("var<storage, read> trees"), std::string::npos);
    EXPECT_NE(shaderSource_.find("@builtin(instance_index)"), std::string::npos);
}

TEST_F(DecorationShaderTest, BuildsCrossedBillboardsProcedurally) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("vertexIndex / 6u"), std::string::npos);
    EXPECT_NE(shaderSource_.find("quadIndex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("localIndex"), std::string::npos);
}

TEST_F(DecorationShaderTest, SupportsRayDepthOcclusion) {
    ASSERT_FALSE(shaderSource_.empty());
    EXPECT_NE(shaderSource_.find("useRayDepth"), std::string::npos);
    EXPECT_NE(shaderSource_.find("textureLoad(rayDepthTex"), std::string::npos);
    EXPECT_NE(shaderSource_.find("terrainDepth > 0.0"), std::string::npos);
    EXPECT_NE(shaderSource_.find("discard"), std::string::npos);
}

} // namespace voxy
