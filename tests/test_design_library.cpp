#include "game/expedition/design_library.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <limits>

namespace {
using namespace voxy::game::expedition;
// This isolated transport fixture has a valid SVBP envelope. Full part/catalog
// admission remains the existing host callback, tested by CoveMovement.
std::vector<std::byte> blueprint(uint8_t marker = 1) {
    std::vector<std::byte> bytes(16 + 59, std::byte{0});
    bytes[0] = std::byte{'S'}; bytes[1] = std::byte{'V'}; bytes[2] = std::byte{'B'}; bytes[3] = std::byte{'P'};
    bytes[4] = std::byte{1}; bytes[8] = std::byte{1}; bytes[16] = std::byte{1}; bytes[20] = static_cast<std::byte>(marker);
    const auto hash = voxy::core::sha256(bytes); bytes.insert(bytes.end(), hash.bytes.begin(), hash.bytes.end()); return bytes;
}
TEST(DesignLibrary, NamesPreserveUtf8AndMatchBoundedSingleLineBrowserRules) {
    EXPECT_TRUE(validDesignName("Harbor tug")); EXPECT_TRUE(validDesignName("Tug \xc3\xa9 \xf0\x9f\x9a\xa2"));
    EXPECT_TRUE(validDesignName(std::string(96, 'x'))); EXPECT_FALSE(validDesignName(std::string(97, 'x')));
    for (const auto* name : {"", " tug", "tug ", "bad\nname", "bad\x7f", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xc3", "\xc2\xa0tug", "tug\xe3\x80\x80"})
        EXPECT_FALSE(validDesignName(name));
    std::string unicode;
    for (int i = 0; i < 48; ++i) unicode += "\xc3\xa9";
    EXPECT_TRUE(validDesignName(unicode)); unicode += "x"; EXPECT_FALSE(validDesignName(unicode));
}
TEST(DesignLibrary, ExactBrowserExchangeRoundTripsUnicodeWithoutOwnershipMetadata) {
    const DesignFile source{"Tug \xc3\xa9", blueprint()}; std::string text, error;
    ASSERT_TRUE(encodeDesignFile(source, text, error)) << error;
    const auto object = nlohmann::json::parse(text);
    ASSERT_EQ(object.size(), 3u); EXPECT_EQ(object["version"], 1); EXPECT_EQ(object["name"], source.name);
    EXPECT_EQ(object["blueprint"], designBlueprintHex(source.blueprint));
    DesignFile decoded; ASSERT_TRUE(parseDesignFile(text, decoded, error)) << error; EXPECT_EQ(decoded, source);
    // Browser JSON.stringify uses a different property order, accepted exactly.
    const std::string browser = "{\"version\":1,\"name\":\"Tug \\u00e9\",\"blueprint\":\"" + designBlueprintHex(source.blueprint) + "\"}";
    ASSERT_TRUE(parseDesignFile(browser, decoded, error)); EXPECT_EQ(decoded, source);
}
TEST(DesignLibrary, DamagedOversizedUnknownAndExtraFieldFilesPreserveOutput) {
    const DesignFile sentinel{"Preserve", blueprint()}; DesignFile output = sentinel; std::string error, valid;
    ASSERT_TRUE(encodeDesignFile(sentinel, valid, error));
    auto object = nlohmann::json::parse(valid);
    for (const auto& field : {"version", "name", "blueprint"}) {
        auto bad = object; bad.erase(field); EXPECT_FALSE(parseDesignFile(bad.dump(), output, error)); EXPECT_EQ(output, sentinel);
    }
    auto bad = object; bad["inventory"] = 999; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    bad = object; bad["version"] = 2; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    bad = object; bad["version"] = "1"; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    bad = object; bad["name"] = " \ntug"; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    bad = object; auto hex = designBlueprintHex(sentinel.blueprint); hex[40] = hex[40] == '0' ? '1' : '0';
    bad["blueprint"] = hex; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    bad["blueprint"] = "AA"; EXPECT_FALSE(parseDesignFile(bad.dump(), output, error));
    EXPECT_FALSE(parseDesignFile(std::string(kMaximumDesignFileBytes + 1, ' '), output, error));
    EXPECT_FALSE(parseDesignFile("{", output, error)); EXPECT_EQ(output, sentinel);
    auto corrupted = sentinel.blueprint; corrupted[20] ^= std::byte{1}; std::string text = "unchanged";
    EXPECT_FALSE(encodeDesignFile({"Good", corrupted}, text, error)); EXPECT_EQ(text, "unchanged");
}
TEST(DesignLibrary, CollectionPreservesExactCurrentBackupAndLargeRevision) {
    DesignLibraryImage source;
    source.nextId = 4;
    source.rows = {{{1, 9007199254740999ull, "Current", blueprint(2)}, SavedDesign{1, 9007199254740998ull, "Previous", blueprint(1)}},
                   {{3, 1, "Second", blueprint(3)}, {}}};
    std::vector<std::byte> bytes; std::string error;
    ASSERT_TRUE(encodeDesignLibrary(source, bytes, error)) << error;
    DesignLibraryImage decoded; ASSERT_TRUE(decodeDesignLibrary(bytes, decoded, error)) << error; EXPECT_EQ(decoded, source);
    std::vector<std::byte> same; ASSERT_TRUE(encodeDesignLibrary(decoded, same, error)); EXPECT_EQ(same, bytes);
    source = {}; ASSERT_TRUE(encodeDesignLibrary(source, bytes, error)); ASSERT_TRUE(decodeDesignLibrary(bytes, decoded, error)); EXPECT_EQ(decoded, source);
}
TEST(DesignLibrary, CollectionCorruptionTruncationAndInvalidIdentityNeverPublish) {
    const DesignLibraryImage source{2, {{{1, 2, "Current", blueprint(2)}, SavedDesign{1, 1, "Previous", blueprint()}}}};
    DesignLibraryImage output = source; std::vector<std::byte> bytes; std::string error;
    ASSERT_TRUE(encodeDesignLibrary(source, bytes, error));
    for (size_t size = 0; size < bytes.size(); ++size) { EXPECT_FALSE(decodeDesignLibrary(std::span(bytes).first(size), output, error)); EXPECT_EQ(output, source); }
    auto corrupt = bytes; corrupt[27] ^= std::byte{1}; EXPECT_FALSE(decodeDesignLibrary(corrupt, output, error)); EXPECT_EQ(output, source);
    corrupt = bytes; corrupt.push_back(std::byte{0}); EXPECT_FALSE(decodeDesignLibrary(corrupt, output, error));
    for (int kind = 0; kind < 5; ++kind) {
        auto bad = source;
        if (kind == 0) bad.nextId = 1;
        if (kind == 1) bad.rows[0].current.id = 0;
        if (kind == 2) bad.rows[0].current.revision = 0;
        if (kind == 3) bad.rows[0].backup->id = 2;
        if (kind == 4) bad.rows[0].backup->revision = 2;
        auto saved = bytes; EXPECT_FALSE(encodeDesignLibrary(bad, saved, error)); EXPECT_EQ(saved, bytes);
    }
}
TEST(DesignLibrary, ExactCapacityAndDuplicateNamesAreBoundedBeforeStorage) {
    DesignLibraryImage image; image.nextId = 33;
    for (uint64_t id = 1; id <= 32; ++id) image.rows.push_back({{id, 1, "Design " + std::to_string(id), blueprint()}, {}});
    std::string error; std::vector<std::byte> bytes;
    ASSERT_TRUE(encodeDesignLibrary(image, bytes, error));
    image.nextId = 34; image.rows.push_back({{33, 1, "Overflow", blueprint()}, {}});
    auto output = bytes; EXPECT_FALSE(encodeDesignLibrary(image, output, error)); EXPECT_EQ(output, bytes);
    image.rows.pop_back(); image.rows[31].current.name = image.rows[0].current.name;
    EXPECT_FALSE(encodeDesignLibrary(image, output, error)); EXPECT_EQ(output, bytes);
}
} // namespace
