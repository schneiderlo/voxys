#include "game/assets/cooked_part_directory.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <cstdlib>

#if defined(__unix__) || defined(__EMSCRIPTEN__)
#include <sys/stat.h>
#include <unistd.h>

namespace voxy::game::assets {
namespace {
struct DirectoryFixture : testing::Test {
    std::filesystem::path root;
    void SetUp() override {
        char name[] = "/tmp/voxys-bundle-XXXXXX";
        const auto result = ::mkdtemp(name);
        ASSERT_NE(result, nullptr);
        root = result;
    }
    void TearDown() override {
        std::error_code ignored;
        if (!root.empty()) std::filesystem::remove_all(root, ignored);
    }
    void write(const std::filesystem::path& path, std::string_view text) {
        std::ofstream file(path, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(file);
    }
};

TEST_F(DirectoryFixture, OwnsSnapshotAndProviderSurvivesFactoryScope) {
    write(root / "gameplay.json", "abc");
    std::string error;
    auto provider = openCookedPartDirectory(root, error);
    ASSERT_TRUE(provider) << error;
    const auto original = (*provider)("gameplay.json", 3, error);
    ASSERT_TRUE(original) << error;
    EXPECT_EQ(std::string(original->begin(), original->end()), "abc");
    write(root / "gameplay.json", "xyz");
    const auto changed = (*provider)("gameplay.json", 3, error);
    ASSERT_TRUE(changed) << error;
    EXPECT_EQ(std::string(changed->begin(), changed->end()), "xyz");
    EXPECT_EQ(std::string(original->begin(), original->end()), "abc");
}

TEST_F(DirectoryFixture, RejectsUnsafeNamesAndInvalidReadCaps) {
    std::string error;
    auto provider = openCookedPartDirectory(root, error);
    ASSERT_TRUE(provider);
    for (const auto& name : {"../outside", "/etc/passwd", ".", "", "a/b", "https://example.test/a"}) {
        EXPECT_FALSE((*provider)(name, 100, error));
        EXPECT_NE(error.find("leaf name"), std::string::npos) << error;
    }
    EXPECT_FALSE((*provider)("gameplay.json", 0, error));
    EXPECT_FALSE((*provider)("gameplay.json", SIZE_MAX, error));
}

TEST_F(DirectoryFixture, RejectsEmptyOversizedAndNonregularFilesBeforeRead) {
    write(root / "empty", "");
    write(root / "large", "abcd");
    std::filesystem::create_directory(root / "directory");
#if !defined(__EMSCRIPTEN__)
    ASSERT_EQ(::mkfifo((root / "pipe").c_str(), 0600), 0);
#endif
    std::string error;
    auto provider = openCookedPartDirectory(root, error);
    ASSERT_TRUE(provider);
    EXPECT_FALSE((*provider)("empty", 100, error));
    EXPECT_FALSE((*provider)("large", 3, error));
    EXPECT_FALSE((*provider)("directory", 100, error));
    // Nonblocking open must reject the FIFO without waiting for a writer.
#if !defined(__EMSCRIPTEN__)
    EXPECT_FALSE((*provider)("pipe", 100, error));
#endif
    EXPECT_FALSE((*provider)("missing", 100, error));
}

TEST_F(DirectoryFixture, RejectsLeafSymlinkEvenWhenItsTargetIsAValidFile) {
    write(root / "real", "abc");
    std::filesystem::create_symlink("real", root / "alias");
    std::string error;
    auto provider = openCookedPartDirectory(root, error);
    ASSERT_TRUE(provider);
    EXPECT_FALSE((*provider)("alias", 100, error));
    EXPECT_NE(error.find("without following"), std::string::npos) << error;
}

TEST_F(DirectoryFixture, RejectsLinkedRootAndIntermediateDirectories) {
    std::filesystem::create_directories(root / "real/child");
    std::filesystem::create_directory_symlink("real", root / "alias");
    std::string error;
    EXPECT_FALSE(openCookedPartDirectory(root / "alias", error));
    EXPECT_FALSE(openCookedPartDirectory(root / "alias/child", error));
    EXPECT_TRUE(openCookedPartDirectory(root / "real/child", error)) << error;
}

TEST_F(DirectoryFixture, RejectsRootTraversalAndEmbeddedNulls) {
    std::string error;
    EXPECT_FALSE(openCookedPartDirectory({}, error));
    EXPECT_FALSE(openCookedPartDirectory(root / "..", error));
    std::string name = root.string();
    name.push_back('\0');
    name += "ignored";
    EXPECT_FALSE(openCookedPartDirectory(std::filesystem::path(name), error));
}

TEST_F(DirectoryFixture, ReplacedRootCannotRedirectAnAdmittedProvider) {
    std::filesystem::create_directory(root / "package");
    write(root / "package/gameplay.json", "original");
    std::string error;
    auto provider = openCookedPartDirectory(root / "package", error);
    ASSERT_TRUE(provider);
    std::filesystem::rename(root / "package", root / "old");
    std::filesystem::create_directory(root / "package");
    write(root / "package/gameplay.json", "replaced");
    const auto value = (*provider)("gameplay.json", 100, error);
#if defined(__EMSCRIPTEN__)
    EXPECT_FALSE(value);
    EXPECT_EQ(error, "packaged MEMFS root changed");
#else
    ASSERT_TRUE(value) << error;
    EXPECT_EQ(std::string(value->begin(), value->end()), "original");
#endif
}

TEST_F(DirectoryFixture, ReadsActualInstalledCookedProbeThroughVerifiedAdapter) {
    // Bazel's declared runfiles are symlinks. Materialize this trusted test
    // package as regular files, exactly as packaging does; do not weaken the
    // production adapter's no-follow boundary to accommodate the test harness.
    for (const auto& name : {"cook-manifest.json", "gameplay.json", "lod-9007199254740997.vmesh"}) {
        ASSERT_TRUE(std::filesystem::copy_file(std::filesystem::path("data/salvage/runtime_probe_v1") / name, root / name));
    }
    std::string error;
    auto provider = openCookedPartDirectory(root, error);
    ASSERT_TRUE(provider) << error;
    CookedPartSelection selection;
    selection.part.id.world.bytes = {0x53,0xa1,0x7c,0x2c,0xb6,0x62,0x4a,0x39,0x85,0x40,0x10,0x3c,0xc1,0x8b,0x64,0xaa};
    selection.part.id.counter = 9007199254740993ull;
    selection.part.version = 1;
    selection.manifestSha256 = "f595f6b490fbbcc6f947eb1ac31f7328cc4dedf9d84896268a8aed45dea80898";
    selection.lodRules.push_back({9007199254740997ull, {}});
    const auto bundle = admitCookedPartBundle(selection, *provider, error);
    ASSERT_TRUE(bundle) << error;
    EXPECT_EQ(bundle->lods().size(), 1u);
}
} // namespace
} // namespace voxy::game::assets
#endif
