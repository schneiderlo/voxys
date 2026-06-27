// Tests for browser/WASM shell assets.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFirstExisting(const std::vector<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) continue;
        std::ifstream file(path);
        if (!file.is_open()) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    return {};
}

std::vector<std::filesystem::path> webPaths(const std::string& filename) {
    return {
        std::filesystem::path("web") / filename,
        std::filesystem::path("../web") / filename,
        std::filesystem::path("../../web") / filename,
        std::filesystem::path("voxys/web") / filename,
    };
}

}  // namespace

TEST(WebAssetsTest, IndexRegistersWaterProAdapter) {
    const std::string index = readFirstExisting(webPaths("index.html"));
    ASSERT_FALSE(index.empty()) << "web/index.html was not available to the test";
    EXPECT_NE(index.find("water-pro-adapter.js"), std::string::npos);
    EXPECT_NE(index.find("\"threejs-water-pro\""), std::string::npos);
    EXPECT_NE(index.find("window.VoxyWaterPro.configure"), std::string::npos);
}

TEST(WebAssetsTest, WaterProAdapterExposesLicensedVendorSlotAndFallback) {
    const std::string adapter = readFirstExisting(webPaths("water-pro-adapter.js"));
    ASSERT_FALSE(adapter.empty()) << "web/water-pro-adapter.js was not available to the test";
    EXPECT_NE(adapter.find("DEFAULT_VENDOR_MODULE"), std::string::npos);
    EXPECT_NE(adapter.find("threejs-water-pro"), std::string::npos);
    EXPECT_NE(adapter.find("createSurface"), std::string::npos);
    EXPECT_NE(adapter.find("createFallbackSurface"), std::string::npos);
}

TEST(WebAssetsTest, WaterProVendoringNotesStayPackaged) {
    const std::string docs = readFirstExisting(webPaths("WATER_PRO_VENDORING.md"));
    ASSERT_FALSE(docs.empty()) << "web/WATER_PRO_VENDORING.md was not available to the test";
    EXPECT_NE(docs.find("web/vendor/threejs-water-pro/threejs-water-pro.module.js"), std::string::npos);
    EXPECT_NE(docs.find("?waterpro=1"), std::string::npos);
}
